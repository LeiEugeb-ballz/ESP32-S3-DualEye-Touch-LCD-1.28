"""Bounded per-connection voice turns. No legacy UDP, hardware tools or shared history."""
import asyncio
from collections import deque
import contextlib
import secrets
import time
import webrtcvad
from .audio import OpusCodec, RATE, SAMPLES

PERSONAS = {
    "japa": "You are JAPA, a warm, curious conversational companion. Be relaxed, concise and natural. A little dry humour is welcome when appropriate.",
    "calm": "You are JAPA, a calm, attentive companion. Speak gently and plainly, with short thoughtful replies. Never sound like a formal assistant.",
    "playful": "You are JAPA, a playful, kind companion. Use light wit without being relentless. Keep replies conversational and brief.",
}
GUARDRAILS = (" Do not claim to be human or to see anything: no camera is connected. "
              "Do not narrate robot status, repeat greetings, or use markdown. "
              "Allow the person room to speak. Ask at most one useful question. "
              "Never invent memories or capabilities. You have no device or filesystem tools.")

_event_logger = None

def set_event_logger(fn):
    global _event_logger
    _event_logger = fn

def emit_event(source, message, level="info"):
    if _event_logger:
        try:
            _event_logger(source, message, level)
        except Exception:
            pass


class TurnDetector:
    """20 ms VAD with 400 ms pre-roll, 80 ms onset and 1 s pause tolerance."""
    def __init__(self, vad=None):
        self.vad = vad or webrtcvad.Vad(2)
        self.reset()

    def reset(self):
        self.pending = bytearray()
        self.pre = deque(maxlen=20)
        self.votes = deque(maxlen=6)
        self.turn = bytearray()
        self.silence = 0
        self.started = False

    def feed(self, pcm):
        self.pending.extend(pcm)
        while len(self.pending) >= 640:
            frame = bytes(self.pending[:640])
            del self.pending[:640]
            voiced = self.vad.is_speech(frame, RATE)
            if not self.started:
                self.pre.append(frame)
                self.votes.append(voiced)
                if sum(self.votes) >= 4:
                    self.started = True
                    self.turn.extend(b"".join(self.pre))
            else:
                self.turn.extend(frame)
                self.silence = 0 if voiced else self.silence + 1
                if self.silence >= 50 or len(self.turn) >= RATE * 2 * 20:
                    return self.finish()
        return None

    def finish(self):
        result = bytes(self.turn) if self.started else None
        self.reset()
        return result


class VoiceSession:
    def __init__(self, ws, provider, persona="japa"):
        self.ws, self.provider = ws, provider
        self.id = secrets.token_hex(12)
        self.persona = persona
        self.codec = OpusCodec()
        self.detector = TurnDetector()
        self.history = []
        self.task = None
        self.generation = 0
        self.listening = False
        self.closed = False
        self.state = "idle"
        self.metrics = {"turns": 0, "cancellations": 0, "errors": 0}

    @property
    def busy(self):
        return self.listening or (self.task is not None and not self.task.done())

    async def send(self, kind, **fields):
        if not self.closed:
            await self.ws.send_json({"type": kind, "session_id": self.id, **fields})

    async def cancel(self):
        self.generation += 1
        self.listening = False
        self.detector.reset()
        if self.task and not self.task.done():
            self.metrics["cancellations"] += 1
            self.task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.task
        self.task = None
        self.state = "idle"

    async def control(self, message):
        kind, state = message.get("type"), message.get("state")
        if kind == "abort":
            await self.cancel()
            await self.send("tts", state="stop")
        elif kind == "listen" and state == "start":
            if self.task and not self.task.done():
                return
            self.listening, self.state = True, "listening"
            emit_event("ESP32", "Microphone listening active", "info")
            self.detector.reset()
        elif kind == "listen" and state == "stop":
            self.listening = False
            emit_event("ESP32", "Microphone listening paused", "info")
            pcm = self.detector.finish()
            if pcm:
                self.start_turn(pcm)
            elif not self.task or self.task.done():
                self.state = "idle"
        # detect is only the wake word, not a user request; pre-wake audio is ignored.
        # MCP is deliberately not implemented. There are no inherited host tools.

    def audio(self, packet):
        if not self.listening or (self.task and not self.task.done()):
            return
        self.packet_count = getattr(self, "packet_count", 0) + 1
        if self.packet_count % 35 == 1:
            emit_event("MIC", f"Audio stream active ({len(packet)}B Opus packet)", "info")
        pcm = self.detector.feed(self.codec.decode(packet))
        if pcm:
            self.start_turn(pcm)

    def start_turn(self, pcm):
        self.listening = False
        self.state = "thinking"
        self.generation += 1
        emit_event("VAD", f"Speech detected ({len(pcm)} bytes PCM). Starting transcription...", "warn")
        self.task = asyncio.create_task(self.respond(pcm, self.generation))

    async def respond(self, pcm, generation):
        started = time.monotonic()
        spoken = False
        response = ""
        transcript = ""
        try:
            async with asyncio.timeout(90):
                await self.send("japa_state", state="thinking")
                transcript = (await self.provider.transcribe(pcm))[:4000]
                if not transcript.strip():
                    emit_event("STT", "No speech detected in audio segment", "warn")
                    await self.send("japa_state", state="listening")
                    self.listening, self.state = True, "listening"
                    return
                emit_event("STT", f"Heard: \"{transcript}\"", "success")
                await self.send("stt", text=transcript)
                messages = [{"role": "system", "content": PERSONAS[self.persona] + GUARDRAILS}]
                messages += self.history + [{"role": "user", "content": transcript}]
                emit_event("MISTRAL", "Generating response...", "info")
                # One streamed chat request. Sentence-sized TTS starts before the full reply.
                pending = ""
                async for part in self.provider.chat(messages):
                    pending += part
                    if len(pending) > 4000:
                        raise ValueError("response_too_large")
                    boundary = max(pending.rfind(". "), pending.rfind("! "), pending.rfind("? "), pending.rfind("\n"))
                    if boundary < 0 and len(pending) > 220:
                        boundary = pending.rfind(" ", 0, 220)
                    if boundary >= 0:
                        sentence, pending = pending[:boundary + 1].strip(), pending[boundary + 1:]
                        if sentence:
                            spoken = await self.speak(sentence, generation, spoken, started)
                            response += sentence + " "
                if pending.strip():
                    spoken = await self.speak(pending.strip(), generation, spoken, started)
                    response += pending.strip()
                if generation != self.generation:
                    return
                if not response.strip():
                    raise ValueError("empty_response")
                self.history += [{"role": "user", "content": transcript}, {"role": "assistant", "content": response.strip()}]
                self.history = self.history[-12:]  # six turns, never shared across providers/connections
                self.metrics["turns"] += 1
                self.metrics["last_turn_ms"] = round((time.monotonic() - started) * 1000)
                await self.send("tts", state="stop")  # firmware waits for its playback to drain
                self.state = "idle"  # firmware sends listen/start once drained
        except asyncio.CancelledError:
            raise
        except Exception as error:
            self.metrics["errors"] += 1
            self.metrics["last_error"] = f"{type(error).__name__}:http_{getattr(error, 'status_code', 'unavailable')}"
            self.provider.ready_until = 0
            self.provider.last_error = self.metrics["last_error"]
            # No invented success or repeated apology audio. Close returns firmware to idle.
            self.state = "failed"
            await self.send("tts", state="stop")
            await self.ws.close(code=1011, message=b"provider_failed_check_local_control_board")

    async def speak(self, text, generation, spoken, started):
        emit_event("TTS", f"JAPA: \"{text}\"", "success")
        pcm = await self.provider.speech(text)
        if generation != self.generation:
            raise asyncio.CancelledError
        if not pcm:
            raise ValueError("empty_speech")
        if not spoken:
            self.metrics["first_audio_ms"] = round((time.monotonic() - started) * 1000)
            await self.send("tts", state="start")
            self.state = "speaking"
        await self.send("tts", state="sentence_start", text=text)
        # Jitter-compensated pacing: pre-buffer 3 packets to fill the ESP32's I2S queue,
        # then pace slightly ahead of real-time (~48ms per 60ms frame) to eliminate buffer underruns.
        start_time = time.monotonic()
        for idx, offset in enumerate(range(0, len(pcm), SAMPLES * 2)):
            if generation != self.generation:
                raise asyncio.CancelledError
            chunk = pcm[offset:offset + SAMPLES * 2].ljust(SAMPLES * 2, b"\0")
            for packet in self.codec.encode(chunk):
                await self.ws.send_bytes(packet)
            if idx >= 3:
                target_time = start_time + (idx - 2) * 0.048
                delay = target_time - time.monotonic()
                if delay > 0.005:
                    await asyncio.sleep(delay)
        return True

    async def trigger_startup_greeting(self):
        """Dynamic startup greeting generated via Mistral LLM with true randomness entropy (never a canned list)."""
        await asyncio.sleep(1.2)
        if self.closed or self.busy:
            return
        entropy = secrets.token_hex(4)
        messages = [
            {"role": "system", "content": PERSONAS[self.persona] + GUARDRAILS},
            {"role": "user", "content": f"Say a brief, warm, natural greeting (under 7 words) to your friend who just turned you on. System entropy: {entropy}. Do not use markdown, quotes, or robot cliches."}
        ]
        text = ""
        try:
            emit_event("MISTRAL", "Generating warm companion greeting...", "info")
            async for part in self.provider.chat(messages):
                text += part
                if len(text) > 80:
                    break
            text = text.strip().strip('"').strip("'")
            if text and not self.closed and not self.busy:
                emit_event("GREETING", f"Greeting: \"{text}\"", "success")
                self.generation += 1
                await self.send("japa_state", state="thinking")
                await self.speak(text, self.generation, False, time.monotonic())
                await self.send("tts", state="stop")
                self.history.append({"role": "assistant", "content": text})
        except Exception:
            pass
        finally:
            self.state = "idle"

    async def close(self):
        self.closed = True
        await self.cancel()
        self.history.clear()

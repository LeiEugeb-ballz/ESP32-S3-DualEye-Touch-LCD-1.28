"""GPT-Live-1 server-side WebSocket adapter.

The ESP32 keeps its existing 16 kHz mono, 60 ms Opus wire contract. This
adapter converts only at the gateway boundary and never exposes an API key to
the device or browser.
"""
import asyncio
import base64
import contextlib
import os
import secrets
from aiohttp import ClientSession, ClientTimeout, WSMsgType
from .audio import OpusCodec, RATE, SAMPLES


LIVE_URL = os.environ.get("JAPA_OPENAI_LIVE_URL", "wss://api.openai.com/v1/live/sessions")
LIVE_MODEL = "gpt-live-1"
DEFAULT_BACKEND = "gpt-5.6-luna"

LIVE_INSTRUCTIONS = """You are JAPA, a warm and natural voice companion.
Speak briefly and conversationally. Use moderate listening backchannels without
competing with the person. Stop speaking when interrupted and listen. Keep
listening through short pauses. Delegate requests needing careful reasoning,
device state, or tools to the backend; do not guess a backend result. Never
claim an action succeeded until the backend confirms it. Do not claim to be
human or to see through a camera."""


class LiveProvider:
    """Credential/configuration and readiness for GPT-Live sessions."""
    def __init__(self):
        self.key = os.environ.get("JAPA_OPENAI_API_KEY", "")
        # OPENAI_API_KEY is accepted only when no alternate base URL is set.
        # This prevents the existing Alibaba-compatible credential from being
        # sent to api.openai.com by accident.
        if not self.key and not os.environ.get("OPENAI_BASE_URL"):
            self.key = os.environ.get("OPENAI_API_KEY", "")
        self.ready = bool(self.key)
        self.last_error = None if self.ready else "missing_JAPA_OPENAI_API_KEY"

    def status(self):
        return {
            "configured": bool(self.key),
            "ready": self.ready,
            "model": LIVE_MODEL,
            "backend": DEFAULT_BACKEND,
            "transport": "server_websocket",
            "error": self.last_error,
        }

    async def verify(self):
        if not self.key:
            self.ready = False
            self.last_error = "missing_JAPA_OPENAI_API_KEY"
            return False
        # A full Live session probe would consume billable voice time. Startup
        # readiness therefore validates configuration; the first session
        # performs the authoritative session.started check.
        self.ready = True
        self.last_error = None
        return True

    def headers(self):
        return {"Authorization": f"Bearer {self.key}"}


class LiveVoiceSession:
    """Bridge one ESP32 WebSocket to one GPT-Live WebSocket."""
    def __init__(self, device_ws, provider, http):
        self.device_ws = device_ws
        self.provider = provider
        self.http = http
        self.live_ws = None
        self.codec_in = OpusCodec()
        self.codec_out = OpusCodec()
        self.session_id = secrets.token_hex(12)
        self.started = False
        self.closed = False
        self.output_started = False
        self.metrics = {"turns": 0, "interruptions": 0, "errors": 0}
        self.receiver = None

    async def send_device(self, kind, **fields):
        if not self.closed:
            await self.device_ws.send_json({"type": kind, "session_id": self.session_id, **fields})

    async def start(self):
        if not await self.provider.verify():
            raise RuntimeError(self.provider.last_error)
        self.live_ws = await self.http.ws_connect(
            LIVE_URL, headers=self.provider.headers(),
            heartbeat=25, timeout=ClientTimeout(total=None),
            max_msg_size=2 * 1024 * 1024,
        )
        await self.live_ws.send_json({
            "type": "session.start",
            "event_id": "japa-live-start",
            "session": {
                "model": LIVE_MODEL,
                "instructions": LIVE_INSTRUCTIONS,
                "audio": {
                    "format": {"type": "audio/pcm", "rate": RATE},
                    "output": {"voice": os.environ.get("JAPA_OPENAI_VOICE", "marin")},
                },
                "delegation": {
                    "type": "responses",
                    "responses": {
                        "model": DEFAULT_BACKEND,
                        "instructions": "Use only verified JAPA backend/device results. Keep spoken results concise.",
                        "tool_choice": "auto",
                    },
                },
            },
        })
        while True:
            message = await self.live_ws.receive()
            if message.type != WSMsgType.TEXT:
                if message.type in (WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.ERROR):
                    raise RuntimeError("live_session_closed_before_start")
                continue
            event = message.json()
            if event.get("type") == "session.started":
                self.started = True
                await self.send_device("japa_state", state="listening", live=True)
                self.receiver = asyncio.create_task(self.receive())
                return
            if event.get("type") == "error":
                raise RuntimeError("live_session_rejected")

    async def send_audio(self, packet):
        if not self.started or self.closed or not self.live_ws:
            return
        pcm = self.codec_in.decode(packet)
        if pcm:
            await self.live_ws.send_json({
                "type": "session.input_audio.append",
                "audio": base64.b64encode(pcm).decode("ascii"),
            })

    async def receive(self):
        if not self.live_ws:
            return
        async for message in self.live_ws:
            if message.type == WSMsgType.TEXT:
                event = message.json()
                kind = event.get("type", "")
                if kind == "session.output_audio.delta":
                    pcm = base64.b64decode(event.get("delta", ""), validate=True)
                    if not pcm:
                        continue
                    if not self.output_started:
                        self.output_started = True
                        await self.send_device("tts", state="start")
                        await self.send_device("japa_state", state="speaking", live=True)
                    for offset in range(0, len(pcm), SAMPLES * 2):
                        chunk = pcm[offset:offset + SAMPLES * 2]
                        if len(chunk) < SAMPLES * 2:
                            chunk = chunk.ljust(SAMPLES * 2, bytes(1))
                        for encoded in self.codec_out.encode(chunk):
                            await self.device_ws.send_bytes(encoded)
                elif kind == "session.input_transcript.delta":
                    await self.send_device("stt", text=event.get("delta", ""))
                elif kind == "session.output_transcript.delta":
                    await self.send_device("tts", state="sentence_start", text=event.get("delta", ""))
                elif kind == "session.delegation.created":
                    await self.send_device("japa_state", state="thinking", live=True)
                elif kind in ("session.output_audio.done", "session.response.done"):
                    self.output_started = False
                    await self.send_device("tts", state="stop")
                    await self.send_device("japa_state", state="listening", live=True)
                elif kind == "error":
                    self.metrics["errors"] += 1
                    raise RuntimeError("live_session_error")
            elif message.type in (WSMsgType.CLOSE, WSMsgType.CLOSED, WSMsgType.ERROR):
                break

    async def close(self):
        self.closed = True
        if self.receiver and not self.receiver.done() and self.receiver is not asyncio.current_task():
            self.receiver.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.receiver
        if self.live_ws and not self.live_ws.closed:
            await self.live_ws.send_json({"type": "session.close", "event_id": "japa-live-close"})
            await self.live_ws.close()

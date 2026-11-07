"""Explicit real-provider/real-WebSocket/real-Opus round trip with synthetic speech.

Consumes Mistral quota. Never changes a device profile, stores transcripts or
opens hardware. This proves host integration, not board or human acceptance.
"""
import asyncio
import json
from pathlib import Path
import time
from aiohttp import ClientSession, ClientTimeout, WSMsgType
from .audio import OpusCodec, SAMPLES
from .provider import MistralProvider


async def run():
    provider = MistralProvider()
    try:
        prompt = await provider.speech("Tell me something cheerful in one short sentence.")
        token = (Path(__file__).parent / ".pairing-token").read_text(encoding="ascii").strip()
        async with ClientSession(timeout=ClientTimeout(total=120)) as http:
            async with http.get("http://localhost:8765/api/status") as response:
                status = await response.json()
            if not status["provider"]["ready"]:
                raise RuntimeError("Verify the provider in the local control board first")
            async with http.ws_connect("http://localhost:8765/xiaozhi/v1", headers={
                "Authorization": "Bearer " + token, "Protocol-Version": "1"}) as ws:
                await ws.send_json({"type": "hello", "version": 1, "transport": "websocket", "audio_params": {
                    "format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}})
                hello = await ws.receive_json()
                if not hello.get("japa_ready"):
                    raise RuntimeError("Gateway handshake failed")
                await ws.send_json({"type": "listen", "state": "start", "mode": "auto"})
                codec = OpusCodec()
                # Trailing silence tests the actual VAD end-of-turn path.
                pcm = prompt + b"\0" * (16000 * 2 * 2)
                started = time.monotonic()
                for offset in range(0, len(pcm), SAMPLES * 2):
                    chunk = pcm[offset:offset + SAMPLES * 2].ljust(SAMPLES * 2, b"\0")
                    for packet in codec.encode(chunk):
                        await ws.send_bytes(packet)
                    await asyncio.sleep(.06)
                result = {"scope": "synthetic live host round trip; no ESP32 acceptance", "stt": False,
                          "tts_start": False, "tts_stop": False, "opus_packets": 0, "decoded_samples": 0}
                async with asyncio.timeout(90):
                    async for message in ws:
                        if message.type == WSMsgType.BINARY:
                            if not result["tts_start"]: raise RuntimeError("Audio arrived before speaking event")
                            result["opus_packets"] += 1
                            result["decoded_samples"] += len(codec.decode(message.data)) // 2
                        elif message.type == WSMsgType.TEXT:
                            event = json.loads(message.data)
                            if event.get("type") == "stt": result["stt"] = bool(event.get("text", "").strip())
                            if event.get("type") == "tts":
                                if event.get("state") == "start": result["tts_start"] = True
                                if event.get("state") == "stop": result["tts_stop"] = True; break
                        elif message.type in (WSMsgType.CLOSE, WSMsgType.ERROR): break
                result["elapsed_ms"] = round((time.monotonic() - started) * 1000)
                result["passed"] = all(result[k] for k in ("stt", "tts_start", "tts_stop", "opus_packets", "decoded_samples"))
                print(json.dumps(result))
                return result["passed"]
    except Exception as error:
        print(json.dumps({"passed": False, "error": type(error).__name__, "http_status": getattr(error, "status_code", None)}))
        return False
    finally:
        await provider.close()


if __name__ == "__main__":
    raise SystemExit(0 if asyncio.run(run()) else 1)

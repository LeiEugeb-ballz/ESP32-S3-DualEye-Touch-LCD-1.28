"""Host tests; provider doubles are confined here. No paid calls or hardware writes."""
import asyncio
import math
import struct
import unittest
from unittest.mock import AsyncMock, patch
from aiohttp.test_utils import TestClient, TestServer
from .audio import OpusCodec, decode_wav, encode_wav
from .server import make_app, private_device
from .session import TurnDetector, VoiceSession


class AudioTests(unittest.TestCase):
    def test_real_opus_roundtrip(self):
        encoder, decoder = OpusCodec(), OpusCodec()
        pcm = b"".join(struct.pack("<h", int(9000 * math.sin(i * 2 * math.pi * 440 / 16000))) for i in range(960))
        packets = [packet for _ in range(3) for packet in encoder.encode(pcm)]
        decoded = b"".join(decoder.decode(packet) for packet in packets)
        self.assertEqual(len(packets), 3)
        self.assertTrue(5600 <= len(decoded) <= 5760)
        self.assertTrue(any(decoded))

    def test_wav_metadata_resampled(self):
        import io, wave
        blob = io.BytesIO()
        with wave.open(blob, "wb") as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(24000)
            wav.writeframes(b"\0" * 4800)
        self.assertEqual(len(decode_wav(blob.getvalue())), 3200)

    def test_wav_roundtrip(self):
        self.assertEqual(decode_wav(encode_wav(b"\0" * 3200)), b"\0" * 3200)

    def test_invalid_packet_rejected(self):
        for data in (b"", b"a" * 1501):
            with self.assertRaises(ValueError): OpusCodec().decode(data)

    def test_invalid_pcm_duration_rejected(self):
        with self.assertRaises(ValueError): OpusCodec().encode(b"\0" * 640)

    def test_public_and_loopback_addresses_rejected(self):
        from aiohttp import web
        for ip in ("8.8.8.8", "127.0.0.1", "0.0.0.0", "localhost", "192.168.1.2:3233", "224.0.0.1"):
            with self.assertRaises(web.HTTPBadRequest): private_device(ip)
        self.assertEqual(private_device("192.168.1.20"), "192.168.1.20")


class SpeechFlag:
    def is_speech(self, frame, rate):
        return frame[0] == 1


class DetectorTests(unittest.TestCase):
    def test_silence_and_single_noise_do_not_start(self):
        detector = TurnDetector(SpeechFlag())
        for frame in [b"\0" * 640] * 100 + [b"\1" * 640] + [b"\0" * 640] * 100:
            self.assertIsNone(detector.feed(frame))
        self.assertIsNone(detector.finish())
        self.assertLessEqual(len(detector.pre), 20)

    def test_hesitation_then_end(self):
        detector = TurnDetector(SpeechFlag())
        for frame in [b"\1" * 640] * 10 + [b"\0" * 640] * 30 + [b"\1" * 640] * 10 + [b"\0" * 640] * 49:
            self.assertIsNone(detector.feed(frame))
        self.assertIsNotNone(detector.feed(b"\0" * 640))

    def test_twenty_second_cap(self):
        detector = TurnDetector(SpeechFlag())
        result = None
        for _ in range(1000):
            result = detector.feed(b"\1" * 640)
            if result: break
        self.assertIsNotNone(result)
        self.assertLessEqual(len(result), 640000)

    def test_partial_frames(self):
        detector = TurnDetector(SpeechFlag())
        for _ in range(8): detector.feed(b"\1" * 320)
        self.assertTrue(detector.started)


class TestProvider:
    def __init__(self, ready=True):
        self.ready_until = 1 if ready else 0
        self.last_error = None if ready else "chat:SDKError:http_429"
        self.calls = 0
    def status(self):
        return {"ready": bool(self.ready_until), "error": self.last_error, "models": {}}
    async def verify(self): return bool(self.ready_until)
    async def close(self): pass
    async def transcribe(self, pcm): return "What do you reckon?"
    async def chat(self, messages):
        self.calls += 1
        yield "Let's find out."
    async def speech(self, text): return b"\0" * 1920


class TestSocket:
    def __init__(self): self.events = []
    async def send_json(self, value): self.events.append(value)
    async def send_bytes(self, value): self.events.append(value)
    async def close(self, **kwargs): self.events.append({"closed": True})


class SessionTests(unittest.IsolatedAsyncioTestCase):
    async def test_turn_and_audio_event_order(self):
        ws, provider = TestSocket(), TestProvider()
        session = VoiceSession(ws, provider)
        await session.respond(b"\0" * 6400, 0)
        kinds = [e.get("state", e.get("type")) if isinstance(e, dict) else "audio" for e in ws.events]
        self.assertLess(kinds.index("thinking"), kinds.index("start"))
        self.assertLess(kinds.index("start"), kinds.index("audio"))
        self.assertLess(kinds.index("audio"), kinds.index("stop"))
        self.assertEqual(provider.calls, 1)
        self.assertEqual(len(session.history), 2)

    async def test_cancel_inflight_rejects_late_speech(self):
        gate = asyncio.Event()
        provider, ws = TestProvider(), TestSocket()
        async def delayed(text):
            await gate.wait()
            return b"\0" * 1920
        provider.speech = delayed
        session = VoiceSession(ws, provider)
        session.start_turn(b"\0" * 6400)
        await asyncio.sleep(.01)
        await session.cancel()
        gate.set()
        await asyncio.sleep(.01)
        self.assertFalse(any(isinstance(e, bytes) for e in ws.events))
        self.assertEqual(session.history, [])

    async def test_session_history_isolated(self):
        provider = TestProvider()
        a, b = VoiceSession(TestSocket(), provider), VoiceSession(TestSocket(), provider)
        await a.respond(b"\0" * 6400, 0)
        self.assertEqual(b.history, [])

    async def test_failure_does_not_claim_speech(self):
        provider, ws = TestProvider(), TestSocket()
        provider.transcribe = AsyncMock(side_effect=TimeoutError())
        session = VoiceSession(ws, provider)
        await session.respond(b"\0" * 6400, 0)
        self.assertEqual(session.state, "failed")
        self.assertFalse(provider.status()["ready"])
        self.assertFalse(any(isinstance(e, bytes) for e in ws.events))

    async def test_busy_persona_boundary(self):
        session = VoiceSession(TestSocket(), TestProvider())
        await session.control({"type": "listen", "state": "start"})
        self.assertTrue(session.busy)
        await session.cancel()
        self.assertFalse(session.busy)


class HttpTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.provider = TestProvider(False)
        self.app = make_app(self.provider, "test-pairing-token-123456789")
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.headers = {"Host": "localhost:8765", "X-JAPA-Admin": self.app["admin_token"]}

    async def asyncTearDown(self): await self.client.close()

    async def test_page_and_redacted_status(self):
        response = await self.client.get("/", headers=self.headers)
        self.assertEqual(response.status, 200)
        self.assertIn("Same eyes.", await response.text())
        response = await self.client.get("/api/status", headers=self.headers)
        raw = await response.text()
        self.assertNotIn("test-pairing-token", raw)
        self.assertNotIn("MISTRAL_API_KEY", raw)

    async def test_cross_origin_post_rejected(self):
        response = await self.client.post("/api/readiness", headers={**self.headers, "Origin": "https://evil.invalid"})
        self.assertEqual(response.status, 403)

    async def test_missing_admin_token_rejected(self):
        response = await self.client.post("/api/readiness", headers={"Host": "localhost:8765"})
        self.assertEqual(response.status, 403)

    async def test_dns_rebinding_host_rejected(self):
        response = await self.client.get("/api/status", headers={"Host": "evil.invalid:8765"})
        self.assertEqual(response.status, 403)

    async def test_failed_readiness_never_writes_device(self):
        with patch("voice_gateway.server.device_request", AsyncMock(return_value={"service": "japa_voice_control", "control_token": "boot_nonce", "can_switch": True})) as call:
            response = await self.client.post("/api/provider", headers=self.headers, json={"ip": "192.168.1.20", "gateway_ip": "192.168.1.21", "profile": "gateway"})
            self.assertEqual(response.status, 503)
            self.assertEqual(call.await_count, 1)
            self.assertEqual(call.call_args.args[2], "GET")

    async def test_busy_device_never_switches(self):
        with patch("voice_gateway.server.device_request", AsyncMock(return_value={"service": "japa_voice_control", "control_token": "nonce", "can_switch": False})) as call:
            response = await self.client.post("/api/provider", headers=self.headers, json={"ip": "192.168.1.20", "profile": "xiaozhi"})
            self.assertEqual(response.status, 409)
            self.assertEqual(call.await_count, 1)

    async def test_live_selection_is_host_only_and_mistral_returns(self):
        self.app["live"].key = "test-openai-key"
        self.app["live"].ready = True
        self.provider.ready_until = 1
        voice = {"service": "japa_voice_control", "control_token": "nonce",
                 "can_switch": True, "selected": "gateway"}
        with patch("voice_gateway.server.device_request", AsyncMock(return_value=voice)) as call:
            response = await self.client.post("/api/provider", headers=self.headers,
                                              json={"ip": "192.168.1.20", "profile": "live"})
            self.assertEqual(response.status, 200)
            self.assertTrue(self.app["live_state"]["preferred"])
            self.assertEqual(call.await_count, 1)  # status only; no device write
            response = await self.client.get("/api/status", headers=self.headers)
            self.assertEqual((await response.json())["selected_provider"], "live")
            call.reset_mock()
            response = await self.client.post("/api/provider", headers=self.headers,
                                              json={"ip": "192.168.1.20", "gateway_ip": "192.168.1.21", "profile": "mistral"})
            self.assertEqual(response.status, 202)
            self.assertFalse(self.app["live_state"]["preferred"])

    async def test_unready_websocket_rejected(self):
        response = await self.client.get("/xiaozhi/v1", headers={"Authorization": "Bearer test-pairing-token-123456789", "Protocol-Version": "1"})
        self.assertEqual(response.status, 503)

    async def test_real_websocket_hello_contract(self):
        self.provider.ready_until = 1
        ws = await self.client.ws_connect("/xiaozhi/v1", headers={"Authorization": "Bearer test-pairing-token-123456789", "Protocol-Version": "1"})
        await ws.send_json({"type": "hello", "version": 1, "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1, "frame_duration": 60}})
        hello = await ws.receive_json()
        self.assertTrue(hello["japa_ready"])
        self.assertTrue(hello["session_id"])
        await ws.close()


if __name__ == "__main__": unittest.main()

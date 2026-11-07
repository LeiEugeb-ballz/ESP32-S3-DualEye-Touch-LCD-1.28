"""Mistral SDK 2.9.1. Keys and voice IDs are supplied through the process environment."""
import asyncio
import base64
import os
import time
from mistralai.client import Mistral
from .audio import encode_wav, decode_wav
DEFAULT_MISTRAL_VOICE_ID = "cbe96cf0-85ec-4a10-accb-0b35c93b6dfd"


class MistralProvider:
    def __init__(self):
        self.key = os.environ.get("MISTRAL_API_KEY", "")
        self.voice = os.environ.get("JAPA_MISTRAL_VOICE_ID", "") or DEFAULT_MISTRAL_VOICE_ID
        self.stt_model = os.environ.get("JAPA_STT_MODEL", "voxtral-mini-latest")
        self.chat_model = os.environ.get("JAPA_CHAT_MODEL", "mistral-small-latest")
        self.tts_model = os.environ.get("JAPA_TTS_MODEL", "voxtral-mini-tts-2603")
        self.client = Mistral(api_key=self.key, timeout_ms=20000) if self.key else None
        self.ready_until = 0.0
        self.retry_after = 0.0
        self.last_error = "provider_not_verified"
        self.verify_lock = asyncio.Lock()

    def status(self):
        return {"configured": bool(self.key and self.voice),
                "ready": bool(self.ready_until > time.monotonic()),
                "error": self.last_error,
                "retry_after_seconds": max(0, round(self.retry_after - time.monotonic())),
                "models": {"stt": self.stt_model, "chat": self.chat_model, "tts": self.tts_model}}

    async def verify(self):
        async with self.verify_lock:
            if self.status()["ready"]:
                return True
            if not self.key or not self.voice:
                self.last_error = "missing_MISTRAL_API_KEY_or_JAPA_MISTRAL_VOICE_ID"
                return False
            if time.monotonic() < self.retry_after:
                return False
            try:
                # Real chain probe with generated, non-private audio. No user history.
                async with asyncio.timeout(60):
                    stage = "models"
                    catalog = await self.client.models.list_async(timeout_ms=15000)
                    ids = {model.id for model in catalog.data}
                    if any(name not in ids for name in (self.stt_model, self.chat_model, self.tts_model)):
                        raise ValueError("configured_model_unavailable")
                    stage = "voice"
                    await self.client.audio.voices.get_async(voice_id=self.voice, timeout_ms=15000)
                    stage = "tts"
                    pcm = await self.speech("Hello there.")
                    stage = "stt"
                    if not (await self.transcribe(pcm)).strip():
                        raise ValueError("empty_probe_transcript")
                    stage = "chat"
                    text = "".join([part async for part in self.chat([
                        {"role": "user", "content": "Reply with one word: hello."}])])
                    if not text.strip():
                        raise ValueError("empty_probe_response")
                self.ready_until = time.monotonic() + 900
                self.last_error = None
                return True
            except Exception as error:
                # SDK messages can contain input/headers. Expose only stable type/status.
                self.last_error = f"{stage}:{type(error).__name__}:http_{getattr(error, 'status_code', 'unavailable')}"
                self.ready_until = 0
                self.retry_after = time.monotonic() + 30
                return False

    async def transcribe(self, pcm):
        result = await self.client.audio.transcriptions.complete_async(
            model=self.stt_model, file={"file_name": "turn.wav", "content": encode_wav(pcm)},
            timeout_ms=20000, retries=None)
        return result.text.strip()

    async def chat(self, messages):
        stream = await self.client.chat.stream_async(
            model=self.chat_model, messages=messages, max_tokens=320,
            timeout_ms=20000, retries=None)
        async with stream:
            async for event in stream:
                delta = event.data.choices[0].delta.content
                if isinstance(delta, str) and delta:
                    yield delta

    async def speech(self, text):
        result = await self.client.audio.speech.complete_async(
            model=self.tts_model, input=text, voice_id=self.voice,
            response_format="wav", timeout_ms=20000, retries=None)
        return decode_wav(base64.b64decode(result.audio_data, validate=True))

    async def close(self):
        if self.client:
            await self.client.__aexit__(None, None, None)

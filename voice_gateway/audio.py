"""Real Opus/PCM conversion; PyAV wheels include FFmpeg's libopus implementation."""
import io
from fractions import Fraction
import av

RATE = 16000
SAMPLES = 960  # Xiaozhi's 60 ms output packets


def frame_pcm(frame):
    return bytes(frame.planes[0])[:frame.samples * 2]


def pcm_frame(pcm, rate=RATE):
    frame = av.AudioFrame(format="s16", layout="mono", samples=len(pcm) // 2)
    frame.sample_rate = rate
    frame.planes[0].update(pcm)
    return frame


class OpusCodec:
    def __init__(self):
        self.decoder = av.CodecContext.create("opus", "r")
        self.resampler = av.AudioResampler(format="s16", layout="mono", rate=RATE)
        self.encoder = av.CodecContext.create("libopus", "w")
        self.encoder.sample_rate = RATE
        self.encoder.layout = "mono"
        self.encoder.format = "s16"
        self.encoder.bit_rate = 24000
        self.encoder.time_base = Fraction(1, RATE)
        self.encoder.options = {"frame_duration": "60", "application": "voip"}
        self.encoder.open()
        self.pts = 0

    def decode(self, packet):
        if not 1 <= len(packet) <= 1500:
            raise ValueError("invalid_opus_packet_size")
        result = bytearray()
        for decoded in self.decoder.decode(av.Packet(packet)):
            for frame in self.resampler.resample(decoded):
                result.extend(frame_pcm(frame))
        if len(result) > RATE * 2 * .12:
            raise ValueError("invalid_opus_duration")
        return bytes(result)

    def encode(self, pcm):
        if len(pcm) != SAMPLES * 2:
            raise ValueError("expected_60ms_pcm")
        frame = pcm_frame(pcm)
        frame.pts = self.pts
        frame.time_base = Fraction(1, RATE)
        self.pts += SAMPLES
        return [bytes(packet) for packet in self.encoder.encode(frame)]


def decode_wav(data):
    if len(data) > 16 * 1024 * 1024:
        raise ValueError("tts_audio_too_large")
    out = bytearray()
    resampler = av.AudioResampler(format="s16", layout="mono", rate=RATE)
    with av.open(io.BytesIO(data), format="wav") as container:
        for decoded in container.decode(audio=0):
            for frame in resampler.resample(decoded):
                out.extend(frame_pcm(frame))
            if len(out) > RATE * 2 * 90:
                raise ValueError("tts_audio_too_long")
        for frame in resampler.resample(None):
            out.extend(frame_pcm(frame))
    if not out:
        raise ValueError("empty_tts_audio")
    return bytes(out)


def encode_wav(pcm):
    import wave
    result = io.BytesIO()
    with wave.open(result, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm)
    return result.getvalue()

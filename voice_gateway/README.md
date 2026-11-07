# JAPA voice gateway — development acceptance

An optional host adapter for this ESP32's **16 kHz mono, 60 ms Opus WebSocket
protocol**, not the other device's UDP transport. Xiaozhi remains the firmware
default and its saved credentials are not overwritten. No legacy LED, filesystem,
or general-purpose execution tools are loaded. GPT-Live-1 is an opt-in adapter
when `JAPA_OPENAI_API_KEY` is present; Mistral and Xiaozhi remain fallbacks.

## Environment and launch

Verified on Windows x64, CPython 3.12.10. From the project root:

```powershell
uv venv --python 3.12.10 voice_gateway/.venv
uv pip sync --python voice_gateway/.venv/Scripts/python.exe voice_gateway/requirements.lock.txt
# Set MISTRAL_API_KEY securely in this process; do not put the value in Git.
$env:JAPA_MISTRAL_VOICE_ID = 'cbe96cf0-85ec-4a10-accb-0b35c93b6dfd'
.\voice_gateway\.venv\Scripts\python.exe -m voice_gateway.server
```

For GPT-Live, set `JAPA_OPENAI_API_KEY` as a user or process environment
variable. It must be an OpenAI project key. Leave any existing
`OPENAI_API_KEY`/`OPENAI_BASE_URL` pair unchanged when it serves another
provider. The adapter uses the official server WebSocket at
`wss://api.openai.com/v1/live/sessions`, model `gpt-live-1`, native 16 kHz PCM
at the gateway boundary, and Responses delegation defaulting to `gpt-5.6-luna`.
Readiness checks configuration; `session.started` is the authoritative session
check and Live voice time is billable.

For a hidden local process with logs kept out of source fingerprints, use
`powershell -NoProfile -ExecutionPolicy Bypass -File voice_gateway\start.ps1`
instead of the last command. Runtime logs go in `.jafr/gateway-runtime/`.

The voice ID is the user's existing Jane voice reference, not an API credential.
The API key is process-environment-only. The ignored `.pairing-token` is a separate
random local device pairing credential, retained across gateway restarts. Protect
that file and NVS like other LAN configuration. Do not expose ports to the Internet.

Open **http://localhost:8765** on this PC. Enter the device's Wi-Fi IPv4 and this
PC's reachable Wi-Fi IPv4. Read status, verify the Mistral chain, then select a
provider outside a conversation. The ESP32 also checks the candidate handshake in
a background internal-RAM task and applies only at idle. Verify **Active provider**;
an HTTP 202 response only means the request is pending. A failed check preserves
the old profile. Cloud MQTT/WebSocket NVS configuration is never replaced.

The LAN listener is port 8765. Administration is restricted to loopback plus a
per-process token and Origin/Host checks. Device WebSockets require a separate
Bearer pairing token. This workstation has the explicitly narrow rule
`JAPA voice gateway (private LAN)`: the venv program, TCP 8765, Private profile,
remote `192.168.88.0/24` only. LAN transport is plaintext `ws://`;
use only a trusted private network. Reverse-proxy TLS/public deployment is not set up.

Mistral host must stay on. Use **Use Xiaozhi** to return to the independently hosted
cloud path. If the gateway is down after a device reboot, the selected profile
remains selected; automatic fallback is **not implemented/accepted**. The device
control service still exposes `/voice/status` and protected `/voice/profile` on
port 3233 for explicit return to Xiaozhi. No history transfers between providers.

## Personality and interaction

JAPA, Calm and Playful are separate Mistral system-prompt choices. Changes are
refused while a session is listening/speaking; applying clears session history.
Personality resets to JAPA when the gateway process restarts. Xiaozhi personality
stays in its cloud console. No persistent cross-provider memory is promised.

VAD uses 20 ms frames, 400 ms pre-roll, 80 ms speech onset and a 1 second silence
window, capped at 20 seconds per utterance. Short hesitations remain in one turn.
One streamed chat request generates each reply, with sentence-sized TTS and
real-time-paced Opus delivery. Speaking starts when PCM is available. Firmware
waits for playback/decode completion before listening again. Cancellation clears
queued work and uses generation IDs to reject in-flight results; one already
submitted I2S chunk can still finish. Audio AEC is disabled, so **no acoustic
barge-in/full-duplex acceptance is claimed**. Wake/touch interruption needs human
testing. Timing measurements are evidence, not proof of human naturalness.

## Tests and real provider evidence

```powershell
.\voice_gateway\.venv\Scripts\python.exe -m unittest voice_gateway.test_gateway -v
.\voice_gateway\.venv\Scripts\python.exe -m voice_gateway.probe
```

The 23 offline tests include real codec conversion and HTTP/WebSocket boundaries.
Provider doubles exist only in `test_gateway.py`, never in the running adapter.
The explicit probe makes small paid-or-quota-consuming speech, transcription and
chat calls. Credentials, transcripts and SDK response bodies are not logged.

2026-09-10: both supplied temporary keys passed model/voice/TTS/STT checks but
`mistral-small-latest` chat returned HTTP 429. A separate tiny chat request with the
second key returned error `1300`, allowed requests/minute **0**, remaining **0**.
Those initial responses were **transient**: the user then demonstrated chat success,
our complete readiness probe passed, and `eval_live` completed a real synthetic
WebSocket/Opus → STT → streamed chat → TTS round trip (99 reply packets). The old
429 snapshot is not a current permanent blocker. Rotating keys by itself does not
establish a usable quota. For recurrence, check API organisation/model limits:
https://admin.mistral.ai/plateforme/limits

Run that explicit quota-consuming host integration eval with:
`.\voice_gateway\.venv\Scripts\python.exe -m voice_gateway.eval_live`
after starting the gateway and verifying it in the UI. It uses generated speech,
not user conversation, and logs only counters/outcomes. No hardware profile changes.

At the current physical checkpoint, JAPA is on `2.4GHz` / `192.168.88.211` and this
PC is `192.168.88.252`. Gateway → Xiaozhi → gateway switching/rollback settled at
idle with no device-reported error; the gateway is selected for the next user test.

## GPT-Live boundary

The host recognizes `features.japa_live: true` in a device hello and otherwise
preserves the existing Mistral protocol. The current checked-in firmware does
not advertise that capability: its AEC options remain disabled and it stops
voice processing during ordinary speaking. No hardware GPT-Live acceptance or
full-duplex claim is made yet. The next firmware change must be a guarded,
board-specific AEC/live capability followed by build, serial memory stress,
barge-in and playback acceptance on approved COM13, without changing pins, eye
assets, Xiaozhi defaults or the Mistral fallback.

## Still HIGH release blockers

Real Mistral user conversation and interruption/naturalness acceptance; gateway
output-encoder reset/cancel-boundary hardening; live voice/reconnect memory stress;
connected-startup random greeting; offline AP/network-choice acceptance;
alternate-slot/OTA recovery and failed-gateway reboot recovery.
Gateway tests do not close those. No new eye geometry/effects are part of this work.

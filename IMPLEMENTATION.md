# JAPA release completion

User direction recorded 2026-09-10: clear every HIGH release blocker before new
graphics/capabilities expansion. Start now (supersedes the proposed 10:20 start).

JAPA should feel attentive and alive while listening, considering and speaking.
Every change must serve that continuous interaction. Tests cover correctness,
memory, timing and recovery; the user's ears and eyes provide final naturalness
acceptance. No numerical human-likeness score is claimed.

## Work streams — all HIGH

| Dependency sequence | Work | Acceptance |
| --- | --- | --- |
| A — foundation | Conversation/neutral state and SRAM margin | Neutral never cancels listening; emotion overlays preserve turn state; repeated updates do not restart motion. Voice/reconnect stress records free/minimum/largest internal blocks and errors; adequate reserve demonstrated. |
| B — dependable operation | Truthful web status, network discovery/configuration, fallback AP, OTA/alternate-slot recovery | Real device identity/state/metrics; useful timeout errors; 60-second AP fallback and rejoin demonstrated; switch refuses invalid image and recovery is proven. |
| C — full voice path | Connected-startup greeting and provider/personality control | Greeting runs once per intended startup, does not interrupt the user and is not repetitive. Candidate pipeline validated before an idle-boundary switch; old profile remains recoverable; cancellation rejects stale audio. |
| D — release acceptance | Repeatable full conversations and user listening tests | Fresh build/image identity, voice/eyes/touch/recovery scenarios and provider failure rollback captured. User approves naturalness. Every blocker closes with actual evidence. |

These are dependency groups, not priority rankings. Work in parallel where safe.
New eye scaling or effects wait until the release-blocker list is empty.

## Mistral integration boundary

The user's `voice_pipeline.py` is a host UDP PCM16 gateway for a different device.
Reuse the communication idea (speech detection/transcription, conversational
model, streamed text/speech, session cancellation), not that device's transport,
LED/file tools, global state or hardware assumptions. Keep that input file intact.

Implement a host adapter compatible with JAPA's existing Xiaozhi audio protocol.
Keep the known-working Xiaozhi profile as default; Mistral is an additional profile.
The web UI should expose voice provider and persona separately, show active and
pending selections, verify readiness, apply only when idle, and retain rollback.
Changing a provider must not implicitly export earlier conversation history.

## Natural-interaction acceptance scenarios

- Silence: subtle continuous motion, no unsolicited repeated speech.
- Wake/listen: attention follows actual readiness; neutral emotion cannot erase it.
- Thinking: a real turn is pending; do not pretend speech has started before audio.
- Speaking: motion follows playback energy and ends when playback drains.
- Pause/continue: tolerate natural hesitations without chopping sentences.
- Interrupt: cancel active generation/playback, reject late packets and attend again.
- Failure/switch: bounded recovery, honest UI feedback, no old sentence or persona
  leaking into the next session.
- Human review: test ordinary back-and-forth, corrections, long pauses and short
  interjections with the user; adjust based on their observed discomfort.

## Current preparation facts

The two initial preparation agents were blocked by a temporary usage-limit response;
their tasks were subsequently restarted. Work was distributed as protocol review,
interaction-state implementation and memory audit/implementation. No deferred
10:20 automation was created after the user selected an immediate start.

At the first hardware inventory COM13 was absent. It subsequently appeared as the
expected Espressif 303A:1001 device (ESP32-S3, 8 MB PSRAM, 16 MB flash). Development
firmware 2.2.7-dev1 was rebuilt after a provider-probe PSRAM-stack crash, flashed to
COM13 without NVS, and accepted through gateway/Xiaozhi/gateway profile switches. The
current physical gate is user conversation/touch/interruption acceptance and recovery
evidence, not USB or LAN reachability. See STATUS.md for current addresses and passed
host/live evals.

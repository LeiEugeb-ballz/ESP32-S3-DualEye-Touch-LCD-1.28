# JAPA development contract

User direction, 2026-09-10. JAPA is the current owned project; use old checkpoints
for recovery/provenance, not as a mandate to replace this implementation upstream.

- All open release blockers and capability gaps are HIGH priority. Work order is
  determined by dependencies, not by downgrading any item. No new graphic effects
  or eye-geometry work until all release blockers are resolved and accepted.
- Natural, human-like interaction is the core product requirement in every task.
  Preserve attentive listening, subtle continuous idle motion, sensible pauses,
  emotion layered over conversation state, and speaking aligned to actual audio.
  Handle interruption, cancellation, silence, and failure without abrupt robotic
  state changes, repeated greetings, or speech leaking from a previous turn.
- Fixing listening/neutral interference is a release fix, not a graphics expansion.
  Do not reset gaze/blink motion just because the same state is reported twice.
- Automated tests establish behaviour and timing; they cannot certify a percentage
  of human naturalness. The user owns final subjective conversational acceptance.
- Preserve the working Xiaozhi default and rollback checkpoints. Treat
  `voice_pipeline.py` as the user's communication reference only: do not execute,
  transplant unrelated ESP32-S31 hardware controls, expose secrets, or enable its
  filesystem tools. Build an isolated compatible host adapter for Mistral.
- Provider selection and personality selection are separate user controls.
  Validate a candidate before switching; switch only at a safe idle boundary.
  Keep a recoverable working profile, bounded state/queues, clear status and errors.
  Do not silently send conversation history to a newly selected provider.
- Complete current evidence, source review, host tests, build and physical
  acceptance for affected behaviour. Never mark unavailable hardware/provider/user
  acceptance as passed or clear a blocker merely by renaming it a recommendation.
- See `IMPLEMENTATION.md` for work streams and acceptance. Existing hardware
  approvals in this conversation remain valid; verify the actual attached board
  before flashing. Preserve NVS, known-working hardware mappings and eye assets.

Implementation was initially requested for 10:20 Johannesburg on 2026-09-10.
The user explicitly superseded that time with **Start implementation now**.

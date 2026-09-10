# JAFR DualEye Companion status

Assessed 2026-09-10 (Africa/Johannesburg). **Development baseline finalised;
ready to begin graphics/capabilities work. Full production acceptance is pending.**

The active project is this repository; firmware is in `xiaozhi-esp32/`.
Source baseline: `93ea20824b85609be6aff9c679e828c67e60c194`, including SRAM changes
`b42995d` and MQTT speak-request support `c8d686d`.
The untracked `xiaozhi-esp32/checkpoints/flash_step1.py` predates this handover
and has been preserved. It is not included in the source checkpoint commit.

User milestone, recorded 2026-09-06: **"BEST AND FURTHERIST ON PROJECT"**.

## Evidence

| Check | Result and limits |
| --- | --- |
| Current environment/build | September 10 incremental ESP-IDF build passed from the promoted path. ESP-IDF 5.5.5, Xtensa GCC 14.2.0; exact versions and input hashes in the manifest. This is local reproducibility, not a fresh-machine dependency installation test. |
| Host acceptance | 43 checks passed: generated/source configuration agreement, correct board, flash layout, app size, wake-word assets, linked eye/audio/boot-control implementations, valid application image. |
| Firmware | App version 2.2.6, 3,904,864 bytes; each 4,128,768-byte OTA app slot has 223,904 bytes free (about 5.4%). This is flash capacity, not runtime RAM. |
| September 9 hardware evidence | Local flash log verifies five images. Serial capture shows Hi ESP detection, two speaking turns, eye movement and both touch controls. No panic/reset appears in the captured interval. |
| Evidence limitation | That serial capture starts after boot and has no firmware hash/version header. Its association with the flash relies on timestamps and image size; it is not exact running-image proof or a long soak test. |
| Workspace status | `.jafr/status.json` records stage results and blockers. Generated build logs/caches no longer invalidate the source fingerprint; board code, sdkconfig and dependency lock changes do. |

Evidence files:

- [.jafr/status.json](.jafr/status.json): current machine-readable assessment.
- [build log](.jafr/evidence/2026-09-10-build.log) and [host checks](.jafr/evidence/2026-09-10-host-checks.log).
- [artifact/configuration manifest](.jafr/evidence/baseline-artifacts.json): SHA256 hashes and environment details.
- [runtime summary](.jafr/evidence/2026-09-09-runtime-summary.json): extracted technical results and hashes of the original local logs; no conversation text or credentials.
- [workspace tests](.jafr/evidence/2026-09-10-workspace-tests.log).
- [audit and acceptance boundaries](.jafr/evidence/2026-09-10-assessment.md).

## Confirmed implementation and open work

There are 11 eye presets (IDs 0–10), an active direct-DMA dual-display renderer,
gaze/blink/pupil behaviour, touch preset controls and PCM-energy pupil response.
The current assets contain the English `wn9_hiesp` wake word. The local logs prove
voice interaction on this board; they do not establish a particular cloud persona.

Release blockers and capability gaps, in priority order:

1. **Internal SRAM margin:** the September 9 conversation reached a recorded minimum
   of **972 bytes**. The board continued running, but repeated conversation and
   reconnect tests with minimum free memory, largest free block and allocation
   failures recorded are needed before release. Existing queue/buffer improvements
   are retained; no additional renderer buffers should be introduced casually.
2. **Listening/connecting eye state:** `application.cc` sends a status followed by
   neutral emotion; both overwrite the same renderer state. The neutral update
   cancels the intended listening/connecting profile. This is the first graphics fix.
3. **Random greeting:** MQTT `speak_request` reception is linked in the app; a random
   greeting trigger/sender and a successful startup-greeting capture are outstanding.
4. **Web control:** the desktop companion offers local previews and boot controls.
   It has no implemented OpenAI/Mistral voice-provider configuration. Its Connect
   action probes port 80 even though normal boot status uses port 3233; timeout
   feedback is generic. Device-reported preset/FPS fields also contain fixed values
   and must not be treated as measurements.
5. **Remaining hardware acceptance:** no recorded end-to-end test of one-minute
   offline AP recovery, multi-network choice, OTA, or alternate-slot boot/recovery.
   Two OTA slots alone do not prove two working installed firmware personalities.
   Device and server AEC are disabled in the current configuration; barge-in is unproven.

## Next graphics/capabilities pass

1. Preserve this checkpoint and fix the listening/neutral state interaction.
2. Add the requested proportionate eye scale adjustment as an optional control,
   keeping the current visual default. Compare both eyes, eyelid clipping, gaze,
   touch, and speech at each scale; retain the existing hardware mapping.
3. Measure frame times and internal SRAM while speaking before adding richer effects.
4. Complete random connected-startup greetings and truthful web status; then implement
   provider configuration against a real voice backend with end-to-end acceptance.

## Resume and refresh

From `xiaozhi-esp32/`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -BuildDir build_safe -NoDriveAlias
pwsh -NoProfile -File tools\verify_baseline.ps1
```

Use the same build-directory/drive-alias choice for an existing CMake cache.
Capture new command output to new evidence logs when making a new assessment.
From the parent JAFR workspace, `pwsh -NoProfile -File tools\workspace_check.ps1 -Project Eyes -Json`
checks freshness. After reviewing new evidence, update the stage records and use
`tools/record_status.ps1 -Project Eyes -InputFile <assessment.json>`.
Do not re-stamp an old assessment after source changes without rerunning its checks.

The local snapshot `.jafr/checkpoints/2026-09-10-baseline/` preserves app, bootloader,
partition table, OTA metadata and speech assets, with their SHA256 manifest. Binary
snapshots are ignored by Git; the evidence and source checkpoint are tracked. No
flash or live hardware operation was performed for this September 10 closeout.

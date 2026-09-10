# Baseline audit and acceptance record

Assessed 2026-09-10; scope: close the missing evidence record after project
promotion and prepare a preserved baseline for graphics/capabilities work.
This is not an upstream equivalence claim or a complete firmware/security audit.

## Commands and evidence

- `powershell -NoProfile -ExecutionPolicy Bypass -File xiaozhi-esp32/tools/build_firmware.ps1 -BuildDir build_safe -NoDriveAlias`: exit 0, timestamps in `2026-09-10-build.log`.
- `pwsh -NoProfile -File xiaozhi-esp32/tools/verify_baseline.ps1`: exit 0, 43 host artifact/configuration/linkage checks; timestamps in `2026-09-10-host-checks.log`.
- `pwsh -NoProfile -File tools/test_workspace.ps1` from the outer workspace: results in `2026-09-10-workspace-tests.log`.
- Read existing September 9 flash/serial logs. Runtime summary retains their SHA256 hashes, timestamps, state transitions and minimum SRAM, with no conversational/private payloads.

## Findings verified against local source

Paths below are relative to `xiaozhi-esp32/` at the source baseline `93ea208`.

| Finding | Evidence |
| --- | --- |
| 11 preset IDs including Japanese at 1 | `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/eye_presets.h:9-21` |
| Actual eye renderer, audio and MQTT greeting implementation linked | ELF symbol checks in host-check log; compile/flash manifest inspected |
| Eye pupil activity receives PCM energy | `main/audio/audio_service.cc:312-324` |
| Neutral cancels connecting/listening eye profiles | `main/application.cc:954-961`; board `display_manager.cc:1010-1027` shares one state for status and emotion |
| Internal SRAM metric, not PSRAM | `main/system_info.cc:149-151`; serial log records minimum 972 after speaking |
| Greeting receiver exists, random startup sender absent | `main/application.cc:593-609,853-899`; local web companion has no speak-request sender |
| Port mismatch and generic boot timeout feedback | `web_companion/index.html:540,556-585`; normal boot server is port 3233 |
| Web preview buttons affect browser canvas only | `web_companion/index.html:940-946`; no provider setup/API implementation found |
| Fixed status values are not runtime measurements | board `esp32-s3-dualeye-lcd-1.28.cc:473-476` hardcodes preset/FPS/service descriptions |
| One-minute AP fallback implemented, not accepted on hardware here | `main/boards/common/wifi_board.cc:29,91-105,154-169` |
| Boot/OTA services started but slot acceptance unproven | board `esp32-s3-dualeye-lcd-1.28.cc:458-466`; `partitions/v2/16m.csv` |
| Current AEC disabled | generated SDK config `USE_DEVICE_AEC=false`, `USE_SERVER_AEC=false` |

## Stage decisions

- Audit passed for baseline inventory and documented limitations.
- Environment passed for the currently installed toolchain; lock/config SHA256
  values are recorded. Fresh-machine lock restoration was not tested.
- Build passed; this closeout used an incremental build from the relocated cache.
- Tests passed for host build artifacts/configuration; no claim of full firmware unit coverage.
- Regression passed for unchanged firmware source plus host checks and workspace
  fingerprint tests. No live hardware regression was executed today.
- Evals blocked: historical voice/touch evidence is useful, but full scenario/soak,
  image-identity and recovery acceptance is incomplete.
- Fault correction blocked overall: evidence/tooling/doc faults corrected; memory,
  visual state and web control defects are still open.
- Production blocked by these remaining acceptance gaps and defects.

## Changes in this closeout

Workspace fingerprinting now excludes specifically named generated ESP-IDF build
directories and dated relocation caches. Tests verify build-log changes do not
invalidate source evidence, but board source, sdkconfig, dependencies.lock and
other build-prefixed source inputs still do. No firmware implementation, pin,
display timing or flash contents changed. Docs now distinguish historical Step 2
recovery files from current development artifacts, and correct stale flash/voice
statements and the preset ID table.

## Next acceptance criteria

- Record firmware identity at boot, both displays and touch, Hi ESP and repeated
  speaking/listening cycles. Record SRAM minimum/largest-block and failures; assess
  adequate workload-specific reserve before a production pass.
- Verify connecting/listening visuals survive neutral emotion and speaking pupil
  response still follows audio after the graphics state fix.
- Exercise AP fallback after 60 seconds without a reachable saved network,
  reconfiguration/reconnection, OTA and selected-slot rollback with recovery available.
- Demonstrate a random connected-startup greeting and provider selection using the
  actual backend. Keep unimplemented options explicitly unavailable in the UI.

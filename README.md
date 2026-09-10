# JAFR DualEye Companion

Xiaozhi Dual-Eye AI Companion firmware for the **Waveshare ESP32-S3-DualEye-Touch-LCD-1.28** board.

## Current development baseline — 2026-09-10

The relocation and evidence handover are complete. Start with [STATUS.md](STATUS.md)
for confirmed behaviour, remaining defects, and the next graphics/capabilities work.
The workspace reads [.jafr/status.json](.jafr/status.json); it records current evidence
without claiming complete production acceptance. Current local build checks pass;
the September 9 logs demonstrate wake-word interaction, voice replies, moving eyes,
and touch. Conversation memory margin remains an open release blocker.

## Project Structure

Workspace path:

`C:\Users\txr45\Documents\Project TakeOver\JAFR-Workspace\JAFR-DualEye-Companion`

```
xiaozhi-esp32/          Production firmware (ESP-IDF 5.5.2+)
├── main/               Application source code
│   └── boards/waveshare/esp32-s3-dualeye-lcd-1.28/
│                       Board-specific hardware drivers, display engine, and eye presets
├── docs/               Technical documentation
└── tools/              Build, flash, monitor, and conversion utilities
```

## Quick Start

See [xiaozhi-esp32/README.md](xiaozhi-esp32/README.md) for build instructions, hardware specs, and documentation.

From `xiaozhi-esp32/`, the relocation-safe build command is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -BuildDir build_safe -NoDriveAlias
pwsh -NoProfile -File tools\verify_baseline.ps1
```

Flash only after explicit hardware approval. The COM13 helper now resolves build artifacts relative to this repository instead of the old standalone checkout path.

## Firmware & checkpoint registry

Current development artifacts and SHA256 hashes are recorded in
[baseline-artifacts.json](.jafr/evidence/baseline-artifacts.json). A local copy of
the five images is preserved under `.jafr/checkpoints/2026-09-10-baseline/`.
This copy excludes NVS and is not a dump of the running device.

The older `xiaozhi-esp32/checkpoints/step2_10_with_wifi.bin` is a historical recovery
image, not the latest source build. It may overwrite saved settings if restored.
See the [checkpoint registry](xiaozhi-esp32/checkpoints/CHECKPOINT_REGISTRY.md)
before selecting any restoration image.

## Hardware

* **SoC**: ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz
* **Memory**: 16 MB SPI Flash + 8 MB Octal PSRAM
* **Displays**: Dual 1.28" Round IPS LCDs (GC9A01, 240×240)
* **Touch**: Dual CST816S Capacitive Touch Controllers
* **Audio**: ES8311 output codec + ES7210 input ADC. Current device/server AEC settings are disabled; see STATUS.md.

## Links

* [Waveshare Product Page](https://www.waveshare.net/shop/ESP32-S3-DualEye-LCD-1.28.htm)

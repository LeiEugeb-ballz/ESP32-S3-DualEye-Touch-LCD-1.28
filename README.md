# JAFR DualEye Companion

Xiaozhi Dual-Eye AI Companion firmware for the **Waveshare ESP32-S3-DualEye-Touch-LCD-1.28** board.

## Baseline Version — v3.0.0.1 (Eval+Review Phase)

`v3.0.0.1` represents the integrated Eval+Review release baseline on the verified
Waveshare ESP32-S3-DualEye hardware. All legacy checkpoint names and stale baselines
have been consolidated. Physical hardware acceptance on COM13 confirmed zero allocation
failures, stable ~99KB free SRAM, responsive wake-word ("Hi ESP"), dual GC9A01 eye
rendering, and clean boot control.

Key architectural updates in v3.0.0.1:
- **Unified Control WebUI**: Consolidated into `xiaozhi-esp32/web_ui/index.html` (and served via `http://localhost:8765`), combining photorealistic eye simulation (11 presets), real-time telemetry, Mistral/Xiaozhi provider switching, and dual-slot OTA recovery.
- **Dynamic COM & IP Discovery**: `flash_current_build.py` auto-detects ESP32 serial ports dynamically; the WebUI probes and remembers dynamic board IPs (`192.168.88.x`). Legacy static flasher scripts are quarantined.
- **Automatic Gateway Fallback**: If the host voice gateway is asleep or unreachable, the device automatically falls back to Xiaozhi cloud rather than blocking user interaction.
- **True Random Dynamic Greeting**: Connecting to the companion triggers a spontaneous, LLM-generated greeting with high-entropy randomness rather than a canned drafted list.
- **Acoustic Echo Cancellation (AEC)**: Formally documented as an opt-in feature flag in `sdkconfig.defaults` (`CONFIG_USE_DEVICE_AEC`). Disabled by default to preserve internal SRAM headroom during conversation.
- **Evaluations Unblocked**: Removed blocking dependency on external OpenAI keys; Mistral Voxtral (STT/Chat/TTS) verified as primary default provider.

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
voice_gateway/          Optional Mistral host, local control UI, reproducible environment and evals
```

## Quick Start

See [xiaozhi-esp32/README.md](xiaozhi-esp32/README.md) for build instructions, hardware specs, and documentation.

From `xiaozhi-esp32/`, the relocation-safe build command is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_firmware.ps1 -BuildDir build_safe -NoDriveAlias
pwsh -NoProfile -File tools\verify_baseline.ps1
```

Flash only after explicit hardware approval. The COM13 helper now resolves build artifacts relative to this repository instead of the old standalone checkout path. Use `pwsh` (PowerShell 7), not Windows PowerShell 5.1, for the `#Requires 7.0` evidence scripts.

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

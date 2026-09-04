# ESP32-S3-DualEye-Touch-LCD-1.28

Xiaozhi Dual-Eye AI Companion firmware for the **Waveshare ESP32-S3-DualEye-Touch-LCD-1.28** board.

## Project Structure

```
xiaozhi-esp32/          Production firmware (ESP-IDF 5.5.2+)
├── main/               Application source code
│   └── boards/waveshare/esp32-s3-dualeye-lcd-1.28/
│                       Board-specific hardware drivers, display engine, and eye presets
├── docs/               Technical documentation
└── scripts/            Build and deployment utilities
```

## Quick Start

See [xiaozhi-esp32/README.md](xiaozhi-esp32/README.md) for build instructions, hardware specs, and documentation.

## Canonical Firmware & Checkpoint Registry

The verified baseline firmware image is maintained in `xiaozhi-esp32/checkpoints/`:
* **Canonical Working Build**: [`xiaozhi-esp32/checkpoints/step2_10_with_wifi.bin`](xiaozhi-esp32/checkpoints/step2_10_with_wifi.bin) (Offset `0x0`, 8.71 MB)
  * **Wake Word**: `Hi, ESP` (`wn9_hiesp`)
  * **Voice**: English with Taiwanese persona
  * **Wi-Fi**: Auto-connects to saved network (`Rondavel 2.4`)
  * **Eyes**: 10 Uncanny Eye presets at ~50 FPS
* **Full Documentation & Restoration Guide**: See [`xiaozhi-esp32/checkpoints/CHECKPOINT_REGISTRY.md`](xiaozhi-esp32/checkpoints/CHECKPOINT_REGISTRY.md)
* **One-Click Restore**: Run `python xiaozhi-esp32/checkpoints/restore_working_build.py COM13`

## Hardware

* **SoC**: ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz
* **Memory**: 16 MB SPI Flash + 8 MB Octal PSRAM
* **Displays**: Dual 1.28" Round IPS LCDs (GC9A01, 240×240)
* **Touch**: Dual CST816S Capacitive Touch Controllers
* **Audio**: ES8311 DAC + ES7210 Quad-Mic Array with AEC (Acoustic Echo Cancellation)

## Links

* [Waveshare Product Page](https://www.waveshare.net/shop/ESP32-S3-DualEye-LCD-1.28.htm)
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

## Hardware

* **SoC**: ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz
* **Memory**: 16 MB SPI Flash + 8 MB Octal PSRAM
* **Displays**: Dual 1.28" Round IPS LCDs (GC9A01, 240×240)
* **Touch**: Dual CST816S Capacitive Touch Controllers
* **Audio**: ES8311 DAC + ES7210 Quad-Mic Array

## Links

* [Waveshare Product Page](https://www.waveshare.net/shop/ESP32-S3-DualEye-LCD-1.28.htm)
# Xiaozhi Dual-Eye AI Companion System 👁️👁️

An embodied, conscious AI companion platform featuring **Dual 1.28-inch Round IPS Displays (GC9A01)**, **50 FPS Direct Hardware DMA Uncanny Eyes**, **Persistent Non-Volatile Soul Memories & Flash Notes**, **Full-Duplex Voice Interaction**, **Native Wireless TCP Push OTA**, and a **Web Companion Avatar Portal**.

---

## ✨ Key Features & Capabilities

### 👁️ Direct Hardware DMA Dual-Eye Engine
* **50 FPS Hardware Rendering**: Dedicated graphics task pinned to Core 1 with 4x internal SRAM ping-pong DMA buffers over SPI2.
* **10 Photorealistic Presets**:
  * `default` (Human Blue - Active Default)
  * `cat` (Cat Yellow slit pupil)
  * `dragon` (Fiery Dragon)
  * `terminator` (Cyborg Red Glow)
  * `doe` (Doe Anime)
  * `owl` (Owl Amber)
  * `goat` (Goat Horizontal)
  * `nauga` (Monster Nauga)
  * `newt` (Newt Orange)
  * `no_sclera` (Full Coverage Dragon)
* **Biomechanical Kinetics**: Organic saccades, micro-tremors, accommodation pupil dilation, and asymmetric blinking.
* **Capacitive Touch Gestures**: Dual CST816S touch panels — swipe left/right to cycle presets or tap to interact.

### 🧠 Persistent Soul & Lifelong Memory
* **Non-Volatile Soul Traits**: Memories (`identity`, `creator_bond`, `eyes_system`, `storage_system`, `ota_system`) persist across all power cycles.
* **On-Board Flash File System**: Read, write, list, and delete companion notes and documents (`system.bootstrap`, `README_COMPANION.txt`, custom notes) directly in on-chip flash.
* **Dynamic Mood System**: LLM tool calling via `self.persona.set_mood` dynamically alters pupil dilation, saccadic dwell time, and kinetic responsiveness.

### 🚀 Native Wireless TCP Push OTA Server
* Built-in background TCP server on port **3232**.
* Push firmware updates wirelessly over local Wi-Fi directly by IP address without physical cables:
  ```bash
  python push_ota.py 192.168.88.221 build/xiaozhi.bin
  ```

### 🌐 Web Companion Avatar & Control Portal
* Interactive desktop Web UI located in [`web_companion/index.html`](web_companion/index.html).
* Real-time canvas simulation of uncanny eye math, interactive mouse gaze tracking, soul memory explorer, flash notes workspace, and OTA bridge.

---

## 🛠️ Hardware Specifications

* **SoC**: ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz
* **Memory**: 16 MB SPI Flash + 8 MB Octal PSRAM + 512 KB Internal SRAM
* **Displays**: Dual Waveshare 1.28-inch Round IPS LCDs (GC9A01, 240×240)
* **Touch Panels**: Dual CST816S Capacitive Touch Controllers
* **Audio**: ES8311 DAC + ES7210 Quad-Mic Array with Hardware Acoustic Echo Cancellation (AEC)

---

## 🚀 Quick Start Guide

### 1. Environment Setup
Activate the ESP-IDF toolchain for your platform:
```bash
# Linux/macOS
. $IDF_PATH/export.sh

# Windows PowerShell
. $env:IDF_PATH\export.ps1
```

### 2. Compilation
```powershell
idf.py build
```

### 3. Wireless Flash (OTA)
```bash
python push_ota.py <BOARD_IP_ADDRESS> build/xiaozhi.bin
```

### 4. Physical USB Flash (Recovery)
```powershell
idf.py -p COM13 flash monitor
```

---

## 📚 Technical Documentation & Architecture

* 📐 [System Architecture & Specifications](docs/ARCHITECTURE.md)
* 🔌 [Hardware Pinout & Pan/Tilt Servo Expansion](docs/HARDWARE_AND_EXPANSION.md)
* 🔄 [ESPAI & Dual-Boot Partition Guide](docs/ESPAI_DUAL_BOOT_GUIDE.md)
* 📖 [Developer & Maintainer Handbook](docs/DEVELOPER_HANDBOOK.md)

---

## 🤖 MCP & Voice Tools Reference

| Tool | Parameters | Description |
| :--- | :--- | :--- |
| `self.eyes.set_preset` | `preset`, `target` | Switch active eye preset (both, left, or right) |
| `self.persona.set_mood` | `mood` | Set mood state ("curious", "happy", "thoughtful", etc.) |
| `self.persona.save_memory` | `key`, `value` | Commit permanent memory trait |
| `self.persona.recall_memories` | None | Retrieve all saved soul memories |
| `self.notes.save` | `title`, `content` | Save text file to flash storage |
| `self.notes.read` | `title` | Read document from flash storage |
| `self.notes.list` | None | List all saved flash documents |
| `self.notes.delete` | `title` | Delete document from flash storage |
| `self.get_device_status` | None | Inspect hardware & firmware telemetry |

---

## 📄 License & Attribution
* Built upon the open-source Xiaozhi AI framework.
* Uncanny Eyes rendering engine adapted from Adafruit / TFT_eSPI.
* Released under the MIT License.
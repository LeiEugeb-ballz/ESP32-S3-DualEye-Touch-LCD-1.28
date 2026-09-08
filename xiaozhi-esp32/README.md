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
* Interactive desktop Web UI located in [`web_companion/index.html`](web_companion/index.html). This is a local desktop companion page, not an ESP32-hosted captive portal.
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

## Current Hardware Checkpoint — 2026-09-06

User checkpoint note dated 2026-09-06: **"BEST AND FURTHERIST ON PROJECT"**.

Current preserved baseline:
* The idle Uncanny Eyes renderer is the known-good visual state. Do not alter eye geometry, display pin mappings, SPI panel setup, or touch mappings without measured evidence.
* Startup LCD self-test intentionally flashes color phases before normal eye motion. This proves both displays receive live DMA frames.
* Voice wake baseline is English `Hi ESP` using the `wn9_hiesp` WakeNet model. The app binary and `generated_assets.bin` must be built/flashed together.
* A bottom text strip during boot is expected only for valid short ASCII status/chat text. UTF-8 or unsupported characters are sanitized to avoid garbage glyphs.
* Requested visual refinement not yet applied: scale the eye/corner geometry down so the inner caruncle/tear-duct corner sits closer to the nose-side corner while preserving the current successful eye ratio.

Memory-pressure checkpoint dated 2026-09-08:
* Conversation audio queue depth is intentionally capped at about 1.08 seconds instead of 2.4 seconds to avoid storing unnecessary Opus backlog in SRAM.
* The Opus encoder and MQTT encrypted-audio send path reuse buffers instead of allocating new buffers for every audio packet.
* Optional custom JSON message handling frees printed cJSON strings immediately after use.
* Build and USB flash to COM13 succeeded with `build_safe/xiaozhi.bin` size `0x3b8b10`; app partition free space was `0x374f0`.
* A 180-second post-flash serial monitor showed stable boot, Wi-Fi, MQTT, WakeNet/AFE load, and moving idle eyes with minimum SRAM `13240`. A second idle-only monitor pass stayed stable with minimum SRAM `17352` and free SRAM around `28167-28683`. A wake-word conversation pass should still be monitored after this checkpoint to confirm the new conversation-time low-water mark.

Web/config reality:
* If the Xiaozhi console shows the device online, the board has joined Wi-Fi and the fallback AP is not expected to broadcast.
* The ESP32 config AP appears only when Wi-Fi provisioning/known-network connection fails or config mode is deliberately triggered.
* The local visual/test page is `web_companion/index.html`.
* The board boot/status endpoint is `http://<device-ip>:3233/boot/status` when the board is connected to Wi-Fi.

### 1. Environment Setup
Activate the ESP-IDF toolchain for your platform:
```bash
# Linux/macOS
. $IDF_PATH/export.sh

# Windows PowerShell
. $env:IDF_PATH\export.ps1
```

### 2. Safe local compilation

On Windows, use the checked-in build wrapper. It creates a temporary short drive alias before invoking ESP-IDF, avoiding failures from spaces in paths such as `Project TakeOver` and reducing Windows object-path length risk.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

The default output directory is `build_safe/`. This is a build-only command; it does not flash hardware. After changing wake-word or language config, verify the build log says `wakenet models: wn9_hiesp (will be packaged)`.

If your ESP-IDF install is not in `C:\Espressif\frameworks\esp-idf`, set `IDF_PATH` first.

If the ESP-IDF Python launcher is broken on Windows, set `ESP_PYTHON` to a working Python executable before running the wrapper. The wrapper also normalizes `PROCESSOR_ARCHITECTURE` and adds the ESP-IDF venv site-packages path when `ESP_PYTHON` is used.

### 3. Direct ESP-IDF compilation

Use this only when your checkout path has no spaces and is short enough for Windows object paths:

```powershell
idf.py build
```

### 4. Wireless Flash (OTA)

Only run this when you intentionally want to update a connected board.

```bash
python push_ota.py <BOARD_IP_ADDRESS> build/xiaozhi.bin
```

### 5. Physical USB Flash (Recovery)

Only run this when you intentionally want to write firmware over USB.

```powershell
idf.py -p COM13 flash monitor
```

Or use the project-local safe flasher, which waits for the board and flashes the matching app plus speech assets from `build_safe/`:

```powershell
python .\tools\flash_current_build.py --port COM13 --wait 180 --monitor 30
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

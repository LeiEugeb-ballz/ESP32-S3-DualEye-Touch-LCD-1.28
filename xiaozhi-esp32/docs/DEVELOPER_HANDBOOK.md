# Developer & Maintainer Handbook

This handbook contains all essential commands, workflows, and procedures needed to build, flash, update, test, and maintain the **Xiaozhi Dual-Eye AI Companion System**.

Current assessment: [project STATUS.md](../../STATUS.md), updated 2026-09-10.
The local ESP-IDF 5.5.5 build and 43 host checks pass. Existing September 9 logs
confirm voice/touch/eye operation, but conversation minimum internal SRAM was
972 bytes. Production acceptance remains open; the graphics baseline is preserved.
Use `tools/build_firmware.ps1 -BuildDir build_safe -NoDriveAlias` for the current
relocated cache and `pwsh -File tools/verify_baseline.ps1` for host validation.

---

## 1. Environment Setup

### Prerequisites
* **ESP-IDF v5.5.2+**
* **Python 3.10+** (with `pyserial`)

### Initializing Environment
Activate the ESP-IDF toolchain:
```bash
# Linux/macOS
. $IDF_PATH/export.sh

# Windows PowerShell
. $env:IDF_PATH\export.ps1
```

---

## 2. Build & Compilation Workflows

### Standard Build
```bash
cd xiaozhi-esp32
idf.py build
```

### Full Clean Reconfiguration
Whenever new `.cc` or `.h` source files are added to a board folder:
```powershell
idf.py reconfigure
idf.py build
```

---

## 3. Flashing Procedures

### Current Known-Good Target — 2026-09-06

User checkpoint note dated 2026-09-06: **"BEST AND FURTHERIST ON PROJECT"**.

For the Waveshare ESP32-S3 DualEye 1.28 board, the current target behavior is:
* keep the existing idle Uncanny Eyes appearance intact;
* keep the direct DMA dual-GC9A01 rendering path active;
* use English UI/persona assets;
* package `wn9_hiesp` for the local wake phrase **"Hi ESP"**;
* flash the app and assets partition together after changing wake-word config;
* avoid flashing NVS when preserving saved Wi-Fi credentials;
* do not change hardware mappings or eye geometry without evidence and a rollback checkpoint.

The wake-word proof point during build is:

```text
wakenet models: wn9_hiesp (will be packaged)
```

The runtime proof points after flashing are:
* startup color sweep appears on both displays;
* idle eyes continue moving/blinking;
* serial logs show Wi-Fi connection and Xiaozhi/MQTT connection;
* serial logs show the WakeNet/AFE pipeline loaded;
* saying **"Hi ESP"** transitions the device into listening/responding.

Memory stability note: if serial logs show `esp-aes: Failed to allocate memory`
after WakeNet/AFE starts, keep the eye DMA slice size conservative. The current
checkpoint uses one 20-scanline internal-DMA transfer buffer per display. That
preserves the same eye geometry while reducing internal SRAM pressure for
WakeNet, TLS, audio, and MQTT.

2026-09-08 memory-pressure hardening:
* Audio packet queues are capped at about 1.08 seconds of 60 ms Opus frames instead of 2.4 seconds.
* The Opus encoder output buffer is allocated once and reused by the codec task.
* MQTT audio encryption reuses nonce and ciphertext buffers across packets and releases them when the audio channel closes.
* Optional custom JSON message logging now frees `cJSON_PrintUnformatted()` output.
* Verified build artifact: `build_safe/xiaozhi.bin` size `0x3b8b10`, with `0x374f0` app partition free space.
* Historical idle runtime baseline: USB flash and 180-second monitor on COM13, Wi-Fi connected, MQTT connected, WakeNet/AFE loaded, idle eyes moving, no crash/reset, minimum SRAM `13240`. A later idle-only monitor pass stayed stable with minimum SRAM `17352` and free SRAM around `28167-28683`. Newer September 9 logs show two voice exchanges and a minimum of only `972` bytes; see current STATUS.md for the open memory-margin acceptance issue.

2026-09-09 greeting/speak-request support:
* The device handles inbound MQTT `speak_request` only while idle.
* Handling sequence: optional preview text -> `connecting` -> open normal audio channel -> send UDP NAT activation packets -> `speaking` -> send `speak_ready`.
* The normal wake-word/listening flow remains unchanged.
* WebSocket cannot receive an idle server push in this design; use MQTT for server-initiated greetings.
* A random startup greeting requires a server sender using `speak_request`; that sender/boot trigger is not implemented by the checked-in web companion. Do not fake it with long wake-word `detect` text.
* Verified build artifact: `build_safe/xiaozhi.bin` size `0x3b9560`, with `0x36aa0` app partition free space. September 9 flash logs verify this image size and matching assets; subsequent serial logs prove normal voice interaction, not a greeting. The serial capture has no boot hash/version header.

### Option 1: Wireless TCP Push OTA (Recommended)
Flash updated firmware without physical cables over your local network:
```bash
python push_ota.py <BOARD_IP_ADDRESS> build/xiaozhi.bin

# Example:
python push_ota.py 192.168.88.221 build/xiaozhi.bin
```

### Option 2: Physical USB Serial (COM Port)
When recovering a blank board or performing initial bootstrap:
```powershell
idf.py -p COM13 flash monitor
```
Or with `esptool.py` directly:
```powershell
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 build\bootloader\bootloader.bin 0x8000 build\partition_table\partition-table.bin 0xd000 build\ota_data_initial.bin 0x20000 build\xiaozhi.bin 0x800000 build\generated_assets.bin
```

For this workspace, prefer the project-local flasher after building with `tools\build_firmware.ps1`:

```powershell
python .\tools\flash_current_build.py --port COM13 --wait 180 --monitor 30
```

It uses this checkout's `build_safe/` directory and flashes the app plus `generated_assets.bin` together.

### Option 3: Instant Golden Checkpoint Recovery
To revert immediately to the canonical tested build with "Hi ESP" wake word, English/Taiwanese persona, Wi-Fi credentials, and all 10 presets:
```powershell
python checkpoints/restore_working_build.py COM13
```
See [`checkpoints/CHECKPOINT_REGISTRY.md`](../checkpoints/CHECKPOINT_REGISTRY.md) for full history and partition breakdown.

---

## 4. Uncanny Eye Presets Pack

The firmware defines 11 eye presets; IDs below match `eye_presets.h`:

| ID | Preset Key | Display Style | Pupil Shape | Notes |
| :---: | :--- | :--- | :--- | :--- |
| **0** | `default` | Human Blue | Circular | Default on boot |
| **1** | `japanese` | Japanese | Preset-defined | Additional preset |
| **2** | `cat` | Cat Yellow | Vertical Slit | Feline aesthetic |
| **3** | `dragon` | Dragon Fire | Vertical Slit | Fiery orange iris |
| **4** | `terminator`| Cyborg Red | Circular Glow | Mechanical red eye |
| **5** | `doe` | Doe Anime | Large Circular | Soft anime aesthetic |
| **6** | `owl` | Owl Amber | Circular | Amber golden iris |
| **7** | `goat` | Goat Horizontal | Horizontal Bar | Caprine pupil |
| **8** | `nauga` | Monster Nauga | Circular | Playful cartoon creature |
| **9** | `newt` | Newt Orange | Circular | Amphibian spotted |
| **10** | `no_sclera` | Dragon Full | Full Coverage | Fiery edge-to-edge |

### Regenerating Presets from Assets
To regenerate the preset C++ tables from header data:
```bash
python generate_presets.py
```

---

## 5. Web Companion Portal

The checked-in web companion is a desktop/local browser tool:

1. Open `web_companion/index.html` directly in any modern browser.
2. Use it for visual/avatar experiments and local control-board work.

It is not currently an ESP32-hosted captive portal. On-device Wi-Fi config AP behavior is separate: the AP appears only when saved Wi-Fi cannot connect or config mode is deliberately triggered. If the cloud console shows the board online, no AP broadcast is expected.

When the board is already on Wi-Fi, the firmware boot-control/status server is available at:

```text
http://<device-ip>:3233/boot/status
```

Do not mistake the local `web_companion/index.html` for that device endpoint.

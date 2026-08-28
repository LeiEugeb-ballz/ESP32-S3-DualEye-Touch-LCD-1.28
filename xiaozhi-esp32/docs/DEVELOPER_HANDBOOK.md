# Developer & Maintainer Handbook

This handbook contains all essential commands, workflows, and procedures needed to build, flash, update, test, and maintain the **Xiaozhi Dual-Eye AI Companion System**.

---

## 1. Environment Setup

### Prerequisites
* **ESP-IDF v5.5** (Installed at `C:\Espressif\frameworks\esp-idf`)
* **Python 3.10+** (with `pyserial`)
* **PowerShell 7+** / Command Prompt

### Initializing Environment
In any PowerShell terminal, activate the ESP-IDF toolchain:
```powershell
. C:\Espressif\frameworks\esp-idf\export.ps1
```

---

## 2. Build & Compilation Workflows

### Standard Build
```powershell
cd c:\Users\txr45\Documents\Eyes\ESP32-S3-DualEye-Touch-LCD-1.28\xiaozhi-esp32
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

---

## 4. Uncanny Eye Presets Pack

The firmware includes 10 photorealistic, high-performance eye presets:

| ID | Preset Key | Display Style | Pupil Shape | Notes |
| :---: | :--- | :--- | :--- | :--- |
| **0** | `default` | Human Blue | Circular | Default on boot |
| **1** | `cat` | Cat Yellow | Vertical Slit | Feline aesthetic |
| **2** | `dragon` | Dragon Fire | Vertical Slit | Fiery orange iris |
| **3** | `terminator`| Cyborg Red | Circular Glow | Mechanical red eye |
| **4** | `doe` | Doe Anime | Large Circular | Soft anime aesthetic |
| **5** | `owl` | Owl Amber | Circular | Amber golden iris |
| **6** | `goat` | Goat Horizontal | Horizontal Bar | Caprine pupil |
| **7** | `nauga` | Monster Nauga | Circular | Playful cartoon creature |
| **8** | `newt` | Newt Orange | Circular | Amphibian spotted |
| **9** | `no_sclera` | Dragon Full | Full Coverage | Fiery edge-to-edge |

### Regenerating Presets from Assets
To regenerate the preset C++ tables from header data:
```bash
python generate_presets.py
```

---

## 5. Web Companion Portal

To interact with her visual avatar and memory files in a desktop browser:
1. Open [`web_companion/index.html`](file:///c:/Users/txr45/Documents/Eyes/ESP32-S3-DualEye-Touch-LCD-1.28/xiaozhi-esp32/web_companion/index.html) directly in any modern browser.
2. Experience real-time eye gaze tracking, test moods, manage lifelong soul memories, and push OTA firmware updates directly.

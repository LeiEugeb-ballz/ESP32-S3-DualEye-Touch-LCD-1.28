# Checkpoint Registry & Known-Good Firmware Index

This document is the **canonical ground-truth record** of all firmware checkpoints for the **Waveshare ESP32-S3-DualEye-Touch-LCD-1.28"** AI Companion.
Any future agent or developer must consult this document before flashing or rolling back.

---

## 1. Active Baseline Checkpoint (The Golden Build)

* **Filename**: `checkpoints/step2_10_with_wifi.bin`
* **Flash Offset**: `0x00000000` (Full 16MB SPI Flash layout, 8,711,256 bytes)
* **Flash Command**:
  ```powershell
  python -m esptool --chip esp32s3 -p COM13 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 checkpoints/step2_10_with_wifi.bin
  ```
* **One-Click Restore Script**:
  ```powershell
  python checkpoints/restore_working_build.py COM13
  ```

### Verified Features on Hardware:
| Component | Status | Verified Details |
| :--- | :--- | :--- |
| **Wake Word** | ✅ Working | `wn9_hiesp` ("Hi, ESP") listening via hardware AFE pipeline |
| **Persona & Language** | ✅ Working | English (`CONFIG_LANGUAGE_EN_US=y`) with Taiwanese accent cloud persona |
| **Wi-Fi Connectivity** | ✅ Working | Embedded valid NVS credentials for `Rondavel 2.4`, auto-connects to MQTT |
| **Eye Presets** | ✅ Working | 10 authentic Uncanny Eye presets (Human Blue, Cat, Dragon, Terminator, Doe, Owl, Goat, Nauga, Newt, Full Dragon) |
| **Eye Dynamics** | ✅ Working | Bodmer ballistic saccadic gaze exploration + audio pupil accommodation |
| **Voice Barge-In** | ✅ Working | Acoustic Echo Cancellation (AEC) cancels speaker playback; saying "Hi ESP" aborts speaking instantly |

---

## 2. Flash Memory Partition Layout Reference

When flashing, remember the ESP32-S3 flash layout:

```
Offset      Size      Partition    Description
--------------------------------------------------------------------------------
0x000000    0x008000  bootloader   2nd stage bootloader (bootloader.bin)
0x008000    0x001000  partition    Partition table (partition-table.bin)
0x009000    0x004000  nvs          Wi-Fi credentials, soul memory, volume
0x00D000    0x002000  otadata      Active boot partition selector
0x020000    0x3F0000  ota_0        Application Firmware Slot 1 (xiaozhi.bin)
0x410000    0x3F0000  ota_1        Application Firmware Slot 2 (OTA target)
0x800000    0x800000  assets       SPIFFS speech models (srmodels.bin: wn9_hiesp)
```

> [!CAUTION]
> **Do not flash app-only binaries (`3.6 MB`) to `0x20000` without matching assets at `0x800000`!**
> If you flash an app binary expecting `wn9_hiesp` while the assets partition has `wn9_nihaoxiaozhi`, speech recognition will fail to initialize or listen for the wrong wake word.

---

## 3. Historic Checkpoint Inventory

| Checkpoint File | Size | Intended Offset | Wake Word | Wi-Fi in NVS | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `step2_10_with_wifi.bin` | 8.71 MB | `0x0` | **`Hi, ESP`** | ✅ `Rondavel 2.4` | **Canonical Golden Build**: 10 presets, "Hi ESP", English/Taiwanese persona, auto Wi-Fi. |
| `step2_10_presets_working.bin` | 8.71 MB | `0x0` | **`Hi, ESP`** | ❌ (0xFF blank) | Pre-Wi-Fi merged image created in session `91e07793` step 1020. |
| `step2_lifelike_working.bin` | 8.71 MB | `0x0` | **`Hi, ESP`** | ❌ (0xFF blank) | Single lifelike eye preset build. |
| `step1_multi_eye_working.bin` | 8.71 MB | `0x0` | **`Hi, ESP`** | ❌ (0xFF blank) | Initial dual-eye baseline image. |
| `japa_v3_default_firmware.bin` | 3.62 MB | `0x20000` | `nihaoxiaozhi` | Preserves NVS | App partition only (lacks `0x800000` assets; configured for mainland Chinese). |
| `step3_bodmer_gaze_and_touch_working.bin` | 3.62 MB | `0x20000` | `nihaoxiaozhi` | Preserves NVS | App partition only for Bodmer saccades test. |

---

## 4. Why Context Discrepancies Happened

When switching AI models (e.g. from 3.7 to 3.8) or across context window compacting:
- Scratchpad chat history is summarized into a compressed synopsis.
- The previous summary recorded the string `japa_v3_default_firmware.bin` as the "default firmware" because the user requested saving it under that name at step 1299.
- However, the script that created `japa_v3` only copied `build/xiaozhi.bin` (`3.6 MB`), omitting the `0x800000` assets partition where `wn9_hiesp` lives.
- **Permanent Prevention**: This document and the `restore_working_build.py` script ensure future agents immediately recognize `step2_10_with_wifi.bin` as the canonical build.

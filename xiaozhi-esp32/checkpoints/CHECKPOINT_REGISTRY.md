# Checkpoint Registry & Known-Good Firmware Index

This document is the **canonical ground-truth record** of all firmware checkpoints for the **Waveshare ESP32-S3-DualEye-Touch-LCD-1.28"** AI Companion.
Any future agent or developer must consult this document before flashing or rolling back.

---

## 1. Current development checkpoint — 2026-09-10

See [current project status](../../STATUS.md) and the
[artifact hash manifest](../../.jafr/evidence/baseline-artifacts.json).
Source baseline is `93ea208`; local flashable copies are preserved under
`.jafr/checkpoints/2026-09-10-baseline/` at the project root (ignored by Git).
These are separate generated images with no NVS dump, not a full-device backup.

September 10 local build/host checks passed. September 9 flash/serial logs show
Hi ESP, two voice replies, moving eyes and both touch inputs. Minimum internal SRAM
fell to 972 bytes during conversation; extended acceptance remains open.
The current source defines 11 presets; AEC is disabled in the current generated
configuration. Older feature tables below describe historical checkpoints only.

## Historical checkpoints

### 2026-09-06 Active Development Checkpoint

User checkpoint note dated 2026-09-06: **"BEST AND FURTHERIST ON PROJECT"**.

Purpose:
* preserve the visually confirmed good idle Uncanny Eyes behavior;
* restore the intended English `Hi ESP` wake-word baseline;
* keep app binary and speech assets aligned on `wn9_hiesp`;
* document that the local `web_companion/index.html` is not an ESP32-hosted AP/web UI;
* defer the requested smaller nose-side caruncle/tear-duct geometry until after voice bootstrap is verified.

### 2026-09-08 Memory-Pressure Development Checkpoint

This checkpoint keeps the 2026-09-06 visual/voice baseline intact and only reduces avoidable SRAM strain:
* audio queue cap reduced from 40 packets / 2.4 seconds to 18 packets / about 1.08 seconds;
* Opus encoder output buffer reused instead of allocated per encoded frame;
* MQTT encrypted audio buffers reused instead of allocated per sent audio packet;
* optional custom JSON message strings freed after printing;
* build wrapper supports a working `ESP_PYTHON` override on Windows when the ESP-IDF venv launcher is broken.

Build and flash evidence:
* build succeeded for `build_safe/xiaozhi.bin`, size `0x3b8b10`;
* app partition free space was `0x374f0`;
* USB flash to COM13 completed with verified hashes;
* 180-second monitor showed stable boot, Wi-Fi connection, MQTT connection, WakeNet/AFE load, and moving idle eyes;
* minimum SRAM during that idle monitor was `13240`;
* a later idle-only monitor pass stayed stable with minimum SRAM `17352` and free SRAM around `28167-28683`;
* this was idle-only evidence; the newer September 9 conversation capture reached minimum SRAM `972`, so sustained voice-stress acceptance remains open.

### 2026-09-09 Greeting/Speak-Request Development Checkpoint

Purpose:
* add the low-strain prerequisite for random voice greetings without opening the microphone session automatically on every boot;
* allow the server/web UI to push a short greeting by sending MQTT `speak_request` while the board is idle;
* keep the normal `Hi ESP` wake-word path unchanged.

Build evidence:
* build succeeded for `build_safe/xiaozhi.bin`, size `0x3b9560`;
* app partition free space was `0x36aa0`;
* September 9 flash logs verify this app size and matching assets; following serial logs show normal voice interaction. They do not verify a startup greeting or capture the running firmware hash/version.

Required build proof:

```text
wakenet models: wn9_hiesp (will be packaged)
```

Required flash set when changing wake word:

```text
0x0      build_safe/bootloader/bootloader.bin
0x8000   build_safe/partition_table/partition-table.bin
0xd000   build_safe/ota_data_initial.bin
0x20000  build_safe/xiaozhi.bin
0x800000 build_safe/generated_assets.bin
```

Do not rely on an app-only flash for wake-word changes. The assets partition is where the speech model bundle is stored.

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

### Historical Step 2 feature claims (not current acceptance)

This table is retained as provenance. Its AEC/persona/FPS claims are not revalidated
for the current build. Current measurements and gaps are in the project STATUS.md.
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

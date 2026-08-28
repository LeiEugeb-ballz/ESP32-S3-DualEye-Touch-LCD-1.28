# Dual-Boot & ESPAI Integration Guide

This guide explains how to integrate the **ESPAI library** (`C:\Users\txr45\Downloads\ESPAI-0.9.0\ESPAI-0.9.0`) and how to configure a **dual-boot partition scheme** or **safe repository fork** so you can experiment with direct on-device LLM calls without losing the current stable Xiaozhi DualEye system.

---

## 1. Architectural Comparison: Xiaozhi vs. ESPAI

| Feature | Xiaozhi Dual-Eye (Current System) | ESPAI (Arduino/PlatformIO Library) |
| :--- | :--- | :--- |
| **Connection Paradigm** | Continuous Full-Duplex WebSockets / MQTT | Discrete HTTP REST Requests / Server-Sent Events |
| **Voice Processing** | Real-time Opus streaming + Hardware AFE AEC/VAD | Text-based chat completions (TTS/STT via API) |
| **Latency** | ~300ms–600ms ultra-low latency voice loop | 1.5s–4.0s HTTP token streaming latency |
| **LLM Gateway** | Xiaozhi Cloud Backend / Tenclass Server | Direct OpenAI, Anthropic Claude, Gemini, or Local Ollama |
| **Display Support** | Hardware DMA Ping-Pong Uncanny Eyes (50 FPS) | Generic display output / Serial |

---

## 2. Option A: Safe Git Branch Forking (Recommended)

To preserve your working firmware 100% while experimenting with new features:

```bash
# 1. Ensure current main is committed and clean
git add .
git commit -m "Release: Xiaozhi Dual-Eye v2.2.6 with 10 Presets & OTA Bridge"

# 2. Create and switch to a dedicated experimentation branch
git checkout -b feature/espai-direct-llm

# 3. If you ever want to return to the rock-solid working system:
git checkout main
idf.py build
python push_ota.py 192.168.88.221 build/xiaozhi.bin
```

---

## 3. Option B: True Dual-Boot via ESP-IDF OTA Partitions

Because our board has **16 MB Flash** with two independent 4 MB app partitions (`ota_0` and `ota_1`), you can flash Xiaozhi into `ota_0` and an ESPAI-based firmware into `ota_1`, and switch between them on demand!

### Dual-Boot Partition Layout (`partitions/v2/16m.csv`)

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,    0x4000,
otadata,  data, ota,     0xd000,    0x2000,
phy_init, data, phy,     0xf000,    0x1000,
ota_0,    app,  ota_0,   0x20000,   0x3f0000,   # Slot 1: Xiaozhi RTOS Voice OS
ota_1,    app,  ota_1,   0x410000,  0x3f0000,   # Slot 2: ESPAI Direct LLM Client
assets,   data, spiffs,  0x800000,  8M
```

### Switching Partitions Programmatically

You can switch active partitions using the ESP-IDF `esp_ota_set_boot_partition` API:

```cpp
#include <esp_ota_ops.h>

void SwitchToSlot(int slot_num) {
    const esp_partition_t* target = (slot_num == 0) 
        ? esp_ota_get_running_partition() 
        : esp_ota_get_next_update_partition(nullptr);

    if (target) {
        ESP_LOGI("DualBoot", "Switching active boot partition to %s...", target->label);
        esp_ota_set_boot_partition(target);
        esp_restart();
    }
}
```

---

## 4. Porting Uncanny Eyes into an ESPAI Project

If you build an ESPAI application, simply copy:
* `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/uncanny_eye_renderer.h`
* `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/uncanny_eye_renderer.cc`
* `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/default_eye_asset.h`
* `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/default_eye_asset.cc`
* `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/eye_presets.h`
* `main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/eye_presets.cc`

These rendering files are self-contained C++ modules requiring zero external dependencies and render directly into any display frame buffer.

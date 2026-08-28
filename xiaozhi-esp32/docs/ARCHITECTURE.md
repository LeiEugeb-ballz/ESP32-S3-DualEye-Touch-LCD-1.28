# System Architecture & Technical Specifications

This document outlines the complete internal architecture of the **Xiaozhi Dual-Eye AI Companion System** running on the Waveshare ESP32-S3 DualEye Touch LCD 1.28" board.

---

## 1. High-Level Architecture Overview

```
                          ┌────────────────────────────────────────┐
                          │         Xiaozhi Cloud Backend          │
                          │   (LLM + Opus ASR + Full-Duplex TTS)   │
                          └───────────────────▲────────────────────┘
                                              │ WebSocket / MQTT (TLS)
                                              ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                ESP32-S3 Dual-Core SoC                                  │
│                                                                                        │
│  ┌───────────────────────────────┐                  ┌───────────────────────────────┐  │
│  │            Core 0             │                  │            Core 1             │  │
│  │                               │                  │                               │  │
│  │ • Wi-Fi 802.11 b/g/n          │                  │ • Direct Hardware DMA Eye     │  │
│  │ • lwIP TCP/IP Stack           │  Atomic Gaze &   │   Render Task (50 FPS)        │  │
│  │ • TCP Push OTA Server (3232)  │  Audio Events    │ • Biomechanical Kinetics      │  │
│  │ • AFE Wake Word Engine        │ ───────────────> │   (Saccades, Micro-Tremors,   │  │
│  │ • I2S Audio Pipeline (ES8311) │                  │    Pupil Dilation & Blink)    │  │
│  │ • Application State Machine   │                  │ • Double-Buffered SPI Ping-   │  │
│  │ • MCP Tool Execution Engine   │                  │   Pong Scanline Transfer      │  │
│  └───────────────┬───────────────┘                  └───────────────┬───────────────┘  │
│                  │                                                  │                  │
│                  │                                                  │ SPI2 Bus         │
│                  ▼                                                  ▼                  │
│       ┌──────────────────────┐                           ┌──────────────────────┐      │
│       │ NVS Flash & SPIFFS   │                           │ Dual GC9A01 IPS LCDs │      │
│       │ Persistent Soul &    │                           │ (Left & Right Eyes)  │      │
│       │ File Storage System  │                           └──────────────────────┘      │
│       └──────────────────────┘                                                         │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Memory Map & Hardware Resources

### Flash Partition Allocation (16 MB SPI Flash)
* **`nvs`** (`0x009000`, 16 KB): Non-Volatile Storage for Wi-Fi credentials, volume settings, and persistent soul traits (`identity`, `creator_bond`, `eyes_system`, `storage_system`, `ota_system`).
* **`otadata`** (`0x00d000`, 8 KB): ESP-IDF OTA boot state register determining active application partition.
* **`phy_init`** (`0x00f000`, 4 KB): Radio RF calibration parameters.
* **`ota_0`** (`0x020000`, 4,032 KB): Primary application firmware partition.
* **`ota_1`** (`0x410000`, 4,032 KB): Secondary application firmware partition (active dual-boot target).
* **`assets`** (`0x800000`, 8,192 KB): Dedicated SPIFFS partition for offline audio prompts, fonts, and persistent companion text documents.

### RAM Breakdown (8 MB Octal PSRAM + 512 KB Internal SRAM)
* **Internal SRAM**:
  * Free dynamic heap: **~101 KB SRAM**.
  * Minimal free SRAM during peak operation: **~40 KB**.
  * DMA Buffers: **4x 9,600-byte ping-pong buffers** (20 scanlines per chunk, 16-bit RGB565) allocated with `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`.
* **Octal PSRAM**:
  * 8 MB pool used for Opus audio encoding/decoding rings, FreeRTOS task stacks, and WebSockets buffers.

---

## 3. Direct DMA Dual-Eye Rendering Engine

The visual rendering engine runs independently on **CPU Core 1** at Priority 1 to guarantee jitter-free 50 FPS performance without blocking Core 0 audio tasks.

```
                  ┌──────────────────────────────────────────────┐
                  │          UncannyEyeRenderer Pipeline         │
                  └──────────────────────┬───────────────────────┘
                                         │
                 ┌───────────────────────┴───────────────────────┐
                 │                                               │
                 ▼                                               ▼
      [Polar Texture Mapping]                        [3D Specular Highlight Pass]
   • 128-entry radial stretch LUT                  • High-precision corneal parallax
   • Real-time bilinear interpolation              • Bounded glint distance testing
   • Dynamic pupil expansion & constriction        • Alpha-blended specular shine
                 │                                               │
                 └───────────────────────┬───────────────────────┘
                                         │
                                         ▼
                             [Anatomical Eyelid Pass]
                       • Organic upper/lower eyelid curves
                       • Left/right caruncle mirroring
                       • Asymmetric organic blink dynamics
                                         │
                                         ▼
                            [Double-Buffered DMA Tx]
                       • 2x 20-line ping-pong internal RAM
                       • SPI2 asynchronous hardware transfer
```

---

## 4. MCP Tools & Persona System

The companion exposes tools via Model Context Protocol (MCP) and on-device voice parsing:

| Tool Identifier | Description | Parameters |
| :--- | :--- | :--- |
| `self.eyes.set_preset` | Switches photorealistic eye styling on the fly | `preset` (e.g. "default", "cat", "cyborg", "dragon"), `target` ("both", "left", "right") |
| `self.persona.set_mood` | Adjusts emotional state & eye kinetics | `mood` ("curious", "happy", "thoughtful", "tender", "playful", "sleepy", "focused") |
| `self.persona.save_memory` | Commits lifelong user memories to flash | `key` (string), `value` (string) |
| `self.persona.recall_memories` | Retrieves all saved soul traits | None |
| `self.notes.save` | Creates/updates flash companion notes | `title` (string), `content` (string) |
| `self.notes.read` | Reads a document from internal storage | `title` (string) |
| `self.notes.list` | Returns indexed list of stored files | None |
| `self.notes.delete` | Deletes a stored file from flash | `title` (string) |
| `self.get_device_status` | Returns real-time hardware telemetry | None |

---

## 5. Background Wireless Push OTA Server

* **Protocol**: Direct TCP raw stream over port **3232**.
* **Flow**:
  1. PC client runs `push_ota.py <board_ip> build/xiaozhi.bin`.
  2. ESP32 accepts socket connection and creates 8 KB FreeRTOS OTA task.
  3. Firmware chunks are streamed in 4 KB buffers and written sequentially to passive partition (`esp_ota_write`).
  4. Once stream completes, SHA256 checksum is verified, active partition swapped (`esp_ota_set_boot_partition`), and device automatically reboots.

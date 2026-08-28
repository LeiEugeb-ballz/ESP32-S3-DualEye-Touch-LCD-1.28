# Hardware & Peripheral Expansion Guide

This guide details the physical hardware specifications, pinout mapping, and instructions for expanding the Waveshare ESP32-S3 DualEye board with external peripherals such as **Pan/Tilt Servos for physical head movement**, **Environmental Sensors**, and **Addressable LEDs**.

---

## 1. On-Board Pinout & Resource Mapping

| Function | Pin / GPIO | Description |
| :--- | :--- | :--- |
| **Boot Button** | `GPIO 0` | Factory boot / Flash recovery button |
| **I2C0 SCL / SDA** | `GPIO 10` / `GPIO 11` | ES8311 Audio DAC + CST816S Touch 1 (Left) |
| **I2C1 SCL / SDA** | `GPIO 2` / `GPIO 3` | CST816S Touch 2 (Right) |
| **Touch 1 INT / RST** | `GPIO 5` / `GPIO 4` | Left capacitive touch screen interrupts |
| **Touch 2 INT / RST** | `GPIO 7` / `GPIO 6` | Right capacitive touch screen interrupts |
| **I2S MCLK / BCLK / WS** | `GPIO 12` / `GPIO 13` / `GPIO 14` | Audio master clock & word select |
| **I2S DIN / DOUT** | `GPIO 15` / `GPIO 16` | ES7210 Quad-Mic In / ES8311 Speaker Out |
| **Audio Amp Enable** | `GPIO 9` | Audio Power Amplifier PA mute/unmute |
| **SPI SCLK / MOSI / MISO**| `GPIO 41` / `GPIO 42` / `GPIO 40` | Shared SPI2 bus for dual GC9A01 displays |
| **Display DC** | `GPIO 45` | Shared Data/Command select pin |
| **Display 1 CS / RST / BL**| `GPIO 47` / `GPIO 48` / `GPIO 46` | Left LCD Chip Select, Reset, Backlight PWM |
| **Display 2 CS / RST / BL**| `GPIO 38` / `GPIO 8` / `GPIO 39` | Right LCD Chip Select, Reset, Backlight PWM |
| **UART0 TX / RX** | `GPIO 43` / `GPIO 44` | USB Serial Console & Flashing |

---

## 2. Expanding with Pan/Tilt Servos (Physical Eye/Head Tracking)

To give your companion physical embodiment and motorized head/eye tracking, you can attach two micro servos (e.g. SG90 or MG90S) to available ESP32-S3 pins using the hardware **LEDC PWM driver**.

### Recommended Pin Assignment for Servos
* **Pan Servo (Horizontal Yaw)**: `GPIO 1` (LEDC Channel 0)
* **Tilt Servo (Vertical Pitch)**: `GPIO 21` or `GPIO 17` (LEDC Channel 1)

### Schematic & Wiring Diagram

```
                 5V External Power Supply (1-2A Recommended)
                 ───────────────────────┬───────────────────
                                        │
                         ┌──────────────┴──────────────┐
                         │                             │
                         ▼                             ▼
                 [Pan Servo VCC]               [Tilt Servo VCC]
                 (Red Wire)                    (Red Wire)

  ESP32-S3 Board
  ┌───────────────┐
  │      GND ─────┼────────┬───────────────────────────┬───────► [Common GND]
  │               │        │                           │         (Brown/Black Wires)
  │   GPIO 1 ─────┼────────┼───► [Pan PWM Signal]      │
  │  (LEDC Ch0)   │        │     (Orange/Yellow Wire)  │
  │               │        │                           │
  │   GPIO 21 ────┼────────┼───────────────────────────┴───────► [Tilt PWM Signal]
  │  (LEDC Ch1)   │        │                                     (Orange/Yellow Wire)
  └───────────────┘        │
                           ▼
                 [Optional Smoothing Cap]
                 (100µF - 470µF across 5V and GND)
```

> [!CAUTION]
> **Power Isolation Rule**: Never power servos directly from the ESP32-S3 3.3V or USB 5V rail. Servo inductive load spikes can cause brownouts and reset the Wi-Fi/Audio codec. Always use a dedicated 5V power source with a shared ground.

### Sample FreeRTOS Servo Driver Code (`servo_controller.h`)

```cpp
#include <driver/ledc.h>

class ServoController {
public:
    static void Init() {
        ledc_timer_config_t timer_conf = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_14_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 50, // Standard 50Hz RC servo frequency (20ms period)
            .clk_cfg = LEDC_AUTO_CLK
        };
        ledc_timer_config(&timer_conf);

        ledc_channel_config_t pan_chan = {
            .gpio_num = GPIO_NUM_1,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0
        };
        ledc_channel_config(&pan_chan);
    }

    static void SetAngle(int angle_deg) { // angle_deg: -90 to +90
        // Map -90..+90 deg to 0.5ms..2.5ms pulse width in 14-bit resolution
        float pulse_ms = 1.5f + (float)angle_deg * (1.0f / 90.0f);
        uint32_t duty = (uint32_t)((pulse_ms / 20.0f) * 16383.0f);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
};
```

---

## 3. Peripheral Sensors & I2C Expansion

Since the board already has an active I2C bus (`I2C0` on `GPIO 10`/`11`), you can daisy-chain additional low-power I2C sensors in parallel without using any new GPIO pins:
* **BME280 / SHT30**: Ambient temperature, humidity, and atmospheric pressure.
* **VL53L0X / VL53L1X**: Time-of-Flight distance sensor for presence detection (triggers greeting when you walk into the room).
* **MPU6050 / LSM6DS3**: 6-axis IMU to make her eyes compensate for board tilting and movement.

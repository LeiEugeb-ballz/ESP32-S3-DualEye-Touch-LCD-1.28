// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Direct Hardware DMA Dual-Eye Display Manager
#include "display_manager.h"
#include "default_eye_asset.h"
#include "eye_presets.h"
#include "config.h"
#include "assets/lang_config.h"
#include <esp_err.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_cst816s.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <mutex>

static const char* TAG = "DualEyeDisplay";

// ---------------------------------------------------------------------------
// DisplayManager Static Members & Touch Tracking
// ---------------------------------------------------------------------------
static esp_lcd_touch_handle_t s_tp_left = nullptr;
static esp_lcd_touch_handle_t s_tp_right = nullptr;

struct TouchTracker {
    bool is_touching = false;
    uint16_t start_x = 0;
    uint16_t start_y = 0;
    uint16_t last_x = 0;
    uint16_t last_y = 0;
    uint32_t start_time_ms = 0;
    bool gesture_triggered = false;
};

static TouchTracker s_tracker_left;
static TouchTracker s_tracker_right;

static std::atomic<float> s_drag_dx_left{0.0f};
static std::atomic<bool> s_dragging_left{false};
static std::atomic<float> s_drag_dx_right{0.0f};
static std::atomic<bool> s_dragging_right{false};
static std::atomic<float> s_slide_offset_left{0.0f};
static std::atomic<float> s_slide_offset_right{0.0f};
static std::atomic<bool> s_sleeping{false};
static std::atomic<bool> s_manual_gaze{false};
static std::atomic<float> s_manual_gaze_x{0.0f};
static std::atomic<float> s_manual_gaze_y{0.0f};
static std::atomic<bool> s_manual_pupil{false};
static std::atomic<float> s_manual_pupil_val{32.0f};
static std::atomic<bool> s_trigger_blink{false};
static std::atomic<float> s_live_fps{60.0f};
static std::atomic<float> s_eye_scale{0.86f};
static std::string s_current_mood = "idle";
static std::mutex s_mood_mutex;
static std::mutex s_chat_mutex;
static std::string s_chat_role;
static std::string s_chat_text;
static uint32_t s_chat_until_ms = 0;

struct EyeBehaviorProfile {
    float gaze_radius_x;
    float gaze_radius_y;
    uint32_t dwell_min_ms;
    uint32_t dwell_span_ms;
    uint32_t move_min_ms;
    uint32_t move_span_ms;
    float pupil_base;
    float upper_lid_bias;
    float lower_lid_bias;
    uint32_t blink_min_ms;
    uint32_t blink_span_ms;
    uint8_t double_blink_chance;
    float tremor_x;
    float tremor_y;
};

static inline uint32_t RandRangeMs(uint32_t min_ms, uint32_t span_ms) {
    return min_ms + (span_ms > 0 ? (esp_random() % span_ms) : 0);
}

static inline float RandUnit() {
    return (float)(esp_random() % 1000) / 1000.0f;
}

static inline float RandSigned(float amplitude) {
    return ((RandUnit() * 2.0f) - 1.0f) * amplitude;
}

static EyeBehaviorProfile GetEyeBehaviorProfile(EyeAnimationState state, bool speech_active) {
    if (speech_active && state == EYE_STATE_IDLE) {
        state = EYE_STATE_SPEAKING;
    }

    switch (state) {
        case EYE_STATE_LISTENING:
            // Low-amplitude fixation: looks at the user instead of wandering.
            return {6.0f, 5.0f, 1200, 2200, 80, 70, 36.0f, -2.0f, 0.0f, 3600, 3200, 10, 0.7f, 0.5f};
        case EYE_STATE_THINKING:
            // Intentional off-centre glances while the model is processing.
            return {18.0f, 10.0f, 500, 1300, 110, 120, 28.0f, 0.0f, 0.0f, 1800, 1800, 22, 0.9f, 0.6f};
        case EYE_STATE_SPEAKING:
            // Direct conversational gaze with small beat-to-beat movement.
            return {10.0f, 6.0f, 350, 950, 75, 65, 33.0f, -6.0f, 6.0f, 2700, 2400, 12, 1.0f, 0.7f};
        case EYE_STATE_HAPPY:
            return {18.0f, 11.0f, 450, 1500, 70, 75, 34.0f, 0.0f, 0.0f, 2500, 2600, 18, 1.2f, 0.8f};
        case EYE_STATE_SAD_SLEEPY:
            return {10.0f, 8.0f, 1400, 2600, 120, 130, 27.0f, 0.0f, 4.0f, 1600, 1700, 28, 0.5f, 0.4f};
        case EYE_STATE_ANGRY:
            return {12.0f, 8.0f, 650, 1300, 90, 90, 30.0f, 22.0f, 24.0f, 2600, 2200, 8, 0.6f, 0.4f};
        case EYE_STATE_SURPRISED:
            return {8.0f, 5.0f, 900, 1600, 65, 70, 39.0f, 0.0f, 0.0f, 4200, 2600, 4, 1.1f, 0.8f};
        case EYE_STATE_IDLE:
        default:
            return {24.0f, 18.0f, 400, 1600, 35, 35, 32.0f, 0.0f, 0.0f, 2200, 3000, 18, 1.4f, 1.0f};
    }
}

static void PickConversationGaze(EyeAnimationState state, uint32_t state_age_ms, const EyeBehaviorProfile& profile,
                                 float audio_energy, float& nx, float& ny) {
    if (state == EYE_STATE_LISTENING) {
        // Start with eye contact, then allow rare tiny glances so it does not look frozen.
        bool locked_on_user = state_age_ms < 1600 || (esp_random() % 100) < 72;
        nx = locked_on_user ? RandSigned(1.5f) : RandSigned(profile.gaze_radius_x);
        ny = -4.0f + (locked_on_user ? RandSigned(1.2f) : RandSigned(profile.gaze_radius_y));
        return;
    }

    if (state == EYE_STATE_SPEAKING) {
        // Speech should feel addressed to the user: mostly centred, with tiny prosodic emphasis.
        nx = RandSigned(profile.gaze_radius_x);
        ny = -3.0f + RandSigned(profile.gaze_radius_y * 0.7f) - (audio_energy * 2.0f);
        return;
    }

    if (state == EYE_STATE_THINKING) {
        nx = ((esp_random() % 2) == 0 ? -1.0f : 1.0f) * (10.0f + RandUnit() * 8.0f);
        ny = -8.0f - (RandUnit() * 5.0f);
        return;
    }

    if (state == EYE_STATE_SAD_SLEEPY) {
        nx = RandSigned(profile.gaze_radius_x * 0.55f);
        ny = 5.0f + RandUnit() * profile.gaze_radius_y;
        return;
    }

    if (state == EYE_STATE_ANGRY) {
        nx = RandSigned(profile.gaze_radius_x * 0.7f);
        ny = -2.0f + RandSigned(profile.gaze_radius_y * 0.35f);
        return;
    }

    int attempts = 0;
    do {
        nx = RandSigned(profile.gaze_radius_x);
        ny = RandSigned(profile.gaze_radius_y);
        attempts++;
    } while (((nx * nx) / (profile.gaze_radius_x * profile.gaze_radius_x) +
              (ny * ny) / (profile.gaze_radius_y * profile.gaze_radius_y) > 1.0f) && attempts < 10);
}

static inline uint16_t DmaRgb565(uint16_t color) {
    return (uint16_t)((color >> 8) | (color << 8));
}

static const uint8_t* Glyph3x5(char ch) {
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t qmark[5] = {7, 1, 3, 0, 2};
    switch (ch) {
        case '0': { static const uint8_t g[5] = {7, 5, 5, 5, 7}; return g; }
        case '1': { static const uint8_t g[5] = {2, 6, 2, 2, 7}; return g; }
        case '2': { static const uint8_t g[5] = {7, 1, 7, 4, 7}; return g; }
        case '3': { static const uint8_t g[5] = {7, 1, 7, 1, 7}; return g; }
        case '4': { static const uint8_t g[5] = {5, 5, 7, 1, 1}; return g; }
        case '5': { static const uint8_t g[5] = {7, 4, 7, 1, 7}; return g; }
        case '6': { static const uint8_t g[5] = {7, 4, 7, 5, 7}; return g; }
        case '7': { static const uint8_t g[5] = {7, 1, 2, 2, 2}; return g; }
        case '8': { static const uint8_t g[5] = {7, 5, 7, 5, 7}; return g; }
        case '9': { static const uint8_t g[5] = {7, 5, 7, 1, 7}; return g; }
        case 'A': { static const uint8_t g[5] = {7, 5, 7, 5, 5}; return g; }
        case 'B': { static const uint8_t g[5] = {6, 5, 6, 5, 6}; return g; }
        case 'C': { static const uint8_t g[5] = {7, 4, 4, 4, 7}; return g; }
        case 'D': { static const uint8_t g[5] = {6, 5, 5, 5, 6}; return g; }
        case 'E': { static const uint8_t g[5] = {7, 4, 6, 4, 7}; return g; }
        case 'F': { static const uint8_t g[5] = {7, 4, 6, 4, 4}; return g; }
        case 'G': { static const uint8_t g[5] = {7, 4, 5, 5, 7}; return g; }
        case 'H': { static const uint8_t g[5] = {5, 5, 7, 5, 5}; return g; }
        case 'I': { static const uint8_t g[5] = {7, 2, 2, 2, 7}; return g; }
        case 'J': { static const uint8_t g[5] = {1, 1, 1, 5, 7}; return g; }
        case 'K': { static const uint8_t g[5] = {5, 5, 6, 5, 5}; return g; }
        case 'L': { static const uint8_t g[5] = {4, 4, 4, 4, 7}; return g; }
        case 'M': { static const uint8_t g[5] = {5, 7, 7, 5, 5}; return g; }
        case 'N': { static const uint8_t g[5] = {5, 7, 7, 7, 5}; return g; }
        case 'O': { static const uint8_t g[5] = {7, 5, 5, 5, 7}; return g; }
        case 'P': { static const uint8_t g[5] = {7, 5, 7, 4, 4}; return g; }
        case 'Q': { static const uint8_t g[5] = {7, 5, 5, 7, 1}; return g; }
        case 'R': { static const uint8_t g[5] = {7, 5, 7, 6, 5}; return g; }
        case 'S': { static const uint8_t g[5] = {7, 4, 7, 1, 7}; return g; }
        case 'T': { static const uint8_t g[5] = {7, 2, 2, 2, 2}; return g; }
        case 'U': { static const uint8_t g[5] = {5, 5, 5, 5, 7}; return g; }
        case 'V': { static const uint8_t g[5] = {5, 5, 5, 5, 2}; return g; }
        case 'W': { static const uint8_t g[5] = {5, 5, 7, 7, 5}; return g; }
        case 'X': { static const uint8_t g[5] = {5, 5, 2, 5, 5}; return g; }
        case 'Y': { static const uint8_t g[5] = {5, 5, 2, 2, 2}; return g; }
        case 'Z': { static const uint8_t g[5] = {7, 1, 2, 4, 7}; return g; }
        case ':': { static const uint8_t g[5] = {0, 2, 0, 2, 0}; return g; }
        case '.': { static const uint8_t g[5] = {0, 0, 0, 0, 2}; return g; }
        case ',': { static const uint8_t g[5] = {0, 0, 0, 2, 4}; return g; }
        case '!': { static const uint8_t g[5] = {2, 2, 2, 0, 2}; return g; }
        case '?': return qmark;
        case '-': { static const uint8_t g[5] = {0, 0, 7, 0, 0}; return g; }
        case '/': { static const uint8_t g[5] = {1, 1, 2, 4, 4}; return g; }
        case '_': { static const uint8_t g[5] = {0, 0, 0, 0, 7}; return g; }
        case ' ': return blank;
        default: return blank;
    }
}

static void DrawGlyph3x5(uint16_t* buf, int chunk_y, int x, int y, char ch, uint16_t color) {
    if (ch >= 'a' && ch <= 'z') ch = (char)std::toupper((unsigned char)ch);
    const uint8_t* rows = Glyph3x5(ch);
    for (int gy = 0; gy < 5; ++gy) {
        for (int gx = 0; gx < 3; ++gx) {
            if ((rows[gy] & (1 << (2 - gx))) == 0) continue;
            for (int sy = 0; sy < 2; ++sy) {
                int py = y + gy * 2 + sy;
                if (py < chunk_y || py >= chunk_y + 20 || py < 0 || py >= 240) continue;
                for (int sx = 0; sx < 2; ++sx) {
                    int px = x + gx * 2 + sx;
                    if (px < 0 || px >= 240) continue;
                    buf[(py - chunk_y) * 240 + px] = color;
                }
            }
        }
    }
}

static std::string SanitizeOverlayText(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    bool last_was_space = false;

    for (unsigned char ch : input) {
        bool supported = (ch >= '0' && ch <= '9') ||
                         (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') ||
                         ch == ':' || ch == '.' || ch == ',' || ch == '!' ||
                         ch == '?' || ch == '-' || ch == '/' || ch == '_' ||
                         ch == ' ';
        if (!supported) {
            if (!last_was_space) {
                output.push_back(' ');
                last_was_space = true;
            }
            continue;
        }

        output.push_back((char)ch);
        last_was_space = (ch == ' ');
    }

    return output;
}

static void DrawChatOverlay(uint16_t* buf, int chunk_y) {
    (void)buf;
    (void)chunk_y;
}

void DisplayManager::SlideEyeIn(bool is_left, float start_offset_x) {
    if (is_left) {
        s_slide_offset_left.store(start_offset_x, std::memory_order_relaxed);
    } else {
        s_slide_offset_right.store(start_offset_x, std::memory_order_relaxed);
    }
}

void DisplayManager::SetSleeping(bool sleeping) {
    s_sleeping.store(sleeping, std::memory_order_relaxed);
    ESP_LOGI(TAG, "DisplayManager sleeping mode set to: %d", sleeping ? 1 : 0);
}

bool DisplayManager::IsSleeping() {
    return s_sleeping.load(std::memory_order_relaxed);
}

void DisplayManager::SetMood(const std::string& mood) {
    {
        std::lock_guard<std::mutex> lock(s_mood_mutex);
        s_current_mood = mood;
    }
    std::string lower = mood;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    EyeAnimationState state = EYE_STATE_IDLE;
    if (lower == "listening") state = EYE_STATE_LISTENING;
    else if (lower == "thinking" || lower == "processing") state = EYE_STATE_THINKING;
    else if (lower == "speaking") state = EYE_STATE_SPEAKING;
    else if (lower == "happy") state = EYE_STATE_HAPPY;
    else if (lower == "sleepy" || lower == "sad") state = EYE_STATE_SAD_SLEEPY;
    else if (lower == "surprised") state = EYE_STATE_SURPRISED;
    else if (lower == "angry" || lower == "error") state = EYE_STATE_ANGRY;

    /* The renderer consumes current_state_. target_state_ is retained for the
     * existing transition API, but writing it alone made ESP-NOW commands dead
     * code: they parsed successfully and never changed the rendered behavior. */
    target_state_.store(state, std::memory_order_release);
    current_state_.store(state, std::memory_order_release);
    ESP_LOGI(TAG, "External eye state: %s -> %d", lower.c_str(), static_cast<int>(state));
}

std::string DisplayManager::GetMood() {
    std::lock_guard<std::mutex> lock(s_mood_mutex);
    return s_current_mood;
}

void DisplayManager::SetManualGaze(float x, float y) {
    s_manual_gaze_x.store(std::clamp(x, -30.0f, 30.0f), std::memory_order_relaxed);
    s_manual_gaze_y.store(std::clamp(y, -25.0f, 25.0f), std::memory_order_relaxed);
    s_manual_gaze.store(true, std::memory_order_release);
}

void DisplayManager::ClearManualGaze() {
    s_manual_gaze.store(false, std::memory_order_release);
}

bool DisplayManager::IsManualGaze() {
    return s_manual_gaze.load(std::memory_order_relaxed);
}

void DisplayManager::SetManualPupil(float dilation) {
    s_manual_pupil_val.store(std::clamp(dilation, 18.0f, 44.0f), std::memory_order_relaxed);
    s_manual_pupil.store(true, std::memory_order_release);
}

void DisplayManager::ClearManualPupil() {
    s_manual_pupil.store(false, std::memory_order_release);
}

void DisplayManager::TriggerBlink() {
    s_trigger_blink.store(true, std::memory_order_release);
}

float DisplayManager::GetFps() {
    return s_live_fps.load(std::memory_order_relaxed);
}

void DisplayManager::SetEyeScale(float scale) {
    if (scale < 0.2f) scale = 0.2f;
    if (scale > 2.0f) scale = 2.0f;
    s_eye_scale.store(scale, std::memory_order_relaxed);
    ESP_LOGI(TAG, "Eye scale updated to %.2f", scale);
}

float DisplayManager::GetEyeScale() {
    return s_eye_scale.load(std::memory_order_relaxed);
}

static void ProcessTouchInput(esp_lcd_touch_handle_t tp, TouchTracker& tracker, bool is_left, uint32_t now_ms) {
    if (!tp) return;

    gpio_num_t int_pin = is_left ? TOUCH1_INT_PIN : TOUCH2_INT_PIN;
    bool pin_low = (gpio_get_level(int_pin) == 0);

    // Only read I2C when INT is pulled low by CST816S or during an active touch session
    if (!pin_low && !tracker.is_touching) {
        return;
    }

    esp_err_t err = esp_lcd_touch_read_data(tp);
    if (err != ESP_OK) {
        tracker.is_touching = false;
        if (is_left) {
            s_dragging_left.store(false, std::memory_order_relaxed);
            s_drag_dx_left.store(0.0f, std::memory_order_relaxed);
        } else {
            s_dragging_right.store(false, std::memory_order_relaxed);
            s_drag_dx_right.store(0.0f, std::memory_order_relaxed);
        }
        return;
    }

    uint16_t tx[1] = {0};
    uint16_t ty[1] = {0};
    uint8_t touch_cnt = 0;
    bool pressed = esp_lcd_touch_get_coordinates(tp, tx, ty, NULL, &touch_cnt, 1);

    if (pressed && touch_cnt > 0) {
        if (!tracker.is_touching) {
            tracker.is_touching = true;
            tracker.start_x = tx[0];
            tracker.start_y = ty[0];
            tracker.last_x = tx[0];
            tracker.last_y = ty[0];
            tracker.start_time_ms = now_ms;
            tracker.gesture_triggered = false;
            if (is_left) {
                s_dragging_left.store(true, std::memory_order_relaxed);
                s_drag_dx_left.store(0.0f, std::memory_order_relaxed);
            } else {
                s_dragging_right.store(true, std::memory_order_relaxed);
                s_drag_dx_right.store(0.0f, std::memory_order_relaxed);
            }
        } else {
            tracker.last_x = tx[0];
            tracker.last_y = ty[0];
            int dx = (int)tracker.last_x - (int)tracker.start_x;

            if (is_left) {
                s_drag_dx_left.store((float)dx, std::memory_order_relaxed);
            } else {
                s_drag_dx_right.store((float)dx, std::memory_order_relaxed);
            }

            if (!tracker.gesture_triggered) {
                if (dx > 30) { // Dragged Right -> Previous Preset
                    tracker.gesture_triggered = true;
                    if (is_left) {
                        s_dragging_left.store(false, std::memory_order_relaxed);
                        s_drag_dx_left.store(0.0f, std::memory_order_relaxed);
                        ESP_LOGI(TAG, "Left Drag RIGHT -> Previous Preset");
                        DisplayManager::CycleLeftEyeBackwards();
                        DisplayManager::SlideEyeIn(true, -28.0f); // Incoming eye glides in from left
                    } else {
                        s_dragging_right.store(false, std::memory_order_relaxed);
                        s_drag_dx_right.store(0.0f, std::memory_order_relaxed);
                        ESP_LOGI(TAG, "Right Drag RIGHT -> Previous Preset");
                        DisplayManager::CycleRightEyeBackwards();
                        DisplayManager::SlideEyeIn(false, -28.0f);
                    }
                } else if (dx < -30) { // Dragged Left -> Next Preset
                    tracker.gesture_triggered = true;
                    if (is_left) {
                        s_dragging_left.store(false, std::memory_order_relaxed);
                        s_drag_dx_left.store(0.0f, std::memory_order_relaxed);
                        ESP_LOGI(TAG, "Left Drag LEFT -> Next Preset");
                        DisplayManager::CycleLeftEye();
                        DisplayManager::SlideEyeIn(true, 28.0f); // Incoming eye glides in from right
                    } else {
                        s_dragging_right.store(false, std::memory_order_relaxed);
                        s_drag_dx_right.store(0.0f, std::memory_order_relaxed);
                        ESP_LOGI(TAG, "Right Drag LEFT -> Next Preset");
                        DisplayManager::CycleRightEye();
                        DisplayManager::SlideEyeIn(false, 28.0f);
                    }
                }
            }
        }
    } else {
        if (tracker.is_touching) {
            int dx = (int)tracker.last_x - (int)tracker.start_x;
            if (is_left) {
                s_dragging_left.store(false, std::memory_order_relaxed);
                s_drag_dx_left.store(0.0f, std::memory_order_relaxed);
            } else {
                s_dragging_right.store(false, std::memory_order_relaxed);
                s_drag_dx_right.store(0.0f, std::memory_order_relaxed);
            }

            // Quick light TAP (< 600ms and minimal movement < 18px)
            if (!tracker.gesture_triggered && (now_ms - tracker.start_time_ms < 600) && std::abs(dx) < 18) {
                if (is_left) {
                    ESP_LOGI(TAG, "Left Screen TAPPED -> Next Preset");
                    DisplayManager::CycleLeftEye();
                    DisplayManager::SlideEyeIn(true, 24.0f);
                } else {
                    ESP_LOGI(TAG, "Right Screen TAPPED -> Next Preset");
                    DisplayManager::CycleRightEye();
                    DisplayManager::SlideEyeIn(false, 24.0f);
                }
            }
            tracker.is_touching = false;
        }
    }
}

static void TouchPollingTask(void* arg) {
    ESP_LOGI(TAG, "High-Frequency Touch Polling Task started (100Hz)");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ProcessTouchInput(s_tp_left, s_tracker_left, true, now_ms);
        ProcessTouchInput(s_tp_right, s_tracker_right, false, now_ms);
    }
}
std::vector<SpiLcdDisplayExtended*> DisplayManager::displays_;
Display* DisplayManager::primary_display_ = nullptr;

std::atomic<EyeAnimationState> DisplayManager::current_state_{EYE_STATE_IDLE};
std::atomic<EyeAnimationState> DisplayManager::target_state_{EYE_STATE_IDLE};

std::atomic<float> DisplayManager::audio_energy_{0.0f};
std::atomic<bool> DisplayManager::speech_active_{false};

static std::atomic<int> s_left_preset_id{0}; // Boot directly to PRESET_DEFAULT (Human Blue)
static std::atomic<int> s_right_preset_id{0}; // Boot directly to PRESET_DEFAULT (Human Blue)

// Saccadic Gaze State
float DisplayManager::current_gaze_x_ = 0.0f;
float DisplayManager::current_gaze_y_ = 0.0f;
float DisplayManager::target_gaze_x_ = 0.0f;
float DisplayManager::target_gaze_y_ = 0.0f;
uint32_t DisplayManager::last_saccade_time_ms_ = 0;
uint32_t DisplayManager::saccade_dwell_time_ms_ = 1800;

// Pupil State
float DisplayManager::current_pupil_dilation_ = 32.0f;
float DisplayManager::target_pupil_dilation_ = 32.0f;

// Blink State
bool DisplayManager::blink_in_progress_ = false;
uint32_t DisplayManager::blink_start_time_ms_ = 0;
uint32_t DisplayManager::last_blink_time_ms_ = 0;
uint32_t DisplayManager::next_blink_interval_ms_ = 3500;
bool DisplayManager::pending_double_blink_ = false;
uint32_t DisplayManager::double_blink_trigger_ms_ = 0;

// Render Task & Benchmark
TaskHandle_t DisplayManager::render_task_handle_ = nullptr;
std::atomic<bool> DisplayManager::render_task_running_{false};
uint32_t DisplayManager::render_time_eye1_us_ = 0;
uint32_t DisplayManager::render_time_both_us_ = 0;
uint32_t DisplayManager::frame_count_ = 0;

DisplayManager::DisplayManager() {
    primary_display_ = this;
}

DisplayManager::~DisplayManager() {
    render_task_running_ = false;
    if (render_task_handle_) {
        vTaskDelete(render_task_handle_);
        render_task_handle_ = nullptr;
    }
}

void DisplayManager::AddDisplay(SpiLcdDisplayExtended* display) {
    for (auto* d : displays_) {
        if (d == display) return;
    }
    displays_.push_back(display);
    ESP_LOGI(TAG, "Display added, total displays: %d", (int)displays_.size());

    // Start dedicated rendering task once both displays are ready
    if (displays_.size() == 2 && !render_task_handle_) {
        render_task_running_ = true;
        // Pinned to Core 1 at Priority 5 (dedicated primary graphics pipeline)
        xTaskCreatePinnedToCore(EyeRenderTask, "eye_render", 4096, nullptr, 5, &render_task_handle_, 1);
        ESP_LOGI(TAG, "Direct DMA Eye Render Task started on Core 1 (Priority 5, dedicated graphics mode)");
    }
}

void DisplayManager::RemoveDisplay(SpiLcdDisplayExtended* display) {
    for (auto it = displays_.begin(); it != displays_.end(); ++it) {
        if (*it == display) {
            displays_.erase(it);
            break;
        }
    }
}

size_t DisplayManager::GetDisplayCount() {
    return displays_.size();
}

Display* DisplayManager::GetPrimaryDisplay() {
    return primary_display_;
}

const std::vector<SpiLcdDisplayExtended*>& DisplayManager::GetAllDisplays() {
    return displays_;
}

void DisplayManager::EyeRenderTask(void* arg) {
    ESP_LOGI(TAG, "EyeRenderTask entry - starting high-performance DMA rendering loop with Step 2 Lifelike Dynamics");

    // Micro-saccadic tremor state
    float micro_tremor_x = 0.0f;
    float micro_tremor_y = 0.0f;
    uint32_t last_tremor_time_ms = 0;

    // Smoothed eyelid threshold tracking
    float current_u_thresh = 110.0f;
    float current_l_thresh = 45.0f;

    while (render_task_running_) {
        if (displays_.size() < 2) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        EyeAnimationState state = current_state_.load(std::memory_order_relaxed);
        float audio_energy = std::clamp(audio_energy_.load(std::memory_order_relaxed), 0.0f, 1.0f);
        bool speech_active = speech_active_.load(std::memory_order_relaxed);
        if (speech_active && state == EYE_STATE_IDLE) {
            state = EYE_STATE_SPEAKING;
        }
        EyeBehaviorProfile behavior = GetEyeBehaviorProfile(state, speech_active);

        static EyeAnimationState last_behavior_state = EYE_STATE_IDLE;
        static uint32_t state_entered_ms = 0;
        bool behavior_state_changed = false;
        if (state != last_behavior_state) {
            last_behavior_state = state;
            state_entered_ms = now_ms;
            behavior_state_changed = true;
        }
        uint32_t state_age_ms = now_ms - state_entered_ms;

        // 0. Safely apply pending preset transitions between frames on render core
        static int s_applied_left_preset = -1;
        static int s_applied_right_preset = -1;

        int target_left = s_left_preset_id.load(std::memory_order_acquire);
        if (target_left != s_applied_left_preset) {
            s_applied_left_preset = target_left;
            const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)target_left);
            if (!displays_.empty() && displays_[0] && asset) {
                displays_[0]->SetAsset(asset);
                ESP_LOGI(TAG, "Applied LEFT eye preset [%d]: %s on render core", target_left, asset->name);
            }
        }

        int target_right = s_right_preset_id.load(std::memory_order_acquire);
        if (target_right != s_applied_right_preset) {
            s_applied_right_preset = target_right;
            const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)target_right);
            if (displays_.size() > 1 && displays_[1] && asset) {
                displays_[1]->SetAsset(asset);
                ESP_LOGI(TAG, "Applied RIGHT eye preset [%d]: %s on render core", target_right, asset->name);
            }
        }

        // 1. Bodmer Ballistic Saccade & Natural Exploration Gaze Engine
        static bool eye_in_motion = false;
        static float eye_start_x = 0.0f, eye_start_y = 0.0f;
        static float eye_target_x = 0.0f, eye_target_y = 0.0f;
        static uint32_t move_start_time_ms = 0;
        static uint32_t move_duration_ms = 100;
        static uint32_t dwell_duration_ms = 1200;

        if (behavior_state_changed) {
            eye_in_motion = false;
            dwell_duration_ms = 0;
            move_start_time_ms = now_ms - 1;
        }

        uint32_t dt = now_ms - move_start_time_ms;

        if (s_manual_gaze.load(std::memory_order_relaxed)) {
            eye_in_motion = false;
            float target_x = s_manual_gaze_x.load(std::memory_order_relaxed);
            float target_y = s_manual_gaze_y.load(std::memory_order_relaxed);
            current_gaze_x_ += (target_x - current_gaze_x_) * 0.45f;
            current_gaze_y_ += (target_y - current_gaze_y_) * 0.45f;
        } else if (s_sleeping.load(std::memory_order_relaxed)) {
            eye_in_motion = false;
            current_gaze_x_ += (0.0f - current_gaze_x_) * 0.2f;
            current_gaze_y_ += (6.0f - current_gaze_y_) * 0.2f;
        } else if (eye_in_motion) {
            if (dt >= move_duration_ms) {
                eye_in_motion = false;
                dwell_duration_ms = RandRangeMs(behavior.dwell_min_ms, behavior.dwell_span_ms);
                move_start_time_ms = now_ms;
                current_gaze_x_ = eye_target_x;
                current_gaze_y_ = eye_target_y;
            } else {
                // Smooth Hermite Cubic Ease-in/Ease-out: 3*t^2 - 2*t^3
                float t_norm = (float)dt / (float)move_duration_ms;
                float ease_t = t_norm * t_norm * (3.0f - 2.0f * t_norm);
                current_gaze_x_ = eye_start_x + (eye_target_x - eye_start_x) * ease_t;
                current_gaze_y_ = eye_start_y + (eye_target_y - eye_start_y) * ease_t;
            }
        } else {
            if (dt > dwell_duration_ms) {
                // Conversation-aware Bodmer style gaze target. Each high-level state keeps its
                // own fixation amplitude, dwell and saccade speed so the eyes feel attentive
                // during speech instead of randomly wandering.
                float nx = 0.0f, ny = 0.0f;
                PickConversationGaze(state, state_age_ms, behavior, audio_energy, nx, ny);
                target_pupil_dilation_ = behavior.pupil_base;
                if (state == EYE_STATE_IDLE) {
                    target_pupil_dilation_ += RandUnit() * 4.0f;
                }

                eye_start_x = current_gaze_x_;
                eye_start_y = current_gaze_y_;
                eye_target_x = nx;
                eye_target_y = ny;
                // Ballistic saccade duration remains fast, but varies by state.
                move_duration_ms = RandRangeMs(behavior.move_min_ms, behavior.move_span_ms);
                move_start_time_ms = now_ms;
                eye_in_motion = true;
            }
        }

        // 2. Micro-Saccadic Physiological Fixation Tremor (200ms rhythm)
        if (now_ms - last_tremor_time_ms >= 180 + (esp_random() % 120)) {
            last_tremor_time_ms = now_ms;
            micro_tremor_x = RandSigned(behavior.tremor_x);
            micro_tremor_y = RandSigned(behavior.tremor_y);
        }

        // Smooth Pupil Accommodation + Speech Energy Reactivity
        float effective_pupil = 32.0f;
        if (s_manual_pupil.load(std::memory_order_relaxed)) {
            effective_pupil = s_manual_pupil_val.load(std::memory_order_relaxed);
            current_pupil_dilation_ = effective_pupil;
        } else {
            float audio_pulse = audio_energy * (speech_active ? 6.0f : 2.5f);
            current_pupil_dilation_ += (target_pupil_dilation_ - current_pupil_dilation_) * 0.15f;
            effective_pupil = current_pupil_dilation_ + audio_pulse;
            if (effective_pupil > 44.0f) effective_pupil = 44.0f;
            if (effective_pupil < 18.0f) effective_pupil = 18.0f;
        }

        // 3. Anatomical Eyelid Pitch Tracking & Blinking (Authentic Bodmer Uncanny Eyes)
        int left_pid = s_applied_left_preset;
        int right_pid = s_applied_right_preset;

        // All assets except full-bleed dragon (PRESET_NO_SCLERA) have authentic eyelid rendering
        bool left_has_eyelids = (left_pid != EyeRenderer::PRESET_NO_SCLERA);
        bool right_has_eyelids = (right_pid != EyeRenderer::PRESET_NO_SCLERA);
        bool either_has_eyelids = (left_has_eyelids || right_has_eyelids);

        float render_u_thresh = 0.0f;
        float render_l_thresh = 0.0f;

        if (either_has_eyelids) {
            bool is_sleeping = s_sleeping.load(std::memory_order_relaxed);
            float target_u_thresh = 0.0f;
            float target_l_thresh = 0.0f;

            if (is_sleeping) {
                target_u_thresh = 255.0f; // Eyelids closed in peaceful sleep
                target_l_thresh = 255.0f;
                blink_in_progress_ = false;
            } else {
                // Authentic Bodmer Uncanny Eyes resting aperture
                target_u_thresh = 52.0f + (current_gaze_y_ * 1.5f);
                target_l_thresh = 30.0f - (current_gaze_y_ * 0.8f);

                // Emotion & Mood modulations
                target_u_thresh += behavior.upper_lid_bias;
                target_l_thresh += behavior.lower_lid_bias;

                if (state == EYE_STATE_HAPPY) {
                    target_l_thresh += 35.0f;
                    target_u_thresh += 8.0f;
                } else if (state == EYE_STATE_SAD_SLEEPY) {
                    target_u_thresh += 50.0f;
                    target_l_thresh += 20.0f;
                } else if (state == EYE_STATE_SURPRISED) {
                    target_u_thresh = 15.0f;
                    target_l_thresh = 15.0f;
                } else if (state == EYE_STATE_ANGRY) {
                    target_u_thresh += 30.0f;
                    target_l_thresh += 25.0f;
                }

                if (target_u_thresh < 10.0f) target_u_thresh = 10.0f;
                if (target_u_thresh > 200.0f) target_u_thresh = 200.0f;
                if (target_l_thresh < 10.0f) target_l_thresh = 10.0f;
                if (target_l_thresh > 200.0f) target_l_thresh = 200.0f;
            }

            // Trigger manual blink if requested via UART or WebUI
            if (s_trigger_blink.exchange(false)) {
                blink_in_progress_ = true;
                blink_start_time_ms_ = now_ms;
                last_blink_time_ms_ = now_ms;
            }

            // 4. Organic Asymmetric Blink Engine
            if (!is_sleeping && !blink_in_progress_) {
                if (now_ms - last_blink_time_ms_ >= next_blink_interval_ms_) {
                    blink_in_progress_ = true;
                    blink_start_time_ms_ = now_ms;
                    last_blink_time_ms_ = now_ms;
                    next_blink_interval_ms_ = RandRangeMs(behavior.blink_min_ms, behavior.blink_span_ms);
                    pending_double_blink_ = ((esp_random() % 100) < behavior.double_blink_chance);
                } else if (pending_double_blink_ && (now_ms - last_blink_time_ms_ >= 220)) {
                    blink_in_progress_ = true;
                    blink_start_time_ms_ = now_ms;
                    last_blink_time_ms_ = now_ms;
                    pending_double_blink_ = false;
                }
            }

            render_u_thresh = target_u_thresh;
            render_l_thresh = target_l_thresh;

            if (blink_in_progress_) {
                uint32_t elapsed = now_ms - blink_start_time_ms_;
                const uint32_t close_dur = 35; // Fast 35ms snap closure
                const uint32_t hold_dur = 15;  // 15ms seal
                const uint32_t open_dur = 70;  // 70ms ease-back opening
                const uint32_t total_dur = close_dur + hold_dur + open_dur;

                if (elapsed < close_dur) {
                    float t = (float)elapsed / (float)close_dur;
                    float ease_t = t * t;
                    render_u_thresh = target_u_thresh + (255.0f - target_u_thresh) * ease_t;
                    render_l_thresh = target_l_thresh + (255.0f - target_l_thresh) * ease_t;
                } else if (elapsed < close_dur + hold_dur) {
                    render_u_thresh = 255.0f;
                    render_l_thresh = 255.0f;
                } else if (elapsed < total_dur) {
                    float t = (float)(elapsed - close_dur - hold_dur) / (float)open_dur;
                    float inv_t = 1.0f - t;
                    float ease_t = inv_t * inv_t;
                    render_u_thresh = target_u_thresh + (255.0f - target_u_thresh) * ease_t;
                    render_l_thresh = target_l_thresh + (255.0f - target_l_thresh) * ease_t;
                } else {
                    blink_in_progress_ = false;
                }
            }

            // Snappy, organic threshold filter (fast response, zero scanline lag)
            current_u_thresh += (render_u_thresh - current_u_thresh) * 0.55f;
            current_l_thresh += (render_l_thresh - current_l_thresh) * 0.55f;
        } else {
            current_u_thresh = 0.0f;
            current_l_thresh = 0.0f;
            blink_in_progress_ = false;
        }

        // 5. Assemble RenderParams for Left and Right Eye
        // Smooth slide-in exponential decay (~150ms glide into center)
        float slide_l = s_slide_offset_left.load(std::memory_order_relaxed);
        if (std::abs(slide_l) > 0.5f) {
            slide_l *= 0.74f;
            s_slide_offset_left.store(slide_l, std::memory_order_relaxed);
        } else {
            s_slide_offset_left.store(0.0f, std::memory_order_relaxed);
            slide_l = 0.0f;
        }

        float slide_r = s_slide_offset_right.load(std::memory_order_relaxed);
        if (std::abs(slide_r) > 0.5f) {
            slide_r *= 0.74f;
            s_slide_offset_right.store(slide_r, std::memory_order_relaxed);
        } else {
            s_slide_offset_right.store(0.0f, std::memory_order_relaxed);
            slide_r = 0.0f;
        }

        // Apply interactive touch drag or slide-in offset
        float gaze_x_l = current_gaze_x_ + micro_tremor_x + 1.8f;
        if (s_dragging_left.load(std::memory_order_relaxed)) {
            float drag = s_drag_dx_left.load(std::memory_order_relaxed) * 0.45f;
            gaze_x_l = std::clamp(drag, -28.0f, 28.0f);
        } else if (std::abs(slide_l) > 0.1f) {
            gaze_x_l += slide_l;
        }
        gaze_x_l = std::clamp(gaze_x_l, -30.0f, 30.0f);

        float gaze_x_r = current_gaze_x_ + micro_tremor_x - 1.8f;
        if (s_dragging_right.load(std::memory_order_relaxed)) {
            float drag = s_drag_dx_right.load(std::memory_order_relaxed) * 0.45f;
            gaze_x_r = std::clamp(drag, -28.0f, 28.0f);
        } else if (std::abs(slide_r) > 0.1f) {
            gaze_x_r += slide_r;
        }
        gaze_x_r = std::clamp(gaze_x_r, -30.0f, 30.0f);

        // Binocular vergence (+1.8px inward on left, -1.8px inward on right)
        EyeRenderer::RenderParams params_left;
        params_left.gaze_x = (int16_t)roundf(gaze_x_l);
        params_left.gaze_y = (int16_t)roundf(current_gaze_y_ + micro_tremor_y);
        params_left.pupil_dilation = (uint8_t)roundf(effective_pupil);
        params_left.upper_eyelid_threshold = left_has_eyelids ? (uint8_t)std::clamp((int)roundf(current_u_thresh), 0, 255) : 0;
        params_left.lower_eyelid_threshold = left_has_eyelids ? (uint8_t)std::clamp((int)roundf(current_l_thresh), 0, 255) : 0;
        params_left.eyelid_color = 0x0000;
        params_left.enable_highlights = true;
        params_left.scale = s_eye_scale.load(std::memory_order_relaxed);

        EyeRenderer::RenderParams params_right = params_left;
        params_right.gaze_x = (int16_t)roundf(gaze_x_r);
        params_right.upper_eyelid_threshold = right_has_eyelids ? (uint8_t)std::clamp((int)roundf(current_u_thresh), 0, 255) : 0;
        params_right.lower_eyelid_threshold = right_has_eyelids ? (uint8_t)std::clamp((int)roundf(current_l_thresh), 0, 255) : 0;

        // 6. Direct Hardware DMA Render Passes
        int64_t t0 = esp_timer_get_time();
        displays_[0]->RenderEye(params_left);
        int64_t t1 = esp_timer_get_time();
        displays_[1]->RenderEye(params_right);
        int64_t t2 = esp_timer_get_time();

        render_time_eye1_us_ = (uint32_t)(t1 - t0);
        render_time_both_us_ = (uint32_t)(t2 - t0);
        frame_count_++;

        if (frame_count_ % 120 == 0) {
            ESP_LOGI(TAG, "Step 2 Render: LeftPreset=%d RightPreset=%d | Gaze=(%.1f, %.1f) Pupil=%.1f",
                     left_pid, right_pid,
                     current_gaze_x_, current_gaze_y_, effective_pupil);
        }

        // Live FPS measurement
        static int64_t s_fps_start_us = 0;
        static uint32_t s_fps_frames = 0;
        if (s_fps_start_us == 0) s_fps_start_us = t0;
        s_fps_frames++;
        if (s_fps_frames >= 60) {
            int64_t now_us = esp_timer_get_time();
            int64_t diff = now_us - s_fps_start_us;
            if (diff > 0) {
                s_live_fps.store((float)s_fps_frames * 1000000.0f / (float)diff, std::memory_order_relaxed);
            }
            s_fps_start_us = now_us;
            s_fps_frames = 0;
        }

        // Target locked silky-smooth 60 FPS (~16.6ms per frame)
        int64_t frame_spent_us = t2 - t0;
        constexpr int64_t kTargetFrameUs = 16666;
        if (frame_spent_us < kTargetFrameUs) {
            uint32_t delay_ms = (uint32_t)((kTargetFrameUs - frame_spent_us) / 1000);
            if (delay_ms > 0) {
                TickType_t ticks = pdMS_TO_TICKS(delay_ms);
                vTaskDelay(ticks > 0 ? ticks : 1);
            } else {
                taskYIELD();
            }
        } else {
            taskYIELD();
        }
    }

    vTaskDelete(nullptr);
}

void DisplayManager::GetBenchmarkMetrics(uint32_t& eye1_us, uint32_t& both_us) {
    eye1_us = render_time_eye1_us_;
    both_us = render_time_both_us_;
}

void DisplayManager::SetLeftEyePreset(int preset_id) {
    int id = (preset_id >= 0 && preset_id < EyeRenderer::PRESET_COUNT) ? preset_id : 0;
    s_left_preset_id.store(id, std::memory_order_release);
    const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)id);
    ESP_LOGI(TAG, "Requested LEFT eye preset [%d]: %s (delegated to render core)", id, asset ? asset->name : "Default");
}

void DisplayManager::SetRightEyePreset(int preset_id) {
    int id = (preset_id >= 0 && preset_id < EyeRenderer::PRESET_COUNT) ? preset_id : 0;
    s_right_preset_id.store(id, std::memory_order_release);
    const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)id);
    ESP_LOGI(TAG, "Requested RIGHT eye preset [%d]: %s (delegated to render core)", id, asset ? asset->name : "Default");
}

void DisplayManager::SetEyePreset(int preset_id, const std::string& target) {
    if (target == "left" || target == "Left") {
        SetLeftEyePreset(preset_id);
    } else if (target == "right" || target == "Right") {
        SetRightEyePreset(preset_id);
    } else { // "both"
        SetLeftEyePreset(preset_id);
        SetRightEyePreset(preset_id);
    }
}

void DisplayManager::CycleLeftEye() {
    int next_id = (s_left_preset_id.load(std::memory_order_relaxed) + 1) % EyeRenderer::PRESET_COUNT;
    SetLeftEyePreset(next_id);
}

void DisplayManager::CycleLeftEyeBackwards() {
    int cur_id = s_left_preset_id.load(std::memory_order_relaxed);
    int prev_id = (cur_id - 1 + EyeRenderer::PRESET_COUNT) % EyeRenderer::PRESET_COUNT;
    SetLeftEyePreset(prev_id);
}

void DisplayManager::CycleRightEye() {
    int next_id = (s_right_preset_id.load(std::memory_order_relaxed) + 1) % EyeRenderer::PRESET_COUNT;
    SetRightEyePreset(next_id);
}

void DisplayManager::CycleRightEyeBackwards() {
    int cur_id = s_right_preset_id.load(std::memory_order_relaxed);
    int prev_id = (cur_id - 1 + EyeRenderer::PRESET_COUNT) % EyeRenderer::PRESET_COUNT;
    SetRightEyePreset(prev_id);
}

void DisplayManager::CycleBothEyes() {
    int next_id = (s_left_preset_id.load(std::memory_order_relaxed) + 1) % EyeRenderer::PRESET_COUNT;
    SetEyePreset(next_id, "both");
}

void DisplayManager::CycleEyePreset() {
    CycleBothEyes();
}

int DisplayManager::GetLeftPresetId() {
    return s_left_preset_id.load(std::memory_order_relaxed);
}

int DisplayManager::GetRightPresetId() {
    return s_right_preset_id.load(std::memory_order_relaxed);
}

int DisplayManager::GetCurrentPresetId() {
    return s_left_preset_id.load(std::memory_order_relaxed);
}

void DisplayManager::SetTouchHandles(esp_lcd_touch_handle_t tp1, esp_lcd_touch_handle_t tp2) {
    s_tp_left = tp1;
    s_tp_right = tp2;
    ESP_LOGI(TAG, "Touch Handles registered in DisplayManager: Left=%p, Right=%p", tp1, tp2);

    static TaskHandle_t s_touch_task = nullptr;
    if (!s_touch_task && (tp1 || tp2)) {
        xTaskCreatePinnedToCore(TouchPollingTask, "touch_poll", 4096, nullptr, 2, &s_touch_task, 0);
        ESP_LOGI(TAG, "Touch Polling Task started on Core 0 (Priority 2, 100Hz)");
    }
}

void DisplayManager::SetAudioActivity(float energy, bool speech_active) {
    audio_energy_.store(energy, std::memory_order_relaxed);
    speech_active_.store(speech_active, std::memory_order_relaxed);
}

void DisplayManager::SetAudioEnergy(float energy, bool speech_active) {
    audio_energy_.store(energy, std::memory_order_relaxed);
    speech_active_.store(speech_active, std::memory_order_relaxed);
}

EyeAnimationState DisplayManager::MapStatusToState(const char* status) {
    if (!status) return EYE_STATE_IDLE;
    if (std::strcmp(status, Lang::Strings::LISTENING) == 0) return EYE_STATE_LISTENING;
    if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) return EYE_STATE_SPEAKING;
    if (std::strcmp(status, Lang::Strings::STANDBY) == 0) return EYE_STATE_IDLE;
    if (std::strcmp(status, Lang::Strings::CONNECTING) == 0 ||
        std::strcmp(status, Lang::Strings::REGISTERING_NETWORK) == 0 ||
        std::strcmp(status, Lang::Strings::DETECTING_MODULE) == 0 ||
        std::strcmp(status, Lang::Strings::LOADING_PROTOCOL) == 0 ||
        std::strcmp(status, Lang::Strings::CHECKING_NEW_VERSION) == 0 ||
        std::strcmp(status, Lang::Strings::PLEASE_WAIT) == 0) {
        return EYE_STATE_THINKING;
    }

    std::string s(status);
    if (s.find("Listen") != std::string::npos || s.find("listening") != std::string::npos) return EYE_STATE_LISTENING;
    if (s.find("Think") != std::string::npos || s.find("thinking") != std::string::npos ||
        s.find("Logging") != std::string::npos || s.find("Connecting") != std::string::npos) return EYE_STATE_THINKING;
    if (s.find("Speak") != std::string::npos || s.find("speaking") != std::string::npos) return EYE_STATE_SPEAKING;
    return EYE_STATE_IDLE;
}

EyeAnimationState DisplayManager::MapEmotionToState(const char* emotion) {
    if (!emotion) return EYE_STATE_IDLE;
    std::string e(emotion);
    for (auto& c : e) c = std::tolower(c);
    if (e.find("happy") != std::string::npos || e.find("excited") != std::string::npos || e.find("laugh") != std::string::npos) return EYE_STATE_HAPPY;
    if (e.find("sad") != std::string::npos || e.find("sleep") != std::string::npos || e.find("tired") != std::string::npos) return EYE_STATE_SAD_SLEEPY;
    if (e.find("angry") != std::string::npos) return EYE_STATE_ANGRY;
    if (e.find("surpris") != std::string::npos || e.find("shock") != std::string::npos) return EYE_STATE_SURPRISED;
    return EYE_STATE_IDLE;
}

void DisplayManager::SetStatus(const char* status) {
    current_state_.store(MapStatusToState(status), std::memory_order_relaxed);
}

void DisplayManager::SetEmotion(const char* emotion) {
    if (emotion && strlen(emotion) > 0) {
        current_state_.store(MapEmotionToState(emotion), std::memory_order_relaxed);
    }
}

void DisplayManager::ShowNotification(const char* message, int duration_ms) {}
void DisplayManager::ShowNotification(const std::string& notification, int duration_ms) {}
void DisplayManager::SetChatMessage(const char* role, const char* content) {
    std::lock_guard<std::mutex> lock(s_chat_mutex);
    s_chat_role = role ? role : "";
    s_chat_text = content ? content : "";
    if (s_chat_text.size() > 96) {
        s_chat_text.resize(96);
    }
    s_chat_until_ms = (uint32_t)(esp_timer_get_time() / 1000ULL) + 20000;
}

void DisplayManager::ClearChatMessages() {
    std::lock_guard<std::mutex> lock(s_chat_mutex);
    s_chat_role.clear();
    s_chat_text.clear();
    s_chat_until_ms = 0;
}
void DisplayManager::SetTheme(Theme* theme) {}
Theme* DisplayManager::GetTheme() { return nullptr; }
void DisplayManager::UpdateStatusBar(bool update_all) {}
void DisplayManager::SetPowerSaveMode(bool on) {}
void DisplayManager::SetupUI() {
    for (auto* display : displays_) {
        if (display) display->SetupUI();
    }
}

bool DisplayManager::Lock(int timeout_ms) {
    return true;
}

void DisplayManager::Unlock() {}

// ---------------------------------------------------------------------------
// SpiLcdDisplayExtended Implementation (Zero-Copy DMA Streaming)
// ---------------------------------------------------------------------------

SpiLcdDisplayExtended::SpiLcdDisplayExtended(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel,
    int width, int height,
    int offset_x, int offset_y,
    bool mirror_x, bool mirror_y,
    bool swap_xy,
    bool is_left_eye
) : Display(),
    panel_io_(panel_io),
    panel_(panel),
    is_left_eye_(is_left_eye),
    eye_renderer_(&EyeRenderer::kDefaultEyeAsset) {

    width_ = width;
    height_ = height;
    color_done_sem_ = xSemaphoreCreateBinary();
    if (!color_done_sem_) {
        ESP_LOGE(TAG, "CRITICAL: Failed to create LCD transfer semaphore for %s eye",
                 is_left_eye ? "Left" : "Right");
    } else if (panel_io_) {
        esp_lcd_panel_io_callbacks_t callbacks = {};
        callbacks.on_color_trans_done = OnColorTransferDone;
        esp_err_t callback_err = esp_lcd_panel_io_register_event_callbacks(panel_io_, &callbacks, this);
        if (callback_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register LCD transfer callback for %s eye: %s (%d)",
                     is_left_eye ? "Left" : "Right",
                     esp_err_to_name(callback_err),
                     (int)callback_err);
        }
    }

    // Allocate one DMA chunk buffer in internal SRAM.
    dma_buffer_ = (uint16_t*)heap_caps_malloc(kChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (!dma_buffer_) {
        ESP_LOGE(TAG, "CRITICAL: Failed to allocate %u bytes DMA chunk buffer in internal SRAM for %s eye!",
                 (unsigned)kChunkBytes, is_left_eye ? "Left" : "Right");
    } else {
        ESP_LOGI(TAG, "Allocated 1x %u bytes DMA chunk buffer in internal SRAM for %s eye",
                 (unsigned)kChunkBytes, is_left_eye ? "Left" : "Right");
    }

    // Direct hardware screen clear (Black 0x0000)
    if (dma_buffer_) {
        memset(dma_buffer_, 0, kChunkBytes);
        for (int y = 0; y < height_; y += kChunkLines) {
            esp_err_t err = ESP_FAIL;
            for (int retry = 0; retry < 20; ++retry) {
                err = esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + kChunkLines, dma_buffer_);
                if (err == ESP_OK) break;
                esp_rom_delay_us(500);
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "%s eye initial clear failed at y=%d: %s (%d)",
                         is_left_eye ? "Left" : "Right",
                         y,
                         esp_err_to_name(err),
                         (int)err);
            } else {
                WaitForColorTransfer(y);
            }
        }
    }

    // Explicitly turn GC9A01 display output ON
    ESP_LOGI(TAG, "Turning display on for %s eye", is_left_eye ? "Left" : "Right");
    esp_err_t err = esp_lcd_panel_disp_on_off(panel_, true);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "disp_on_off returned %d", err);
    }

    SetupUI();
    DisplayManager::AddDisplay(this);
}

SpiLcdDisplayExtended::~SpiLcdDisplayExtended() {
    if (dma_buffer_) {
        heap_caps_free(dma_buffer_);
        dma_buffer_ = nullptr;
    }
    if (color_done_sem_) {
        vSemaphoreDelete(color_done_sem_);
        color_done_sem_ = nullptr;
    }
}

bool SpiLcdDisplayExtended::OnColorTransferDone(esp_lcd_panel_io_handle_t panel_io,
                                                esp_lcd_panel_io_event_data_t* edata,
                                                void* user_ctx) {
    auto* self = static_cast<SpiLcdDisplayExtended*>(user_ctx);
    if (!self || !self->color_done_sem_) {
        return false;
    }
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(self->color_done_sem_, &high_task_woken);
    return high_task_woken == pdTRUE;
}

bool SpiLcdDisplayExtended::WaitForColorTransfer(int y) {
    if (!color_done_sem_) {
        return true;
    }
    if (xSemaphoreTake(color_done_sem_, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGE(TAG, "%s eye LCD transfer timeout at y=%d",
                 is_left_eye_ ? "Left" : "Right",
                 y);
        return false;
    }
    return true;
}

void SpiLcdDisplayExtended::FillColor(uint16_t rgb565_color) {
    if (!panel_ || !dma_buffer_) return;

    const uint16_t color = DmaRgb565(rgb565_color);
    for (int i = 0; i < 240 * kChunkLines; i++) {
        dma_buffer_[i] = color;
    }

    for (int y = 0; y < 240; y += kChunkLines) {
        esp_err_t err = ESP_FAIL;
        for (int retry = 0; retry < 20; ++retry) {
            err = esp_lcd_panel_draw_bitmap(panel_, 0, y, 240, y + kChunkLines, dma_buffer_);
            if (err == ESP_OK) break;
            esp_rom_delay_us(500);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "%s eye LCD fill failed at y=%d: %s (%d)",
                     is_left_eye_ ? "Left" : "Right",
                     y,
                     esp_err_to_name(err),
                     (int)err);
        } else {
            WaitForColorTransfer(y);
        }
    }
}

bool SpiLcdDisplayExtended::Lock(int timeout_ms) {
    return true;
}

void SpiLcdDisplayExtended::Unlock() {}

void SpiLcdDisplayExtended::SetupUI() {
    // Initial static frame pass
    EyeRenderer::RenderParams params;
    params.gaze_x = is_left_eye_ ? 2 : -2;
    params.gaze_y = 0;
    params.pupil_dilation = 32;
    params.upper_eyelid_threshold = 0;
    params.lower_eyelid_threshold = 0;
    params.eyelid_color = 0x0000;
    params.enable_highlights = true;

    RenderEye(params);
}

void SpiLcdDisplayExtended::RenderEye(const EyeRenderer::RenderParams& params) {
    if (!panel_ || !dma_buffer_) return;

    static uint32_t s_left_draw_errors = 0;
    static uint32_t s_right_draw_errors = 0;
    uint32_t& draw_errors = is_left_eye_ ? s_left_draw_errors : s_right_draw_errors;

    // Stream fixed-height chunks directly into SPI DMA.
    for (int y = 0; y < 240; y += kChunkLines) {
        uint16_t* active_buf = dma_buffer_;
        eye_renderer_.RenderChunk(active_buf, y, kChunkLines, params, is_left_eye_);
        esp_err_t err = ESP_FAIL;
        for (int retry = 0; retry < 20; ++retry) {
            err = esp_lcd_panel_draw_bitmap(panel_, 0, y, 240, y + kChunkLines, active_buf);
            if (err == ESP_OK) break;
            esp_rom_delay_us(500);
        }
        if (err != ESP_OK) {
            draw_errors++;
            if (draw_errors <= 8 || (draw_errors % 120) == 0) {
                ESP_LOGE(TAG, "%s eye LCD draw failed at y=%d: %s (%d), total=%lu",
                         is_left_eye_ ? "Left" : "Right",
                         y,
                         esp_err_to_name(err),
                         (int)err,
                         (unsigned long)draw_errors);
            }
        } else {
            WaitForColorTransfer(y);
        }
    }
}

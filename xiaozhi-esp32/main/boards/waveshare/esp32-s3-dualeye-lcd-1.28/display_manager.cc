// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Direct Hardware DMA Dual-Eye Display Manager
#include "display_manager.h"
#include "default_eye_asset.h"
#include "eye_presets.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_cst816s.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

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

static void ProcessTouchInput(esp_lcd_touch_handle_t tp, TouchTracker& tracker, bool is_left, uint32_t now_ms) {
    if (!tp) return;
    esp_lcd_touch_read_data(tp);
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
        } else {
            tracker.last_x = tx[0];
            tracker.last_y = ty[0];
            int dx = (int)tracker.last_x - (int)tracker.start_x;
            if (!tracker.gesture_triggered && (now_ms - tracker.start_time_ms < 700)) {
                if (dx > 35) { // Swipe Right -> Previous Preset
                    tracker.gesture_triggered = true;
                    if (is_left) {
                        ESP_LOGI(TAG, "Left Touch Swipe RIGHT -> Prev Preset");
                        DisplayManager::CycleLeftEyeBackwards();
                    } else {
                        ESP_LOGI(TAG, "Right Touch Swipe RIGHT -> Prev Preset");
                        DisplayManager::CycleRightEyeBackwards();
                    }
                } else if (dx < -35) { // Swipe Left -> Next Preset
                    tracker.gesture_triggered = true;
                    if (is_left) {
                        ESP_LOGI(TAG, "Left Touch Swipe LEFT -> Next Preset");
                        DisplayManager::CycleLeftEye();
                    } else {
                        ESP_LOGI(TAG, "Right Touch Swipe LEFT -> Next Preset");
                        DisplayManager::CycleRightEye();
                    }
                }
            }
        }
    } else {
        if (tracker.is_touching) {
            tracker.is_touching = false;
        }
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
        // Pinned to Core 1 at Priority 1 (lower than Opus at 2, Audio Out at 4, Audio In at 8)
        xTaskCreatePinnedToCore(EyeRenderTask, "eye_render", 4096, nullptr, 1, &render_task_handle_, 1);
        ESP_LOGI(TAG, "Direct DMA Eye Render Task started on Core 1 (Priority 1, cooperative pacing)");
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

        // 0. Process Capacitive Touch Gestures for Left & Right Screens
        ProcessTouchInput(s_tp_left, s_tracker_left, true, now_ms);
        ProcessTouchInput(s_tp_right, s_tracker_right, false, now_ms);

        // 1. Bodmer Ballistic Saccade & Natural Exploration Gaze Engine
        static bool eye_in_motion = false;
        static float eye_start_x = 0.0f, eye_start_y = 0.0f;
        static float eye_target_x = 0.0f, eye_target_y = 0.0f;
        static uint32_t move_start_time_ms = 0;
        static uint32_t move_duration_ms = 100;
        static uint32_t dwell_duration_ms = 1200;

        uint32_t dt = now_ms - move_start_time_ms;

        if (eye_in_motion) {
            if (dt >= move_duration_ms) {
                eye_in_motion = false;
                // Natural biological stop dwell duration: 400ms to 2800ms
                dwell_duration_ms = 400 + (esp_random() % 2400);
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
                // Bodmer style natural circular amplitude (+/-24px in X, +/-18px in Y)
                float nx = 0.0f, ny = 0.0f;
                int attempts = 0;
                do {
                    nx = (float)((int)(esp_random() % 49) - 24); // -24 to +24
                    ny = (float)((int)(esp_random() % 37) - 18); // -18 to +18
                    attempts++;
                } while (((nx * nx) / (24.0f * 24.0f) + (ny * ny) / (18.0f * 18.0f) > 1.0f) && attempts < 10);

                if (state == EYE_STATE_LISTENING) {
                    nx = 0.0f;
                    ny = -4.0f; // Attentive direct gaze
                    target_pupil_dilation_ = 36.0f;
                } else if (state == EYE_STATE_THINKING) {
                    nx = (esp_random() % 2 == 0) ? -16.0f : 16.0f;
                    ny = -12.0f; // Introspective glance
                    target_pupil_dilation_ = 28.0f;
                } else if (state == EYE_STATE_SPEAKING) {
                    target_pupil_dilation_ = 33.0f;
                } else {
                    target_pupil_dilation_ = 29.0f + (float)(esp_random() % 5);
                }

                eye_start_x = current_gaze_x_;
                eye_start_y = current_gaze_y_;
                eye_target_x = nx;
                eye_target_y = ny;
                // Ballistic saccade duration: 70ms to 150ms
                move_duration_ms = 70 + (esp_random() % 80);
                move_start_time_ms = now_ms;
                eye_in_motion = true;
            }
        }

        // 2. Micro-Saccadic Physiological Fixation Tremor (200ms rhythm)
        if (now_ms - last_tremor_time_ms >= 180 + (esp_random() % 120)) {
            last_tremor_time_ms = now_ms;
            micro_tremor_x = ((float)(esp_random() % 100) / 100.0f - 0.5f) * 1.4f; // +/- 0.7px
            micro_tremor_y = ((float)(esp_random() % 100) / 100.0f - 0.5f) * 1.0f; // +/- 0.5px
        }

        // Smooth Pupil Accommodation + Speech Energy Reactivity
        float audio_pulse = audio_energy_.load(std::memory_order_relaxed) * 4.5f;
        current_pupil_dilation_ += (target_pupil_dilation_ - current_pupil_dilation_) * 0.12f;
        float effective_pupil = current_pupil_dilation_ + audio_pulse;
        if (effective_pupil > 40.0f) effective_pupil = 40.0f;
        if (effective_pupil < 20.0f) effective_pupil = 20.0f;

        // 3. Anatomical Eyelid Pitch Tracking & Blinking (Human Blue & Owl Amber only)
        int left_pid = s_left_preset_id.load(std::memory_order_relaxed);
        int right_pid = s_right_preset_id.load(std::memory_order_relaxed);

        bool left_has_eyelids = (left_pid == EyeRenderer::PRESET_DEFAULT || left_pid == EyeRenderer::PRESET_JAPANESE || left_pid == EyeRenderer::PRESET_OWL);
        bool right_has_eyelids = (right_pid == EyeRenderer::PRESET_DEFAULT || right_pid == EyeRenderer::PRESET_JAPANESE || right_pid == EyeRenderer::PRESET_OWL);
        bool either_has_eyelids = (left_has_eyelids || right_has_eyelids);

        float render_u_thresh = 0.0f;
        float render_l_thresh = 0.0f;

        if (either_has_eyelids) {
            // Resting posture creates the almond ocular socket from Bodmer's uncanny eyes
            float target_u_thresh = 110.0f + (current_gaze_y_ * 2.2f);
            float target_l_thresh = 45.0f - (current_gaze_y_ * 0.8f);

            // Emotion-specific Eyelid Modulations
            if (state == EYE_STATE_LISTENING) {
                target_u_thresh -= 16.0f; // Attentive wide gaze
                target_l_thresh -= 8.0f;
            } else if (state == EYE_STATE_THINKING) {
                target_u_thresh += 12.0f; // Pensive squint
            } else if (state == EYE_STATE_HAPPY) {
                target_l_thresh += 65.0f; // Smiling Duchenne lower lid arch
                target_u_thresh += 5.0f;
            } else if (state == EYE_STATE_SAD_SLEEPY) {
                target_u_thresh += 45.0f; // Drooping heavy eyelid
            } else if (state == EYE_STATE_SURPRISED) {
                target_u_thresh = 20.0f;  // Wide open surprise
                target_l_thresh = 20.0f;
            }

            // Clamp baseline thresholds to anatomically valid ranges
            if (target_u_thresh < 20.0f) target_u_thresh = 20.0f;
            if (target_u_thresh > 200.0f) target_u_thresh = 200.0f;
            if (target_l_thresh < 20.0f) target_l_thresh = 20.0f;
            if (target_l_thresh > 200.0f) target_l_thresh = 200.0f;

            // 4. Organic Asymmetric Blink Engine
            if (!blink_in_progress_) {
                if (now_ms - last_blink_time_ms_ >= next_blink_interval_ms_) {
                    blink_in_progress_ = true;
                    blink_start_time_ms_ = now_ms;
                    last_blink_time_ms_ = now_ms;
                    next_blink_interval_ms_ = 2400 + (esp_random() % 3200);
                    pending_double_blink_ = ((esp_random() % 100) < 18);
                } else if (pending_double_blink_ && (now_ms - last_blink_time_ms_ >= 260)) {
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
                const uint32_t close_dur = 40; // Fast 40ms closure
                const uint32_t hold_dur = 20;  // 20ms seal
                const uint32_t open_dur = 90;  // 90ms ease-back opening
                const uint32_t total_dur = close_dur + hold_dur + open_dur;

                if (elapsed < close_dur) {
                    float t = (float)elapsed / (float)close_dur;
                    float ease_t = t * t; // Quadratic ease-in
                    render_u_thresh = target_u_thresh + (255.0f - target_u_thresh) * ease_t;
                    render_l_thresh = target_l_thresh + (255.0f - target_l_thresh) * ease_t;
                } else if (elapsed < close_dur + hold_dur) {
                    render_u_thresh = 255.0f;
                    render_l_thresh = 255.0f;
                } else if (elapsed < total_dur) {
                    float t = (float)(elapsed - close_dur - hold_dur) / (float)open_dur;
                    float inv_t = 1.0f - t;
                    float ease_t = inv_t * inv_t; // Quadratic ease-out
                    render_u_thresh = target_u_thresh + (255.0f - target_u_thresh) * ease_t;
                    render_l_thresh = target_l_thresh + (255.0f - target_l_thresh) * ease_t;
                } else {
                    blink_in_progress_ = false;
                }
            }

            // Smooth threshold filtering to prevent scanline jitter
            current_u_thresh += (render_u_thresh - current_u_thresh) * 0.35f;
            current_l_thresh += (render_l_thresh - current_l_thresh) * 0.35f;
        } else {
            current_u_thresh = 0.0f;
            current_l_thresh = 0.0f;
            blink_in_progress_ = false;
        }

        // 5. Assemble RenderParams for Left and Right Eye
        // Binocular vergence (+1.8px inward on left, -1.8px inward on right)
        EyeRenderer::RenderParams params_left;
        params_left.gaze_x = (int16_t)roundf(current_gaze_x_ + micro_tremor_x + 1.8f);
        params_left.gaze_y = (int16_t)roundf(current_gaze_y_ + micro_tremor_y);
        params_left.pupil_dilation = (uint8_t)roundf(effective_pupil);
        params_left.upper_eyelid_threshold = left_has_eyelids ? (uint8_t)std::clamp((int)roundf(current_u_thresh), 0, 255) : 0;
        params_left.lower_eyelid_threshold = left_has_eyelids ? (uint8_t)std::clamp((int)roundf(current_l_thresh), 0, 255) : 0;
        params_left.eyelid_color = 0x0000;
        params_left.enable_highlights = true;

        EyeRenderer::RenderParams params_right = params_left;
        params_right.gaze_x = (int16_t)roundf(current_gaze_x_ + micro_tremor_x - 1.8f);
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

        // Target ~50 FPS (20ms frame time) with explicit RTOS yield to guarantee audio priority
        vTaskDelay(pdMS_TO_TICKS(18));
    }

    vTaskDelete(nullptr);
}

void DisplayManager::GetBenchmarkMetrics(uint32_t& eye1_us, uint32_t& both_us) {
    eye1_us = render_time_eye1_us_;
    both_us = render_time_both_us_;
}

void DisplayManager::SetLeftEyePreset(int preset_id) {
    int id = (preset_id >= 0 && preset_id < EyeRenderer::PRESET_COUNT) ? preset_id : 0;
    s_left_preset_id.store(id, std::memory_order_relaxed);
    const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)id);
    ESP_LOGI(TAG, "Switched LEFT eye to preset [%d]: %s", id, asset ? asset->name : "Default");
    if (!displays_.empty() && displays_[0]) {
        displays_[0]->SetAsset(asset);
    }
}

void DisplayManager::SetRightEyePreset(int preset_id) {
    int id = (preset_id >= 0 && preset_id < EyeRenderer::PRESET_COUNT) ? preset_id : 0;
    s_right_preset_id.store(id, std::memory_order_relaxed);
    const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)id);
    ESP_LOGI(TAG, "Switched RIGHT eye to preset [%d]: %s", id, asset ? asset->name : "Default");
    if (displays_.size() > 1 && displays_[1]) {
        displays_[1]->SetAsset(asset);
    }
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
void DisplayManager::SetChatMessage(const char* role, const char* content) {}
void DisplayManager::ClearChatMessages() {}
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

    // Allocate 2 ping-pong DMA chunk buffers in internal SRAM (zero wait-state L1)
    dma_buffer_[0] = (uint16_t*)heap_caps_malloc(kChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    dma_buffer_[1] = (uint16_t*)heap_caps_malloc(kChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (!dma_buffer_[0] || !dma_buffer_[1]) {
        ESP_LOGE(TAG, "CRITICAL: Failed to allocate %u bytes DMA chunk buffers in internal SRAM for %s eye!",
                 (unsigned)kChunkBytes, is_left_eye ? "Left" : "Right");
    } else {
        ESP_LOGI(TAG, "Allocated 2x %u bytes DMA ping-pong buffers in internal SRAM for %s eye",
                 (unsigned)kChunkBytes, is_left_eye ? "Left" : "Right");
    }

    // Direct hardware screen clear (Black 0x0000)
    if (dma_buffer_[0]) {
        memset(dma_buffer_[0], 0, kChunkBytes);
        for (int y = 0; y < height_; y += kChunkLines) {
            esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + kChunkLines, dma_buffer_[0]);
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
    if (dma_buffer_[0]) {
        heap_caps_free(dma_buffer_[0]);
        dma_buffer_[0] = nullptr;
    }
    if (dma_buffer_[1]) {
        heap_caps_free(dma_buffer_[1]);
        dma_buffer_[1] = nullptr;
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
    if (!panel_ || !dma_buffer_[0] || !dma_buffer_[1]) return;

    // Stream 12 chunks (20 lines each) directly into SPI DMA
    for (int y = 0; y < 240; y += kChunkLines) {
        uint16_t* active_buf = dma_buffer_[current_dma_buf_];
        eye_renderer_.RenderChunk(active_buf, y, kChunkLines, params, is_left_eye_);
        esp_lcd_panel_draw_bitmap(panel_, 0, y, 240, y + kChunkLines, active_buf);
        current_dma_buf_ = !current_dma_buf_;
    }
}

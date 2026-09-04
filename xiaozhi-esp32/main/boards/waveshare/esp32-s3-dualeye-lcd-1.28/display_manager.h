#pragma once

#include "display.h"
#include "eye_types.h"
#include "uncanny_eye_renderer.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <string>
#include <atomic>

// Eye animation states
enum EyeAnimationState {
    EYE_STATE_IDLE,
    EYE_STATE_LISTENING,
    EYE_STATE_THINKING,
    EYE_STATE_SPEAKING,
    EYE_STATE_HAPPY,
    EYE_STATE_SAD_SLEEPY,
    EYE_STATE_ANGRY,
    EYE_STATE_SURPRISED
};

class SpiLcdDisplayExtended;

class DisplayManager : public Display {
private:
    static std::vector<SpiLcdDisplayExtended*> displays_;
    static Display* primary_display_;

    // Shared eye state across displays
    static std::atomic<EyeAnimationState> current_state_;
    static std::atomic<EyeAnimationState> target_state_;

    // Lock-free audio activity states
    static std::atomic<float> audio_energy_;
    static std::atomic<bool> speech_active_;

    // Saccadic Gaze & Kinematic Motion State
    static float current_gaze_x_;
    static float current_gaze_y_;
    static float target_gaze_x_;
    static float target_gaze_y_;
    static uint32_t last_saccade_time_ms_;
    static uint32_t saccade_dwell_time_ms_;

    // Pupil Tracking & Accommodation
    static float current_pupil_dilation_;
    static float target_pupil_dilation_;

    // Organic Asymmetric Blink Engine
    static bool blink_in_progress_;
    static uint32_t blink_start_time_ms_;
    static uint32_t last_blink_time_ms_;
    static uint32_t next_blink_interval_ms_;
    static bool pending_double_blink_;
    static uint32_t double_blink_trigger_ms_;

    // Render task & benchmark metrics
    static TaskHandle_t render_task_handle_;
    static std::atomic<bool> render_task_running_;
    static uint32_t render_time_eye1_us_;
    static uint32_t render_time_both_us_;
    static uint32_t frame_count_;

    static EyeAnimationState MapStatusToState(const char* status);
    static EyeAnimationState MapEmotionToState(const char* emotion);
    static void EyeRenderTask(void* arg);

public:
    DisplayManager();
    virtual ~DisplayManager();

    static void AddDisplay(SpiLcdDisplayExtended* display);
    static void RemoveDisplay(SpiLcdDisplayExtended* display);
    static size_t GetDisplayCount();
    static Display* GetPrimaryDisplay();
    static const std::vector<SpiLcdDisplayExtended*>& GetAllDisplays();

    // The Display interface implementation applied to all screens
    virtual void SetStatus(const char* status) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void ShowNotification(const char* message, int duration_ms = 3000) override;
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void SetTheme(Theme* theme) override;
    virtual Theme* GetTheme() override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void SetPowerSaveMode(bool on) override;
    virtual void SetupUI() override;

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    // Lock-free audio activity hook (preserves solid audio foundation)
    virtual void SetAudioActivity(float energy, bool speech_active) override;
    static void SetAudioEnergy(float energy, bool speech_active);

    // Benchmark metrics getter
    static void GetBenchmarkMetrics(uint32_t& eye1_us, uint32_t& both_us);

    // Independent & Synchronized Preset Switching
    static void SetLeftEyePreset(int preset_id);
    static void SetRightEyePreset(int preset_id);
    static void SetEyePreset(int preset_id, const std::string& target = "both");
    static void CycleLeftEye();
    static void CycleLeftEyeBackwards();
    static void CycleRightEye();
    static void CycleRightEyeBackwards();
    static void CycleBothEyes();
    static void CycleEyePreset();
    static int GetLeftPresetId();
    static int GetRightPresetId();
    static int GetCurrentPresetId();

    // Capacitive Touch Integration
    static void SetTouchHandles(esp_lcd_touch_handle_t tp1, esp_lcd_touch_handle_t tp2);
};

// Single eye display instance representing one physical screen surface
class SpiLcdDisplayExtended : public Display {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool is_left_eye_ = true;
    EyeRenderer::UncannyEyeRenderer eye_renderer_;

    // Internal SRAM DMA ping-pong buffers
    static constexpr int kChunkLines = 20;
    static constexpr size_t kChunkBytes = 240 * kChunkLines * sizeof(uint16_t);
    uint16_t* dma_buffer_[2] = {nullptr, nullptr};
    uint8_t current_dma_buf_ = 0;

public:
    SpiLcdDisplayExtended(esp_lcd_panel_io_handle_t panel_io,
                          esp_lcd_panel_handle_t panel,
                          int width, int height,
                          int offset_x, int offset_y,
                          bool mirror_x, bool mirror_y,
                          bool swap_xy,
                          bool is_left_eye = true);
    virtual ~SpiLcdDisplayExtended();

    virtual void SetupUI() override;
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetAsset(const EyeRenderer::EyeAssetConfig* asset) {
        eye_renderer_.SetAsset(asset);
    }

    // High-performance slice-by-slice DMA render pass
    void RenderEye(const EyeRenderer::RenderParams& params);

    bool IsLeftEye() const { return is_left_eye_; }
    esp_lcd_panel_handle_t GetPanel() const { return panel_; }
};
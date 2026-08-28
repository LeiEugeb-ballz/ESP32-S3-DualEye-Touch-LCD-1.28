#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <esp_log.h>
#include "i2c_device.h"
#include <driver/i2c.h>
#include <driver/ledc.h>
#include <wifi_station.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include "esp_io_expander_tca95xx_16bit.h"
#include <driver/spi_common.h>
#include "display_manager.h"

#include <esp_lcd_gc9a01.h>
#include <esp_lvgl_port.h>
#include <ssid_manager.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_cst816s.h>
#include "mcp_server.h"
#include "eye_presets.h"
#include "ota_server.h"
#include "memory_manager.h"
#include "bootstrap.h"
#include "wifi_manager.h"
#include "system_info.h"

#define TAG "waveshare_s3_dualeye_lcd_1_28"

class DualBacklight : public Backlight {
private:
    gpio_num_t pin1_;
    gpio_num_t pin2_;
public:
    DualBacklight(gpio_num_t pin1, gpio_num_t pin2) : Backlight(), pin1_(pin1), pin2_(pin2) {
        const ledc_timer_config_t timer_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
            .deconfigure = false
        };
        ledc_timer_config(&timer_cfg);

        const ledc_channel_config_t ch0 = {
            .gpio_num = pin1_,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 1023,
            .hpoint = 0,
        };
        ledc_channel_config(&ch0);

        const ledc_channel_config_t ch1 = {
            .gpio_num = pin2_,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = LEDC_TIMER_0,
            .duty = 1023,
            .hpoint = 0,
        };
        ledc_channel_config(&ch1);
    }

protected:
    virtual void SetBrightnessImpl(uint8_t brightness) override {
        uint32_t duty = (1023 * brightness) / 100;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    }
};

class CustomBoard : public WifiBoard {
private:
    Button boot_button_;
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_bus_handle_t i2c_bus2_ = nullptr;
    esp_lcd_touch_handle_t tp_ = nullptr;
    esp_lcd_touch_handle_t tp2_ = nullptr;
    esp_io_expander_handle_t io_expander = NULL;
    SpiLcdDisplayExtended* display_;
    SpiLcdDisplayExtended* display2_;
    DisplayManager display_manager_;

    void InitializeI2c() {
        // Initialize I2C peripheral (I2C0 for Audio Codec and Left Touch)
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = I2C_SDA_IO,
            .scl_io_num = I2C_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeTouch() {
        ESP_LOGI(TAG, "Initializing Touch Panels (CST816S)...");
        // 1. Touch 1 (Left Screen) on i2c_bus_ (I2C0)
        esp_lcd_panel_io_handle_t tp1_io_handle = nullptr;
        esp_lcd_panel_io_i2c_config_t tp1_io_config = {};
        tp1_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
        tp1_io_config.scl_speed_hz = 100000;
        tp1_io_config.control_phase_bytes = 1;
        tp1_io_config.dc_bit_offset = 0;
        tp1_io_config.lcd_cmd_bits = 8;
        tp1_io_config.flags.disable_control_phase = 1;

        esp_err_t ret1 = esp_lcd_new_panel_io_i2c(i2c_bus_, &tp1_io_config, &tp1_io_handle);
        if (ret1 == ESP_OK) {
            esp_lcd_touch_config_t tp1_cfg = {
                .x_max = DISPLAY_WIDTH,
                .y_max = DISPLAY_HEIGHT,
                .rst_gpio_num = TOUCH1_RST_PIN,
                .int_gpio_num = TOUCH1_INT_PIN,
                .flags = {
                    .swap_xy = 0,
                    .mirror_x = 0,
                    .mirror_y = 0,
                },
            };
            ret1 = esp_lcd_touch_new_i2c_cst816s(tp1_io_handle, &tp1_cfg, &tp_);
            if (ret1 == ESP_OK) {
                ESP_LOGI(TAG, "Touch 1 (Left) initialized successfully");
            } else {
                ESP_LOGW(TAG, "Failed to initialize Touch 1: %s", esp_err_to_name(ret1));
            }
        } else {
            ESP_LOGW(TAG, "Failed to create Touch 1 IO: %s", esp_err_to_name(ret1));
        }

        // 2. Touch 2 (Right Screen) on i2c_bus2_ (I2C1)
        i2c_master_bus_config_t i2c2_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = I2C1_SDA_IO,
            .scl_io_num = I2C1_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
        };
        esp_err_t ret_i2c2 = i2c_new_master_bus(&i2c2_bus_cfg, &i2c_bus2_);
        if (ret_i2c2 == ESP_OK) {
            esp_lcd_panel_io_handle_t tp2_io_handle = nullptr;
            esp_lcd_panel_io_i2c_config_t tp2_io_config = {};
            tp2_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
            tp2_io_config.scl_speed_hz = 100000;
            tp2_io_config.control_phase_bytes = 1;
            tp2_io_config.dc_bit_offset = 0;
            tp2_io_config.lcd_cmd_bits = 8;
            tp2_io_config.flags.disable_control_phase = 1;

            esp_err_t ret2 = esp_lcd_new_panel_io_i2c(i2c_bus2_, &tp2_io_config, &tp2_io_handle);
            if (ret2 == ESP_OK) {
                esp_lcd_touch_config_t tp2_cfg = {
                    .x_max = DISPLAY_WIDTH,
                    .y_max = DISPLAY_HEIGHT,
                    .rst_gpio_num = TOUCH2_RST_PIN,
                    .int_gpio_num = TOUCH2_INT_PIN,
                    .flags = {
                        .swap_xy = 0,
                        .mirror_x = 1,
                        .mirror_y = 1,
                    },
                };
                ret2 = esp_lcd_touch_new_i2c_cst816s(tp2_io_handle, &tp2_cfg, &tp2_);
                if (ret2 == ESP_OK) {
                    ESP_LOGI(TAG, "Touch 2 (Right) initialized successfully");
                } else {
                    ESP_LOGW(TAG, "Failed to initialize Touch 2: %s", esp_err_to_name(ret2));
                }
            } else {
                ESP_LOGW(TAG, "Failed to create Touch 2 IO: %s", esp_err_to_name(ret2));
            }
        } else {
            ESP_LOGW(TAG, "Failed to initialize I2C1 bus: %s", esp_err_to_name(ret_i2c2));
        }

        DisplayManager::SetTouchHandles(tp_, tp2_);
    }
    

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MISO_PIN;
        buscfg.sclk_io_num = DISPLAY_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RESET_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));    
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display_ = new SpiLcdDisplayExtended(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    true);
    }

    void InitializeLcdDisplay_2() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY2_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY2_RESET_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));    
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY2_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY2_MIRROR_X, DISPLAY2_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);

        display2_ = new SpiLcdDisplayExtended(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY2_MIRROR_X, DISPLAY2_MIRROR_Y, DISPLAY2_SWAP_XY,
                                    false);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // Double-click BOOT button to cycle between photorealistic eye styles
        boot_button_.OnDoubleClick([this]() {
            ESP_LOGI(TAG, "BOOT button double-click -> Cycling Eye Preset");
            DisplayManager::CycleEyePreset();
        });
    }

    void InitializeBacklights() {
        ESP_LOGI(TAG, "Initializing hardware backlights on GPIO %d and GPIO %d", DISPLAY_BACKLIGHT_PIN, DISPLAY2_BACKLIGHT_PIN);
        GetBacklight()->SetBrightness(100);
    }

    void InitializeMcpTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.eyes.set_preset",
            "Set the character eye preset style on one or both screens (presets: japanese, default, cat, dragon, terminator, doe, owl, goat, nauga, newt, no_sclera; target: both, left, right)",
            PropertyList({
                Property("preset", kPropertyTypeString, std::string("japanese")),
                Property("target", kPropertyTypeString, std::string("both"))
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string preset_name = "japanese";
                std::string target = "both";
                try {
                    preset_name = properties["preset"].value<std::string>();
                } catch (...) {}
                try {
                    target = properties["target"].value<std::string>();
                } catch (...) {}

                const EyeRenderer::EyeAssetConfig* asset = EyeRenderer::GetEyePresetByName(preset_name.c_str());
                if (!asset) {
                    return "Eye preset not found";
                }
                int pid = 0;
                for (int i = 0; i < EyeRenderer::PRESET_COUNT; i++) {
                    if (EyeRenderer::GetEyePreset((EyeRenderer::EyePresetId)i) == asset) {
                        pid = i;
                        break;
                    }
                }
                DisplayManager::SetEyePreset(pid, target);
                std::string msg = "Changed ";
                msg += target;
                msg += " eye(s) to ";
                msg += asset->name;
                return msg;
            }
        );

        // Persistent File / Notes Tools
        mcp_server.AddTool("self.notes.save",
            "Save a persistent text note or document into flash memory for future reference",
            PropertyList({
                Property("title", kPropertyTypeString),
                Property("content", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string title = properties["title"].value<std::string>();
                std::string content = properties["content"].value<std::string>();
                bool ok = MemoryManager::WriteNote(title, content);
                return ok ? ("Saved note: " + title) : "Failed to save note";
            }
        );

        mcp_server.AddTool("self.notes.read",
            "Read the content of a persistent text note or document from flash memory",
            PropertyList({
                Property("title", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string title = properties["title"].value<std::string>();
                std::string content = MemoryManager::ReadNote(title);
                return content.empty() ? ("Note not found: " + title) : content;
            }
        );

        mcp_server.AddTool("self.notes.list",
            "List all saved persistent notes and reference documents stored on flash",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                return MemoryManager::GetNotesSummaryJson();
            }
        );

        mcp_server.AddTool("self.notes.delete",
            "Delete a saved note or document from flash memory",
            PropertyList({
                Property("title", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string title = properties["title"].value<std::string>();
                bool ok = MemoryManager::DeleteNote(title);
                return ok ? ("Deleted note: " + title) : "Failed to delete note";
            }
        );

        // Persona & Soul & Mood System
        mcp_server.AddTool("self.persona.set_mood",
            "Set your emotional mood state (e.g. curious, happy, thoughtful, tender, playful, sleepy, focused). This dynamically reflects in your dual eye expressions and pupil dilation.",
            PropertyList({
                Property("mood", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string mood = properties["mood"].value<std::string>();
                MemoryManager::SetMood(mood);
                return "Emotional mood set to: " + mood;
            }
        );

        mcp_server.AddTool("self.persona.save_memory",
            "Store a core memory, user preference, soul trait, or personal belief into lifelong persistent memory",
            PropertyList({
                Property("key", kPropertyTypeString),
                Property("value", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string key = properties["key"].value<std::string>();
                std::string value = properties["value"].value<std::string>();
                bool ok = MemoryManager::SaveTrait(key, value);
                return ok ? ("Saved soul memory: " + key) : "Failed to save memory";
            }
        );

        mcp_server.AddTool("self.persona.recall_memories",
            "Recall all stored lifelong soul memories, persona traits, and user preferences",
            PropertyList(),
            [](const PropertyList& properties) -> ReturnValue {
                return MemoryManager::GetAllTraitsJson();
            }
        );
    }

public:
    CustomBoard() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeBacklights();
        InitializeI2c();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeLcdDisplay_2();
        InitializeTouch();
        InitializeButtons();
        InitializeMcpTools();

        // Initialize Memory & Soul System
        MemoryManager::Initialize();

        // Run System Bootstrap & Awareness Initialization
        Bootstrap::Initialize();

        // Start Direct TCP Push OTA Server on Port 3232
        OtaPushServer::StartServer(3232);

        // Auto-configure user Wi-Fi credentials
        auto& ssid_manager = SsidManager::GetInstance();
        bool found = false;
        for (const auto& item : ssid_manager.GetSsidList()) {
            if (item.ssid == "2.4GHz" && item.password == "0828973709") {
                found = true;
                break;
            }
        }
        if (!found) {
            ESP_LOGI(TAG, "Configuring default Wi-Fi network: 2.4GHz");
            ssid_manager.AddSsid("2.4GHz", "0828973709");
        }
    }

    virtual std::string GetBoardJson() override {
        auto& wifi = WifiManager::GetInstance();
        std::string json = R"({"type":")" + std::string(BOARD_TYPE) + R"(",)";
        json += R"("name":"Waveshare ESP32-S3 DualEye LCD 1.28",)";
        json += R"("eyes":{"active_preset":"japanese","presets_count":11,"renderer":"DMA Ping-Pong 50FPS"},)";
        json += R"("soul_and_memory":{"storage":"NVS Flash","notes_support":true,"soul_memory_support":true},)";
        json += R"("ota_server":{"status":"active","port":3232,"mode":"TCP Push OTA"},)";

        if (!wifi.IsConfigMode()) {
            json += R"("ssid":")" + wifi.GetSsid() + R"(",)";
            json += R"("rssi":)" + std::to_string(wifi.GetRssi()) + R"(,)";
            json += R"("channel":)" + std::to_string(wifi.GetChannel()) + R"(,)";
            json += R"("ip":")" + wifi.GetIpAddress() + R"(",)";
        }

        json += R"("mac":")" + SystemInfo::GetMacAddress() + R"("})";
        return json;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(i2c_bus_, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, AUDIO_CODEC_ES7210_ADDR, AUDIO_INPUT_REFERENCE);
            return &audio_codec;
    }
    virtual Display* GetDisplay() override {
        return &display_manager_;
    }

    virtual Backlight* GetBacklight() override {
        static DualBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY2_BACKLIGHT_PIN);
        return &backlight;
    }
};

DECLARE_BOARD(CustomBoard);

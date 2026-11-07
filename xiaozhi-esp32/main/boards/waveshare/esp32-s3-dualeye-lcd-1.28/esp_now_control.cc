// Copyright (c) 2026 Waveshare / JAPA Project
// ESP-NOW inter-board persona & graphics control implementation
#include "esp_now_control.h"
#include "display_manager.h"
#include "eye_presets.h"

#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <cstring>
#include <string>

namespace DualEyeEspNow {

static const char* TAG = "DualEyeEspNow";

static bool HandleTextCommand(const std::string& line) {
    if (line.empty()) return false;

    // Remove any trailing \r or \n if present
    std::string clean_line = line;
    while (!clean_line.empty() && (clean_line.back() == '\r' || clean_line.back() == '\n' || clean_line.back() == '\0')) {
        clean_line.pop_back();
    }

    // S31 Frozen Protocol Boundary
    if (clean_line.rfind("EXPRESSION ", 0) == 0) {
        std::string mood = clean_line.substr(11);
        if (!mood.empty() && mood[0] == ' ') {
            mood = mood.substr(1);
        }
        if (!mood.empty()) {
            DisplayManager::SetMood(mood);
        }
        return true;
    } else if (clean_line.rfind("LOOK ", 0) == 0) {
        std::string coords = clean_line.substr(5);
        size_t comma = coords.find(',');
        if (comma != std::string::npos) {
            try {
                float gx = std::stof(coords.substr(0, comma));
                float gy = std::stof(coords.substr(comma + 1));
                DisplayManager::SetManualGaze(gx, gy);
                return true;
            } catch (...) {
                return false;
            }
        }
        return false;
    } else if (clean_line == "BLINK") {
        DisplayManager::TriggerBlink();
        return true;
    } else if (clean_line == "SLEEP") {
        DisplayManager::SetSleeping(true);
        return true;
    } else if (clean_line == "WAKE") {
        DisplayManager::SetSleeping(false);
        return true;
    } else {
        return false;
    }
}

static void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len) {
    std::string payload((const char*)data, len);
    ESP_LOGI(TAG, "RX RAW: '%s'", payload.c_str());
    bool parsed = HandleTextCommand(payload);
    if (parsed) {
        ESP_LOGI(TAG, "RX PARSE: SUCCESS");
    } else {
        ESP_LOGW(TAG, "RX PARSE: UNKNOWN COMMAND");
    }
}

void Initialize() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint8_t primary;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    
    ESP_LOGI(TAG, "Initializing ESP-NOW. STA MAC: %02x:%02x:%02x:%02x:%02x:%02x, Wi-Fi Channel: %d", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], primary);
             
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    esp_now_register_recv_cb(OnDataRecv);
}

} // namespace DualEyeEspNow

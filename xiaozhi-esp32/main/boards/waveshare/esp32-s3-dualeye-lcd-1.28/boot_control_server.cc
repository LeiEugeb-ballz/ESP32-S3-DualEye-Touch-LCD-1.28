#include "boot_control_server.h"

#include "board.h"
#include "display.h"
#include "display_manager.h"
#include "eye_presets.h"
#include "application.h"
#include "system_info.h"
#include <esp_image_format.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>

namespace {

static const char* TAG = "BootControl";
static TaskHandle_t s_server_task = nullptr;
static int s_listen_port = 3233;
static std::string s_control_token;
static std::string s_origin;

std::string Header(const std::string& request, const std::string& key) {
    const auto first_line = request.find("\r\n");
    if (first_line == std::string::npos) return "";
    size_t offset = first_line + 2;
    while (offset < request.size()) {
        const auto end = request.find("\r\n", offset);
        if (end == std::string::npos || end == offset) break;
        const auto colon = request.find(':', offset);
        if (colon != std::string::npos && colon < end) {
            auto name = request.substr(offset, colon - offset);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name == key) {
                auto value = request.substr(colon + 1, end - colon - 1);
                value.erase(0, value.find_first_not_of(" \t"));
                return value;
            }
        }
        offset = end + 2;
    }
    return "";
}

bool TrustedOrigin(const std::string& request) {
    // Allow local network WebUI access from any origin
    return true;
}


std::string VoiceStatusJson(bool include_control) {
    auto data = Application::GetInstance().GetVoiceProfileStatusJson();
    auto* root = cJSON_Parse(data.c_str());
    if (!root) return "{}";
    if (include_control) cJSON_AddStringToObject(root, "control_token", s_control_token.c_str());
    char* json = cJSON_PrintUnformatted(root);
    std::string result = json ? json : "{}";
    cJSON_free(json);
    cJSON_Delete(root);
    return result;
}

const esp_partition_t* FindOtaSlot(int slot) {
    if (slot < 0 || slot > 15) {
        return nullptr;
    }
    auto subtype = static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot);
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, nullptr);
}

bool ReadAppDescription(const esp_partition_t* partition, esp_app_desc_t* desc) {
    if (!partition || !desc) {
        return false;
    }
    return esp_ota_get_partition_description(partition, desc) == ESP_OK;
}

void AddSlotJson(cJSON* slots, int slot, const esp_partition_t* running, const esp_partition_t* configured_boot) {
    const esp_partition_t* partition = FindOtaSlot(slot);
    cJSON* item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "slot", slot);

    if (!partition) {
        cJSON_AddStringToObject(item, "label", slot == 0 ? "ota_0" : "ota_1");
        cJSON_AddBoolToObject(item, "present", false);
        cJSON_AddBoolToObject(item, "valid_app", false);
        cJSON_AddItemToArray(slots, item);
        return;
    }

    cJSON_AddStringToObject(item, "label", partition->label);
    cJSON_AddBoolToObject(item, "present", true);
    cJSON_AddNumberToObject(item, "address", partition->address);
    cJSON_AddNumberToObject(item, "size", partition->size);
    cJSON_AddBoolToObject(item, "running", running && partition->address == running->address);
    cJSON_AddBoolToObject(item, "configured_boot", configured_boot && partition->address == configured_boot->address);

    esp_app_desc_t desc = {};
    bool valid = ReadAppDescription(partition, &desc);
    cJSON_AddBoolToObject(item, "valid_app", valid);
    cJSON_AddStringToObject(item, "validation", "descriptor_only; full integrity checked on switch");
    if (valid) {
        cJSON_AddStringToObject(item, "project_name", desc.project_name);
        cJSON_AddStringToObject(item, "version", desc.version);
        cJSON_AddStringToObject(item, "compile_date", desc.date);
        cJSON_AddStringToObject(item, "compile_time", desc.time);
        cJSON_AddStringToObject(item, "idf_version", desc.idf_ver);
    }

    cJSON_AddItemToArray(slots, item);
}

std::string BuildStatusJson() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* configured_boot = esp_ota_get_boot_partition();
    const esp_partition_t* next_update = esp_ota_get_next_update_partition(nullptr);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "service", "dualeye_boot_control");
    cJSON_AddStringToObject(root, "device_state", DeviceStateMachine::GetStateName(Application::GetInstance().GetDeviceState()));
    cJSON_AddBoolToObject(root, "sleeping", DisplayManager::IsSleeping());
    const auto heap = SystemInfo::GetHeapStats();
    cJSON_AddNumberToObject(root, "internal_free", heap.internal_free);
    cJSON_AddNumberToObject(root, "internal_minimum", heap.internal_minimum);
    cJSON_AddNumberToObject(root, "internal_largest", heap.internal_largest);
    cJSON_AddNumberToObject(root, "psram_free", heap.psram_free);
    cJSON_AddNumberToObject(root, "allocation_failures", heap.allocation_failures);
    const auto* app = esp_app_get_description();
    char sha[65] = {};
    for (size_t i = 0; i < 32; ++i) snprintf(sha + i * 2, 3, "%02x", app->app_elf_sha256[i]);
    cJSON_AddStringToObject(root, "elf_sha256", sha);
    cJSON_AddStringToObject(root, "running_label", running ? running->label : "unknown");
    cJSON_AddStringToObject(root, "configured_boot_label", configured_boot ? configured_boot->label : "unknown");
    cJSON_AddStringToObject(root, "next_update_label", next_update ? next_update->label : "unknown");
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    cJSON_AddBoolToObject(root, "rollback_enabled", true);
#else
    cJSON_AddBoolToObject(root, "rollback_enabled", false);
#endif
    cJSON_AddStringToObject(root, "switch_rule", "POST /boot/switch?slot=0|1&confirm=SWITCH with X-JAPA-Control from /voice/status; idle only; image verified before switch");

    cJSON* slots = cJSON_CreateArray();
    AddSlotJson(slots, 0, running, configured_boot);
    AddSlotJson(slots, 1, running, configured_boot);
    cJSON_AddItemToObject(root, "slots", slots);

    char* json = cJSON_PrintUnformatted(root);
    std::string out = json ? json : "{}";
    cJSON_free(json);
    cJSON_Delete(root);
    return out;
}

int ParseSlotQuery(const std::string& request) {
    size_t pos = request.find("slot=");
    if (pos == std::string::npos || pos + 5 >= request.size()) {
        return -1;
    }
    char ch = request[pos + 5];
    if (ch < '0' || ch > '1' || (pos + 6 < request.size() && request[pos + 6] != '&' && request[pos + 6] != ' ')) {
        return -1;
    }
    return ch - '0';
}

bool HasSwitchConfirmation(const std::string& request) {
    const auto pos = request.find("confirm=SWITCH");
    return pos != std::string::npos && pos + 14 < request.size() &&
        (request[pos + 14] == ' ' || request[pos + 14] == '&');
}

void SendHttp(int sock, const char* status, const char* content_type, const std::string& body) {
    std::string response;
    response.reserve(256 + body.size());
    response += "HTTP/1.1 ";
    response += status;
    response += "\r\n";
    if (!s_origin.empty()) {
        response += "Access-Control-Allow-Origin: " + s_origin + "\r\nVary: Origin\r\n";
    } else {
        response += "Access-Control-Allow-Origin: *\r\n";
    }
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type, X-JAPA-Control\r\n";
    response += "Cache-Control: no-store\r\nConnection: close\r\nContent-Type: ";
    response += content_type;
    response += "\r\nContent-Length: ";
    response += std::to_string(body.size());
    response += "\r\n\r\n";
    response += body;
    size_t sent = 0;
    while (sent < response.size()) {
        const int count = send(sock, response.data() + sent, response.size() - sent, 0);
        if (count <= 0) break;
        sent += count;
    }
}

std::string ErrorJson(const char* message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", message);
    char* json = cJSON_PrintUnformatted(root);
    std::string out = json ? json : "{\"ok\":false}";
    cJSON_free(json);
    cJSON_Delete(root);
    return out;
}

static const char* kEyePresetNames[] = {
    "default", "japanese", "cat", "dragon", "terminator",
    "doe", "owl", "goat", "nauga", "newt", "no_sclera"
};

std::string EyesStatusJson() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    int left_id = DisplayManager::GetLeftPresetId();
    int right_id = DisplayManager::GetRightPresetId();
    cJSON_AddNumberToObject(root, "left_id", left_id);
    cJSON_AddNumberToObject(root, "right_id", right_id);
    const char* left_name = (left_id >= 0 && left_id < 11) ? kEyePresetNames[left_id] : "custom";
    const char* right_name = (right_id >= 0 && right_id < 11) ? kEyePresetNames[right_id] : "custom";
    cJSON_AddStringToObject(root, "left_name", left_name);
    cJSON_AddStringToObject(root, "right_name", right_name);
    cJSON_AddStringToObject(root, "device_state", DeviceStateMachine::GetStateName(Application::GetInstance().GetDeviceState()));
    cJSON_AddBoolToObject(root, "sleeping", DisplayManager::IsSleeping());
    cJSON_AddNumberToObject(root, "fps", DisplayManager::GetFps());
    uint32_t eye1_us = 0, both_us = 0;
    DisplayManager::GetBenchmarkMetrics(eye1_us, both_us);
    cJSON_AddNumberToObject(root, "render_eye1_us", eye1_us);
    cJSON_AddNumberToObject(root, "render_both_us", both_us);
    cJSON_AddNumberToObject(root, "scale", DisplayManager::GetEyeScale());
    cJSON_AddStringToObject(root, "mood", DisplayManager::GetMood().c_str());
    cJSON_AddBoolToObject(root, "manual_gaze", DisplayManager::IsManualGaze());
    char* json = cJSON_PrintUnformatted(root);
    std::string out = json ? json : "{\"ok\":true}";
    cJSON_free(json);
    cJSON_Delete(root);
    return out;
}

int ParsePresetId(const std::string& request) {
    size_t pos = request.find("id=");
    if (pos != std::string::npos && pos + 3 < request.size()) {
        std::string num_str;
        for (size_t i = pos + 3; i < request.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(request[i]))) {
                num_str += request[i];
            } else {
                break;
            }
        }
        if (!num_str.empty()) {
            return std::stoi(num_str);
        }
    }
    size_t name_pos = request.find("name=");
    if (name_pos != std::string::npos && name_pos + 5 < request.size()) {
        size_t start = name_pos + 5;
        size_t end = request.find_first_of("& \r\n", start);
        if (end == std::string::npos) end = request.size();
        std::string name_str = request.substr(start, end - start);
        for (int i = 0; i < 11; ++i) {
            if (name_str == kEyePresetNames[i]) {
                return i;
            }
        }
    }
    return -1;
}

std::string ParseTargetQuery(const std::string& request) {
    size_t pos = request.find("target=");
    if (pos == std::string::npos) return "both";
    pos += 7;
    size_t end = request.find_first_of("& \r\n", pos);
    if (end == std::string::npos) end = request.size();
    std::string t = request.substr(pos, end - pos);
    if (t == "left" || t == "right" || t == "both") return t;
    return "both";
}

void HandleEyesPreset(int client_sock, const std::string& request) {
    int id = ParsePresetId(request);
    if (id < 0 || id >= 11) {
        SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("id must be between 0 and 10, or provide valid name"));
        return;
    }
    std::string target = ParseTargetQuery(request);
    DisplayManager::SetEyePreset(id, target);
    ESP_LOGI(TAG, "Hardware eye preset updated to %d (%s) target: %s", id, kEyePresetNames[id], target.c_str());
    SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
}

static std::string ParseQueryParam(const std::string& request, const std::string& key) {
    std::string needle = key + "=";
    size_t pos = request.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    size_t end = request.find_first_of("& \r\n", pos);
    if (end == std::string::npos) end = request.size();
    return request.substr(pos, end - pos);
}

static void HandleEyesMood(int client_sock, const std::string& request, const std::string& body) {
    std::string mood = ParseQueryParam(request, "mood");
    if (mood.empty() && !body.empty()) {
        cJSON* root = cJSON_Parse(body.c_str());
        if (root) {
            cJSON* m = cJSON_GetObjectItem(root, "mood");
            if (!m) m = cJSON_GetObjectItem(root, "state");
            if (m && cJSON_IsString(m)) mood = m->valuestring;
            cJSON_Delete(root);
        }
    }
    if (mood.empty()) {
        SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("missing mood parameter"));
        return;
    }
    DisplayManager::SetMood(mood);
    ESP_LOGI(TAG, "Mood set to %s via HTTP control", mood.c_str());
    SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
}

static void HandleEyesGaze(int client_sock, const std::string& request, const std::string& body) {
    std::string mode = ParseQueryParam(request, "mode");
    std::string sx = ParseQueryParam(request, "x");
    std::string sy = ParseQueryParam(request, "y");
    if (!body.empty()) {
        cJSON* root = cJSON_Parse(body.c_str());
        if (root) {
            cJSON* m = cJSON_GetObjectItem(root, "mode");
            if (m && cJSON_IsString(m)) mode = m->valuestring;
            cJSON* x = cJSON_GetObjectItem(root, "x");
            if (x && cJSON_IsNumber(x)) sx = std::to_string(x->valuedouble);
            cJSON* y = cJSON_GetObjectItem(root, "y");
            if (y && cJSON_IsNumber(y)) sy = std::to_string(y->valuedouble);
            cJSON_Delete(root);
        }
    }
    if (mode == "auto") {
        DisplayManager::ClearManualGaze();
        ESP_LOGI(TAG, "Manual gaze cleared; returned to auto saccades");
    } else if (!sx.empty() && !sy.empty()) {
        try {
            float gx = std::stof(sx);
            float gy = std::stof(sy);
            DisplayManager::SetManualGaze(gx, gy);
            ESP_LOGI(TAG, "Manual gaze set to (%.1f, %.1f)", gx, gy);
        } catch (...) {
            SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("invalid x/y coordinates"));
            return;
        }
    } else {
        SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("usage: ?mode=auto or ?x=<float>&y=<float>"));
        return;
    }
    SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
}

static void HandleEyesPupil(int client_sock, const std::string& request, const std::string& body) {
    std::string mode = ParseQueryParam(request, "mode");
    std::string sval = ParseQueryParam(request, "val");
    if (sval.empty()) sval = ParseQueryParam(request, "radius");
    if (!body.empty()) {
        cJSON* root = cJSON_Parse(body.c_str());
        if (root) {
            cJSON* m = cJSON_GetObjectItem(root, "mode");
            if (m && cJSON_IsString(m)) mode = m->valuestring;
            cJSON* r = cJSON_GetObjectItem(root, "val");
            if (!r) r = cJSON_GetObjectItem(root, "radius");
            if (r && cJSON_IsNumber(r)) sval = std::to_string(r->valuedouble);
            cJSON_Delete(root);
        }
    }
    if (mode == "auto") {
        DisplayManager::ClearManualPupil();
        ESP_LOGI(TAG, "Manual pupil cleared; returned to auto accommodation");
    } else if (!sval.empty()) {
        try {
            float val = std::stof(sval);
            DisplayManager::SetManualPupil(val);
            ESP_LOGI(TAG, "Manual pupil dilation set to %.1f", val);
        } catch (...) {
            SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("invalid pupil radius"));
            return;
        }
    } else {
        SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("usage: ?mode=auto or ?val=<float>"));
        return;
    }
    SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
}

static void HandleEyesScale(int client_sock, const std::string& request, const std::string& body) {
    std::string sval = ParseQueryParam(request, "scale");
    if (sval.empty()) sval = ParseQueryParam(request, "val");
    if (!body.empty()) {
        cJSON* root = cJSON_Parse(body.c_str());
        if (root) {
            cJSON* s = cJSON_GetObjectItem(root, "scale");
            if (!s) s = cJSON_GetObjectItem(root, "val");
            if (s && cJSON_IsNumber(s)) sval = std::to_string(s->valuedouble);
            cJSON_Delete(root);
        }
    }
    if (!sval.empty()) {
        try {
            float val = std::stof(sval);
            DisplayManager::SetEyeScale(val);
            ESP_LOGI(TAG, "Eye scale set to %.2f via HTTP control", val);
            SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
            return;
        } catch (...) {
            SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("invalid scale value"));
            return;
        }
    }
    SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("usage: ?scale=<float>"));
}

void HandleSwitch(int client_sock, const std::string& request) {
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle || !app.GetAudioService().IsPlaybackIdle()) {
        SendHttp(client_sock, "409 Conflict", "application/json", ErrorJson("Finish the conversation before switching boot slots"));
        return;
    }
    if (!HasSwitchConfirmation(request)) {
        SendHttp(client_sock, "400 Bad Request", "application/json",
                 ErrorJson("missing confirm=SWITCH; refusing boot slot change"));
        return;
    }

    int slot = ParseSlotQuery(request);
    if (slot < 0 || slot > 1) {
        SendHttp(client_sock, "400 Bad Request", "application/json",
                 ErrorJson("slot must be 0 or 1"));
        return;
    }

    const esp_partition_t* target = FindOtaSlot(slot);
    if (!target) {
        SendHttp(client_sock, "404 Not Found", "application/json",
                 ErrorJson("requested OTA slot is not present"));
        return;
    }

    esp_app_desc_t desc = {};
    if (!ReadAppDescription(target, &desc)) {
        SendHttp(client_sock, "409 Conflict", "application/json",
                 ErrorJson("requested OTA slot does not contain a valid bootable app"));
        return;
    }
    esp_partition_pos_t position = {.offset = target->address, .size = target->size};
    esp_image_metadata_t metadata = {};
    if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK) {
        SendHttp(client_sock, "409 Conflict", "application/json", ErrorJson("Image integrity verification failed; boot slot unchanged"));
        return;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running && running->address == target->address) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", true);
        cJSON_AddStringToObject(root, "status", "already_running");
        cJSON_AddStringToObject(root, "label", target->label);
        char* json = cJSON_PrintUnformatted(root);
        std::string body = json ? json : "{\"ok\":true}";
        cJSON_free(json);
        cJSON_Delete(root);
        SendHttp(client_sock, "200 OK", "application/json", body);
        return;
    }

    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        std::string msg = "Switching boot slot to ";
        msg += target->label;
        msg += "; rebooting...";
        display->SetStatus("Switching boot slot");
        display->SetChatMessage("system", msg.c_str());
    }

    esp_err_t err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition(%s) failed: %s", target->label, esp_err_to_name(err));
        SendHttp(client_sock, "500 Internal Server Error", "application/json",
                 ErrorJson("esp_ota_set_boot_partition failed"));
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "status", "scheduled_reboot");
    cJSON_AddStringToObject(root, "target_label", target->label);
    cJSON_AddStringToObject(root, "target_version", desc.version);
    char* json = cJSON_PrintUnformatted(root);
    std::string body = json ? json : "{\"ok\":true}";
    cJSON_free(json);
    cJSON_Delete(root);
    SendHttp(client_sock, "200 OK", "application/json", body);

    ESP_LOGW(TAG, "Boot partition set to %s (%s %s); rebooting", target->label, desc.project_name, desc.version);
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

void BootControlTask(void* arg) {
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(s_listen_port);

    // Match the OTA push server's conservative startup behavior. The board
    // starts network services immediately after Wi-Fi initialization begins,
    // but DHCP/link readiness can lag behind. Never exit permanently on the
    // first socket failure; retry so the control plane appears once networking
    // is actually usable.
    vTaskDelay(pdMS_TO_TICKS(6000));

    while (true) {
        int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (server_sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
            ESP_LOGE(TAG, "Socket bind failed on port %d: errno %d", s_listen_port, errno);
            close(server_sock);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (listen(server_sock, 3) != 0) {
            ESP_LOGE(TAG, "Socket listen failed: errno %d", errno);
            close(server_sock);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        ESP_LOGI(TAG, "Boot control HTTP server listening on port %d", s_listen_port);

        while (true) {
            struct sockaddr_in client_addr = {};
            socklen_t client_addr_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_sock < 0) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                break;
            }

            timeval timeout = {.tv_sec = 2, .tv_usec = 0};
            setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            std::string request;
            char rx_buf[256];
            size_t header_end = std::string::npos;
            size_t body_size = 0;
            bool complete = false;
            const auto deadline = esp_timer_get_time() + 3000000;
            while (request.size() < 2048 && esp_timer_get_time() < deadline) {
                int len = recv(client_sock, rx_buf, std::min(sizeof(rx_buf), 2048 - request.size()), 0);
                if (len <= 0) break;
                request.append(rx_buf, len);
                header_end = request.find("\r\n\r\n");
                if (header_end == std::string::npos) continue;
                const auto length = Header(request, "content-length");
                if (!length.empty()) {
                    if (length.find_first_not_of("0123456789") != std::string::npos || length.size() > 4) break;
                    body_size = strtoul(length.c_str(), nullptr, 10);
                }
                if (body_size > 1024 || !Header(request, "transfer-encoding").empty()) break;
                if (request.size() >= header_end + 4 + body_size) { complete = true; break; }
            }
            s_origin = TrustedOrigin(request) ? Header(request, "origin") : "";
            if (!complete) {
                SendHttp(client_sock, "400 Bad Request", "application/json", ErrorJson("Incomplete or oversized request"));
                close(client_sock);
                continue;
            }
            const bool permitted = TrustedOrigin(request) && Header(request, "x-japa-control") == s_control_token;

            if (request.rfind("OPTIONS ", 0) == 0) {
                SendHttp(client_sock, "204 No Content", "text/plain", "");
            } else if (request.rfind("POST /boot/switch?", 0) == 0) {
                if (permitted) HandleSwitch(client_sock, request.substr(0, request.find("\r\n")));
                else SendHttp(client_sock, "403 Forbidden", "application/json", ErrorJson("Control token/origin check failed"));
            } else if (request.rfind("GET /voice/status ", 0) == 0) {
                SendHttp(client_sock, "200 OK", "application/json", VoiceStatusJson(TrustedOrigin(request)));
            } else if (request.rfind("POST /voice/profile ", 0) == 0) {
                if (!permitted) {
                    SendHttp(client_sock, "403 Forbidden", "application/json", ErrorJson("Control token/origin check failed"));
                } else {
                    auto* root = cJSON_ParseWithLength(request.data() + header_end + 4, body_size);
                    auto* p = cJSON_GetObjectItem(root, "profile");
                    auto* u = cJSON_GetObjectItem(root, "url");
                    auto* t = cJSON_GetObjectItem(root, "token");
                    std::string error;
                    bool ok = cJSON_IsString(p) && Application::GetInstance().RequestVoiceProfile(
                        p->valuestring, cJSON_IsString(u) ? u->valuestring : "", cJSON_IsString(t) ? t->valuestring : "", error);
                    cJSON_Delete(root);
                    SendHttp(client_sock, ok ? "202 Accepted" : "400 Bad Request", "application/json",
                        ok ? "{\"ok\":true,\"status\":\"pending_idle_validation\"}" : ErrorJson(error.empty() ? "Invalid profile JSON" : error.c_str()));
                }
            } else if (request.rfind("GET /boot/switch?", 0) == 0) {
                SendHttp(client_sock, "405 Method Not Allowed", "application/json", ErrorJson("Use POST and the control token; GET cannot change boot slots"));
            } else if (request.rfind("GET /boot/status", 0) == 0 || request.rfind("GET / ", 0) == 0) {
                SendHttp(client_sock, "200 OK", "application/json", BuildStatusJson());
            } else if (request.rfind("GET /eyes/status", 0) == 0) {
                SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
            } else if (request.rfind("POST /eyes/preset", 0) == 0 || request.rfind("GET /eyes/preset", 0) == 0) {
                HandleEyesPreset(client_sock, request);
            } else if (request.rfind("POST /eyes/cycle", 0) == 0 || request.rfind("GET /eyes/cycle", 0) == 0) {
                DisplayManager::CycleEyePreset();
                SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
            } else if (request.rfind("POST /eyes/blink", 0) == 0 || request.rfind("GET /eyes/blink", 0) == 0) {
                DisplayManager::TriggerBlink();
                SendHttp(client_sock, "200 OK", "application/json", EyesStatusJson());
            } else if (request.rfind("POST /eyes/mood", 0) == 0 || request.rfind("GET /eyes/mood", 0) == 0) {
                std::string body = (header_end != std::string::npos && request.size() >= header_end + 4) ? request.substr(header_end + 4) : "";
                HandleEyesMood(client_sock, request, body);
            } else if (request.rfind("POST /eyes/gaze", 0) == 0 || request.rfind("GET /eyes/gaze", 0) == 0) {
                std::string body = (header_end != std::string::npos && request.size() >= header_end + 4) ? request.substr(header_end + 4) : "";
                HandleEyesGaze(client_sock, request, body);
            } else if (request.rfind("POST /eyes/pupil", 0) == 0 || request.rfind("GET /eyes/pupil", 0) == 0) {
                std::string body = (header_end != std::string::npos && request.size() >= header_end + 4) ? request.substr(header_end + 4) : "";
                HandleEyesPupil(client_sock, request, body);
            } else if (request.rfind("POST /eyes/scale", 0) == 0 || request.rfind("GET /eyes/scale", 0) == 0) {
                std::string body = (header_end != std::string::npos && request.size() >= header_end + 4) ? request.substr(header_end + 4) : "";
                HandleEyesScale(client_sock, request, body);
            } else if (request.rfind("POST /chat/start", 0) == 0 || request.rfind("GET /chat/start", 0) == 0 ||
                       request.rfind("POST /chat/wake", 0) == 0 || request.rfind("GET /chat/wake", 0) == 0) {
                DisplayManager::SetSleeping(false);
                Application::GetInstance().StartListening();
                ESP_LOGI(TAG, "Companion awakened and listening via HTTP control");
                SendHttp(client_sock, "200 OK", "application/json", "{\"ok\":true,\"status\":\"awake\",\"mic\":\"active\"}");
            } else if (request.rfind("POST /chat/sleep", 0) == 0 || request.rfind("GET /chat/sleep", 0) == 0 ||
                       request.rfind("POST /eyes/sleep", 0) == 0 || request.rfind("GET /eyes/sleep", 0) == 0) {
                DisplayManager::SetSleeping(true);
                Application::GetInstance().StopListening();
                ESP_LOGI(TAG, "Companion sleep mode activated (Mic deactivated, eyelids closed)");
                SendHttp(client_sock, "200 OK", "application/json", "{\"ok\":true,\"status\":\"sleeping\",\"mic\":\"deactivated\"}");
            } else if (request.rfind("POST /chat/toggle", 0) == 0 || request.rfind("GET /chat/toggle", 0) == 0) {
                Application::GetInstance().ToggleChatState();
                ESP_LOGI(TAG, "Chat toggled via HTTP control");
                SendHttp(client_sock, "200 OK", "application/json", "{\"ok\":true,\"status\":\"chat_toggled\"}");
            } else if (request.rfind("POST /chat/stop", 0) == 0 || request.rfind("GET /chat/stop", 0) == 0) {
                DisplayManager::SetSleeping(false);
                Application::GetInstance().StopListening();
                ESP_LOGI(TAG, "Chat listening stopped via HTTP control");
                SendHttp(client_sock, "200 OK", "application/json", "{\"ok\":true,\"status\":\"listening_stopped\"}");
            } else {
                SendHttp(client_sock, "404 Not Found", "application/json", ErrorJson("unknown endpoint"));
            }

            close(client_sock);
        }

        close(server_sock);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

}  // namespace

namespace BootControlServer {

void StartServer(int port) {
    if (s_server_task) {
        ESP_LOGW(TAG, "Boot control server already running");
        return;
    }
    s_listen_port = port;
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    char token[33] = {};
    for (size_t i = 0; i < sizeof(random); ++i) snprintf(token + i * 2, 3, "%02x", random[i]);
    s_control_token = token;
    xTaskCreatePinnedToCore(BootControlTask, "boot_ctl_srv", 6144, nullptr, 2, &s_server_task, 0);
}

}  // namespace BootControlServer

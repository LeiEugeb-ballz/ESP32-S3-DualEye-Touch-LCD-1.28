#pragma once
#include <string>
#include <cJSON.h>
#include <nvs.h>

// One atomic NVS blob; cloud mqtt/websocket namespaces are never overwritten.
struct VoiceProfile {
    std::string profile = "xiaozhi";
    std::string url;
    std::string token;

    bool Valid() const {
        if (profile == "xiaozhi") return url.empty() && token.empty();
        if (profile != "gateway" || url.size() > 256 || token.size() < 16 || token.size() > 256) return false;
        if (url.rfind("ws://", 0) != 0 && url.rfind("wss://", 0) != 0) return false;
        const auto host = url.find("://") + 3;
        const auto path = url.find('/', host);
        return path != std::string::npos && path > host && url.substr(path) == "/xiaozhi/v1" &&
            url.find_first_of("\r\n\t @#?") == std::string::npos &&
            token.find_first_of("\r\n\t ") == std::string::npos;
    }

    static VoiceProfile Load() {
        VoiceProfile result;
        nvs_handle_t handle = 0;
        if (nvs_open("japa_voice", NVS_READONLY, &handle) != ESP_OK) return result;
        size_t length = 0;
        if (nvs_get_str(handle, "config", nullptr, &length) == ESP_OK && length <= 1024) {
            std::string value(length, '\0');
            if (nvs_get_str(handle, "config", value.data(), &length) == ESP_OK) {
                auto* root = cJSON_Parse(value.c_str());
                auto* p = cJSON_GetObjectItem(root, "profile");
                auto* u = cJSON_GetObjectItem(root, "url");
                auto* t = cJSON_GetObjectItem(root, "token");
                if (cJSON_IsString(p)) result.profile = p->valuestring;
                if (cJSON_IsString(u)) result.url = u->valuestring;
                if (cJSON_IsString(t)) result.token = t->valuestring;
                cJSON_Delete(root);
            }
        }
        nvs_close(handle);
        return result.Valid() ? result : VoiceProfile{};
    }

    bool Save() const {
        if (!Valid()) return false;
        auto* root = cJSON_CreateObject();
        if (!root) return false;
        cJSON_AddStringToObject(root, "profile", profile.c_str());
        cJSON_AddStringToObject(root, "url", url.c_str());
        cJSON_AddStringToObject(root, "token", token.c_str());
        char* json = cJSON_PrintUnformatted(root);
        nvs_handle_t handle = 0;
        bool ok = false;
        if (json && nvs_open("japa_voice", NVS_READWRITE, &handle) == ESP_OK) {
            ok = nvs_set_str(handle, "config", json) == ESP_OK && nvs_commit(handle) == ESP_OK;
            nvs_close(handle);
        }
        cJSON_free(json);
        cJSON_Delete(root);
        return ok;
    }
};

#include "boot_control_server.h"

#include "board.h"
#include "display.h"

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

namespace {

static const char* TAG = "BootControl";
static TaskHandle_t s_server_task = nullptr;
static int s_listen_port = 3233;

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
    cJSON_AddStringToObject(root, "running_label", running ? running->label : "unknown");
    cJSON_AddStringToObject(root, "configured_boot_label", configured_boot ? configured_boot->label : "unknown");
    cJSON_AddStringToObject(root, "next_update_label", next_update ? next_update->label : "unknown");
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    cJSON_AddBoolToObject(root, "rollback_enabled", true);
#else
    cJSON_AddBoolToObject(root, "rollback_enabled", false);
#endif
    cJSON_AddStringToObject(root, "switch_rule", "GET /boot/switch?slot=0|1&confirm=SWITCH; invalid app partitions are refused");

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
    if (ch < '0' || ch > '9') {
        return -1;
    }
    return ch - '0';
}

bool HasSwitchConfirmation(const std::string& request) {
    return request.find("confirm=SWITCH") != std::string::npos;
}

void SendHttp(int sock, const char* status, const char* content_type, const std::string& body) {
    std::string response;
    response.reserve(256 + body.size());
    response += "HTTP/1.1 ";
    response += status;
    response += "\r\nAccess-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Cache-Control: no-store\r\nConnection: close\r\nContent-Type: ";
    response += content_type;
    response += "\r\nContent-Length: ";
    response += std::to_string(body.size());
    response += "\r\n\r\n";
    response += body;
    send(sock, response.data(), response.size(), 0);
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

void HandleSwitch(int client_sock, const std::string& request) {
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
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        s_server_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(s_listen_port);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        ESP_LOGE(TAG, "Socket bind failed on port %d: errno %d", s_listen_port, errno);
        close(server_sock);
        s_server_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (listen(server_sock, 3) != 0) {
        ESP_LOGE(TAG, "Socket listen failed: errno %d", errno);
        close(server_sock);
        s_server_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "Boot control HTTP server listening on port %d", s_listen_port);

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t client_addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            continue;
        }

        char rx_buf[512] = {};
        int len = recv(client_sock, rx_buf, sizeof(rx_buf) - 1, 0);
        if (len <= 0) {
            close(client_sock);
            continue;
        }
        rx_buf[len] = '\0';
        std::string request(rx_buf);

        if (request.rfind("OPTIONS ", 0) == 0) {
            SendHttp(client_sock, "204 No Content", "text/plain", "");
        } else if (request.rfind("GET /boot/switch?", 0) == 0) {
            HandleSwitch(client_sock, request);
        } else if (request.rfind("GET /boot/status", 0) == 0 || request.rfind("GET / ", 0) == 0) {
            SendHttp(client_sock, "200 OK", "application/json", BuildStatusJson());
        } else {
            SendHttp(client_sock, "404 Not Found", "application/json", ErrorJson("unknown endpoint"));
        }

        close(client_sock);
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
    xTaskCreatePinnedToCore(BootControlTask, "boot_ctl_srv", 6144, nullptr, 2, &s_server_task, 0);
}

}  // namespace BootControlServer

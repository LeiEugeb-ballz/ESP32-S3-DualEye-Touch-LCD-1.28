// Copyright (c) 2026 Waveshare / Xiaozhi Project
// Direct TCP Push OTA Server Implementation
#include "ota_server.h"
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <cstring>
#include <unistd.h>

static const char* TAG = "OtaPushServer";
static TaskHandle_t s_server_task = nullptr;
static int s_listen_port = 3232;

namespace OtaPushServer {

static void OtaTask(void* pvParameters) {
    // Wait for network & TCP/IP stack initialization
    vTaskDelay(pdMS_TO_TICKS(6000));

    int server_sock = -1;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(s_listen_port);

    while (true) {
        server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (server_sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
            ESP_LOGE(TAG, "Socket unable to bind on port %d: errno %d", s_listen_port, errno);
            close(server_sock);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        if (listen(server_sock, 1) != 0) {
            ESP_LOGE(TAG, "Error during listen: errno %d", errno);
            close(server_sock);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        ESP_LOGI(TAG, ">>> TCP Push OTA Server listening on port %d <<<", s_listen_port);

        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_addr_len);
            if (client_sock < 0) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                break;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntoa_r(client_addr.sin_addr, client_ip, sizeof(client_ip));
            ESP_LOGI(TAG, "[+] Incoming Push OTA connection from %s:%d", client_ip, ntohs(client_addr.sin_port));

            const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
            if (!update_partition) {
                ESP_LOGE(TAG, "Failed to find valid OTA update partition");
                const char* err_msg = "ERR: No OTA partition\n";
                send(client_sock, err_msg, strlen(err_msg), 0);
                close(client_sock);
                continue;
            }

            ESP_LOGI(TAG, "Writing OTA to partition '%s' at offset 0x%lx (size: 0x%lx)",
                     update_partition->label, update_partition->address, update_partition->size);

            esp_ota_handle_t ota_handle = 0;
            esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                const char* err_msg = "ERR: esp_ota_begin failed\n";
                send(client_sock, err_msg, strlen(err_msg), 0);
                close(client_sock);
                continue;
            }

            char* rx_buf = (char*)malloc(4096);
            if (!rx_buf) {
                ESP_LOGE(TAG, "Failed to allocate 4KB OTA rx buffer");
                esp_ota_abort(ota_handle);
                close(client_sock);
                continue;
            }

            size_t total_received = 0;
            bool success = true;

            while (true) {
                int len = recv(client_sock, rx_buf, 4096, 0);
                if (len < 0) {
                    ESP_LOGE(TAG, "Socket receive error: errno %d", errno);
                    success = false;
                    break;
                } else if (len == 0) {
                    // Client finished sending
                    break;
                }

                err = esp_ota_write(ota_handle, (const void*)rx_buf, len);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed at %u bytes: %s", (unsigned int)total_received, esp_err_to_name(err));
                    success = false;
                    break;
                }

                total_received += len;
                if ((total_received % (128 * 1024)) < 4096) {
                    ESP_LOGI(TAG, "OTA Progress: %u KB received...", (unsigned int)(total_received / 1024));
                }
            }

            free(rx_buf);

            if (success && total_received > 65536) {
                err = esp_ota_end(ota_handle);
                if (err == ESP_OK) {
                    err = esp_ota_set_boot_partition(update_partition);
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG, ">>> OTA UPGRADE SUCCESS! %u bytes written. Rebooting in 1s... <<<", (unsigned int)total_received);
                        const char* ok_msg = "OK: Upgrade Successful. Rebooting...\n";
                        send(client_sock, ok_msg, strlen(ok_msg), 0);
                        close(client_sock);
                        close(server_sock);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        esp_restart();
                        return;
                    } else {
                        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
                    }
                } else {
                    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
                }
            } else {
                esp_ota_abort(ota_handle);
                ESP_LOGE(TAG, "OTA aborted, total received: %u bytes", (unsigned int)total_received);
            }

            const char* fail_msg = "ERR: OTA flash write or verification failed\n";
            send(client_sock, fail_msg, strlen(fail_msg), 0);
            close(client_sock);
        }

        if (server_sock >= 0) {
            close(server_sock);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void StartServer(int port) {
    if (s_server_task != nullptr) {
        return;
    }
    s_listen_port = port;
    xTaskCreatePinnedToCore(OtaTask, "ota_push_srv", 8192, nullptr, 3, &s_server_task, 0);
}

void StopServer() {
    if (s_server_task != nullptr) {
        vTaskDelete(s_server_task);
        s_server_task = nullptr;
    }
}

} // namespace OtaPushServer

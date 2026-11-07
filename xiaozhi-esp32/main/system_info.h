#ifndef _SYSTEM_INFO_H_
#define _SYSTEM_INFO_H_

#include <string>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

class SystemInfo {
public:
    struct HeapStats {
        size_t internal_free;
        size_t internal_minimum;
        size_t internal_largest;
        size_t psram_free;
        size_t psram_minimum;
        size_t psram_largest;
        uint32_t allocation_failures;
        uint32_t last_failed_size;
        uint32_t last_failed_caps;
    };

    static void InitializeHeapDiagnostics();
    static HeapStats GetHeapStats();
    static size_t GetFlashSize();
    static size_t GetMinimumFreeHeapSize();
    static size_t GetFreeHeapSize();
    static std::string GetMacAddress();
    static std::string GetChipModelName();
    static std::string GetUserAgent();
    static esp_err_t PrintTaskCpuUsage(TickType_t xTicksToWait);
    static void PrintTaskList();
    static void PrintHeapStats();
    static void PrintPmLocks();
};

#endif // _SYSTEM_INFO_H_

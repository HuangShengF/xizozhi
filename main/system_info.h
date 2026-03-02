#ifndef _SYSTEM_INFO_H_
#define _SYSTEM_INFO_H_

#include <string>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

class SystemInfo {
public:
    static size_t GetFlashSize();
    static size_t GetMinimumFreeHeapSize();
    static size_t GetFreeHeapSize();
    static std::string GetMacAddress();           // 格式: b8:f8:62:f4:6a:54 (带冒号，小写)
    static std::string GetMacAddressRaw();        // 格式: B8F862F46A54 (无冒号，大写，用于UI显示)
    static std::string GetMacAddressLast4();      // 格式: 6A54 (仅后4位，大写，用于配网提示)
    static std::string GetChipModelName();
    static std::string GetUserAgent();
    static esp_err_t PrintTaskCpuUsage(TickType_t xTicksToWait);
    static void PrintTaskList();
    static void PrintHeapStats();
    // 新增：芯片温度监控
    static esp_err_t GetChipTemperature(float& temperature);
    // 新增：CPU 频率查询（支持双核）
    static uint32_t GetCpuFrequencyMHz(int core = 0);
};

#endif // _SYSTEM_INFO_H_

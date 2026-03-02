#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>
#include <time.h>

#include "application.h"
#include "system_info.h"

#define TAG "main"

// 解析编译时间并设置为系统默认时间
static void SetCompileTimeAsDefault() {
    struct tm compile_time = {};

    // 解析编译日期 (__DATE__ 格式: "Nov 22 2025")
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month_str[4];
    int day, year;
    sscanf(__DATE__, "%s %d %d", month_str, &day, &year);

    // 查找月份索引
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_str, months[i]) == 0) {
            compile_time.tm_mon = i;
            break;
        }
    }
    compile_time.tm_mday = day;
    compile_time.tm_year = year - 1900;

    // 解析编译时间 (__TIME__ 格式: "08:51:26")
    int hour, min, sec;
    sscanf(__TIME__, "%d:%d:%d", &hour, &min, &sec);
    compile_time.tm_hour = hour;
    compile_time.tm_min = min;
    compile_time.tm_sec = sec;

    // 转换为时间戳并设置系统时间
    time_t compile_timestamp = mktime(&compile_time);
    struct timeval tv = {
        .tv_sec = compile_timestamp,
        .tv_usec = 0
    };
    settimeofday(&tv, nullptr);

    ESP_LOGI(TAG, "系统默认时间已设置为编译时间: %s %s", __DATE__, __TIME__);
}

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 设置编译时间作为系统默认时间 (避免显示 1970-01-01)
    SetCompileTimeAsDefault();

    // Launch the application
    auto& app = Application::GetInstance();
    app.Start();
}

#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "websocket_joyai_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#include <esp_adc/adc_oneshot.h> //add YZT

#include <esp_adc/adc_cali.h> // hsf
#include <esp_adc/adc_cali_scheme.h>  // hsf
#include <driver/adc.h> //hsf
#include <esp_err.h>

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include "settings.h"
#include "boards/mydazy-p30/power_manager.h" //hsf ，使用ADC1


extern "C" {
#include "nfc.h"
#include "ws1850_iic.h"
}

#define TAG "Application"

static volatile bool g_status_reporting = false;


// hsf add
// GPIO10用来开关喇叭
#define AUDIO_CODEC_PA_PIN      GPIO_NUM_10

#define CC_ADC_PIN              GPIO_NUM_6 //CC_ADC hsf 
#define USB_DET_GPIO            GPIO_NUM_7
#define USB_MIC_ADC_GPIO        GPIO_NUM_9
#define USB_SW_GPIO             GPIO_NUM_46
#define CC_VDD_GPIO             GPIO_NUM_40
#define MIC_SELECT_GPIO         GPIO_NUM_21

#define USB_MIC_ADC_UNIT        ADC_UNIT_1
#define USB_MIC_ADC_CHANNEL     ADC_CHANNEL_8
#define CC_ADC_UNIT             ADC_UNIT_1
#define CC_ADC_CHANNEL          ADC_CHANNEL_5

#define USB_MIC_ADC_HIGH_MV     1300
#define USB_MIC_ADC_LOW_MV      500
#define CC_ADC_HEADSET_MV       100

// 耳机插入检测状态定义
// typedef enum {
//     HEADSET_NOT_PRESENT = 0,  // 无耳机插入
//     HEADSET_NORMAL_INSERT,    // 正插耳机
//     HEADSET_REVERSE_INSERT    // 反插耳机
// } headset_insert_type_t;

// hsf add  code 当前到static StaticTask_t g_status_task_buffer;之间的代码
static adc_oneshot_unit_handle_t usb_mic_adc_handle = nullptr;
static adc_oneshot_unit_handle_t cc_adc_handle = nullptr;
static adc_cali_handle_t usb_mic_cali_handle = nullptr;
static adc_cali_handle_t cc_adc_cali_handle = nullptr;
static bool usb_mic_adc_calibrated = false;
static bool cc_adc_calibrated = false;
bool headset_present = false;  // 移除static关键字，以便在audio_service.cc中访问
static int mic_select_level = 0;
// static headset_insert_type_t headset_insert_type = HEADSET_NOT_PRESENT;  // 记录耳机插入类型


static bool AdcCalibrationInit(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t* out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    return calibrated;
}

static bool ReadAdcMv(adc_oneshot_unit_handle_t handle, adc_cali_handle_t cali_handle, bool calibrated, adc_channel_t channel, int& out_mv) {
    int raw = 0;
    esp_err_t err = adc_oneshot_read(handle, channel, &raw);
    if (err != ESP_OK) {
        printf( "ADC read failed: channel=%d err=%s", channel, esp_err_to_name(err));
        return false;
    }

    if (calibrated && cali_handle != nullptr) {
        if (adc_cali_raw_to_voltage(cali_handle, raw, &out_mv) == ESP_OK) {
            return true;
        }
    }
    out_mv = raw * 3300 / 4095;
    // printf("ADC raw: %d, mV: %d\n", raw, out_mv);
    return true;
}

// 检测耳机正反插的函数
// static headset_insert_type_t DetectHeadsetOrientation() {
//     int mic_mv = 0;
    
//     // 先尝试一种MIC_SELECT电平读取ADC值
//     gpio_set_level(MIC_SELECT_GPIO, 0);
//     vTaskDelay(pdMS_TO_TICKS(10));  // 延迟等待稳定
//     ReadAdcMv(usb_mic_adc_handle, usb_mic_cali_handle, usb_mic_adc_calibrated, USB_MIC_ADC_CHANNEL, mic_mv);
    
//     int mic_mv_low = mic_mv;
    
//     // 切换MIC_SELECT电平再次读取ADC值
//     gpio_set_level(MIC_SELECT_GPIO, 1);
//     vTaskDelay(pdMS_TO_TICKS(10));  // 延迟等待稳定
//     ReadAdcMv(usb_mic_adc_handle, usb_mic_cali_handle, usb_mic_adc_calibrated, USB_MIC_ADC_CHANNEL, mic_mv);
    
//     int mic_mv_high = mic_mv;
    
//     // 根据两次读取的电压差异判断正反插
//     // 正常情况下，正插和反插的电压响应会有不同
//     if (abs(mic_mv_high - mic_mv_low) > 200) {  // 电压差大于200mV认为有明显区别
//         if (mic_mv_high > mic_mv_low) {
//             // 第二次读数更高，说明MIC_SELECT=1时更符合某种插入方式
//             return HEADSET_REVERSE_INSERT;  // 假设这是反插
//         } else {
//             // 第一次读数更高，说明MIC_SELECT=0时更符合某种插入方式
//             return HEADSET_NORMAL_INSERT;   // 假设这是正插
//         }
//     } else {
//         // 电压变化不大，可能是充电器或其他设备
//         return HEADSET_NOT_PRESENT;
//     }
// }

static void InitUsbAudioDetect() {

    gpio_reset_pin(USB_MIC_ADC_GPIO);
    gpio_reset_pin(CC_VDD_GPIO);
    gpio_reset_pin(USB_SW_GPIO);
    gpio_reset_pin(MIC_SELECT_GPIO);
    gpio_reset_pin(AUDIO_CODEC_PA_PIN);

    // 在初始化时为CC_ADC_PIN配置为ADC模式
    // adc1_config_width(ADC_WIDTH_BIT_12);
    // adc1_config_channel_atten(CC_ADC_CHANNEL, ADC_ATTEN_DB_12);

    gpio_config_t input_conf = {};
    input_conf.intr_type = GPIO_INTR_DISABLE;
    input_conf.mode = GPIO_MODE_INPUT;
    input_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    input_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    input_conf.pin_bit_mask = (1ULL << USB_DET_GPIO);
    gpio_config(&input_conf);

    gpio_config_t output_conf = {};
    output_conf.intr_type = GPIO_INTR_DISABLE;
    output_conf.mode = GPIO_MODE_OUTPUT;
    output_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    output_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    output_conf.pin_bit_mask = (1ULL << USB_SW_GPIO) | (1ULL << CC_VDD_GPIO) | (1ULL << MIC_SELECT_GPIO) | (1ULL << AUDIO_CODEC_PA_PIN);
    gpio_config(&output_conf);

    mic_select_level = 0;
    gpio_set_level(MIC_SELECT_GPIO, mic_select_level);

    // adc_oneshot_unit_init_cfg_t init_cfg1 = {
    //     .unit_id = CC_ADC_UNIT,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };
    // adc_oneshot_new_unit(&init_cfg1, &cc_adc_handle);
    // 用电源管理的ADC，防止重复初始化
    cc_adc_handle = PowerManager::GetSharedAdcHandle();
    cc_adc_cali_handle = PowerManager::GetSharedAdcCaliHandle();
    cc_adc_calibrated = PowerManager::IsSharedAdcCalibrated();
    if (cc_adc_handle == nullptr) {
        adc_oneshot_unit_init_cfg_t init_cfg1 = {
            .unit_id = CC_ADC_UNIT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        adc_oneshot_new_unit(&init_cfg1, &cc_adc_handle);
        cc_adc_calibrated = AdcCalibrationInit(CC_ADC_UNIT, CC_ADC_CHANNEL, ADC_ATTEN_DB_12, &cc_adc_cali_handle);
    }

    // adc_oneshot_unit_init_cfg_t init_cfg2 = {
    //     .unit_id = USB_MIC_ADC_UNIT,
    //     .ulp_mode = ADC_ULP_MODE_DISABLE,
    // };
    // adc_oneshot_new_unit(&init_cfg2, &usb_mic_adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };


    // 复用同一个 ADC1 句柄，避免重复初始化 ADC_UNIT_1
    usb_mic_adc_handle = cc_adc_handle;
    if (usb_mic_adc_handle == nullptr) {
        adc_oneshot_unit_init_cfg_t init_cfg2 = {
            .unit_id = USB_MIC_ADC_UNIT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        adc_oneshot_new_unit(&init_cfg2, &usb_mic_adc_handle);
        // 兜底：如果前面未初始化 CC 的句柄，这里共享给 CC
        if (cc_adc_handle == nullptr) {
            cc_adc_handle = usb_mic_adc_handle;
        }
    }



    // adc_oneshot_config_channel(cc_adc_handle, CC_ADC_CHANNEL, &chan_cfg);
    // adc_oneshot_config_channel(usb_mic_adc_handle, USB_MIC_ADC_CHANNEL, &chan_cfg);

    // cc_adc_calibrated = AdcCalibrationInit(CC_ADC_UNIT, CC_ADC_CHANNEL, ADC_ATTEN_DB_12, &cc_adc_cali_handle);
    // usb_mic_adc_calibrated = AdcCalibrationInit(USB_MIC_ADC_UNIT, USB_MIC_ADC_CHANNEL, ADC_ATTEN_DB_12, &usb_mic_cali_handle);


    //   if (cc_adc_handle != nullptr) {
    //     adc_oneshot_config_channel(cc_adc_handle, CC_ADC_CHANNEL, &chan_cfg);
    // }
    // adc_oneshot_config_channel(usb_mic_adc_handle, USB_MIC_ADC_CHANNEL, &chan_cfg);

    // usb_mic_adc_calibrated = AdcCalibrationInit(USB_MIC_ADC_UNIT, USB_MIC_ADC_CHANNEL, ADC_ATTEN_DB_12, &usb_mic_cali_handle);

    if (cc_adc_handle != nullptr) {
        adc_oneshot_config_channel(cc_adc_handle, CC_ADC_CHANNEL, &chan_cfg);
    }
    if (usb_mic_adc_handle != nullptr) {
        adc_oneshot_config_channel(usb_mic_adc_handle, USB_MIC_ADC_CHANNEL, &chan_cfg);
    }

    usb_mic_adc_calibrated = AdcCalibrationInit(USB_MIC_ADC_UNIT, USB_MIC_ADC_CHANNEL, ADC_ATTEN_DB_12, &usb_mic_cali_handle);

     
}

static void UpdateUsbAudioDetect() {

    static bool last_headset_present = false;
    
    if (gpio_get_level(USB_DET_GPIO)) { // USB_DET为高电平
        // ESP_LOGI(TAG, "USB_DET high: treat as charger");
        // printf("USB_DET high\n");

        // USB_DET为高电平，表示连接了充电器
        // 此时保持CC_VDD为高，USB_SW拉低
        headset_present = false;
        gpio_set_level(CC_VDD_GPIO, 1);  // CC_VDD拉高
        gpio_set_level(USB_SW_GPIO, 0);  // USB_SW拉低
        gpio_set_level(AUDIO_CODEC_PA_PIN, 1);  // 打开扬声器
        if (last_headset_present != headset_present) {
            // ESP_LOGI(TAG, "USB_DET high: treat as charger, CC_VDD=1, USB_SW=0, speaker enabled");
            printf("USB_DET high: treat as charger, CC_VDD=1, USB_SW=0, speaker enabled\n");    
            last_headset_present = headset_present;
        }
        // printf("11111");
        // int mic_mv2 = 0;
        // ReadAdcMv(usb_mic_adc_handle, usb_mic_cali_handle, usb_mic_adc_calibrated, USB_MIC_ADC_CHANNEL, mic_mv2);
        //  printf(" USB_MIC_ADC voltage: %d mV\n", mic_mv2);
        return;
    }

    // USB_DET为低电平，可能连接了耳机
    // 周期控制CC_VDD为0来检查CC_ADC是否被耳机拉低
    gpio_set_level(CC_VDD_GPIO, 0);  // 拉低CC_VDD来检测耳机是否拉低CC_ADC

    vTaskDelay(pdMS_TO_TICKS(1));
    int cc_mv = 0;
    if (!ReadAdcMv(cc_adc_handle, cc_adc_cali_handle, cc_adc_calibrated, CC_ADC_CHANNEL, cc_mv)) {
        printf("read err\n");
        return;
    }
    // ReadAdcMv(cc_adc_handle, cc_adc_cali_handle, cc_adc_calibrated, CC_ADC_CHANNEL, cc_mv);
    // printf("CC_ADC voltage: %d mV\n", cc_mv);

    static int headset_cnt = 0;
    static int no_headset_cnt = 0;
    
    if (cc_mv < CC_ADC_HEADSET_MV) {
        // CC_ADC被拉低，表明耳机插入
        headset_cnt++;
        no_headset_cnt = 0;
    } else {
        // CC_ADC未被拉低，没有耳机插入
        no_headset_cnt++;
        headset_cnt = 0;
    }

    if (!headset_present && headset_cnt >= 2) {
        // 检测到耳机插入，开始识别MIC线路

        gpio_set_level(USB_SW_GPIO, 1);  // USB_SW拉高，连接模拟USB耳机

        // 先将MIC_SELECT置0测电压
        gpio_set_level(MIC_SELECT_GPIO, 0);
        
        vTaskDelay(pdMS_TO_TICKS(1));  // 短暂延迟等待稳定
        
        int mic_mv_0 = 0;
        if (ReadAdcMv(usb_mic_adc_handle, usb_mic_cali_handle, usb_mic_adc_calibrated, USB_MIC_ADC_CHANNEL, mic_mv_0)) {
            printf("MIC_SELECT=0, USB_MIC_ADC voltage: %d mV\n", mic_mv_0);
        }
        
        // 再将MIC_SELECT置1测电压
        gpio_set_level(MIC_SELECT_GPIO, 1);
        
        vTaskDelay(pdMS_TO_TICKS(1));  // 短暂延迟等待稳定
        
        int mic_mv_1 = 0;
        if (ReadAdcMv(usb_mic_adc_handle, usb_mic_cali_handle, usb_mic_adc_calibrated, USB_MIC_ADC_CHANNEL, mic_mv_1)) {
            printf("MIC_SELECT=1, USB_MIC_ADC voltage: %d mV\n", mic_mv_1);
        }
        
        // 比较两个电压，选择较大的那个作为MIC_SELECT的值
        if (mic_mv_0 > mic_mv_1 ) {
            // MIC_SELECT设为0时电压更大且达到最小阈值，选择0
            mic_select_level = 0;
            gpio_set_level(MIC_SELECT_GPIO, mic_select_level);
            headset_present = true;
            printf("Selecting MIC_SELECT=0, higher voltage: %d mV\n", mic_mv_0);
            
            // 设置PA使能为低电平，关闭扬声器，启用耳机
            gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
        } else if (mic_mv_1 > mic_mv_0 ) {
            // MIC_SELECT设为1时电压更大且达到最小阈值，选择1
            mic_select_level = 1;
            gpio_set_level(MIC_SELECT_GPIO, mic_select_level);
            headset_present = true;
            printf("Selecting MIC_SELECT=1, higher voltage: %d mV\n", mic_mv_1);
            
            // 设置PA使能为低电平，关闭扬声器，启用耳机
            gpio_set_level(AUDIO_CODEC_PA_PIN, 0);
        } else {
            // 两个电压都低于阈值或者相等，则默认设置为0
            mic_select_level = 0;
            gpio_set_level(MIC_SELECT_GPIO, mic_select_level);
            headset_present = false;
        }
        
        // 输出最终选择的电压值
        int final_voltage = (mic_select_level == 0) ? mic_mv_0 : mic_mv_1;
        printf("Final selection - MIC_SELECT=%d, voltage: %d mV\n", mic_select_level, final_voltage);
    } else if (headset_present && no_headset_cnt >= 3) {
        // 检测到耳机拔出
        headset_present = false;
        mic_select_level = 0;
        gpio_set_level(MIC_SELECT_GPIO, mic_select_level);
        gpio_set_level(AUDIO_CODEC_PA_PIN, 1);  // 打开扬声器
        
        ESP_LOGI(TAG, "Headset removed");
    }

    if (last_headset_present != headset_present) {
        if (headset_present) {
            ESP_LOGI(TAG, "Headset inserted, MIC_SELECT=%d, PA_EN=0 (headphone mode)", mic_select_level);
        } else {
            ESP_LOGI(TAG, "Device disconnected, PA_EN=1 (speaker mode)");
        }
        last_headset_present = headset_present;
    }
}

// 耳机检测任务
static void UsbAudioDetectTask(void* arg) {
    InitUsbAudioDetect();
    // printf("Starting USB audio detect task\n");
    while (true) {
        UpdateUsbAudioDetect();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// NFC 任务
static void NfcScanTask(void* arg) {
    auto* board = static_cast<Board*>(arg);
    auto bus_handle = board->GetI2cBus();
    if (bus_handle == nullptr) {
        ESP_LOGW(TAG, "NFC scan task aborted: I2C bus is not ready");
        vTaskDelete(NULL);
    }

    // ws1850_NFC_gpio_init();
    ws_iic_init(bus_handle);
    PcdReset();

        for (int i = 0; i < 3; ++i) {
        PcdAntennaOff();
        vTaskDelay(pdMS_TO_TICKS(20));
        PcdAntennaOn();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("NFC init: TxControlReg=0x%02X\n", ReadRawRC(TxControlReg));

    const TickType_t scan_interval = pdMS_TO_TICKS(200);
    // while (true) {
    //     Card_Check();
    //     vTaskDelay(scan_interval);
    // }
    while (true) {
        PcdReset();
        Card_Check();
        PcdAntennaOff();
        vTaskDelay(scan_interval);
    }
}



// 静态任务栈用于状态上报任务，避免频繁分配释放导致内存碎片
static StaticTask_t g_status_task_buffer;
static StackType_t g_status_task_stack[6144 / sizeof(StackType_t)];

static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};



Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    Settings settings("aecMode", false);
    aec_mode_ = (AecMode)settings.GetInt("deviceAec", 1);
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    // MyDazy 定时上报状态 - 使用静态任务栈避免内存碎片
    esp_timer_create_args_t status_timer_args = {
    .callback = [](void* arg) {
        if (g_status_reporting) return;
        // 避免在语音活动期间上报状态，防止打断对话
        auto app = static_cast<Application*>(arg);
        auto state = app->GetDeviceState();
        if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
            return;
        }

        g_status_reporting = true;
        // 使用静态任务栈创建任务，避免频繁分配释放导致内存碎片
        TaskHandle_t task_handle = xTaskCreateStatic(
            [](void *p) {
                Ota ota;
                ota.ReportStatus();
                g_status_reporting = false;
                vTaskDelete(NULL);
            },
            "status_report",
            sizeof(g_status_task_stack) / sizeof(StackType_t),
            nullptr,
            3,
            g_status_task_stack,
            &g_status_task_buffer
        );
        if (task_handle == NULL) {
            ESP_LOGE(TAG, "Failed to create status_report task");
            g_status_reporting = false;
        }
    },
    .arg = this,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "status_timer",
    .skip_unhandled_events = true
    };
    esp_timer_create(&status_timer_args, &status_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    // MyDazy 定时上报状态
    if (status_timer_handle_ != nullptr) {
        esp_timer_stop(status_timer_handle_);
        esp_timer_delete(status_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }


    //hsf
        // Apply assets with display locked to avoid LVGL using freed fonts
    if (display != nullptr) {
        DisplayLockGuard lock(display);
        assets.Apply();
    } else {
        assets.Apply();
    }

    // // Apply assets
    // assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("logo");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota.CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota.GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "logo", Lang::Sounds::OGG_DISCONNECT);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "qrcode", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
//        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
//        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // hsf
    xTaskCreate(UsbAudioDetectTask, "usb_audio_detect", 4096, nullptr, 3, nullptr);

    xTaskCreate(NfcScanTask, "nfc_scan", 4096, &board, 3, nullptr);



    /* Setup the display */
    auto display = board.GetDisplay();

    // // 在启动网络前先加载本地 Assets 资源（使开机即显示logo，配网模式也能显示表情包）
    // auto& assets = Assets::GetInstance();
    // if (assets.partition_valid() && assets.checksum_valid()) {
    //     assets.Apply();
    //     display->SetEmotion("logo");
    //     ESP_LOGI(TAG, "本地 Assets 资源加载完成");
    // }

    // hsf
    // 在启动网络前先加载本地 Assets 资源（使开机即显示logo，配网模式也能显示表情包）
    auto& assets = Assets::GetInstance();
    if (assets.partition_valid() && assets.checksum_valid()) {
        if (display != nullptr) {
            DisplayLockGuard lock(display);
            assets.Apply();
        } else {
            assets.Apply();
        }
        display->SetEmotion("logo");
        ESP_LOGI(TAG, "本地 Assets 资源加载完成");
    }

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);

    //hsf
    // if (codec->input_gain() < 37.0f) {
    // codec->SetInputGain(37.0f);
    // }
// git test 555 888 999
    audio_service_.Start();



    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
    // ScheduleStatusBarUpdate(display,true); // hsf

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        Settings ws_settings("websocket", false);
        std::string ws_url = ws_settings.GetString("url");
        if (!ws_url.empty() && ws_url.find("joyinside.jd.com") != std::string::npos) {
            protocol_ = std::make_unique<WebsocketJoeaiProtocol>();
        } else {
            protocol_ = std::make_unique<WebsocketProtocol>();
        }
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "服务器采样率 %d 与设备输出采样率 %d 不匹配,重采样可能导致失真",
            protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // 用户请求OTA更新后重启
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                auto message = cJSON_GetObjectItem(payload, "message");
                cJSON* msg_json = nullptr;
                bool need_delete = false;

                // 支持两种格式：message 是字符串或对象
                if (cJSON_IsString(message)) {
                    msg_json = cJSON_Parse(message->valuestring);
                    need_delete = true;
                } else if (cJSON_IsObject(message)) {
                    msg_json = message;
                    need_delete = false;
                }

                if (msg_json) {
                    auto msg_type = cJSON_GetObjectItem(msg_json, "type");
                    const char* type_str = cJSON_IsString(msg_type) ? msg_type->valuestring : "";

                    if (strcmp(type_str, "reboot") == 0) {
                        // 重启设备
                        ESP_LOGI(TAG, "Custom action: reboot");
                        Schedule([this]() {
                            Alert("远程控制", "正在重启", "logo", Lang::Sounds::OGG_VIBRATION);
                            vTaskDelay(pdMS_TO_TICKS(2000));
                            Reboot();
                        });
                    } else if (strcmp(type_str, "update") == 0 || strcmp(type_str, "ota") == 0) {
                        // 检查更新
                        ESP_LOGI(TAG, "Custom action: update");
                        Schedule([this]() {
                            Alert("远程控制", "检查更新", "logo", Lang::Sounds::OGG_VIBRATION);
                            Ota ota;
                            ota.CheckVersion();
                        });
                    } else if (strcmp(type_str, "reconnect") == 0) {
                        // 重新连接服务器
                        ESP_LOGI(TAG, "Custom action: reconnect");
                        Schedule([this]() {
                            if (protocol_) {
                                protocol_->CloseAudioChannel();
                                vTaskDelay(pdMS_TO_TICKS(500));
                                //唤醒
                                OnWakeWordDetected();
                            }
                        });
                    } else if (strcmp(type_str, "wakeup") == 0) {
                        // 远程唤醒并发送消息给 AI
                        auto wakeup_item = cJSON_GetObjectItem(msg_json, "text");
                        if (cJSON_IsString(wakeup_item)) {
                            std::string wake_text = wakeup_item->valuestring;
                            ESP_LOGI(TAG, "Custom wakeup: %s", wake_text.c_str());
                            Schedule([this, wake_text]() {
                                if (protocol_) {
                                    protocol_->SendWakeWordDetected(wake_text);
                                }
                                WakeWordInvoke(wake_text);
                            });
                        }
                    } else {
                        ESP_LOGW(TAG, "Unknown custom message type: %s", type_str);
                    }

                    if (need_delete) {
                        cJSON_Delete(msg_json);
                    }
                } else {
                    // 没有有效的 message 字段，直接显示 payload
                    Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                        display->SetChatMessage("system", payload_str.c_str());
                    });
                }
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // 播放成功提示音表示设备就绪
        audio_service_.PlaySound(Lang::Sounds::OGG_CONNECT);
    }

    // MyDazy OTA和协议初始化后启动60秒周期状态上报
    StartStatusReportTimer();


}

// 向主事件循环添加异步任务
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// 主事件循环控制聊天状态和WebSocket连接
// 其他任务如需访问WebSocket或聊天状态,应通过Schedule调用此函数
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_DISCONNECT);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            // 检查 display 是否有效，避免重启清理期间崩溃
            if (display != nullptr && device_state_ != kDeviceStateUnknown) {
                display->UpdateStatusBar();
                // ScheduleStatusBarUpdate(display,false); // hsf
            }
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }

        //ADD YZT 

        // gpio_set_level(GPIO_NUM_9, 1);

        // // gpio_set_direction(GPIO_NUM_40, GPIO_MODE_OUTPUT);//
        // // gpio_set_level(GPIO_NUM_40, 0);


        //  gpio_set_direction(GPIO_NUM_7, GPIO_MODE_INPUT);//USB_DET

        // // gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT);//CC_VDD

        // // gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);//PA_EN

        // // gpio_set_direction(GPIO_NUM_6, GPIO_MODE_INPUT);//CC_ADC

        // gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);//MIC_SELECT

        // gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT);//USB_SW
        //  gpio_set_level(GPIO_NUM_46, 1);

        // // //ADC配置 USB_MIC_ADC(GPIO9 ADC1_CH8)

        // // gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);//USB_MIC_ADC

        // // //当USB_DET采集到高电压，表示USB连接充电器，此时保持CC_VDD为高 && PA_EN高
        // // if(gpio_get_level(GPIO_NUM_7) == 1)//ESP_OK 0
        // // {
        // //     gpio_set_level(GPIO_NUM_46, 1);  
        // //     gpio_set_level(GPIO_NUM_10, 1);  
        // // }
        // // else//当USB_DET采集到低电压，表示可能连接耳机
        // // {
        // //     gpio_set_level(GPIO_NUM_46, 0); 
        // //     gpio_set_level(GPIO_NUM_10, 1);

        // //     if(gpio_get_level(GPIO_NUM_6) == 0)//CC_ADC是否被耳机拉低
        // //     {
        // //         if( EarPhone_Flag == 0)//未识别到耳机插入
        // //         {
        // //             // level = !level;  // 直接翻转状态变量
        //             level = 1;  // 直接翻转状态变量
        //             gpio_set_level(GPIO_NUM_21, level); //交替切换MIC_SELECT

        //             // es7210_codec_cfg_t es7210_cfg2 = {};
        //             // // es7210_cfg2.ctrl_if = in_ctrl_if_;
        //             // es7210_cfg2.mic_selected = ES7210_SEL_MIC4;
        //             // in_codec_if_ = es7210_codec_new(&es7210_cfg2);
        // //             vTaskDelay(10);
        // //         }
        // //         // // if((float)adc1_get_raw(8)*(3.3/4096) >= 1.5) //USB_MIC_ADC采集电压满足匹配范围
        // //         // // {
        // //         // //      EarPhone_Flag = 1;//耳机状态：插入
        // //         // // }
        // //         // ESP_ERROR_CHECK(adc_oneshot_read(adc_handle_mic, ADC_CHANNEL_8, &mic_adc_value));

        // //         // ESP_ERROR_CHECK(adc_cali_raw_to_voltage(mic_adc1_cali_chan0_handle_, mic_adc_value, &mic_voltage));
        // //         // if(mic_voltage >= 1500) //USB_MIC_ADC采集电压满足匹配范围
        // //         // {
        // //         //      EarPhone_Flag = 1;//耳机状态：插入
        // //         // }

        // //         //  if(gpio_get_level(GPIO_NUM_9) == 0)//USB_MIC_ADC = 0?
        // //         //  {
        // //         //     EarPhone_Flag = 1;//耳机状态：插入

        // //         //  }
        // //     }
        // //     else//耳机未插入
        // //     {
        // //         level = 0;
        //         EarPhone_Flag = 0;//耳机状态：未插入
        // //     }
        // // }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // 向服务器发送唤醒词检测事件
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_WAKEUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
//        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "中止语音播报");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "设备状态: %s", STATE_STRINGS[device_state_]);

    // 发送状态变更事件
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");

            // 确保音频处理器正在运行
            if (!audio_service_.IsAudioProcessorRunning()) {
                // 向服务器发送开始监听命令
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
                audio_service_.EnableWakeWordDetection(false);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // 播报模式下AFE唤醒词可以被检测
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        default:
            ESP_LOGW(TAG, "未处理的设备状态: %d", state);
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    // 清理显示资源（防止重启时黑屏）
    auto& board = Board::GetInstance();
    board.CleanupDisplay();

    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // 确保在主线程中发送MCP消息
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        Settings settings("aecMode", true);
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            settings.SetInt("deviceAec", 0);
            audio_service_.EnableDeviceAec(false);
            Alert(Lang::Strings::RTC_MODE_OFF, "", "", Lang::Sounds::OGG_AEC_OFF);
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;
        case kAecOnServerSide:
            settings.SetInt("deviceAec", 0);
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            settings.SetInt("deviceAec", 1);
            audio_service_.EnableDeviceAec(true);
            Alert(Lang::Strings::RTC_MODE_ON, "", "", Lang::Sounds::OGG_AEC_ON);
            vTaskDelay(pdMS_TO_TICKS(2000));
            break;
        }

        // AEC模式改变后关闭音频通道
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
            vTaskDelay(pdMS_TO_TICKS(500));
            protocol_->OpenAudioChannel();
        } else {
            StartListening();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

// MyDazy 定时上报状态定时器
void Application::StartStatusReportTimer() {
    if (status_timer_handle_ == nullptr) {
        return;
    }
    // 60s 周期
    esp_timer_start_periodic(status_timer_handle_, 60ULL * 1000ULL * 1000ULL);
}

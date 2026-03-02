#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"
#include "blufi.h"
#include "application.h"
#include "ota_http_download.h"
#include "assets.h"
#include <wifi_station.h>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>

#define TAG "Ota"


Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // Read Serial Number from efuse user_data
    uint8_t serial_number[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            has_serial_number_ = false;
        } else {
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
    Settings settings("wifi", false);
    std::string url = settings.GetString("ota_url");
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }
    return url;
}

std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    
    // 添加BindingCode到请求头
    std::string binding_code = Blufi::GetBindingCode();
    if (!binding_code.empty()) {
        http->SetHeader("Binding-Code", binding_code.c_str());
        ESP_LOGI(TAG, "Added BindingCode to HTTP header: %s", binding_code.c_str());
    }
    
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/* 
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
esp_err_t Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Check version URL: %s", url.c_str());
    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    ESP_LOGI(TAG, "data: %s\r\n", data.c_str());
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    if (!http->Open(method, url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }

    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return status_code;
    }

    data = http->ReadAll();
    http->Close();

    ESP_LOGI(TAG, "data: %s\r\n", data.c_str());
    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL
    
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
//        cJSON* qrcode_url = cJSON_GetObjectItem(activation, "qrcode_url");
//        if (cJSON_IsString(qrcode_url)) {
//            activation_qrcode_url_ = qrcode_url->valuestring;
//            ESP_LOGI(TAG, "Activation QR code URL: %s", activation_qrcode_url_.c_str());
//        }
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;
            
            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }
            
            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // If the force flag is set to 1, the given version is forced to be installed
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    // 处理自定义内容下载（CheckVersion也支持custom字段）
    Settings settings("ota", false);
    int enabled = settings.GetInt("custom", 0);
    cJSON* custom = cJSON_GetObjectItem(root, "custom");
    if (enabled && cJSON_IsArray(custom)) {
        ProcessCustomContent(custom, "CheckVersion");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

bool Ota::Upgrade(const std::string& firmware_url) {
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    esp_ota_handle_t update_handle = 0;
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    std::string image_header;

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http->Open("GET", firmware_url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get firmware, status code: %d", http->GetStatusCode());
        return false;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        return false;
    }

    char buffer[512];
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            return false;
        }

        // Calculate speed and progress every second
        recent_read += ret;
        total_read += ret;
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            ESP_LOGI(TAG, "Progress: %u%% (%u/%u), Speed: %uB/s", progress, total_read, content_length, recent_read);
            if (upgrade_callback_) {
                upgrade_callback_(progress, recent_read);
            }
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (ret == 0) {
            break;
        }

        if (!image_header_checked) {
            image_header.append(buffer, ret);
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
                
                auto current_version = esp_app_get_description()->version;
                ESP_LOGI(TAG, "Current version: %s, New version: %s", current_version, new_app_info.version);

                if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle)) {
                    esp_ota_abort(update_handle);
                    ESP_LOGE(TAG, "Failed to begin OTA");
                    return false;
                }

                image_header_checked = true;
                std::string().swap(image_header);
            }
        }
        auto err = esp_ota_write(update_handle, buffer, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            return false;
        }
    }
    http->Close();

    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    upgrade_callback_ = callback;
    return Upgrade(firmware_url_);
}

bool Ota::StartUpgradeFromUrl(const std::string& url, std::function<void(int progress, size_t speed)> callback) {
    upgrade_callback_ = callback;
    return Upgrade(url);
}

std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    std::stringstream ss(version);
    std::string segment;
    
    while (std::getline(ss, segment, '.')) {
        versionNumbers.push_back(std::stoi(segment));
    }
    
    return versionNumbers;
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);
    
    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }
    
    return newer.size() > current.size();
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节
    
    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

esp_err_t Ota::Activate() {
//    if (!has_activation_challenge_) {
//        ESP_LOGW(TAG, "No activation challenge found");
//        return ESP_FAIL;
//    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }
    
    auto status_code = http->GetStatusCode();
    if (status_code == 202) {
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}

// MyDazy 上报状态
bool Ota::ReportStatus() {
    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Status URL base not set");
        return false;
    }

    // URL 不包含 "mydazy"，使用固定的 mydazy.cn 域名
    if (url.find("mydazy") == std::string::npos) {
        url = "https://www.mydazy.cn/v1/ota/status";
    } else {
        if (url.back() == '/') {
            url += "status";
        } else {
            url += "/status";
        }
    }

    auto http = SetupHttp();
    auto& board = Board::GetInstance();
    std::string status_json = board.GetDeviceStatusJson();
    std::string board_json = board.GetBoardJson();
    if (status_json.empty()) status_json = "{}";
    if (board_json.empty()) board_json = "{}";
    std::string payload = std::string("{\"status\":") + status_json + ",\"board\":" + board_json + "}";
    ESP_LOGI(TAG, "status url: %s", url.c_str());
    ESP_LOGI(TAG, "status payload: %s", payload.c_str());
    http->SetContent(std::move(payload));
    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP for status");
        return false;
    }
    int code = http->GetStatusCode();
    std::string resp;
    if (code == 200) {
        resp = http->ReadAll();
    }
    http->Close();

    if (code != 200 && code != 202 && code != 204) {
        ESP_LOGW(TAG, "Status post code: %d", code);
        return false;
    }

    // 若服务端返回自定义素材，异步处理
    if (!resp.empty()) {
        auto *payload = new std::string(std::move(resp));
        BaseType_t task_created = xTaskCreate([](void* p){
            std::unique_ptr<std::string> holder((std::string*)p);
            cJSON* root = cJSON_Parse(holder->c_str());
            if (!root) { vTaskDelete(NULL); return; }
            // 同步服务器时间（可选）
            cJSON* server_time = cJSON_GetObjectItem(root, "server_time");
            if (cJSON_IsObject(server_time)) {
                cJSON* timestamp = cJSON_GetObjectItem(server_time, "timestamp");
                cJSON* timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
                if (cJSON_IsNumber(timestamp)) {
                    struct timeval tv; double ts = timestamp->valuedouble;
                    if (cJSON_IsNumber(timezone_offset)) ts += (timezone_offset->valueint * 60 * 1000);
                    tv.tv_sec = (time_t)(ts / 1000);
                    tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;
                    settimeofday(&tv, NULL);
                }
            }

            // 处理远程设置控制
            cJSON* settings_obj = cJSON_GetObjectItem(root, "settings");
            if (cJSON_IsObject(settings_obj)) {
                ESP_LOGI(TAG, "处理远程设置控制");

                // status命名空间的设置（统一所有状态控制）
                cJSON* status_obj = cJSON_GetObjectItem(settings_obj, "status");
                if (cJSON_IsObject(status_obj)) {
                    Settings status_settings("status", true);

                    // deepSleep设置
                    cJSON* deep_sleep = cJSON_GetObjectItem(status_obj, "deepSleep");
                    if (cJSON_IsNumber(deep_sleep)) {
                        int value = deep_sleep->valueint;
                        status_settings.SetInt("deepSleep", value);
                        ESP_LOGI(TAG, "远程设置 status.deepSleep = %d", value);
                    }

                    // report设置（状态上报默认开启）
                    cJSON* report = cJSON_GetObjectItem(status_obj, "report");
                    if (cJSON_IsNumber(report)) {
                        int value = report->valueint;
                        status_settings.SetInt("report", value);
                        ESP_LOGI(TAG, "远程设置 status.report = %d", value);
                    }

                    // autoStart设置（统一到status命名空间）
                    cJSON* auto_start = cJSON_GetObjectItem(status_obj, "autoStart");
                    if (cJSON_IsNumber(auto_start)) {
                        int value = auto_start->valueint;
                        status_settings.SetInt("autoStart", value);
                        ESP_LOGI(TAG, "远程设置 status.autoStart = %d", value);
                    }

                    // pickupWake设置（陀螺仪拿起唤醒）
                    cJSON* pickup_wake = cJSON_GetObjectItem(status_obj, "pickupWake");
                    if (cJSON_IsNumber(pickup_wake)) {
                        int value = pickup_wake->valueint;
                        status_settings.SetInt("pickupWake", value);
                        ESP_LOGI(TAG, "远程设置 status.pickupWake = %d", value);
                    }
                }

                // 通用设置（直接在settings下的字段）
                cJSON* item = NULL;
                cJSON_ArrayForEach(item, settings_obj) {
                    // 跳过已经处理的对象
                    if (strcmp(item->string, "status") == 0) {
                        continue;
                    }
                    // 处理其他通用设置
                    if (cJSON_IsNumber(item)) {
                        Settings general_settings("settings", true);
                        general_settings.SetInt(item->string, item->valueint);
                        ESP_LOGI(TAG, "远程设置 %s = %d", item->string, item->valueint);
                    } else if (cJSON_IsString(item)) {
                        Settings general_settings("settings", true);
                        general_settings.SetString(item->string, item->valuestring);
                        ESP_LOGI(TAG, "远程设置 %s = %s", item->string, item->valuestring);
                    }
                }
            }

            // 处理自定义内容下载（统一使用custom字段）
            Settings settings("ota", false);
            int enabled = settings.GetInt("custom", 1);
            cJSON* custom = cJSON_GetObjectItem(root, "custom");
            if (enabled && cJSON_IsArray(custom)) {
                Ota ota_instance;
                ota_instance.ProcessCustomContent(custom, "ReportStatus");
            }

            cJSON_Delete(root);
            vTaskDelete(NULL);
        }, "status_assets", 4096, payload, 4, NULL);

        // 检查任务创建是否成功，失败则释放内存防止泄漏
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create status_assets task, freeing payload");
            delete payload;
        }
    }

    return true;
}


// 优化的 ProcessCustomContent 方法 - 使用OtaHttpDownload
bool Ota::ProcessCustomContent(cJSON* custom_array, const std::string& context) {
    ESP_LOGI(TAG, "==========自定义下载更新Processing custom content%s...", context.empty() ? "" : (" from " + context).c_str());

    if (!cJSON_IsArray(custom_array)) return false;
    int total_items = cJSON_GetArraySize(custom_array);
    ESP_LOGI(TAG, "发现 %d 个自定义内容项", total_items);

    // 使用单例模式获取下载器实例，复用1MB缓冲区避免内存碎片
    OtaHttpDownload& downloader = OtaHttpDownload::GetInstance();

    // 检查是否正在下载，避免并发问题
    if (downloader.is_downloading()) {
        ESP_LOGW(TAG, "下载器正在执行任务，跳过本次处理");
        return false;
    }

    // 设置进度回调
    downloader.set_progress_callback([](int progress, size_t downloaded, size_t total) {
        ESP_LOGI(TAG, "下载进度: %d%% (%zu/%zu bytes)", progress, downloaded, total);
    });

    // 设置完成回调
    downloader.set_complete_callback([](bool success, const std::string& error) {
        if (success) {
            ESP_LOGI(TAG, "所有自定义内容下载完成");
        } else {
            ESP_LOGE(TAG, "自定义内容下载失败: %s", error.c_str());
        }
    });
    
    cJSON* custom_item = nullptr;
    int item_index = 0;
    cJSON_ArrayForEach(custom_item, custom_array) {
        if (!cJSON_IsObject(custom_item)) continue;
        
        item_index++;
        ESP_LOGI(TAG, "处理第 %d/%d 项", item_index, total_items);
        
        cJSON* url = cJSON_GetObjectItem(custom_item, "url");
        cJSON* path = cJSON_GetObjectItem(custom_item, "path");
        cJSON* md5 = cJSON_GetObjectItem(custom_item, "md5");
        
        if (!cJSON_IsString(url) || !cJSON_IsString(path)) {
            ESP_LOGW(TAG, "跳过无效的文件");
            continue;
        }
        
        std::string download_url = url->valuestring;
        std::string expected_md5 = cJSON_IsString(md5) ? md5->valuestring : "";
        std::string save_path;

        // 简化路径处理：直接使用文件名
        if (cJSON_IsString(path)) {
            std::string original_path = path->valuestring;
            std::string filename = original_path.substr(original_path.find_last_of('/') + 1);
            save_path = std::string(CONFIG_SPIFFS_BASE_PATH) + "/" + filename;
            ESP_LOGI(TAG, "项目 %d: %s -> %s", item_index, original_path.c_str(), save_path.c_str());
        } else {
            ESP_LOGE(TAG, "项目 %d: 路径无效", item_index);
            continue;
        }

        // 添加到下载队列 - 使用OtaHttpDownload的安全文件替换
        downloader.add_download(download_url.c_str(), save_path.c_str(), expected_md5.c_str());
    }
    
    ESP_LOGI(TAG, "自定义内容处理完成，已添加 %d 个下载任务", item_index);
    
    // 启动下载任务
    if (item_index > 0) {
        // 让出CPU时间，确保系统任务正常运行
        vTaskDelay(pdMS_TO_TICKS(10));
        downloader.start_downloads();
        ESP_LOGI(TAG, "已启动自定义内容下载任务");
    }

    return true;
}

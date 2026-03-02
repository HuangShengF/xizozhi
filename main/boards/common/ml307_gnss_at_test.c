// #include "ml307_gnss_at_test.h"

// #include <driver/uart.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <stdio.h>
// #include <string.h>

// #define ML307_AT_UART UART_NUM_1
// #define ML307_AT_RX_BUF_SIZE 4096

// typedef struct {
//     gpio_num_t tx_pin;
//     gpio_num_t rx_pin;
//     int baud_rate;
// } ml307_gnss_at_test_config_t;

// static ml307_gnss_at_test_config_t g_cfg;

// static bool response_complete(const char *buf, size_t len) {
//     if (len == 0 || buf == NULL) {
//         return false;
//     }
//     if (strstr(buf, "\r\nOK\r\n") != NULL) {
//         return true;
//     }
//     if (strstr(buf, "\r\nERROR\r\n") != NULL) {
//         return true;
//     }
//     if (strstr(buf, "+CME ERROR:") != NULL) {
//         return true;
//     }
//     return false;
// }

// static void ml307_gnss_at_test_task(void *arg) {
//     const ml307_gnss_at_test_config_t *cfg = (const ml307_gnss_at_test_config_t *)arg;
//     if (cfg == NULL) {
//         vTaskDelete(NULL);
//         return;
//     }

//     if (!uart_is_driver_installed(ML307_AT_UART)) {
//         uart_config_t uart_config = {
//             .baud_rate = cfg->baud_rate,
//             .data_bits = UART_DATA_8_BITS,
//             .parity = UART_PARITY_DISABLE,
//             .stop_bits = UART_STOP_BITS_1,
//             .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//             .source_clk = UART_SCLK_DEFAULT,
//         };
//         uart_driver_install(ML307_AT_UART, ML307_AT_RX_BUF_SIZE, 0, 0, NULL, 0);
//         uart_param_config(ML307_AT_UART, &uart_config);
//         uart_set_pin(ML307_AT_UART, cfg->tx_pin, cfg->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
//     }

//     const char *cmd = "AT+MGNSSCFG=?\r\n";
//     char resp[1024];

//     while (true) {
//         uart_flush_input(ML307_AT_UART);
//         uart_write_bytes(ML307_AT_UART, cmd, strlen(cmd));

//         size_t total = 0;
//         resp[0] = '\0';
//         const TickType_t timeout_ticks = pdMS_TO_TICKS(2000);
//         TickType_t start = xTaskGetTickCount();

//         while ((xTaskGetTickCount() - start) < timeout_ticks) {
//             int len = uart_read_bytes(ML307_AT_UART, (uint8_t *)&resp[total],
//                                       sizeof(resp) - 1 - total, pdMS_TO_TICKS(200));
//             if (len > 0) {
//                 total += (size_t)len;
//                 resp[total] = '\0';
//                 if (response_complete(resp, total)) {
//                     break;
//                 }
//             }
//         }

//         printf("AT[%s] ->\n%s\n", "AT+MGNSSCFG=?", resp[0] ? resp : "(no response)");
//         vTaskDelay(pdMS_TO_TICKS(3000));
//     }
// }

// void ml307_gnss_at_test_start(gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate) {
//     g_cfg.tx_pin = tx_pin;
//     g_cfg.rx_pin = rx_pin;
//     g_cfg.baud_rate = baud_rate;
//     xTaskCreate(ml307_gnss_at_test_task, "ml307_gnss_at_test", 4096 * 2, &g_cfg, 5, NULL);
// }

#include "ml307_gnss_at_test.h"

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#define ML307_AT_UART UART_NUM_1
#define ML307_AT_RX_BUF_SIZE 4096

typedef struct {
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    int baud_rate;
} ml307_gnss_at_test_config_t;

static ml307_gnss_at_test_config_t g_cfg;

static bool response_complete(const char *buf, size_t len) {
    if (len == 0 || buf == NULL) {
        return false;
    }
    if (strstr(buf, "\r\nOK\r\n") != NULL) {
        return true;
    }
    if (strstr(buf, "\r\nERROR\r\n") != NULL) {
        return true;
    }
    if (strstr(buf, "+CME ERROR:") != NULL) {
        return true;
    }
    return false;
}

static void send_cmd_and_print(const char *cmd, int timeout_ms) {
    char resp[1024];
    size_t total = 0;
    resp[0] = '\0';

    uart_flush_input(ML307_AT_UART);
    uart_write_bytes(ML307_AT_UART, cmd, strlen(cmd));

    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        int len = uart_read_bytes(ML307_AT_UART, (uint8_t *)&resp[total],
                                  sizeof(resp) - 1 - total, pdMS_TO_TICKS(200));
        if (len > 0) {
            total += (size_t)len;
            resp[total] = '\0';
            if (response_complete(resp, total)) {
                break;
            }
        }
    }

    printf("AT[%s] ->\n%s\n", cmd, resp[0] ? resp : "(no response)");
}

static void stream_and_print_lines(void) {
    char line[256];
    size_t line_len = 0;
    uint8_t rx_buf[128];

    while (true) {
        int len = uart_read_bytes(ML307_AT_UART, rx_buf, sizeof(rx_buf), pdMS_TO_TICKS(200));
        if (len <= 0) {
            continue;
        }
        for (int i = 0; i < len; ++i) {
            char ch = (char)rx_buf[i];
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    printf("RX: %s\n", line);
                    line_len = 0;
                }
                continue;
            }
            if (line_len < sizeof(line) - 1) {
                line[line_len++] = ch;
            } else {
                line_len = 0;
            }
        }
    }
}

static void ml307_gnss_at_test_task(void *arg) {
    const ml307_gnss_at_test_config_t *cfg = (const ml307_gnss_at_test_config_t *)arg;
    if (cfg == NULL) {
        vTaskDelete(NULL);
        return;
    }

    if (!uart_is_driver_installed(ML307_AT_UART)) {
        uart_config_t uart_config = {
            .baud_rate = cfg->baud_rate,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT,
        };
        uart_driver_install(ML307_AT_UART, ML307_AT_RX_BUF_SIZE, 0, 0, NULL, 0);
        uart_param_config(ML307_AT_UART, &uart_config);
        uart_set_pin(ML307_AT_UART, cfg->tx_pin, cfg->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }

    send_cmd_and_print("AT+MGNSSCFG=\"nmea/mask\",63\r\n", 2000);
    vTaskDelay(pdMS_TO_TICKS(200));
    send_cmd_and_print("AT+MGNSSLOC=1\r\n", 2000);
    vTaskDelay(pdMS_TO_TICKS(200));
    send_cmd_and_print("AT+MGNSS=1\r\n", 2000);

    stream_and_print_lines();
}

void ml307_gnss_at_test_start(gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate) {
    g_cfg.tx_pin = tx_pin;
    g_cfg.rx_pin = rx_pin;
    g_cfg.baud_rate = baud_rate;
    xTaskCreate(ml307_gnss_at_test_task, "ml307_gnss_at_test", 4096, &g_cfg, 5, NULL);
}


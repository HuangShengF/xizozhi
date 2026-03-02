#ifndef ML307_GNSS_AT_TEST_H
#define ML307_GNSS_AT_TEST_H

#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

void ml307_gnss_at_test_start(gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate);

#ifdef __cplusplus
}
#endif

#endif // ML307_GNSS_AT_TEST_H

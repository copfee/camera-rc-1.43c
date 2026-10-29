#pragma once
#include "esp_err.h"
#include <cstdint>
using gpio_num_t = int;
constexpr gpio_num_t GPIO_NUM_8 = 8;
constexpr int GPIO_MODE_INPUT = 1, GPIO_INTR_POSEDGE = 1;
struct gpio_config_t {
    uint64_t pin_bit_mask{};
    int mode{}, intr_type{};
};
esp_err_t gpio_config(const gpio_config_t* config);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*handler)(void*), void* context);

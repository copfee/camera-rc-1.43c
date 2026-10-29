#pragma once
#include "esp_err.h"
void mock_error_check(esp_err_t result);
#define ESP_ERROR_CHECK(result) mock_error_check(result)

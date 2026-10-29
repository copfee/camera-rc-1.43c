#pragma once
void mock_log(bool warning, const char* tag, const char* format, ...);
#define ESP_LOGI(...) mock_log(false, __VA_ARGS__)
#define ESP_LOGW(...) mock_log(true, __VA_ARGS__)

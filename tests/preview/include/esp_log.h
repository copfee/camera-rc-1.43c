#pragma once
#include <cstdio>

template <class... Args>
inline void preview_log(const char* level, const char* tag, const char* format, Args... args) {
    std::printf("%s %s: ", level, tag);
    if constexpr (sizeof...(args) == 0)
        std::printf("%s", format);
    else
        std::printf(format, args...);
    std::printf("\n");
}
#define ESP_LOGI(...) preview_log("I", __VA_ARGS__)
#define ESP_LOGW(...) preview_log("W", __VA_ARGS__)

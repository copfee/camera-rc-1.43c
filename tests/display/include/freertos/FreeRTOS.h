#pragma once
#include <cstdint>
using BaseType_t = int;
using TickType_t = uint32_t;
constexpr BaseType_t pdFALSE = 0, pdTRUE = 1;
#define pdMS_TO_TICKS(ms) (ms)
void mock_yield();
#define portYIELD_FROM_ISR() mock_yield()

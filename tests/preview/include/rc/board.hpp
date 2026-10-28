#pragma once
#include "lvgl.h"
#include <cstdint>
namespace rc {
struct TouchSample {
    bool pressed{false};
    bool error{false};
    int x{0}, y{0};
};
struct BatterySample {
    bool valid{false};
    int millivolts{0};
    bool charging{false};
};
class Board {
  public:
    inline static TouchSample sample{};
    inline static unsigned shutter_sounds{0};
    void shutter_sound() { ++shutter_sounds; }
    TouchSample touch() { return sample; }
    void brightness(unsigned) {}
    static uint64_t milliseconds() { return 1000; }
};
} // namespace rc

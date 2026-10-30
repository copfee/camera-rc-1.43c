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
    inline static unsigned volume{1}, level{70}, applied_brightness{70}, saved_brightness{70};
    inline static bool fullscreen_value{false};
    bool fullscreen() const { return fullscreen_value; }
    void set_fullscreen(bool enabled) { fullscreen_value = enabled; }
    unsigned volume_level() const { return volume; }
    unsigned display_brightness() const { return level; }
    void set_volume_level(unsigned value) { volume = value; }
    void set_display_brightness(unsigned value, bool save) {
        level = value;
        if (save)
            saved_brightness = value;
    }
    void brightness(unsigned value) { applied_brightness = value; }
    static uint64_t milliseconds() { return 1000 + lv_tick_get(); }
};
} // namespace rc

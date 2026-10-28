#pragma once
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "rc/shutter_audio.hpp"

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

// Waveshare 1.43C only. Pins and CO5300 init sequence are intentionally board-local.
class Board {
  public:
    void init();
    lv_display_t* display() const { return display_; }
    TouchSample touch();
    BatterySample battery();
    bool boot_pressed() const;
    void brightness(unsigned percent);
    void shutter_sound() { audio_.play(); }
    static uint64_t milliseconds();

  private:
    void init_panel();
    void init_touch();
    void init_battery();
    static void flush(lv_display_t*, const lv_area_t*, uint8_t*);
    static bool transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*);
    lv_display_t* display_{nullptr};
    esp_lcd_panel_io_handle_t io_{nullptr};
    esp_lcd_panel_handle_t panel_{nullptr};
    i2c_master_bus_handle_t bus_{nullptr};
    i2c_master_dev_handle_t touch_{nullptr};
    adc_oneshot_unit_handle_t adc_{nullptr};
    adc_cali_handle_t calibration_{nullptr};
    ShutterAudio audio_;
};
} // namespace rc

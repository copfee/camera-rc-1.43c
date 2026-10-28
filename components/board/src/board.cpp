// Pin mapping and display sequence adapted from Waveshare's Apache-2.0 example.
// Upstream 8cea11ddae6167dd591d1f5465fd7b615080360f; see THIRD_PARTY_NOTICES.md.
#include "rc/board.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

namespace rc {
namespace {
constexpr int kResolution = 466;
constexpr int kRows = 40;
constexpr gpio_num_t kBoot = GPIO_NUM_0, kCharge = GPIO_NUM_7;
constexpr gpio_num_t kTouchReset = GPIO_NUM_16;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_17;
constexpr char kTag[] = "board";
uint32_t tick_ms() {
    return static_cast<uint32_t>(Board::milliseconds());
}
void round_area(lv_event_t* e) {
    auto* area = static_cast<lv_area_t*>(lv_event_get_param(e));
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 = std::min<int32_t>(area->x2 | 1, kResolution - 1);
    area->y2 = std::min<int32_t>(area->y2 | 1, kResolution - 1);
}
} // namespace

uint64_t Board::milliseconds() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}

void Board::init() {
    ESP_LOGI(kTag, "初始化板卡：Waveshare ESP32-S3-Touch-AMOLED-1.43C");
    gpio_config_t input{};
    input.pin_bit_mask = (1ULL << kBoot) | (1ULL << kCharge);
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&input));
    // ES8311/ES7210 share the touch I2C lines. Keep their supply on so
    // unpowered codec pins cannot clamp the bus; audio enables PA only during playback.
    gpio_config_t output{};
    output.pin_bit_mask = (1ULL << GPIO_NUM_18) | (1ULL << GPIO_NUM_46);
    output.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&output));
    gpio_set_level(GPIO_NUM_18, 1);
    gpio_set_level(GPIO_NUM_46, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(kTag, "GPIO 已就绪：BOOT=0 充电检测=7；共享 I2C 音频芯片电源已开启（18），功放已关闭（46）");
    init_touch();
    audio_.init(bus_); // Optional feedback failure must not prevent display or input startup.
    ESP_LOGI(kTag, "初始化电池 ADC");
    init_battery();
    lv_init();
    lv_tick_set_cb(tick_ms);
    init_panel();
    brightness(70);
    ESP_LOGI(kTag, "板卡初始化完成");
}

void Board::init_panel() {
    // CO5300 uses the compatible SH8601 QSPI transport with this exact vendor sequence.
    static const uint8_t p00[] = {0x00}, p80[] = {0x80}, p55[] = {0x55}, p20[] = {0x20};
    static const uint8_t pff[] = {0xff}, pc0[] = {0xc0};
    static const uint8_t cols[] = {0x00, 0x06, 0x01, 0xd7}, rows[] = {0x00, 0x00, 0x01, 0xd1};
    static const sh8601_lcd_init_cmd_t commands[] = {
        {0xfe, p00, 1, 0},  {0xc4, p80, 1, 0},  {0x3a, p55, 1, 0},   {0x35, p00, 1, 0},
        {0x53, p20, 1, 0},  {0x51, pff, 1, 0},  {0x36, pc0, 1, 0},   {0x63, pff, 1, 0},
        {0x2a, cols, 4, 0}, {0x2b, rows, 4, 0}, {0x11, p00, 0, 100}, {0x29, p00, 0, 0},
    };
    spi_bus_config_t bus{};
    bus.sclk_io_num = 14;
    bus.data0_io_num = 9;
    bus.data1_io_num = 10;
    bus.data2_io_num = 11;
    bus.data3_io_num = 12;
    bus.max_transfer_sz = kResolution * kRows * 2;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = 15;
    io.dc_gpio_num = -1;
    io.spi_mode = 0;
    io.pclk_hz = 40000000;
    io.trans_queue_depth = 2;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    io.on_color_trans_done = transfer_done;
    io.user_ctx = this;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &io_));
    sh8601_vendor_config_t vendor{};
    vendor.init_cmds = commands;
    vendor.init_cmds_size = sizeof(commands) / sizeof(commands[0]);
    vendor.flags.use_qspi_interface = true;
    esp_lcd_panel_dev_config_t panel{};
    panel.reset_gpio_num = 13;
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel.bits_per_pixel = 16;
    panel.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_, &panel, &panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_, 8, 0));
    display_ = lv_display_create(kResolution, kResolution);
    assert(display_);
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    constexpr size_t bytes = kResolution * kRows * 2;
    void* a = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void* b = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(a && b);
    lv_display_set_buffers(display_, a, b, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display_, flush);
    lv_display_add_event_cb(display_, round_area, LV_EVENT_INVALIDATE_AREA, nullptr);
}

void Board::flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    auto* board = static_cast<Board*>(lv_display_get_user_data(display));
    const auto count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    lv_draw_sw_rgb565_swap(pixels, count);
    const auto result =
        esp_lcd_panel_draw_bitmap(board->panel_, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
    if (result != ESP_OK) {
        lv_display_flush_ready(display);
    }
}

bool Board::transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* context) {
    auto* board = static_cast<Board*>(context);
    if (board->display_)
        lv_display_flush_ready(board->display_);
    return false;
}

void Board::brightness(unsigned percent) {
    const uint8_t value = static_cast<uint8_t>(std::min(percent, 100U) * 255 / 100);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_io_tx_param(io_, (0x02U << 24) | (0x51U << 8), &value, 1));
}

void Board::init_touch() {
    i2c_master_bus_config_t config{};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = GPIO_NUM_47;
    config.scl_io_num = GPIO_NUM_48;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus_));
    i2c_device_config_t device{};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x15;
    device.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_, &device, &touch_));
    gpio_config_t reset{};
    reset.pin_bit_mask = 1ULL << kTouchReset;
    reset.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&reset));
    gpio_config_t interrupt{};
    interrupt.pin_bit_mask = 1ULL << kTouchInterrupt;
    interrupt.mode = GPIO_MODE_INPUT;
    interrupt.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&interrupt));
    gpio_set_level(kTouchReset, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(kTouchReset, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(kTouchReset, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    // CST8xx DisAutoSleep: keep the controller readable while polling, including
    // finger-up samples. Standby otherwise makes idle I2C reads unreliable.
    const uint8_t awake[] = {0xfe, 0xff};
    i2c_master_transmit(touch_, awake, sizeof(awake), 50);
}

TouchSample Board::touch() {
    // One transaction keeps finger count and coordinates from the same sample.
    const uint8_t reg = 0x02;
    uint8_t data[5]{};
    const auto result = i2c_master_transmit_receive(touch_, &reg, 1, data, sizeof(data), 10);
    if (result != ESP_OK)
        return {false, true, 0, 0};
    const bool down = (data[0] & 0x0f) != 0 && (data[1] >> 6) != 1;
    const int x = ((data[1] & 0x0f) << 8) | data[2];
    const int y = ((data[3] & 0x0f) << 8) | data[4];
    if (down && (x >= kResolution || y >= kResolution))
        return {false, true, 0, 0};
    return {down, false, kResolution - 1 - x, kResolution - 1 - y};
}

void Board::init_battery() {
    adc_oneshot_unit_init_cfg_t config{};
    config.unit_id = ADC_UNIT_1;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&config, &adc_));
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_, ADC_CHANNEL_3, &channel));
    adc_cali_curve_fitting_config_t calibration{};
    calibration.unit_id = ADC_UNIT_1;
    calibration.chan = ADC_CHANNEL_3;
    calibration.atten = ADC_ATTEN_DB_12;
    calibration.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&calibration, &calibration_) != ESP_OK)
        ESP_LOGW(kTag, "ADC 校准不可用；电池电压将显示未知");
    else
        ESP_LOGI(kTag, "电池 ADC 已就绪：GPIO4 ADC1 通道3，已校准，分压倍率x2");
}

BatterySample Board::battery() {
    BatterySample result;
    result.charging = gpio_get_level(kCharge) == 0;
    if (!calibration_)
        return result;
    int sum = 0;
    for (unsigned i = 0; i < 16; ++i) {
        int raw = 0, mv = 0;
        if (adc_oneshot_read(adc_, ADC_CHANNEL_3, &raw) != ESP_OK ||
            adc_cali_raw_to_voltage(calibration_, raw, &mv) != ESP_OK)
            return result;
        sum += mv;
    }
    result.millivolts = (sum / 16) * 2;
    result.valid = result.millivolts >= 2500 && result.millivolts <= 4350;
    return result;
}

bool Board::boot_pressed() const {
    return gpio_get_level(kBoot) == 0;
}
} // namespace rc

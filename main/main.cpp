#include "esp_log.h"
#include "nvs_flash.h"
#include "rc/board.hpp"
#include "rc/runtime.hpp"
#include "rc/ui.hpp"

extern "C" void app_main() {
    constexpr const char* tag = "startup";
    ESP_LOGI(tag, "启动开始；初始化 NVS，保留已存数据");
    // Do not auto-erase NVS on failure: that would silently delete BLE bonds.
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(tag, "NVS 已就绪；初始化板卡");
    static rc::Board board;
    board.init();
    ESP_LOGI(tag, "板卡已就绪；启动相机主控任务");
    rc::runtime_start(board);
    rc::ui_start(board);
    ESP_LOGI(tag, "启动完成");
}

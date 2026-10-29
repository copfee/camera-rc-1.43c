#include "rc/display_sync.hpp"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace rc {
namespace {
constexpr gpio_num_t kTe = GPIO_NUM_8;
constexpr uint32_t kTeWaitMs = 25, kTeRetryMs = 1000, kTransferWaitMs = 100;
constexpr char kTag[] = "display_sync";
uint64_t now_ms() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000;
}
} // namespace

void DisplaySync::init() {
    te_ = xSemaphoreCreateBinaryStatic(&te_storage_);
    transfer_ = xSemaphoreCreateBinaryStatic(&transfer_storage_);
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << kTe;
    config.mode = GPIO_MODE_INPUT;
    // CO5300 35h/00h: high during vertical blanking; wait for its leading edge.
    config.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&config));
    const auto result = gpio_install_isr_service(0);
    if (result != ESP_ERR_INVALID_STATE)
        ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(gpio_isr_handler_add(kTe, te_edge, this));
    ESP_LOGI(kTag, "TE 同步已启用：GPIO8 上升沿，首块等待，超时 25ms");
}

void DisplaySync::te_edge(void* context) {
    auto* sync = static_cast<DisplaySync*>(context);
    BaseType_t wake = pdFALSE;
    // A full binary semaphore just coalesces old edges. No LVGL or panel access in ISR.
    xSemaphoreGiveFromISR(sync->te_, &wake);
    if (wake)
        portYIELD_FROM_ISR();
}

void DisplaySync::wait_te() {
    if (now_ms() < retry_after_ms_)
        return;
    // Never start a frame using an edge left over from an idle or previous frame.
    xSemaphoreTake(te_, 0);
    if (xSemaphoreTake(te_, pdMS_TO_TICKS(kTeWaitMs)) == pdTRUE) {
        if (!te_synchronized_)
            ESP_LOGI(kTag, "已捕获新 TE 边沿，按面板节拍刷新");
        te_synchronized_ = true;
        te_timeout_reported_ = false;
        return;
    }
    if (!te_timeout_reported_)
        ESP_LOGW(kTag, "TE 等待超时，暂用异步刷新，1 秒后重试同步");
    te_synchronized_ = false;
    te_timeout_reported_ = true;
    retry_after_ms_ = now_ms() + kTeRetryMs;
}

void DisplaySync::begin_flush(bool last_chunk) {
    wait_transfer();
    if (first_chunk_)
        wait_te();
    first_chunk_ = last_chunk;
    xSemaphoreTake(transfer_, 0);
    transfer_pending_ = true; // Set before submission: completion may arrive immediately.
}

void DisplaySync::submission_failed() {
    transfer_pending_ = false; // esp_lcd did not enqueue a color transaction.
}

void DisplaySync::wait_transfer() {
    if (!transfer_pending_)
        return;
    // A DMA timeout must never release/reuse memory still owned by the peripheral.
    ESP_ERROR_CHECK(xSemaphoreTake(transfer_, pdMS_TO_TICKS(kTransferWaitMs)) == pdTRUE ? ESP_OK
                                                                                        : ESP_ERR_TIMEOUT);
    transfer_pending_ = false;
}

bool DisplaySync::transfer_done_from_isr() {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transfer_, &wake);
    return wake == pdTRUE;
}
} // namespace rc

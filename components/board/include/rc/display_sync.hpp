#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>

namespace rc {
// Board-local TE pacing and DMA ownership. Only ISR signal methods run outside the UI task.
class DisplaySync {
  public:
    void init();
    void begin_flush(bool last_chunk);
    void submission_failed();
    void wait_transfer();
    bool transfer_done_from_isr();

  private:
    static void te_edge(void* context);
    void wait_te();
    StaticSemaphore_t te_storage_{}, transfer_storage_{};
    SemaphoreHandle_t te_{}, transfer_{};
    bool first_chunk_{true}, transfer_pending_{false};
    bool te_synchronized_{false}, te_timeout_reported_{false};
    uint64_t retry_after_ms_{0};
};
} // namespace rc

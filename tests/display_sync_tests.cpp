#include "driver/gpio.h"
#include "rc/display_sync.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int64_t clock_us{};
SemaphoreHandle_t te{}, dma{}, waiting{};
void (*te_isr)(void*){};
void* te_context{};
unsigned te_waits{}, dma_waits{}, warnings{}, yields{};
bool emit_te{true}, emit_dma{false};
gpio_config_t configured{};
struct DriverFailure {
    esp_err_t code;
};
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Fixture {
    rc::DisplaySync sync;
    Fixture() {
        clock_us = 0;
        te = dma = waiting = nullptr;
        te_waits = dma_waits = warnings = yields = 0;
        emit_te = true;
        emit_dma = false;
        sync.init();
    }
    void complete() {
        sync.transfer_done_from_isr();
        sync.wait_transfer();
    }
};
} // namespace

SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) {
    storage->available = false;
    (te ? dma : te) = storage;
    return storage;
}
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* wake) {
    if (semaphore->available)
        return pdFALSE; // Binary queue full: old TE edges may be coalesced.
    semaphore->available = true;
    *wake = waiting == semaphore ? pdTRUE : pdFALSE;
    return pdTRUE;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) {
    if (!semaphore->available && timeout) {
        waiting = semaphore;
        if (semaphore == te) {
            ++te_waits;
            clock_us += emit_te ? 2000 : timeout * 1000;
            if (emit_te)
                te_isr(te_context);
        } else {
            ++dma_waits;
            clock_us += emit_dma ? 1000 : timeout * 1000;
            if (emit_dma) {
                BaseType_t wake = pdFALSE;
                xSemaphoreGiveFromISR(dma, &wake);
            }
        }
        waiting = nullptr;
    }
    if (!semaphore->available)
        return pdFALSE;
    semaphore->available = false;
    return pdTRUE;
}
esp_err_t gpio_config(const gpio_config_t* config) {
    configured = *config;
    return ESP_OK;
}
esp_err_t gpio_install_isr_service(int) {
    return ESP_ERR_INVALID_STATE; // Reuse an ISR service installed by another board device.
}
esp_err_t gpio_isr_handler_add(gpio_num_t pin, void (*handler)(void*), void* context) {
    check(pin == GPIO_NUM_8, "Wrong TE pin");
    te_isr = handler;
    te_context = context;
    return ESP_OK;
}
void mock_yield() {
    ++yields;
}
void mock_error_check(esp_err_t result) {
    if (result != ESP_OK)
        throw DriverFailure{result};
}
void mock_log(bool warning, const char*, const char*, ...) {
    if (warning)
        ++warnings;
}
int64_t esp_timer_get_time() {
    return clock_us;
}

int main() {
    try {
        {
            Fixture f;
            check(configured.pin_bit_mask == (1ULL << 8) && configured.mode == GPIO_MODE_INPUT &&
                      configured.intr_type == GPIO_INTR_POSEDGE,
                  "TE must use GPIO8 rising edge");
            f.sync.begin_flush(false);
            f.complete();
            f.sync.begin_flush(false);
            f.complete();
            f.sync.begin_flush(true);
            f.complete();
            check(te_waits == 1, "Must not wait for TE between chunks of the same frame");
            f.sync.begin_flush(true);
            f.complete();
            check(te_waits == 2 && yields == 2, "Next frame must wait for a fresh edge");
            check(dma_waits == 0, "Early DMA completion must be retained");
        }
        {
            Fixture f;
            te_isr(te_context);
            te_isr(te_context); // Full semaphore is harmless, but neither edge is fresh.
            emit_te = false;
            f.sync.begin_flush(true);
            f.sync.submission_failed();
            check(te_waits == 1 && clock_us == 25000 && warnings == 1,
                  "Stale edge must be discarded; missing TE needs a bounded wait");
            for (int frame = 0; frame < 10; ++frame) {
                f.sync.begin_flush(true);
                f.sync.submission_failed();
            }
            check(te_waits == 1 && warnings == 1, "Missing TE must back off without log flooding");
            clock_us += 1000000;
            f.sync.begin_flush(true);
            f.sync.submission_failed();
            check(te_waits == 2 && warnings == 1, "Persistent TE failure must remain bounded");
            emit_te = true;
            clock_us += 1000000;
            f.sync.begin_flush(true);
            f.complete();
            check(te_waits == 3, "TE synchronization must recover automatically");
        }
        {
            Fixture f;
            f.sync.begin_flush(false);
            f.sync.submission_failed();
            f.sync.wait_transfer();
            check(dma_waits == 0, "Failed submission must not wait for a nonexistent DMA");
            f.sync.begin_flush(true);
            emit_dma = true;
            f.sync.wait_transfer();
            check(dma_waits == 1 && te_waits == 1, "Deferred DMA completion must unblock the UI wait");
        }
        {
            Fixture f;
            f.sync.begin_flush(false);
            emit_dma = true;
            f.sync.begin_flush(true);
            check(dma_waits == 1 && te_waits == 1, "Next chunk must wait until DMA releases its buffer");
            f.complete();
        }
        {
            Fixture f;
            f.sync.begin_flush(true);
            te_isr(te_context); // TE and DMA signals must never be interchangeable.
            const auto before_dma_wait = clock_us;
            bool timed_out = false;
            try {
                f.sync.wait_transfer();
            } catch (const DriverFailure& failure) {
                timed_out = failure.code == ESP_ERR_TIMEOUT;
            }
            check(timed_out && clock_us - before_dma_wait == 100000,
                  "DMA must fail within 100ms without releasing its owned buffer");
            const auto previous_te_waits = te_waits;
            timed_out = false;
            try {
                f.sync.begin_flush(true);
            } catch (const DriverFailure& failure) {
                timed_out = failure.code == ESP_ERR_TIMEOUT;
            }
            check(timed_out && te_waits == previous_te_waits,
                  "DMA timeout must retain ownership and prevent a new frame");
        }
        std::cout << "TE frame pacing, stale/missing edges, recovery and DMA ownership passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    } catch (const DriverFailure& failure) {
        std::cerr << "Unexpected driver failure: " << failure.code << '\n';
        return 1;
    }
    return 0;
}

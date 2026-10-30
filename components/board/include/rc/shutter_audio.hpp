#pragma once
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rc/audio_levels.hpp"
#include <atomic>

namespace rc {
// Board-lifetime service. Only the audio task writes PCM; callers never wait.
class ShutterAudio {
  public:
    bool init(i2c_master_bus_handle_t bus);
    void play();
    void volume(unsigned value) { volume_.store(value); }

  private:
    static void worker(void* argument);
    void release();
    TaskHandle_t task_{nullptr};
    std::atomic<bool> busy_{false};
    std::atomic<unsigned> volume_{kShutterVolumeLevels[1]};
    i2s_chan_handle_t tx_{nullptr};
    const audio_codec_ctrl_if_t* control_{nullptr};
    const audio_codec_data_if_t* data_{nullptr};
    const audio_codec_gpio_if_t* gpio_{nullptr};
    const audio_codec_if_t* codec_{nullptr};
    esp_codec_dev_handle_t device_{nullptr};
};
} // namespace rc

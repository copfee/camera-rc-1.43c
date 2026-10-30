// Audio wiring from Waveshare 1.43C, commit 8cea11ddae6167dd591d1f5465fd7b615080360f.
// See THIRD_PARTY_NOTICES.md. Codec setup follows the ESP-IDF ES8311 example.
#include "rc/shutter_audio.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "rc/shutter_sound.hpp"
#include <algorithm>
#include <array>

namespace rc {
namespace {
constexpr char kTag[] = "audio";
constexpr gpio_num_t kAmplifier = GPIO_NUM_46;
constexpr size_t kChunkFrames = 160; // 10 ms per bounded I2S write.
} // namespace

bool ShutterAudio::init(i2c_master_bus_handle_t bus) {
    i2s_chan_config_t config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    config.auto_clear = true;
    config.dma_desc_num = 4;
    config.dma_frame_num = kChunkFrames;
    auto result = i2s_new_channel(&config, &tx_, nullptr);
    if (result != ESP_OK)
        goto failed;
    {
        i2s_std_config_t standard{};
        standard.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kShutterSampleRate);
        standard.slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        standard.gpio_cfg.mclk = GPIO_NUM_38;
        standard.gpio_cfg.bclk = GPIO_NUM_39;
        standard.gpio_cfg.ws = GPIO_NUM_40;
        standard.gpio_cfg.dout = GPIO_NUM_41;
        standard.gpio_cfg.din = I2S_GPIO_UNUSED;
        result = i2s_channel_init_std_mode(tx_, &standard);
    }
    if (result != ESP_OK)
        goto failed;
    {
        audio_codec_i2c_cfg_t i2c{};
        i2c.port = I2C_NUM_0;
        i2c.addr = ES8311_CODEC_DEFAULT_ADDR;
        i2c.bus_handle = bus; // Reuse the existing touch bus; never reset or delete it.
        control_ = audio_codec_new_i2c_ctrl(&i2c);
        audio_codec_i2s_cfg_t i2s{};
        i2s.port = I2S_NUM_0;
        i2s.tx_handle = tx_;
        data_ = audio_codec_new_i2s_data(&i2s);
        gpio_ = audio_codec_new_gpio();
        if (!control_ || !data_ || !gpio_)
            goto failed;
        es8311_codec_cfg_t codec{};
        codec.ctrl_if = control_;
        codec.gpio_if = gpio_;
        codec.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
        codec.use_mclk = true;
        codec.pa_pin = -1;            // Task controls PA, keeping it off between clicks.
        codec.hw_gain.pa_gain = 6.0f; // Board vendor's amplifier gain.
        codec_ = es8311_codec_new(&codec);
        if (!codec_)
            goto failed;
        esp_codec_dev_cfg_t device{};
        device.dev_type = ESP_CODEC_DEV_TYPE_OUT;
        device.codec_if = codec_;
        device.data_if = data_;
        device_ = esp_codec_dev_new(&device);
        if (!device_)
            goto failed;
        esp_codec_dev_sample_info_t sample{};
        sample.sample_rate = kShutterSampleRate;
        sample.channel = 2;
        sample.bits_per_sample = 16;
        if (esp_codec_dev_open(device_, &sample) != ESP_CODEC_DEV_OK ||
            esp_codec_dev_set_out_vol(device_, kShutterVolumeLevels[1]) != ESP_CODEC_DEV_OK)
            goto failed;
    }
    if (xTaskCreate(worker, "shutter_audio", 4096, this, 3, &task_) != pdPASS)
        goto failed;
    ESP_LOGI(kTag, "快门音效已就绪：ES8311，16kHz；MCLK=38 BCLK=39 WS=40 DOUT=41 PA=46");
    return true;

failed:
    ESP_LOGW(kTag, "音频初始化失败（最后一次 I2S 返回码=%s）；已停用音效", esp_err_to_name(result));
    release();
    return false;
}

void ShutterAudio::release() {
    gpio_set_level(kAmplifier, 0);
    if (device_) {
        esp_codec_dev_close(device_);
        esp_codec_dev_delete(device_);
        device_ = nullptr;
    }
    if (codec_) {
        audio_codec_delete_codec_if(codec_);
        codec_ = nullptr;
    }
    if (data_) {
        audio_codec_delete_data_if(data_);
        data_ = nullptr;
    }
    if (control_) {
        audio_codec_delete_ctrl_if(control_);
        control_ = nullptr;
    }
    if (gpio_) {
        audio_codec_delete_gpio_if(gpio_);
        gpio_ = nullptr;
    }
    if (tx_) {
        i2s_channel_disable(tx_);
        i2s_del_channel(tx_);
        tx_ = nullptr;
    }
}

void ShutterAudio::play() {
    // One in-flight effect, with no backlog. Touch/BOOT may call concurrently.
    if (task_ && !busy_.exchange(true))
        xTaskNotifyGive(task_);
}

void ShutterAudio::worker(void* argument) {
    auto& audio = *static_cast<ShutterAudio*>(argument);
    std::array<int16_t, kChunkFrames * 2> pcm{};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const auto volume = audio.volume_.load();
        if (volume == 0) {
            audio.busy_.store(false);
            continue;
        }
        if (esp_codec_dev_set_out_vol(audio.device_, volume) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(kTag, "设置音量失败");
            audio.busy_.store(false);
            continue;
        }
        gpio_set_level(kAmplifier, 1);
        vTaskDelay(pdMS_TO_TICKS(5));
        bool played = true;
        for (size_t frame = 0; frame < kShutterFrames; frame += kChunkFrames) {
            const size_t frames = std::min(kChunkFrames, kShutterFrames - frame);
            for (size_t i = 0; i < frames; ++i)
                pcm[i * 2] = pcm[i * 2 + 1] = kShutterPcm[frame + i];
            const size_t bytes = frames * 2 * sizeof(int16_t);
            size_t written = 0;
            const auto result = i2s_channel_write(audio.tx_, pcm.data(), bytes, &written, 40);
            if (result != ESP_OK || written != bytes) {
                ESP_LOGW(kTag, "快门音效写入失败：%s 字节数=%u/%u", esp_err_to_name(result),
                         static_cast<unsigned>(written), static_cast<unsigned>(bytes));
                played = false;
                break;
            }
        }
        // Account for the bounded DMA tail before switching the amplifier off.
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(kAmplifier, 0);
        // Never replay a partial effect or accept more requests after an I2S fault.
        if (!played) {
            i2s_channel_disable(audio.tx_);
            ESP_LOGE(kTag, "音频写入失败；重启前停用音效");
            vTaskSuspend(nullptr);
        }
        audio.busy_.store(false);
    }
}
} // namespace rc

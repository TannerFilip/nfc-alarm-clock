#include "audio.hpp"
#include "board.hpp"
#include "core/test_tone.hpp"
#include "esp_log.h"
#include "freertos/task.h"
#include <algorithm>
#include <iterator>
namespace clock_hw {
esp_err_t Audio::init() {
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    cfg.dma_desc_num = 4; cfg.dma_frame_num = 256;
    cfg.auto_clear = true;
    auto err = i2s_new_channel(&cfg,&channel_,nullptr);
    if (err != ESP_OK) return err;
    i2s_std_config_t std_cfg{};
    std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(clock_core::test_sample_rate);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED; std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = bclk; std_cfg.gpio_cfg.ws = lrclk; std_cfg.gpio_cfg.dout = audio_data;
    err = i2s_channel_init_std_mode(channel_,&std_cfg);
    if (err != ESP_OK) return err;
    queue_ = xQueueCreate(1,sizeof(uint8_t));
    if (!queue_) return ESP_ERR_NO_MEM;
    if (xTaskCreate(entry,"clock_audio",4096,this,5,nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    ready_ = true;
    return ESP_OK;
}
bool Audio::request_test(uint8_t level) {
    if (level != 1 && level != 5) return false;
    bool expected = false;
    if (!ready_ || !busy_.compare_exchange_strong(expected,true)) return false;
    if (xQueueSend(queue_,&level,0) != pdTRUE) { busy_ = false; return false; }
    return true;
}
void Audio::entry(void* arg) { static_cast<Audio*>(arg)->run(); }
void Audio::run() {
    uint8_t level;
    while (true) {
        if (xQueueReceive(queue_,&level,portMAX_DELAY) != pdTRUE) continue;
        // Fixed two-second 440 Hz test, explicit 1% or 5%, 100 ms fade in/out.
        // Duplicate samples on both channels for breakout's default mono mix.
        ESP_LOGI("audio", "starting 2s test: 440 Hz, %u percent digital peak",unsigned(level));
        ESP_LOGI("audio", "configured I2S: Philips stereo 16-bit, LRC=16000 Hz GPIO%d, BCLK=512000 Hz GPIO%d, DIN=GPIO%d (not measured)",int(lrclk),int(bclk),int(audio_data));
        auto err = i2s_channel_enable(channel_);
        int16_t samples[256 * 2]{};
        constexpr int frames = clock_core::test_tone_frames;
        for (int first = 0; err == ESP_OK && first < frames; first += 256) {
            const int count = std::min(256,frames-first);
            for (int j = 0; j < count; ++j) {
                const int16_t sample = clock_core::test_tone_sample(first+j,level);
                samples[j*2] = samples[j*2+1] = sample;
            }
            size_t written = 0;
            const size_t bytes = count * 2 * sizeof(int16_t);
            err = i2s_channel_write(channel_,samples,bytes,&written,100);
            if (err == ESP_OK && written != bytes) err = ESP_FAIL;
        }
        // Flush silence through the DMA pipeline before disabling clocks.
        std::fill(std::begin(samples),std::end(samples),0);
        for (int i = 0; err == ESP_OK && i < 8; ++i) {
            size_t written = 0;
            err = i2s_channel_write(channel_,samples,sizeof(samples),&written,100);
            if (err == ESP_OK && written != sizeof(samples)) err = ESP_FAIL;
        }
        const auto stop = i2s_channel_disable(channel_);
        if (err != ESP_OK || stop != ESP_OK) {
            ready_ = false; // Reboot to retry after a driver fault.
            ESP_LOGE("audio", "test fault: %s / stop %s",esp_err_to_name(err),esp_err_to_name(stop));
        } else ESP_LOGI("audio", "test complete; clocks stopped (SD is not controlled)");
        busy_ = false;
    }
}
}

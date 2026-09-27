#pragma once
#include <atomic>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace clock_hw {
class Audio {
public:
    esp_err_t init();
    bool request_test(uint8_t level = 1); // Only 1% or 5%; no backlog/retrigger while active.
    bool start_alarm(uint8_t maximum_percent = 5);
    bool stop_alarm();
    bool alarm_active() const;
    const char* status() const;
private:
    enum class Mode : uint8_t { idle, test, alarm };
    static void entry(void* arg);
    void run();
    i2s_chan_handle_t channel_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    std::atomic<bool> ready_{false}, stop_requested_{false};
    std::atomic<Mode> mode_{Mode::idle};
};
}

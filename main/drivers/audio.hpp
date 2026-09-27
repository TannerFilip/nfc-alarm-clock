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
    const char* status() const { return !ready_ ? "UNAVAILABLE" : (busy_ ? "TESTING" : "READY/UNVERIFIED"); }
private:
    static void entry(void* arg);
    void run();
    i2s_chan_handle_t channel_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    std::atomic<bool> ready_{false}, busy_{false};
};
}

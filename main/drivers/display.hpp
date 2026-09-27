#pragma once
#include "i2c_bus.hpp"
namespace clock_hw {
class Display {
public:
    explicit Display(I2cBus& bus) : bus_(bus) {}
    void clear();
    void text(int x, int y, const char* value, int scale = 1);
    bool wifi_qr(const char* ssid, const char* password, int x = 0, int y = 0);
    void present() { pending_ = true; }
    void service(int64_t now_ms, uint8_t contrast);
    bool online() const { return online_; }
private:
    bool init();
    void pixel(int x, int y);
    I2cBus& bus_;
    uint8_t drawing_[1024]{}, sending_[1024]{};
    bool online_ = false, pending_ = false, reset_configured_ = false;
    int page_ = 16;
    int contrast_ = -1;
    int64_t retry_at_ = 0;
};
}

#include "display.hpp"
#include "board.hpp"
#include "esp_log.h"
#include "freertos/task.h"
#include <cstring>

namespace clock_hw {
namespace {
// Original compact 3x5 glyphs, stored as rows. Unknown characters become spaces.
struct Glyph { char c; const char* bits; };
constexpr Glyph font[]{
    {'0',"111101101101111"},{'1',"010110010010111"},{'2',"111001111100111"},
    {'3',"111001111001111"},{'4',"101101111001001"},{'5',"111100111001111"},
    {'6',"111100111101111"},{'7',"111001010010010"},{'8',"111101111101111"},
    {'9',"111101111001111"},{':',"000010000010000"},{'.',"000000000000010"},
    {'-',"000000111000000"},{'/',"001001010100100"},{'?',"111001011000010"},
    {'A',"010101111101101"},{'B',"110101110101110"},{'C',"111100100100111"},
    {'D',"110101101101110"},{'E',"111100110100111"},{'F',"111100110100100"},
    {'G',"111100101101111"},{'H',"101101111101101"},{'I',"111010010010111"},
    {'J',"001001001101111"},{'K',"101101110101101"},{'L',"100100100100111"},
    {'M',"101111111101101"},{'N',"101111111111101"},{'O',"111101101101111"},
    {'P',"111101111100100"},{'Q',"111101101111001"},{'R',"110101110101101"},
    {'S',"111100111001111"},{'T',"111010010010010"},{'U',"101101101101111"},
    {'V',"101101101101010"},{'W',"101101111111101"},{'X',"101101010101101"},
    {'Y',"101101010010010"},{'Z',"111001010100111"}
};
}
void Display::clear() { std::memset(drawing_,0,sizeof(drawing_)); }
void Display::pixel(int x, int y) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    // FeatherWing is physically 64 columns x 128 rows, rotated into landscape.
    const int native_x = 63 - y, native_y = x;
    drawing_[(native_y / 8) * 64 + native_x] |= 1U << (native_y % 8);
}
void Display::text(int x, int y, const char* value, int scale) {
    for (; *value; ++value, x += 4 * scale) {
        char c = *value;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        for (const auto& g : font) if (g.c == c) {
            for (int r = 0; r < 5; ++r) for (int col = 0; col < 3; ++col)
                if (g.bits[r*3+col] == '1') for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx) pixel(x+col*scale+dx,y+r*scale+dy);
            break;
        }
    }
}
bool Display::init() {
    // IDF v6.1 reserves output GPIOs on configuration. Reconfiguring our own
    // reset pin on every missing-device retry produces a false conflict warning.
    if (!reset_configured_) {
        gpio_config_t cfg{}; cfg.pin_bit_mask = 1ULL << oled_reset; cfg.mode = GPIO_MODE_OUTPUT;
        const auto err = gpio_config(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE("display", "reset GPIO configuration failed: %s", esp_err_to_name(err));
            return false;
        }
        reset_configured_ = true;
    }
    gpio_set_level(oled_reset,1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(oled_reset,0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(oled_reset,1); vTaskDelay(pdMS_TO_TICKS(10));
    const auto probe = bus_.probe(oled);
    if (probe != ESP_OK) {
        ESP_LOGW("display", "OLED probe 0x%02x after reset: %s; check power, SDA/SCL and reset wiring", oled, esp_err_to_name(probe));
        return false;
    }
    // SH1107 FeatherWing register sequence adapted from Adafruit_SH1107.cpp.
    // See THIRD_PARTY_NOTICES.md. Not an SSD1306 initialization sequence.
    const uint8_t command[]{0x00,0xae,0xd5,0x51,0x20,0x81,0x4f,0xad,0x8a,
        0xa0,0xc0,0xdc,0x00,0xd3,0x60,0xd9,0x22,0xdb,0x35,0xa8,0x3f,0xa4,0xa6};
    if (bus_.write(oled,command,sizeof(command)) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t on[]{0,0xaf};
    if (bus_.write(oled,on,2) != ESP_OK) return false;
    contrast_ = -1; pending_ = true; page_ = 16;
    ESP_LOGI("display", "SH1107 initialized; orientation requires physical verification");
    return true;
}
void Display::service(int64_t now_ms, uint8_t contrast) {
    if (!online_) {
        if (now_ms < retry_at_) return;
        online_ = init(); retry_at_ = now_ms + 5000;
        if (!online_) return;
    }
    bool ok = true;
    if (contrast_ != contrast) {
        const uint8_t command[]{0,0x81,contrast};
        ok = bus_.write(oled,command,3) == ESP_OK;
        if (ok) contrast_ = contrast;
    }
    if (ok && page_ == 16 && pending_) {
        std::memcpy(sending_,drawing_,sizeof(sending_));
        page_ = 0; pending_ = false;
    }
    if (ok && page_ < 16) {
        const uint8_t address[]{0,static_cast<uint8_t>(0xb0 + page_),0x10,0x00};
        uint8_t data[65]{0x40};
        std::memcpy(data+1,sending_+page_*64,64);
        ok = bus_.write(oled,address,4) == ESP_OK && bus_.write(oled,data,sizeof(data)) == ESP_OK;
        if (ok) ++page_;
    }
    if (!ok) { online_ = false; retry_at_ = now_ms + 5000; ESP_LOGW("display", "I/O failed; retry in 5s"); }
}
}

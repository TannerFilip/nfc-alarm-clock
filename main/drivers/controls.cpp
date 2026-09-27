#include "controls.hpp"
#include "board.hpp"
#include <initializer_list>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/task.h"
namespace clock_hw {
namespace {
uint32_t be32(const uint8_t* v) { return uint32_t(v[0]) << 24 | uint32_t(v[1]) << 16 | uint32_t(v[2]) << 8 | v[3]; }
}
esp_err_t Controls::init_buttons() {
    gpio_config_t cfg{};
    cfg.pin_bit_mask = (1ULL << button1) | (1ULL << button2);
    cfg.mode = GPIO_MODE_INPUT; cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    const auto err = gpio_config(&cfg);
    gpio_ready_ = err == ESP_OK;
    return err;
}
bool Controls::read_seesaw(uint8_t base, uint8_t reg, uint8_t* data, size_t size) {
    const uint8_t command[]{base,reg};
    if (bus_.write(encoder, command, 2) != ESP_OK) return false;
    // seesaw needs a STOP + processing delay before the separate read. Bus is
    // unlocked here. All seesaw access remains owned by this peripheral task.
    esp_rom_delay_us(250);
    return bus_.read(encoder, data, size) == ESP_OK;
}
bool Controls::init_encoder() {
    if (bus_.probe(encoder) != ESP_OK) return false;
    uint8_t id = 0, version[4]{};
    if (!read_seesaw(0,1,&id,1) || !read_seesaw(0,2,version,4)) return false;
    if (id != 0x55 && (id < 0x84 || id > 0x89)) return false;
    const unsigned product = be32(version) >> 16;
    // 5880 is the preassembled 4991 breakout; verify the firmware product ID.
    if (product != 4991) { ESP_LOGE("encoder", "unexpected seesaw product %u", product); return false; }
    for (uint8_t reg : {uint8_t(0x03),uint8_t(0x0b),uint8_t(0x05)}) {
        const uint8_t command[]{1,reg,1,0,0,0}; // switch is seesaw GPIO24
        if (bus_.write(encoder,command,sizeof(command)) != ESP_OK) return false;
    }
    uint8_t position[4]{};
    if (!read_seesaw(0x11,0x30,position,4)) return false;
    position_ = be32(position);
    buttons_[2] = {};
    ESP_LOGI("encoder", "seesaw product=%u hardware=0x%02x ready", product,id);
    return true;
}
ControlEvents Controls::poll(int64_t now_ms) {
    ControlEvents events;
    if (gpio_ready_) {
        if (buttons_[0].update(!gpio_get_level(button1),now_ms)) {
            events.button1 = buttons_[0].pressed();
            ESP_LOGI("controls", "button1 %s", events.button1 ? "pressed" : "released");
        }
        if (buttons_[1].update(!gpio_get_level(button2),now_ms)) {
            events.button2 = buttons_[1].pressed();
            ESP_LOGI("controls", "button2 %s", events.button2 ? "pressed" : "released");
        }
    }
    if (!online_) {
        if (now_ms < retry_at_) return events;
        online_ = init_encoder(); retry_at_ = now_ms + 5000;
        if (!online_) return events;
    }
    uint8_t position[4]{}, pins[4]{};
    if (!read_seesaw(0x11,0x30,position,4) || !read_seesaw(1,4,pins,4)) {
        online_ = false; retry_at_ = now_ms + 5000;
        ESP_LOGW("encoder", "read failed; retry in 5s"); return events;
    }
    // Preassembled 5880 encoder is reversed relative to a loose encoder.
    events.turn = -clock_core::encoder_delta(position_,be32(position));
    position_ = be32(position);
    if (buttons_[2].update(!(be32(pins) & (1U << 24)), now_ms)) {
        events.encoder_button = buttons_[2].pressed();
        ESP_LOGI("controls", "encoder button %s", events.encoder_button ? "pressed" : "released");
    }
    return events;
}
}

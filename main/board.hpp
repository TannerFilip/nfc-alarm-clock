#pragma once
#include "driver/gpio.h"
namespace clock_hw {
constexpr gpio_num_t sda = GPIO_NUM_8, scl = GPIO_NUM_9, oled_reset = GPIO_NUM_10;
constexpr gpio_num_t button1 = GPIO_NUM_11, button2 = GPIO_NUM_12;
constexpr gpio_num_t bclk = GPIO_NUM_5, lrclk = GPIO_NUM_6, audio_data = GPIO_NUM_7;
constexpr gpio_num_t nfc_irq = GPIO_NUM_15, nfc_ven = GPIO_NUM_16;
constexpr uint8_t oled = 0x3c, rtc = 0x68, light = 0x23, encoder = 0x36;
// Battery is ADC1 channel 0 (GPIO1). Milestone 4 still leaves NFC IRQ/VEN untouched.
}

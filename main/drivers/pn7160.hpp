#pragma once

#include "core/alarm_core.hpp"
#include "i2c_bus.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace clock_hw {

enum class Pn7160State : uint8_t {
    uninitialized,
    ven_low,
    boot_wait,
    scan_address,
    send_reset,
    wait_reset_response,
    wait_reset_notification,
    send_init,
    wait_init_response,
    send_discover_map,
    wait_discover_map_response,
    send_discover,
    wait_discover_response,
    discovering,
    send_select,
    wait_select_response,
    wait_activation,
    send_deactivate,
    wait_deactivate_response,
    backoff,
};

struct Pn7160Status {
    Pn7160State state = Pn7160State::uninitialized;
    uint8_t address = 0;
    bool irq_high = false;
    bool initialized = false;
    bool discovery_active = false;
    esp_err_t last_error = ESP_OK;
    uint32_t transport_errors = 0;
    uint32_t protocol_errors = 0;
    uint32_t restart_count = 0;
    uint32_t tag_count = 0;
    uint32_t callback_rejections = 0;
    uint32_t suppressed_repeats = 0;
    uint32_t discovery_notifications = 0;
    uint32_t activation_errors = 0;
};

class Pn7160 {
public:
    // Returning false leaves the presentation unlatchable so a full consumer
    // queue can accept it after the next bounded discovery cycle.
    using TagCallback = bool (*)(void* context, const clock_core::TagId& tag);

    esp_err_t init(I2cBus& bus, TagCallback callback, void* callback_context);
    // Advances at most one short GPIO/I2C operation. No service call waits for
    // IRQ, and every I2C transaction has the shared bus's finite deadline.
    void service(uint64_t now_ms);
    Pn7160Status status() const { return status_; }
    static const char* state_name(Pn7160State state);

private:
    static constexpr size_t maximum_nci_frame_size = 258;

    bool irq_high() const;
    bool receive_packet(uint64_t now_ms);
    bool send_frame(const uint8_t* frame, size_t size, Pn7160State waiting_state,
                    uint64_t now_ms);
    void process_packet(const uint8_t* frame, size_t size, uint64_t now_ms);
    void advance_address_or_backoff(uint64_t now_ms, esp_err_t error);
    void fail_and_backoff(uint64_t now_ms, esp_err_t error, bool protocol_error);
    void begin_ven_low(uint64_t now_ms);
    void report_tag(const clock_core::TagId& tag, uint64_t now_ms);

    I2cBus* bus_ = nullptr;
    TagCallback callback_ = nullptr;
    void* callback_context_ = nullptr;
    Pn7160Status status_{};
    std::array<uint8_t, maximum_nci_frame_size> receive_buffer_{};
    std::array<uint8_t, 6> select_command_{};
    clock_core::TagId last_tag_{};
    uint8_t candidate_address_ = 0x28;
    uint8_t consecutive_failures_ = 0;
    uint64_t deadline_ms_ = 0;
    uint64_t last_tag_ms_ = 0;
    bool have_last_tag_ = false;
    bool ven_phase_started_ = false;
    bool have_candidate_ = false;
    uint8_t candidate_discovery_id_ = 0;
    uint8_t candidate_protocol_ = 0;
};

} // namespace clock_hw

#include "pn7160.hpp"

#include "board.hpp"
#include "core/nci_protocol.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include <algorithm>

namespace clock_hw {
namespace {
constexpr const char* tag = "pn7160";
constexpr uint64_t ven_low_ms = 1;       // Datasheet minimum is 10 us.
constexpr uint64_t boot_wait_ms = 5;     // PN7160 Tboot.
constexpr uint64_t command_timeout_ms = 1000;
constexpr uint64_t repeated_tag_interval_ms = 1500;
constexpr uint64_t maximum_backoff_ms = 5000;

bool deadline_reached(uint64_t now_ms, uint64_t deadline_ms) {
    return now_ms >= deadline_ms;
}
}

esp_err_t Pn7160::init(I2cBus& bus, TagCallback callback, void* callback_context) {
    bus_ = &bus;
    callback_ = callback;
    callback_context_ = callback_context;
    status_ = {};
    candidate_address_ = 0x28;
    consecutive_failures_ = 0;
    have_last_tag_ = false;
    ven_phase_started_ = false;
    have_candidate_ = false;

    gpio_config_t irq_config{};
    irq_config.pin_bit_mask = 1ULL << nfc_irq;
    irq_config.mode = GPIO_MODE_INPUT;
    irq_config.pull_up_en = GPIO_PULLUP_DISABLE;
    irq_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irq_config.intr_type = GPIO_INTR_DISABLE;
    auto error = gpio_config(&irq_config);
    if (error != ESP_OK) return error;

    gpio_config_t ven_config{};
    ven_config.pin_bit_mask = 1ULL << nfc_ven;
    ven_config.mode = GPIO_MODE_OUTPUT;
    ven_config.pull_up_en = GPIO_PULLUP_DISABLE;
    ven_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ven_config.intr_type = GPIO_INTR_DISABLE;
    error = gpio_config(&ven_config);
    if (error != ESP_OK) return error;
    error = gpio_set_level(nfc_ven, 0);
    if (error != ESP_OK) return error;

    status_.state = Pn7160State::ven_low;
    return ESP_OK;
}

bool Pn7160::irq_high() const {
    return gpio_get_level(nfc_irq) == 1;
}

void Pn7160::begin_ven_low(uint64_t now_ms) {
    gpio_set_level(nfc_ven, 0);
    status_.address = 0;
    status_.initialized = false;
    status_.discovery_active = false;
    have_candidate_ = false;
    status_.state = Pn7160State::ven_low;
    deadline_ms_ = now_ms + ven_low_ms;
    ven_phase_started_ = true;
}

void Pn7160::advance_address_or_backoff(uint64_t now_ms, esp_err_t error) {
    status_.last_error = error;
    if (candidate_address_ < 0x2b) {
        ++candidate_address_;
        status_.state = Pn7160State::scan_address;
        deadline_ms_ = 0;
        return;
    }
    fail_and_backoff(now_ms, error, false);
}

void Pn7160::fail_and_backoff(uint64_t now_ms, esp_err_t error, bool protocol_error) {
    status_.last_error = error;
    if (protocol_error) ++status_.protocol_errors;
    else ++status_.transport_errors;
    ++status_.restart_count;
    consecutive_failures_ = std::min<uint8_t>(consecutive_failures_ + 1, 6);
    const uint64_t delay = std::min<uint64_t>(100ULL << (consecutive_failures_ - 1),
                                               maximum_backoff_ms);
    gpio_set_level(nfc_ven, 0);
    status_.address = 0;
    status_.initialized = false;
    status_.discovery_active = false;
    have_candidate_ = false;
    status_.state = Pn7160State::backoff;
    deadline_ms_ = now_ms + delay;
    ven_phase_started_ = false;
    ESP_LOGW(tag, "restart after %s error %s; backoff %llu ms",
             protocol_error ? "protocol" : "transport", esp_err_to_name(error),
             static_cast<unsigned long long>(delay));
}

bool Pn7160::send_frame(const uint8_t* frame, size_t size, Pn7160State waiting_state,
                        uint64_t now_ms) {
    // The PN7160 forbids a write while it is requesting a host read.
    if (irq_high()) return false;
    const auto error = bus_->write(candidate_address_, frame, size);
    if (error != ESP_OK) {
        if (status_.address == 0) advance_address_or_backoff(now_ms, error);
        else fail_and_backoff(now_ms, error, false);
        return false;
    }
    status_.state = waiting_state;
    deadline_ms_ = now_ms + command_timeout_ms;
    return true;
}

void Pn7160::report_tag(const clock_core::TagId& seen, uint64_t now_ms) {
    if (have_last_tag_ && clock_core::same_tag(last_tag_, seen) &&
        now_ms - last_tag_ms_ < repeated_tag_interval_ms) {
        ++status_.suppressed_repeats;
        return;
    }
    if (callback_ && !callback_(callback_context_, seen)) {
        ++status_.callback_rejections;
        ESP_LOGW(tag, "tag consumer busy; presentation will remain retryable");
        return;
    }
    last_tag_ = seen;
    last_tag_ms_ = now_ms;
    have_last_tag_ = true;
    ++status_.tag_count;
}

void Pn7160::process_packet(const uint8_t* frame, size_t size, uint64_t now_ms) {
    namespace nci = clock_core::nci;
    nci::PacketView packet{};
    if (nci::parse_packet(frame, size, packet) != nci::ParseError::none) {
        fail_and_backoff(now_ms, ESP_ERR_INVALID_RESPONSE, true);
        return;
    }

    uint8_t response_status = 0xff;
    switch (status_.state) {
    case Pn7160State::wait_reset_response:
        if (!nci::matches_response(packet, nci::group_core, nci::opcode_core_reset)) return;
        if (!nci::response_status(packet, nci::group_core, nci::opcode_core_reset,
                                  response_status) || response_status != 0) {
            advance_address_or_backoff(now_ms, ESP_ERR_INVALID_RESPONSE);
            return;
        }
        // A successful NCI response, rather than a bare I2C ACK, identifies the
        // address. NCI 2.0 reset completes with a separate notification.
        status_.address = candidate_address_;
        status_.state = Pn7160State::wait_reset_notification;
        deadline_ms_ = now_ms + command_timeout_ms;
        return;

    case Pn7160State::wait_reset_notification:
        if (nci::matches_notification(packet, nci::group_core, nci::opcode_core_reset)) {
            status_.state = Pn7160State::send_init;
        }
        return;

    case Pn7160State::wait_init_response:
        if (!nci::matches_response(packet, nci::group_core, nci::opcode_core_init)) return;
        if (!nci::response_succeeded(packet, nci::group_core, nci::opcode_core_init)) {
            fail_and_backoff(now_ms, ESP_ERR_INVALID_RESPONSE, true);
            return;
        }
        status_.initialized = true;
        status_.state = Pn7160State::send_discover_map;
        return;

    case Pn7160State::wait_discover_map_response:
        if (!nci::matches_response(packet,nci::group_rf_management,
                                   nci::opcode_rf_discover_map)) return;
        if (!nci::response_succeeded(packet,nci::group_rf_management,
                                     nci::opcode_rf_discover_map)) {
            fail_and_backoff(now_ms,ESP_ERR_INVALID_RESPONSE,true);
            return;
        }
        status_.state = Pn7160State::send_discover;
        return;

    case Pn7160State::wait_discover_response:
        if (!nci::matches_response(packet, nci::group_rf_management,
                                   nci::opcode_rf_discover)) return;
        if (!nci::response_succeeded(packet, nci::group_rf_management,
                                     nci::opcode_rf_discover)) {
            fail_and_backoff(now_ms, ESP_ERR_INVALID_RESPONSE, true);
            return;
        }
        consecutive_failures_ = 0;
        status_.last_error = ESP_OK;
        status_.discovery_active = true;
        status_.state = Pn7160State::discovering;
        ESP_LOGI(tag, "NCI ready at I2C address 0x%02x; NFC-A polling active",
                 status_.address);
        return;

    case Pn7160State::discovering: {
        if (nci::matches_notification(packet,nci::group_rf_management,
                                      nci::opcode_rf_interface_activated)) {
            nci::NfcId1 id{};
            const auto result = nci::extract_nfc_a_nfcid1(packet,id);
            if (result != nci::NfcId1Error::none) {
                ++status_.activation_errors;
                ESP_LOGE(tag,"malformed NFC-A activation notification (%u)",
                         static_cast<unsigned>(result));
                ESP_LOG_BUFFER_HEX_LEVEL(tag,frame,std::min<size_t>(size,64),ESP_LOG_WARN);
                fail_and_backoff(now_ms,ESP_ERR_INVALID_RESPONSE,true);
                return;
            }
            clock_core::TagId seen{};
            seen.size = id.size;
            std::copy_n(id.bytes.begin(),id.size,seen.bytes.begin());
            report_tag(seen,now_ms);
            status_.discovery_active = false;
            status_.state = Pn7160State::send_deactivate;
            return;
        }

        nci::DiscoveredTarget target{};
        const auto result = nci::extract_nfc_a_discovery(packet,target);
        if (result == nci::DiscoverError::not_discovery_notification) return;
        if (result != nci::DiscoverError::none) {
            ESP_LOGW(tag,"ignored malformed/unsupported discovery notification (%u)",
                     static_cast<unsigned>(result));
            if (nci::matches_notification(packet,nci::group_rf_management,
                                          nci::opcode_rf_discover)) {
                fail_and_backoff(now_ms,ESP_ERR_INVALID_RESPONSE,true);
            }
            return;
        }
        ++status_.discovery_notifications;
        uint8_t interface_type = 0;
        const bool supported = nci::discovery_select_interface(target.protocol,interface_type);
        ESP_LOGI(tag,"NFC-A target id=%u protocol=0x%02x UID-bytes=%u notification=%u%s",
                 target.discovery_id,target.protocol,target.nfcid1.size,target.notification_type,
                 supported ? "" : " unsupported protocol");
        if (supported && !have_candidate_) {
            have_candidate_ = true;
            candidate_discovery_id_ = target.discovery_id;
            candidate_protocol_ = target.protocol;
        }
        if (target.notification_type == nci::discover_notification_more) return;
        if (target.notification_type == nci::discover_notification_last_abort || !have_candidate_) {
            fail_and_backoff(now_ms,ESP_ERR_NOT_SUPPORTED,true);
            return;
        }
        if (!nci::build_rf_discover_select(candidate_discovery_id_,candidate_protocol_,select_command_)) {
            fail_and_backoff(now_ms,ESP_ERR_NOT_SUPPORTED,true);
            return;
        }
        status_.discovery_active = false;
        status_.state = Pn7160State::send_select;
        return;
    }

    case Pn7160State::wait_select_response:
        if (!nci::matches_response(packet,nci::group_rf_management,
                                   nci::opcode_rf_discover_select)) return;
        if (!nci::response_succeeded(packet,nci::group_rf_management,
                                     nci::opcode_rf_discover_select)) {
            fail_and_backoff(now_ms,ESP_ERR_INVALID_RESPONSE,true);
            return;
        }
        status_.state = Pn7160State::wait_activation;
        deadline_ms_ = now_ms + command_timeout_ms;
        return;

    case Pn7160State::wait_activation: {
        nci::NfcId1 id{};
        if (!nci::matches_notification(packet,nci::group_rf_management,
                                       nci::opcode_rf_interface_activated)) return;
        const auto result = nci::extract_nfc_a_nfcid1(packet,id);
        if (result != nci::NfcId1Error::none) {
            ++status_.activation_errors;
            ESP_LOGE(tag,"selected target activation parse failed (%u)",static_cast<unsigned>(result));
            ESP_LOG_BUFFER_HEX_LEVEL(tag,frame,std::min<size_t>(size,64),ESP_LOG_WARN);
            fail_and_backoff(now_ms,ESP_ERR_INVALID_RESPONSE,true);
            return;
        }
        clock_core::TagId seen{};
        seen.size = id.size;
        std::copy_n(id.bytes.begin(),id.size,seen.bytes.begin());
        report_tag(seen,now_ms);
        have_candidate_ = false;
        status_.state = Pn7160State::send_deactivate;
        return;
    }

    case Pn7160State::wait_deactivate_response:
        if (!nci::matches_response(packet, nci::group_rf_management,
                                   nci::opcode_rf_deactivate)) return;
        if (!nci::response_succeeded(packet, nci::group_rf_management,
                                     nci::opcode_rf_deactivate)) {
            fail_and_backoff(now_ms, ESP_ERR_INVALID_RESPONSE, true);
            return;
        }
        // DISCOVERY deactivation resumes the existing discovery loop. Any
        // following deactivation notification is drained in `discovering`.
        status_.discovery_active = true;
        status_.state = Pn7160State::discovering;
        return;

    default:
        return;
    }
}

bool Pn7160::receive_packet(uint64_t now_ms) {
    // Do not issue a speculative read: IRQ is the PN7160's read request.
    if (!irq_high()) return false;
    size_t size = 0;
    const auto error = bus_->read_nci_frame(candidate_address_, receive_buffer_.data(),
                                            receive_buffer_.size(), size);
    if (error != ESP_OK) {
        if (status_.address == 0) advance_address_or_backoff(now_ms, error);
        else fail_and_backoff(now_ms, error, false);
        return false;
    }
    process_packet(receive_buffer_.data(), size, now_ms);
    return true;
}

void Pn7160::service(uint64_t now_ms) {
    if (!bus_ || status_.state == Pn7160State::uninitialized) return;
    status_.irq_high = irq_high();

    switch (status_.state) {
    case Pn7160State::ven_low:
        if (!ven_phase_started_) {
            begin_ven_low(now_ms);
        } else if (deadline_reached(now_ms, deadline_ms_)) {
            const auto error = gpio_set_level(nfc_ven, 1);
            if (error != ESP_OK) {
                fail_and_backoff(now_ms, error, false);
                return;
            }
            status_.state = Pn7160State::boot_wait;
            deadline_ms_ = now_ms + boot_wait_ms;
        }
        return;

    case Pn7160State::boot_wait:
        if (deadline_reached(now_ms, deadline_ms_)) {
            candidate_address_ = 0x28;
            status_.state = Pn7160State::scan_address;
        }
        return;

    case Pn7160State::scan_address:
        // A boot/reset notification may already be pending. Drain it before
        // obeying the PN7160 rule that a host write is allowed only with IRQ low.
        if (status_.irq_high) {
            receive_packet(now_ms);
        } else {
            status_.state = Pn7160State::send_reset;
        }
        return;

    case Pn7160State::send_reset:
        if (status_.irq_high) {
            receive_packet(now_ms);
            return;
        }
        send_frame(clock_core::nci::core_reset_keep_config.data(),
                   clock_core::nci::core_reset_keep_config.size(),
                   Pn7160State::wait_reset_response, now_ms);
        return;

    case Pn7160State::send_init:
        if (status_.irq_high) {
            receive_packet(now_ms);
            return;
        }
        send_frame(clock_core::nci::core_init_nci2.data(),
                   clock_core::nci::core_init_nci2.size(),
                   Pn7160State::wait_init_response, now_ms);
        return;

    case Pn7160State::send_discover:
        if (status_.irq_high) {
            receive_packet(now_ms);
            return;
        }
        send_frame(clock_core::nci::rf_discover_nfc_a_passive_poll.data(),
                   clock_core::nci::rf_discover_nfc_a_passive_poll.size(),
                   Pn7160State::wait_discover_response, now_ms);
        return;

    case Pn7160State::send_discover_map:
        if (status_.irq_high) {
            receive_packet(now_ms);
            return;
        }
        send_frame(clock_core::nci::rf_discover_map_iso_dep.data(),
                   clock_core::nci::rf_discover_map_iso_dep.size(),
                   Pn7160State::wait_discover_map_response,now_ms);
        return;

    case Pn7160State::send_select:
        if (status_.irq_high) {
            receive_packet(now_ms);
            return;
        }
        send_frame(select_command_.data(),select_command_.size(),
                   Pn7160State::wait_select_response,now_ms);
        return;

    case Pn7160State::send_deactivate:
        if (status_.irq_high) {
            receive_packet(now_ms);
            return;
        }
        send_frame(clock_core::nci::rf_deactivate_discovery.data(),
                   clock_core::nci::rf_deactivate_discovery.size(),
                   Pn7160State::wait_deactivate_response, now_ms);
        return;

    case Pn7160State::wait_reset_response:
    case Pn7160State::wait_reset_notification:
    case Pn7160State::wait_init_response:
    case Pn7160State::wait_discover_map_response:
    case Pn7160State::wait_discover_response:
    case Pn7160State::wait_select_response:
    case Pn7160State::wait_activation:
    case Pn7160State::wait_deactivate_response:
        if (status_.irq_high) {
            receive_packet(now_ms);
        } else if (deadline_reached(now_ms, deadline_ms_)) {
            if (status_.address == 0) advance_address_or_backoff(now_ms, ESP_ERR_TIMEOUT);
            else fail_and_backoff(now_ms, ESP_ERR_TIMEOUT, false);
        }
        return;

    case Pn7160State::discovering:
        if (status_.irq_high) receive_packet(now_ms);
        return;

    case Pn7160State::backoff:
        if (deadline_reached(now_ms, deadline_ms_)) begin_ven_low(now_ms);
        return;

    case Pn7160State::uninitialized:
        return;
    }
}

const char* Pn7160::state_name(Pn7160State state) {
    switch (state) {
    case Pn7160State::uninitialized: return "uninitialized";
    case Pn7160State::ven_low: return "ven-low";
    case Pn7160State::boot_wait: return "boot-wait";
    case Pn7160State::scan_address: return "scan-address";
    case Pn7160State::send_reset: return "send-reset";
    case Pn7160State::wait_reset_response: return "wait-reset-rsp";
    case Pn7160State::wait_reset_notification: return "wait-reset-ntf";
    case Pn7160State::send_init: return "send-init";
    case Pn7160State::wait_init_response: return "wait-init-rsp";
    case Pn7160State::send_discover_map: return "send-map";
    case Pn7160State::wait_discover_map_response: return "wait-map-rsp";
    case Pn7160State::send_discover: return "send-discover";
    case Pn7160State::wait_discover_response: return "wait-discover-rsp";
    case Pn7160State::discovering: return "ready";
    case Pn7160State::send_select: return "send-select";
    case Pn7160State::wait_select_response: return "wait-select-rsp";
    case Pn7160State::wait_activation: return "wait-activation";
    case Pn7160State::send_deactivate: return "send-deactivate";
    case Pn7160State::wait_deactivate_response: return "wait-deactivate-rsp";
    case Pn7160State::backoff: return "backoff";
    }
    return "unknown";
}

} // namespace clock_hw

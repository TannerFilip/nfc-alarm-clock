#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace clock_core::nci {

constexpr std::size_t header_size = 3;
constexpr std::size_t max_payload_size = 255;
constexpr std::size_t max_frame_size = header_size + max_payload_size;
constexpr std::size_t max_nfcid1_size = 10;

enum class MessageType : uint8_t { command = 1, response = 2, notification = 3 };

enum class ParseError : uint8_t {
    none,
    null_input,
    too_short,
    too_long,
    length_mismatch,
    chained_packet,
    invalid_type,
    reserved_oid_bits,
};

struct PacketView {
    MessageType type = MessageType::command;
    uint8_t group = 0;
    uint8_t opcode = 0;
    const uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
};

ParseError parse_packet(const uint8_t* frame, std::size_t frame_size, PacketView& packet);
bool matches_response(const PacketView& packet, uint8_t group, uint8_t opcode);
bool matches_notification(const PacketView& packet, uint8_t group, uint8_t opcode);
bool response_status(const PacketView& packet, uint8_t group, uint8_t opcode, uint8_t& status);
bool response_succeeded(const PacketView& packet, uint8_t group, uint8_t opcode);

constexpr uint8_t group_core = 0x00;
constexpr uint8_t group_rf_management = 0x01;
constexpr uint8_t opcode_core_reset = 0x00;
constexpr uint8_t opcode_core_init = 0x01;
constexpr uint8_t opcode_rf_discover_map = 0x00;
constexpr uint8_t opcode_rf_discover = 0x03;
constexpr uint8_t opcode_rf_discover_select = 0x04;
constexpr uint8_t opcode_rf_interface_activated = 0x05;
constexpr uint8_t opcode_rf_deactivate = 0x06;
constexpr uint8_t status_ok = 0x00;

// Complete NCI 2.0 frames, including the three-octet NCI header.
inline constexpr std::array<uint8_t, 4> core_reset_keep_config{{0x20, 0x00, 0x01, 0x00}};
inline constexpr std::array<uint8_t, 5> core_init_nci2{{0x20, 0x01, 0x02, 0x00, 0x00}};
inline constexpr std::array<uint8_t, 7> rf_discover_map_iso_dep{{
    0x21, 0x00, 0x04, 0x01, 0x04, 0x03, 0x02,
}};
inline constexpr std::array<uint8_t, 6> rf_discover_nfc_a_passive_poll{{
    0x21, 0x03, 0x03, 0x01, 0x00, 0x01,
}};
inline constexpr std::array<uint8_t, 4> rf_deactivate_discovery{{0x21, 0x06, 0x01, 0x03}};

struct NfcId1 {
    uint8_t size = 0;
    std::array<uint8_t, max_nfcid1_size> bytes{};
};

constexpr uint8_t protocol_t1t = 0x01;
constexpr uint8_t protocol_t2t = 0x02;
constexpr uint8_t protocol_iso_dep = 0x04;
constexpr uint8_t interface_frame = 0x01;
constexpr uint8_t interface_iso_dep = 0x02;
constexpr uint8_t discover_notification_last = 0x00;
constexpr uint8_t discover_notification_last_abort = 0x01;
constexpr uint8_t discover_notification_more = 0x02;

struct DiscoveredTarget {
    uint8_t discovery_id = 0;
    uint8_t protocol = 0;
    uint8_t mode = 0;
    uint8_t notification_type = discover_notification_last_abort;
    NfcId1 nfcid1{};
};

enum class DiscoverError : uint8_t {
    none,
    not_discovery_notification,
    too_short,
    not_nfc_a_poll,
    technology_parameters_truncated,
    invalid_technology_parameters,
    invalid_nfcid1_length,
    invalid_notification_type,
};

DiscoverError extract_nfc_a_discovery(const PacketView& packet, DiscoveredTarget& target);
bool discovery_select_interface(uint8_t protocol, uint8_t& interface_type);
bool build_rf_discover_select(uint8_t discovery_id, uint8_t protocol,
                              std::array<uint8_t, 6>& command);

enum class NfcId1Error : uint8_t {
    none,
    not_activation_notification,
    activation_too_short,
    not_nfc_a_poll,
    technology_parameters_truncated,
    invalid_nfcid1_length,
    invalid_technology_parameters,
    activation_parameters_truncated,
};

NfcId1Error extract_nfc_a_nfcid1(const PacketView& packet, NfcId1& nfcid1);

} // namespace clock_core::nci

#include "nci_protocol.hpp"

#include <algorithm>

namespace clock_core::nci {
namespace {
constexpr uint8_t message_type_mask = 0xe0;
constexpr uint8_t message_type_shift = 5;
constexpr uint8_t packet_boundary_flag = 0x10;
constexpr uint8_t group_mask = 0x0f;
constexpr uint8_t opcode_reserved_mask = 0xc0;
constexpr uint8_t opcode_mask = 0x3f;
constexpr uint8_t discovery_type_poll_a = 0x00;

bool valid_nfcid1_size(uint8_t size) {
    return size == 4 || size == 7 || size == 10;
}

NfcId1Error extract_technology_nfcid1(const uint8_t* technology,
                                      std::size_t technology_size,
                                      NfcId1& nfcid1) {
    if (technology_size < 4) return NfcId1Error::invalid_technology_parameters;
    const uint8_t uid_size = technology[2];
    if (!valid_nfcid1_size(uid_size)) return NfcId1Error::invalid_nfcid1_length;
    const std::size_t selector_length_offset = 3 + uid_size;
    if (selector_length_offset >= technology_size)
        return NfcId1Error::invalid_technology_parameters;
    const uint8_t selector_size = technology[selector_length_offset];
    if (selector_size > 1) return NfcId1Error::invalid_technology_parameters;
    const std::size_t base_size = selector_length_offset + 1 + selector_size;
    if (base_size > technology_size) return NfcId1Error::invalid_technology_parameters;
    // Poll-A technology parameters end with the T1T HR length. Depending on
    // controller/card protocol it may be omitted, present as zero, or followed
    // by the two HR bytes. Accept only those three exact shapes.
    if (technology_size != base_size) {
        const std::size_t trailing = technology_size - base_size;
        if (!((trailing == 1 && technology[base_size] == 0) ||
              (trailing == 3 && technology[base_size] == 2))) {
            return NfcId1Error::invalid_technology_parameters;
        }
    }
    nfcid1.size = uid_size;
    std::copy_n(technology + 3, uid_size, nfcid1.bytes.begin());
    return NfcId1Error::none;
}
} // namespace

ParseError parse_packet(const uint8_t* frame, std::size_t frame_size, PacketView& packet) {
    packet = {};
    if (frame == nullptr && frame_size != 0) return ParseError::null_input;
    if (frame_size < header_size) return ParseError::too_short;
    if (frame_size > max_frame_size) return ParseError::too_long;
    if ((frame[0] & packet_boundary_flag) != 0) return ParseError::chained_packet;

    const auto raw_type = static_cast<uint8_t>((frame[0] & message_type_mask) >> message_type_shift);
    if (raw_type < static_cast<uint8_t>(MessageType::command) ||
        raw_type > static_cast<uint8_t>(MessageType::notification)) {
        return ParseError::invalid_type;
    }
    if ((frame[1] & opcode_reserved_mask) != 0) return ParseError::reserved_oid_bits;

    const std::size_t encoded_size = header_size + frame[2];
    if (frame_size != encoded_size) return ParseError::length_mismatch;

    packet.type = static_cast<MessageType>(raw_type);
    packet.group = frame[0] & group_mask;
    packet.opcode = frame[1] & opcode_mask;
    packet.payload = frame + header_size;
    packet.payload_size = frame[2];
    return ParseError::none;
}

bool matches_response(const PacketView& packet, uint8_t group, uint8_t opcode) {
    return packet.type == MessageType::response && packet.group == group && packet.opcode == opcode;
}

bool matches_notification(const PacketView& packet, uint8_t group, uint8_t opcode) {
    return packet.type == MessageType::notification && packet.group == group && packet.opcode == opcode;
}

bool response_status(const PacketView& packet, uint8_t group, uint8_t opcode, uint8_t& status) {
    if (!matches_response(packet, group, opcode) || packet.payload_size < 1 || packet.payload == nullptr) {
        return false;
    }
    status = packet.payload[0];
    return true;
}

bool response_succeeded(const PacketView& packet, uint8_t group, uint8_t opcode) {
    uint8_t status = 0xff;
    return response_status(packet, group, opcode, status) && status == status_ok;
}

NfcId1Error extract_nfc_a_nfcid1(const PacketView& packet, NfcId1& nfcid1) {
    nfcid1 = {};
    if (!matches_notification(packet, group_rf_management, opcode_rf_interface_activated)) {
        return NfcId1Error::not_activation_notification;
    }
    if (packet.payload == nullptr || packet.payload_size < 7) {
        return NfcId1Error::activation_too_short;
    }

    // RF_DISC_ID, RF Interface, RF Protocol, Activation RF Technology and Mode,
    // Max Data Packet Payload Size, Initial Number of Credits, RF parameters length.
    if (packet.payload[3] != discovery_type_poll_a) return NfcId1Error::not_nfc_a_poll;

    const std::size_t technology_size = packet.payload[6];
    constexpr std::size_t technology_offset = 7;
    constexpr std::size_t activation_tail_size = 4; // data mode, TX rate, RX rate, length
    if (technology_size > packet.payload_size - technology_offset ||
        packet.payload_size - technology_offset - technology_size < activation_tail_size) {
        return NfcId1Error::technology_parameters_truncated;
    }

    const uint8_t* technology = packet.payload + technology_offset;
    const auto technology_result = extract_technology_nfcid1(technology,technology_size,nfcid1);
    if (technology_result != NfcId1Error::none) return technology_result;

    const std::size_t tail_offset = technology_offset + technology_size;
    const uint8_t activation_size = packet.payload[tail_offset + 3];
    if (packet.payload_size != tail_offset + activation_tail_size + activation_size) {
        return NfcId1Error::activation_parameters_truncated;
    }

    return NfcId1Error::none;
}

DiscoverError extract_nfc_a_discovery(const PacketView& packet, DiscoveredTarget& target) {
    target = {};
    if (!matches_notification(packet, group_rf_management, opcode_rf_discover))
        return DiscoverError::not_discovery_notification;
    if (!packet.payload || packet.payload_size < 5) return DiscoverError::too_short;
    if (packet.payload[2] != discovery_type_poll_a) return DiscoverError::not_nfc_a_poll;
    const std::size_t technology_size = packet.payload[3];
    if (packet.payload_size != 5 + technology_size)
        return DiscoverError::technology_parameters_truncated;
    const uint8_t notification_type = packet.payload[4 + technology_size];
    if (notification_type > discover_notification_more)
        return DiscoverError::invalid_notification_type;
    NfcId1 identifier{};
    const auto result = extract_technology_nfcid1(packet.payload + 4, technology_size, identifier);
    if (result == NfcId1Error::invalid_nfcid1_length)
        return DiscoverError::invalid_nfcid1_length;
    if (result != NfcId1Error::none)
        return DiscoverError::invalid_technology_parameters;
    target.discovery_id = packet.payload[0];
    target.protocol = packet.payload[1];
    target.mode = packet.payload[2];
    target.notification_type = notification_type;
    target.nfcid1 = identifier;
    return DiscoverError::none;
}

bool discovery_select_interface(uint8_t protocol, uint8_t& interface_type) {
    if (protocol == protocol_t1t || protocol == protocol_t2t) {
        interface_type = interface_frame;
        return true;
    }
    if (protocol == protocol_iso_dep) {
        interface_type = interface_iso_dep;
        return true;
    }
    return false;
}

bool build_rf_discover_select(uint8_t discovery_id, uint8_t protocol,
                              std::array<uint8_t, 6>& command) {
    uint8_t interface_type = 0;
    if (!discovery_id || !discovery_select_interface(protocol,interface_type)) return false;
    command = {{0x21, opcode_rf_discover_select, 0x03,
                discovery_id, protocol, interface_type}};
    return true;
}

} // namespace clock_core::nci

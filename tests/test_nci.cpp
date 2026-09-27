#include "core/nci_protocol.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace clock_core::nci;

namespace {
std::vector<uint8_t> activation(const std::vector<uint8_t>& uid,
                                uint8_t mode = 0x00,
                                uint8_t selector_size = 1) {
    std::vector<uint8_t> technology{0x44, 0x00, static_cast<uint8_t>(uid.size())};
    technology.insert(technology.end(), uid.begin(), uid.end());
    technology.push_back(selector_size);
    if (selector_size == 1) technology.push_back(0x00);

    std::vector<uint8_t> payload{
        0x01, 0x01, 0x02, mode, 0xfe, 0x01, static_cast<uint8_t>(technology.size()),
    };
    payload.insert(payload.end(), technology.begin(), technology.end());
    payload.insert(payload.end(), {0x00, 0x00, 0x00, 0x00});

    std::vector<uint8_t> frame{0x61, 0x05, static_cast<uint8_t>(payload.size())};
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<uint8_t> discovery(const std::vector<uint8_t>& uid, uint8_t protocol = protocol_t2t,
                               uint8_t notification = discover_notification_last) {
    std::vector<uint8_t> technology{0x44, 0x00, static_cast<uint8_t>(uid.size())};
    technology.insert(technology.end(),uid.begin(),uid.end());
    technology.insert(technology.end(),{0x01,0x00});
    std::vector<uint8_t> payload{0x01,protocol,0x00,static_cast<uint8_t>(technology.size())};
    payload.insert(payload.end(),technology.begin(),technology.end());
    payload.push_back(notification);
    std::vector<uint8_t> frame{0x61,0x03,static_cast<uint8_t>(payload.size())};
    frame.insert(frame.end(),payload.begin(),payload.end());
    return frame;
}

PacketView parse_ok(const std::vector<uint8_t>& frame) {
    PacketView packet{};
    assert(parse_packet(frame.data(), frame.size(), packet) == ParseError::none);
    return packet;
}

void expect_uid(const std::vector<uint8_t>& uid) {
    const auto frame = activation(uid);
    const PacketView packet = parse_ok(frame);
    NfcId1 parsed{};
    assert(extract_nfc_a_nfcid1(packet, parsed) == NfcId1Error::none);
    assert(parsed.size == uid.size());
    for (std::size_t i = 0; i < uid.size(); ++i) assert(parsed.bytes[i] == uid[i]);
    for (std::size_t i = uid.size(); i < parsed.bytes.size(); ++i) assert(parsed.bytes[i] == 0);
}
} // namespace

int main() {
    PacketView packet{};
    const uint8_t byte = 0;
    assert(parse_packet(nullptr, 1, packet) == ParseError::null_input);
    assert(parse_packet(nullptr, 0, packet) == ParseError::too_short);
    for (std::size_t size = 0; size < header_size; ++size) {
        assert(parse_packet(&byte, size, packet) == ParseError::too_short);
    }

    const std::array<uint8_t, 3> empty_response{{0x40, 0x01, 0x00}};
    assert(parse_packet(empty_response.data(), empty_response.size(), packet) == ParseError::none);
    assert(matches_response(packet, group_core, opcode_core_init));
    assert(!matches_notification(packet, group_core, opcode_core_init));
    uint8_t status = 0xff;
    assert(!response_status(packet, group_core, opcode_core_init, status));
    assert(!response_succeeded(packet, group_core, opcode_core_init));

    const std::array<uint8_t, 4> ok_response{{0x40, 0x01, 0x01, 0x00}};
    assert(parse_packet(ok_response.data(), ok_response.size(), packet) == ParseError::none);
    assert(response_status(packet, group_core, opcode_core_init, status) && status == status_ok);
    assert(response_succeeded(packet, group_core, opcode_core_init));
    const std::array<uint8_t, 4> failed_response{{0x41, 0x03, 0x01, 0x09}};
    assert(parse_packet(failed_response.data(), failed_response.size(), packet) == ParseError::none);
    assert(response_status(packet, group_rf_management, opcode_rf_discover, status) && status == 0x09);
    assert(!response_succeeded(packet, group_rf_management, opcode_rf_discover));
    assert(!response_status(packet, group_core, opcode_rf_discover, status));

    const std::array<uint8_t, 4> short_length{{0x40, 0x01, 0x02, 0x00}};
    const std::array<uint8_t, 5> trailing{{0x40, 0x01, 0x01, 0x00, 0xaa}};
    const std::array<uint8_t, 3> chained{{0x50, 0x01, 0x00}};
    const std::array<uint8_t, 3> reserved_oid{{0x40, 0x81, 0x00}};
    assert(parse_packet(short_length.data(), short_length.size(), packet) == ParseError::length_mismatch);
    assert(parse_packet(trailing.data(), trailing.size(), packet) == ParseError::length_mismatch);
    assert(parse_packet(chained.data(), chained.size(), packet) == ParseError::chained_packet);
    assert(parse_packet(reserved_oid.data(), reserved_oid.size(), packet) == ParseError::reserved_oid_bits);
    for (uint8_t type : {uint8_t{0}, uint8_t{4}, uint8_t{5}, uint8_t{6}, uint8_t{7}}) {
        const std::array<uint8_t, 3> invalid{{static_cast<uint8_t>(type << 5), 0x00, 0x00}};
        assert(parse_packet(invalid.data(), invalid.size(), packet) == ParseError::invalid_type);
    }
    std::vector<uint8_t> maximum(max_frame_size, 0);
    maximum[0] = 0x60;
    maximum[2] = 0xff;
    assert(parse_packet(maximum.data(), maximum.size(), packet) == ParseError::none);
    maximum.push_back(0);
    assert(parse_packet(maximum.data(), maximum.size(), packet) == ParseError::too_long);

    assert((core_reset_keep_config == std::array<uint8_t, 4>{{0x20, 0x00, 0x01, 0x00}}));
    assert((core_init_nci2 == std::array<uint8_t, 5>{{0x20, 0x01, 0x02, 0x00, 0x00}}));
    assert((rf_discover_map_iso_dep ==
            std::array<uint8_t, 7>{{0x21,0x00,0x04,0x01,0x04,0x03,0x02}}));
    assert((rf_discover_nfc_a_passive_poll ==
            std::array<uint8_t, 6>{{0x21, 0x03, 0x03, 0x01, 0x00, 0x01}}));
    assert((rf_deactivate_discovery == std::array<uint8_t, 4>{{0x21, 0x06, 0x01, 0x03}}));

    expect_uid({0x01, 0x02, 0x03, 0x04});
    expect_uid({0x04, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6});
    expect_uid({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a});

    DiscoveredTarget target{};
    auto discovered = discovery({0x04,0xa1,0xb2,0xc3});
    assert(extract_nfc_a_discovery(parse_ok(discovered),target) == DiscoverError::none);
    assert(target.discovery_id == 1 && target.protocol == protocol_t2t &&
           target.notification_type == discover_notification_last && target.nfcid1.size == 4);
    std::array<uint8_t,6> select{};
    assert(build_rf_discover_select(target.discovery_id,target.protocol,select));
    assert((select == std::array<uint8_t,6>{{0x21,0x04,0x03,0x01,0x02,0x01}}));
    assert(build_rf_discover_select(2,protocol_iso_dep,select));
    assert((select == std::array<uint8_t,6>{{0x21,0x04,0x03,0x02,0x04,0x02}}));
    assert(!build_rf_discover_select(1,0x80,select));
    auto discover_more = discovery({1,2,3,4},protocol_t2t,discover_notification_more);
    assert(extract_nfc_a_discovery(parse_ok(discover_more),target) == DiscoverError::none &&
           target.notification_type == discover_notification_more);
    auto bad_discover_length = discovery({1,2,3,4});
    ++bad_discover_length[header_size + 3];
    assert(extract_nfc_a_discovery(parse_ok(bad_discover_length),target) ==
           DiscoverError::technology_parameters_truncated);
    auto bad_discover_type = discovery({1,2,3,4});
    bad_discover_type.back() = 3;
    assert(extract_nfc_a_discovery(parse_ok(bad_discover_type),target) ==
           DiscoverError::invalid_notification_type);

    NfcId1 uid{};
    auto wrong_message = activation({1, 2, 3, 4});
    wrong_message[0] = 0x41;
    assert(extract_nfc_a_nfcid1(parse_ok(wrong_message), uid) ==
           NfcId1Error::not_activation_notification);
    auto wrong_opcode = activation({1, 2, 3, 4});
    wrong_opcode[1] = 0x03;
    assert(extract_nfc_a_nfcid1(parse_ok(wrong_opcode), uid) ==
           NfcId1Error::not_activation_notification);
    auto wrong_mode = activation({1, 2, 3, 4}, 0x01);
    assert(extract_nfc_a_nfcid1(parse_ok(wrong_mode), uid) == NfcId1Error::not_nfc_a_poll);

    for (std::size_t bad_size : {std::size_t{0}, std::size_t{1}, std::size_t{3},
                                 std::size_t{5}, std::size_t{8}, std::size_t{11}}) {
        auto malformed = activation(std::vector<uint8_t>(bad_size, 0xaa));
        assert(extract_nfc_a_nfcid1(parse_ok(malformed), uid) ==
               NfcId1Error::invalid_nfcid1_length);
    }
    auto short_activation = activation({1, 2, 3, 4});
    short_activation.resize(header_size + 6);
    short_activation[2] = 6;
    assert(extract_nfc_a_nfcid1(parse_ok(short_activation), uid) ==
           NfcId1Error::activation_too_short);
    auto claimed_technology = activation({1, 2, 3, 4});
    claimed_technology[header_size + 6] = 0xff;
    assert(extract_nfc_a_nfcid1(parse_ok(claimed_technology), uid) ==
           NfcId1Error::technology_parameters_truncated);
    auto missing_selector = activation({1, 2, 3, 4});
    missing_selector[header_size + 6] = 7;
    assert(extract_nfc_a_nfcid1(parse_ok(missing_selector), uid) ==
           NfcId1Error::invalid_technology_parameters);
    auto bad_selector = activation({1, 2, 3, 4}, 0x00, 2);
    assert(extract_nfc_a_nfcid1(parse_ok(bad_selector), uid) ==
           NfcId1Error::invalid_technology_parameters);
    auto trailing_technology = activation({1, 2, 3, 4});
    const std::size_t tech_end = header_size + 7 + trailing_technology[header_size + 6];
    trailing_technology.insert(trailing_technology.begin() + static_cast<std::ptrdiff_t>(tech_end), 0xaa);
    ++trailing_technology[header_size + 6];
    ++trailing_technology[2];
    assert(extract_nfc_a_nfcid1(parse_ok(trailing_technology), uid) ==
           NfcId1Error::invalid_technology_parameters);
    auto bad_activation_length = activation({1, 2, 3, 4});
    bad_activation_length[bad_activation_length.size() - 1] = 1;
    assert(extract_nfc_a_nfcid1(parse_ok(bad_activation_length), uid) ==
           NfcId1Error::activation_parameters_truncated);

    auto t1t = activation({1, 2, 3, 4});
    const std::size_t t1t_tech_end = header_size + 7 + t1t[header_size + 6];
    t1t.insert(t1t.begin() + static_cast<std::ptrdiff_t>(t1t_tech_end), {0x02, 0x11, 0x22});
    t1t[header_size + 6] += 3;
    t1t[2] += 3;
    assert(extract_nfc_a_nfcid1(parse_ok(t1t), uid) == NfcId1Error::none);

    auto zero_length_hr = activation({1, 2, 3, 4});
    const std::size_t zero_hr_tech_end = header_size + 7 + zero_length_hr[header_size + 6];
    zero_length_hr.insert(zero_length_hr.begin() + static_cast<std::ptrdiff_t>(zero_hr_tech_end),0x00);
    ++zero_length_hr[header_size + 6];
    ++zero_length_hr[2];
    assert(extract_nfc_a_nfcid1(parse_ok(zero_length_hr),uid) == NfcId1Error::none);

    std::cout << "PASS: strict NCI frames, command bytes, response status, NFC-A UID extraction\n";
}

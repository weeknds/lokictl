/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "topology.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace platform {
namespace {

constexpr std::size_t kEthernetHeader = 14;
constexpr std::size_t kHardwareSize = 6;
constexpr uint16_t kTypeLldp = 0x88cc;
// IEEE 802.3: a value below this at byte 12 is the frame's length, not an
// EtherType.
constexpr uint16_t kFirstEtherType = 0x0600;
// At most this many fields are read from one frame.
constexpr int kMaxFields = 64;

// LLDP: field (TLV) types, chassis and port ID subtypes, the IPv4 address
// family, and the IEEE 802.1 part's organisation code and subtypes.
constexpr uint8_t kLldpEnd = 0;
constexpr uint8_t kLldpChassis = 1;
constexpr uint8_t kLldpPort = 2;
constexpr uint8_t kLldpPortDescription = 4;
constexpr uint8_t kLldpSystemName = 5;
constexpr uint8_t kLldpSystemDescription = 6;
constexpr uint8_t kLldpManagement = 8;
constexpr uint8_t kLldpOrganisation = 127;
constexpr uint8_t kChassisHardware = 4;
constexpr uint8_t kChassisNetwork = 5;
constexpr uint8_t kPortHardware = 3;
constexpr uint8_t kPortNetwork = 4;
constexpr uint8_t kFamilyIpv4 = 1;
constexpr uint8_t kIeee8021[3] = {0x00, 0x80, 0xc2};
constexpr uint8_t kPortVlanId = 1;
constexpr uint8_t kVlanName = 3;

// CDP: its group address, the LLC SNAP header in front of it, its own
// 4-byte header (version, time to live, checksum), field types, and the
// most addresses read from one address field.
constexpr uint8_t kCdpGroup[kHardwareSize] = {0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc};
constexpr uint8_t kCdpSnap[8] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x0c, 0x20, 0x00};
constexpr std::size_t kCdpFirstField = kEthernetHeader + sizeof(kCdpSnap) + 4;
constexpr std::size_t kCdpFieldHeader = 4;
constexpr uint16_t kCdpDeviceId = 0x0001;
constexpr uint16_t kCdpAddresses = 0x0002;
constexpr uint16_t kCdpPortId = 0x0003;
constexpr uint16_t kCdpPlatform = 0x0006;
constexpr uint16_t kCdpNativeVlan = 0x000a;
constexpr uint16_t kCdpManagement = 0x0016;
constexpr uint32_t kCdpMaxAddresses = 8;
// An address entry's protocol: NLPID 0xcc, IPv4.
constexpr uint8_t kCdpProtocolNlpid = 1;
constexpr uint8_t kCdpIpv4 = 0xcc;

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
}

uint32_t read32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

// Not a group address and not all zeros.
bool unicast_hardware(const uint8_t* hardware) {
    return (hardware[0] & 1U) == 0 && std::any_of(hardware, hardware + kHardwareSize, [](uint8_t b) { return b != 0; });
}

// An IPv4 address one device can have: not 0.0.0.0, not multicast or
// reserved, not 127/8.
bool device_address(uint32_t address) {
    const uint32_t first = address >> 24;
    return address != 0 && first < 224 && first != 127;
}

std::string hardware_text(const uint8_t* hardware) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", hardware[0], hardware[1], hardware[2],
                  hardware[3], hardware[4], hardware[5]);
    return text;
}

std::string address_text(uint32_t address) {
    return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xffU) + "." +
           std::to_string((address >> 8) & 0xffU) + "." + std::to_string(address & 0xffU);
}

// A text as kept: up to the first NUL, carriage return or line feed,
// printable ASCII only (anything else becomes '?'), at most
// model::kNeighbourTextMax characters, without spaces at either end.
std::string clean_text(const uint8_t* bytes, std::size_t size) {
    std::string text;
    for (std::size_t index = 0; index < size && text.size() < model::kNeighbourTextMax; ++index) {
        const uint8_t character = bytes[index];
        if (character == 0 || character == '\r' || character == '\n') {
            break;
        }
        text.push_back(character >= 0x20 && character < 0x7f ? static_cast<char>(character) : '?');
    }
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

// Adds a VLAN ID (1 to 4094) to an ascending list of at most
// model::kNeighbourVlansMax.
void add_vlan(std::vector<int32_t>& vlans, uint16_t id) {
    if (id == 0 || id > 4094 || vlans.size() >= model::kNeighbourVlansMax) {
        return;
    }
    const auto at = std::lower_bound(vlans.begin(), vlans.end(), static_cast<int32_t>(id));
    if (at == vlans.end() || *at != id) {
        vlans.insert(at, id);
    }
}

// The first IPv4 address of a CDP address field: a 4-byte count, then
// entries of protocol type, protocol length, protocol, a 2-byte address
// length and the address.
std::string cdp_address(const uint8_t* value, std::size_t size) {
    if (size < 4) {
        return {};
    }
    const uint32_t count = std::min(read32(value), kCdpMaxAddresses);
    std::size_t at = 4;
    for (uint32_t entry = 0; entry < count; ++entry) {
        if (size - at < 2) {
            break;
        }
        const uint8_t type = value[at];
        const std::size_t protocol_size = value[at + 1];
        at += 2;
        if (size - at < protocol_size) {
            break;
        }
        const uint8_t* protocol = value + at;
        at += protocol_size;
        if (size - at < 2) {
            break;
        }
        const std::size_t address_size = read16(value + at);
        at += 2;
        if (size - at < address_size) {
            break;
        }
        const uint8_t* address = value + at;
        at += address_size;
        if (type == kCdpProtocolNlpid && protocol_size == 1 && protocol[0] == kCdpIpv4 && address_size == 4) {
            const uint32_t ipv4 = read32(address);
            if (device_address(ipv4)) {
                return address_text(ipv4);
            }
        }
    }
    return {};
}

// Builders for the pretend frames.
void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xffU));
}

void put_lldp_field(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value) {
    put16(out, static_cast<uint16_t>((type << 9) | (value.size() & 0x1ffU)));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<uint8_t> with_text(std::vector<uint8_t> head, const std::string& text) {
    head.insert(head.end(), text.begin(), text.end());
    return head;
}

std::vector<uint8_t> vlan_name_field(uint16_t id, const std::string& name) {
    std::vector<uint8_t> value{kIeee8021[0], kIeee8021[1], kIeee8021[2], kVlanName,
                               static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id & 0xffU),
                               static_cast<uint8_t>(name.size())};
    return with_text(std::move(value), name);
}

} // namespace

bool parse_lldp(const uint8_t* frame, std::size_t size, model::Neighbour& out) {
    if (!frame || size < kEthernetHeader || read16(frame + 12) != kTypeLldp || !unicast_hardware(frame + 6)) {
        return false;
    }
    model::Neighbour found;
    found.protocol = "LLDP";
    found.hardware_address = hardware_text(frame + 6);
    std::string port_id;
    bool port_is_address = false;
    std::string port_description;

    std::size_t at = kEthernetHeader;
    for (int fields = 0; fields < kMaxFields && size - at >= 2; ++fields) {
        const uint16_t header = read16(frame + at);
        const auto type = static_cast<uint8_t>(header >> 9);
        const std::size_t length = header & 0x1ffU;
        at += 2;
        if (type == kLldpEnd || length > size - at) {
            break;  // the end, or a field running past the frame
        }
        const uint8_t* value = frame + at;
        at += length;
        switch (type) {
            case kLldpChassis:
                if (length < 2) {
                    break;
                }
                if (value[0] == kChassisHardware) {
                    if (length == 1 + kHardwareSize) {
                        found.chassis = hardware_text(value + 1);
                    }
                } else if (value[0] == kChassisNetwork) {
                    if (length == 6 && value[1] == kFamilyIpv4) {
                        found.chassis = address_text(read32(value + 2));
                    }
                } else {
                    found.chassis = clean_text(value + 1, length - 1);
                }
                break;
            case kLldpPort:
                if (length < 2) {
                    break;
                }
                port_is_address = value[0] == kPortHardware || value[0] == kPortNetwork;
                if (value[0] == kPortHardware) {
                    port_id = length == 1 + kHardwareSize ? hardware_text(value + 1) : std::string();
                } else if (value[0] == kPortNetwork) {
                    port_id = length == 6 && value[1] == kFamilyIpv4 ? address_text(read32(value + 2)) : std::string();
                } else {
                    port_id = clean_text(value + 1, length - 1);
                }
                break;
            case kLldpPortDescription:
                port_description = clean_text(value, length);
                break;
            case kLldpSystemName:
                found.name = clean_text(value, length);
                break;
            case kLldpSystemDescription:
                found.model = clean_text(value, length);
                break;
            case kLldpManagement:
                // Address length (its subtype and the address), subtype,
                // address, then the interface and an OID: only an IPv4
                // address is read, the first one.
                if (found.address.empty() && length >= 6 && value[0] == 5 && value[1] == kFamilyIpv4) {
                    const uint32_t address = read32(value + 2);
                    if (device_address(address)) {
                        found.address = address_text(address);
                    }
                }
                break;
            case kLldpOrganisation:
                if (length < 4 || !std::equal(kIeee8021, kIeee8021 + 3, value)) {
                    break;
                }
                if (value[3] == kPortVlanId && length >= 6) {
                    const uint16_t id = read16(value + 4);
                    if (id >= 1 && id <= 4094) {
                        found.vlan = id;
                    }
                } else if (value[3] == kVlanName && length >= 7) {
                    add_vlan(found.vlans, read16(value + 4));
                }
                break;
            default:
                break;
        }
    }

    // A port ID that is only a hardware or network address says little;
    // the description ("Port 5", "eth4") says more then.
    found.port = (!port_id.empty() && !port_is_address) || port_description.empty() ? port_id : port_description;
    if (found.chassis.empty() && found.port.empty() && found.name.empty()) {
        return false;
    }
    out = std::move(found);
    return true;
}

bool parse_cdp(const uint8_t* frame, std::size_t size, model::Neighbour& out) {
    if (!frame || size < kCdpFirstField || !std::equal(kCdpGroup, kCdpGroup + kHardwareSize, frame) ||
        !unicast_hardware(frame + 6)) {
        return false;
    }
    const uint16_t length = read16(frame + 12);
    if (length >= kFirstEtherType || !std::equal(kCdpSnap, kCdpSnap + sizeof(kCdpSnap), frame + kEthernetHeader)) {
        return false;
    }
    // The 802.3 length says where CDP ends; padding after it is not read.
    const std::size_t end = std::min(size, kEthernetHeader + static_cast<std::size_t>(length));
    if (end < kCdpFirstField) {
        return false;
    }
    model::Neighbour found;
    found.protocol = "CDP";
    found.hardware_address = hardware_text(frame + 6);
    std::string any_address;
    std::string management_address;

    std::size_t at = kCdpFirstField;
    for (int fields = 0; fields < kMaxFields && end - at >= kCdpFieldHeader; ++fields) {
        const uint16_t type = read16(frame + at);
        const std::size_t field_size = read16(frame + at + 2);
        if (field_size < kCdpFieldHeader || field_size > end - at) {
            break;  // shorter than its own header (it would never move on), or past the end
        }
        const uint8_t* value = frame + at + kCdpFieldHeader;
        const std::size_t value_size = field_size - kCdpFieldHeader;
        at += field_size;
        switch (type) {
            case kCdpDeviceId:
                found.name = clean_text(value, value_size);
                break;
            case kCdpPortId:
                found.port = clean_text(value, value_size);
                break;
            case kCdpPlatform:
                found.model = clean_text(value, value_size);
                break;
            case kCdpNativeVlan:
                if (value_size >= 2) {
                    const uint16_t id = read16(value);
                    if (id >= 1 && id <= 4094) {
                        found.vlan = id;
                    }
                }
                break;
            case kCdpAddresses:
                if (any_address.empty()) {
                    any_address = cdp_address(value, value_size);
                }
                break;
            case kCdpManagement:
                if (management_address.empty()) {
                    management_address = cdp_address(value, value_size);
                }
                break;
            default:
                break;
        }
    }
    found.address = management_address.empty() ? any_address : management_address;
    if (found.name.empty() && found.port.empty()) {
        return false;
    }
    out = std::move(found);
    return true;
}

bool HeardNeighbours::note_frame(const uint8_t* frame, std::size_t size, int32_t at_s) {
    model::Neighbour heard;
    if (!parse_lldp(frame, size, heard) && !parse_cdp(frame, size, heard)) {
        return false;
    }
    heard.heard_s = at_s;
    const auto known = std::find_if(neighbours_.begin(), neighbours_.end(), [&heard](const model::Neighbour& entry) {
        return entry.hardware_address == heard.hardware_address;
    });
    if (known == neighbours_.end()) {
        if (neighbours_.size() >= model::kNeighboursMax) {
            return false;
        }
        neighbours_.push_back(std::move(heard));
        return true;
    }
    model::Neighbour merged = *known;
    const auto take = [](std::string& kept, const std::string& said) {
        if (!said.empty()) {
            kept = said;
        }
    };
    merged.protocol = heard.protocol;
    take(merged.chassis, heard.chassis);
    take(merged.name, heard.name);
    take(merged.port, heard.port);
    take(merged.model, heard.model);
    take(merged.address, heard.address);
    if (heard.vlan > 0) {
        merged.vlan = heard.vlan;
    }
    if (!heard.vlans.empty()) {
        merged.vlans = heard.vlans;
    }
    merged.heard_s = at_s;
    if (merged == *known) {
        return false;
    }
    *known = std::move(merged);
    return true;
}

const std::vector<PretendTopologyFrame>& pretend_topology_frames() {
    static const std::vector<PretendTopologyFrame> frames = [] {
        // A made-up switch (locally administered addresses) on the sample's
        // network, its port 5 on network (VLAN) 10.
        const std::vector<uint8_t> sender{0x02, 0x5c, 0x00, 0x00, 0x00, 0x05};
        const std::vector<uint8_t> chassis{0x02, 0x5c, 0x00, 0x00, 0x00, 0x00};
        std::vector<uint8_t> frame{0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e};
        frame.insert(frame.end(), sender.begin(), sender.end());
        put16(frame, kTypeLldp);
        std::vector<uint8_t> chassis_value{kChassisHardware};
        chassis_value.insert(chassis_value.end(), chassis.begin(), chassis.end());
        put_lldp_field(frame, kLldpChassis, chassis_value);
        put_lldp_field(frame, kLldpPort, with_text({7}, "5"));
        put_lldp_field(frame, 3, {0x00, 0x78});  // time to live: 120 s
        put_lldp_field(frame, kLldpPortDescription, with_text({}, "Port 5"));
        put_lldp_field(frame, kLldpSystemName, with_text({}, "office-sw"));
        put_lldp_field(frame, kLldpSystemDescription, with_text({}, "8-port managed switch\r\nfirmware 2.1"));
        put_lldp_field(frame, kLldpManagement, {5, kFamilyIpv4, 10, 0, 0, 2, 2, 0, 0, 0, 5, 0});
        put_lldp_field(frame, kLldpOrganisation, {kIeee8021[0], kIeee8021[1], kIeee8021[2], kPortVlanId, 0x00, 10});
        put_lldp_field(frame, kLldpOrganisation, vlan_name_field(1, "default"));
        put_lldp_field(frame, kLldpOrganisation, vlan_name_field(10, "office"));
        put_lldp_field(frame, kLldpOrganisation, vlan_name_field(20, "guests"));
        put_lldp_field(frame, kLldpEnd, {});
        std::vector<PretendTopologyFrame> list;
        for (uint32_t at_ms = 1800; at_ms <= static_cast<uint32_t>(model::kListenSeconds) * 1000u; at_ms += 30000) {
            list.push_back({at_ms, frame});
        }
        return list;
    }();
    return frames;
}

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "listen.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>

#if !USE_DESKTOP
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#endif

#ifndef APP_CAPTURE_PATH
#define APP_CAPTURE_PATH "/usr/libexec/lokictl/lokictl-capture"
#endif

namespace platform {
namespace {

// The PCAP file header and record header (microsecond times, this
// machine's byte order) and the one link type read: Ethernet.
constexpr uint32_t kPcapMagic = 0xa1b2c3d4u;
constexpr std::size_t kPcapFileHeader = 24;
constexpr std::size_t kPcapRecordHeader = 16;
constexpr uint32_t kLinkEthernet = 1;

constexpr std::size_t kEthernetHeader = 14;
constexpr uint16_t kTypeArp = 0x0806;
constexpr uint16_t kTypeIpv4 = 0x0800;
constexpr std::size_t kArpSize = 28;      // Ethernet over IPv4
constexpr uint8_t kProtocolUdp = 17;
constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDhcpClientPort = 68;
// BOOTP's fixed part, then the DHCP magic cookie and the options.
constexpr std::size_t kBootpFixed = 236;
constexpr uint32_t kDhcpCookie = 0x63825363u;
constexpr uint8_t kBootRequest = 1;
constexpr uint8_t kBootReply = 2;
constexpr uint8_t kOptionPad = 0;
constexpr uint8_t kOptionHostName = 12;
constexpr uint8_t kOptionRequestedAddress = 50;
constexpr uint8_t kOptionMessageType = 53;
constexpr uint8_t kOptionEnd = 255;
constexpr uint8_t kDhcpRequest = 3;
constexpr uint8_t kDhcpAck = 5;
constexpr std::size_t kMaxOwnPorts = 16;

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
}

uint32_t read32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

uint32_t native32(const uint8_t* bytes) {
    uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

// A hardware address that can belong to one device: not a group address
// (lowest bit of the first byte) and not all zeros.
bool unicast_hardware(const uint8_t* hardware) {
    if ((hardware[0] & 1U) != 0) {
        return false;
    }
    for (int index = 0; index < 6; ++index) {
        if (hardware[index] != 0) {
            return true;
        }
    }
    return false;
}

// An IPv4 address one device can have: not 0.0.0.0, not a multicast or
// reserved address (224.0.0.0 and up), not 127/8.
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

// A DHCP host name as shown: the first label, at most kHostNameMax
// characters, printable ASCII only (anything else becomes '?').
std::string host_name(const uint8_t* bytes, std::size_t size) {
    std::string name;
    for (std::size_t index = 0; index < size && name.size() < model::kHostNameMax; ++index) {
        const uint8_t character = bytes[index];
        if (character == '.' || character == 0) {
            break;
        }
        name.push_back(character >= 0x20 && character < 0x7f ? static_cast<char>(character) : '?');
    }
    return name;
}

} // namespace

std::vector<OwnPort> read_own_ports(const std::string& root) {
    std::vector<OwnPort> ports;
    DIR* directory = opendir(root.c_str());
    if (!directory) {
        return ports;
    }
    while (const dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || name == "lo" || ports.size() >= kMaxOwnPorts) {
            continue;
        }
        std::ifstream file(root + "/" + name + "/address");
        std::string address;
        if (!(file >> address) || address.size() != 17 || address == "00:00:00:00:00:00") {
            continue;
        }
        std::transform(address.begin(), address.end(), address.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        ports.push_back({name, address});
    }
    closedir(directory);
    std::sort(ports.begin(), ports.end(),
              [](const OwnPort& left, const OwnPort& right) { return left.name < right.name; });
    return ports;
}

// ---------------------------------------------------------------------------
// CaptureReader

bool CaptureReader::feed(const uint8_t* data, std::size_t size, const FrameFn& on_frame) {
    if (broken_) {
        return false;
    }
    pending_.insert(pending_.end(), data, data + size);
    std::size_t used = 0;
    if (!header_read_) {
        if (pending_.size() < kPcapFileHeader) {
            return true;
        }
        const uint8_t* header = pending_.data();
        const bool magic_ok = native32(header) == kPcapMagic;
        uint16_t major = 0;
        uint16_t minor = 0;
        std::memcpy(&major, header + 4, sizeof(major));
        std::memcpy(&minor, header + 6, sizeof(minor));
        const uint32_t snap = native32(header + 16);
        const uint32_t link = native32(header + 20);
        if (!magic_ok || major != 2 || minor != 4 || snap == 0 || snap > kCaptureSnapLength ||
            link != kLinkEthernet) {
            broken_ = true;
            pending_.clear();
            return false;
        }
        header_read_ = true;
        used = kPcapFileHeader;
    }
    while (pending_.size() - used >= kPcapRecordHeader) {
        const uint8_t* record = pending_.data() + used;
        const uint32_t seconds = native32(record);
        const uint32_t captured = native32(record + 8);
        if (captured > kCaptureSnapLength) {
            broken_ = true;
            pending_.clear();
            return false;
        }
        if (pending_.size() - used - kPcapRecordHeader < captured) {
            break;  // the rest of the frame comes later
        }
        on_frame(record + kPcapRecordHeader, captured, seconds);
        used += kPcapRecordHeader + captured;
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(used));
    return true;
}

// ---------------------------------------------------------------------------
// HeardDevices

HeardDevices::HeardDevices(std::vector<OwnPort> own_ports) : own_ports_(std::move(own_ports)) {}

bool HeardDevices::note_frame(const uint8_t* frame, std::size_t size, int32_t at_s) {
    ++frames_;
    if (size < kEthernetHeader) {
        return false;
    }
    const uint16_t type = read16(frame + 12);
    if (type == kTypeArp) {
        return note_arp(frame + kEthernetHeader, size - kEthernetHeader, at_s);
    }
    if (type == kTypeIpv4) {
        return note_ipv4(frame, size, at_s);
    }
    return false;
}

bool HeardDevices::note_arp(const uint8_t* arp, std::size_t size, int32_t at_s) {
    // Hardware type Ethernet (1), protocol IPv4, 6-byte and 4-byte
    // addresses, a request (1) or a reply (2).
    if (size < kArpSize || read16(arp) != 1 || read16(arp + 2) != kTypeIpv4 || arp[4] != 6 || arp[5] != 4) {
        return false;
    }
    const uint16_t operation = read16(arp + 6);
    if (operation != 1 && operation != 2) {
        return false;
    }
    const uint8_t* sender = arp + 8;
    const uint32_t sender_address = read32(arp + 14);
    return note(sender, sender_address, std::string(), at_s);
}

bool HeardDevices::note_ipv4(const uint8_t* frame, std::size_t size, int32_t at_s) {
    const uint8_t* packet = frame + kEthernetHeader;
    std::size_t length = size - kEthernetHeader;
    if (length < 20 || (packet[0] >> 4) != 4) {
        return false;
    }
    const std::size_t header = static_cast<std::size_t>(packet[0] & 0x0fU) * 4;
    const std::size_t total = read16(packet + 2);
    if (header < 20 || header > length || total < header) {
        return false;
    }
    // Only the bytes of this packet count (a short frame is padded).
    length = std::min(length, total);
    // UDP only, and no fragments (no "more fragments" flag, offset 0).
    if (packet[9] != kProtocolUdp || (read16(packet + 6) & 0x3fffU) != 0) {
        return false;
    }
    const uint32_t source = read32(packet + 12);
    const uint8_t* udp = packet + header;
    const std::size_t udp_room = length - header;
    if (udp_room < 8) {
        return false;
    }
    const uint16_t source_port = read16(udp);
    const uint16_t destination_port = read16(udp + 2);
    const std::size_t udp_length = read16(udp + 4);
    const auto dhcp_port = [](uint16_t port) { return port == kDhcpServerPort || port == kDhcpClientPort; };
    if (!dhcp_port(source_port) || !dhcp_port(destination_port) || udp_length < 8) {
        return false;
    }
    const std::size_t payload = std::min(udp_length, udp_room) - 8;
    return note_dhcp(udp + 8, payload, frame + 6, source, at_s);
}

bool HeardDevices::note_dhcp(const uint8_t* bootp,
                             std::size_t size,
                             const uint8_t* sender,
                             uint32_t source,
                             int32_t at_s) {
    // Hardware type Ethernet, a 6-byte address, and the magic cookie.
    if (size < kBootpFixed + 4 || bootp[1] != 1 || bootp[2] != 6 || read32(bootp + kBootpFixed) != kDhcpCookie) {
        return false;
    }
    const uint8_t operation = bootp[0];
    const uint32_t client_address = read32(bootp + 12);  // ciaddr
    const uint32_t your_address = read32(bootp + 16);    // yiaddr
    const uint8_t* client = bootp + 28;                  // chaddr

    uint8_t message = 0;
    uint32_t requested = 0;
    std::string name;
    // The options, read only while each is whole; one running past the end
    // ends the reading and keeps what came before it.
    std::size_t index = kBootpFixed + 4;
    while (index < size) {
        const uint8_t code = bootp[index];
        if (code == kOptionPad) {
            ++index;
            continue;
        }
        if (code == kOptionEnd || index + 1 >= size) {
            break;
        }
        const std::size_t length = bootp[index + 1];
        if (index + 2 + length > size) {
            break;
        }
        const uint8_t* value = bootp + index + 2;
        if (code == kOptionMessageType && length >= 1) {
            message = value[0];
        } else if (code == kOptionHostName && length >= 1) {
            name = host_name(value, length);
        } else if (code == kOptionRequestedAddress && length == 4) {
            requested = read32(value);
        }
        index += 2 + length;
    }

    bool changed = false;
    if (operation == kBootRequest) {
        uint32_t address = client_address;
        if (address == 0 && message == kDhcpRequest) {
            address = requested;
        }
        changed = note(client, address, name, at_s);
    } else if (operation == kBootReply) {
        if (message == kDhcpAck && your_address != 0) {
            changed = note(client, your_address, std::string(), at_s);
        }
        // The server (or the relay that passed the answer on).
        if (note(sender, source, std::string(), at_s)) {
            changed = true;
        }
    }
    return changed;
}

bool HeardDevices::note(const uint8_t* hardware, uint32_t address, const std::string& name, int32_t at_s) {
    if (!unicast_hardware(hardware)) {
        return false;
    }
    auto found = std::find_if(entries_.begin(), entries_.end(),
                              [hardware](const Entry& entry) { return std::memcmp(entry.hardware, hardware, 6) == 0; });
    if (found == entries_.end()) {
        if (entries_.size() >= model::kListenMaxDevices) {
            return false;
        }
        Entry entry;
        std::memcpy(entry.hardware, hardware, 6);
        const std::string text = hardware_text(hardware);
        for (const OwnPort& port : own_ports_) {
            if (port.hardware_address == text) {
                entry.own_port = port.name;
                break;
            }
        }
        entries_.push_back(entry);
        found = entries_.end() - 1;
    }
    bool changed = false;
    if (device_address(address) && found->address != address) {
        found->address = address;
        changed = true;
    }
    if (!name.empty() && found->name != name) {
        found->name = name;
        changed = true;
    }
    if (found->heard_s != at_s || changed || devices_.size() != entries_.size()) {
        found->heard_s = at_s;
        rebuild();
        return true;
    }
    return false;
}

void HeardDevices::rebuild() {
    std::vector<const Entry*> order;
    order.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        order.push_back(&entry);
    }
    std::sort(order.begin(), order.end(), [](const Entry* left, const Entry* right) {
        if ((left->address == 0) != (right->address == 0)) {
            return left->address != 0;
        }
        if (left->address != right->address) {
            return left->address < right->address;
        }
        return std::memcmp(left->hardware, right->hardware, 6) < 0;
    });
    devices_.clear();
    for (const Entry* entry : order) {
        model::HeardDevice device;
        device.hardware_address = hardware_text(entry->hardware);
        device.address = entry->address != 0 ? address_text(entry->address) : std::string();
        device.name = entry->name;
        device.own_port = entry->own_port;
        device.heard_s = entry->heard_s;
        devices_.push_back(std::move(device));
    }
}

// ---------------------------------------------------------------------------
// The pretend capture.

namespace {

using Hardware = std::array<uint8_t, 6>;

constexpr Hardware kPretendCable{0x02, 0x4c, 0x4b, 0x00, 0x00, 0x01};  // home_examples' cable port
constexpr Hardware kPretendWifi{0x02, 0x4c, 0x4b, 0x00, 0x00, 0x02};   // and its Wi-Fi port
constexpr Hardware kPretendRouter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x01};
constexpr Hardware kPretendTv{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x31};
constexpr Hardware kPretendPrinter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x24};
constexpr Hardware kPretendPhone{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x42};
constexpr Hardware kPretendSpeaker{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x57};
constexpr Hardware kBroadcast{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

constexpr uint32_t ipv4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// The router repeats its ARP question this often.
constexpr uint32_t kPretendRouterPeriodMs = 10000;

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xffU));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value >> 16));
    put16(out, static_cast<uint16_t>(value & 0xffffU));
}

void put_native32(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t bytes[4];
    std::memcpy(bytes, &value, sizeof(bytes));
    out.insert(out.end(), bytes, bytes + 4);
}

void put_hardware(std::vector<uint8_t>& out, const Hardware& hardware) {
    out.insert(out.end(), hardware.begin(), hardware.end());
}

std::vector<uint8_t> ethernet(const Hardware& to, const Hardware& from, uint16_t type) {
    std::vector<uint8_t> frame;
    put_hardware(frame, to);
    put_hardware(frame, from);
    put16(frame, type);
    return frame;
}

std::vector<uint8_t> arp_frame(const Hardware& sender, uint32_t sender_address, uint32_t target_address) {
    std::vector<uint8_t> frame = ethernet(kBroadcast, sender, kTypeArp);
    put16(frame, 1);
    put16(frame, kTypeIpv4);
    frame.push_back(6);
    frame.push_back(4);
    put16(frame, 1);
    put_hardware(frame, sender);
    put32(frame, sender_address);
    put_hardware(frame, Hardware{});
    put32(frame, target_address);
    frame.resize(60, 0);  // padded to Ethernet's shortest frame
    return frame;
}

struct PretendDhcp {
    uint8_t operation{kBootRequest};
    uint8_t message{kDhcpRequest};
    Hardware client{};
    Hardware sender{};
    uint32_t source{0};
    uint32_t your_address{0};
    uint32_t requested{0};
    std::string name;
    bool bad_option{false};  // ends with an option that runs past the packet
};

std::vector<uint8_t> dhcp_frame(const PretendDhcp& dhcp) {
    std::vector<uint8_t> bootp;
    bootp.push_back(dhcp.operation);
    bootp.push_back(1);
    bootp.push_back(6);
    bootp.push_back(0);
    put32(bootp, 0x5eed0000u | dhcp.client[5]);  // xid
    put16(bootp, 0);
    put16(bootp, 0x8000);  // broadcast
    put32(bootp, 0);       // ciaddr
    put32(bootp, dhcp.your_address);
    put32(bootp, 0);
    put32(bootp, 0);
    put_hardware(bootp, dhcp.client);
    bootp.resize(kBootpFixed, 0);
    put32(bootp, kDhcpCookie);
    bootp.push_back(kOptionMessageType);
    bootp.push_back(1);
    bootp.push_back(dhcp.message);
    if (dhcp.requested != 0) {
        bootp.push_back(kOptionRequestedAddress);
        bootp.push_back(4);
        put32(bootp, dhcp.requested);
    }
    if (!dhcp.name.empty()) {
        bootp.push_back(kOptionHostName);
        bootp.push_back(static_cast<uint8_t>(dhcp.name.size()));
        bootp.insert(bootp.end(), dhcp.name.begin(), dhcp.name.end());
    }
    if (dhcp.bad_option) {
        bootp.push_back(kOptionHostName);
        bootp.push_back(200);  // far more than is left
        bootp.push_back('x');
    } else {
        bootp.push_back(kOptionEnd);
    }

    const bool from_server = dhcp.operation == kBootReply;
    std::vector<uint8_t> frame = ethernet(kBroadcast, dhcp.sender, kTypeIpv4);
    const auto udp_length = static_cast<uint16_t>(8 + bootp.size());
    frame.push_back(0x45);
    frame.push_back(0);
    put16(frame, static_cast<uint16_t>(20 + udp_length));
    put32(frame, 0);  // id, flags, fragment offset
    frame.push_back(64);
    frame.push_back(kProtocolUdp);
    put16(frame, 0);  // checksum (not checked)
    put32(frame, dhcp.source);
    put32(frame, 0xffffffffu);
    put16(frame, from_server ? kDhcpServerPort : kDhcpClientPort);
    put16(frame, from_server ? kDhcpClientPort : kDhcpServerPort);
    put16(frame, udp_length);
    put16(frame, 0);
    frame.insert(frame.end(), bootp.begin(), bootp.end());
    return frame;
}

struct PretendFrame {
    uint32_t at_ms;
    std::vector<uint8_t> bytes;
};

const std::vector<PretendFrame>& pretend_frames() {
    static const std::vector<PretendFrame> frames = [] {
        const uint32_t router = ipv4(10, 0, 0, 1);
        std::vector<PretendFrame> list;
        // This device (the cable port) asks for the router; the router asks
        // back.
        list.push_back({150, arp_frame(kPretendCable, ipv4(10, 0, 0, 12), router)});
        list.push_back({300, arp_frame(kPretendRouter, router, ipv4(10, 0, 0, 12))});
        list.push_back({900, arp_frame(kPretendTv, ipv4(10, 0, 0, 31), router)});
        // A printer renews its address, and the router agrees.
        PretendDhcp printer;
        printer.client = kPretendPrinter;
        printer.sender = kPretendPrinter;
        printer.requested = ipv4(10, 0, 0, 24);
        printer.name = "printer";
        list.push_back({1400, dhcp_frame(printer)});
        PretendDhcp ack;
        ack.operation = kBootReply;
        ack.message = kDhcpAck;
        ack.client = kPretendPrinter;
        ack.sender = kPretendRouter;
        ack.source = router;
        ack.your_address = ipv4(10, 0, 0, 24);
        list.push_back({1500, dhcp_frame(ack)});
        // A frame cut short in the middle of its ARP part: ignored.
        std::vector<uint8_t> cut = arp_frame(kPretendSpeaker, ipv4(10, 0, 0, 99), router);
        cut.resize(kEthernetHeader + 10);
        list.push_back({2100, cut});
        // This device's Wi-Fi port, on the same network.
        list.push_back({2600, arp_frame(kPretendWifi, ipv4(10, 0, 0, 13), router)});
        // A phone joins: it looks for a server (its name has a domain and
        // a broken last option), then asks for its address.
        PretendDhcp discover;
        discover.message = 1;
        discover.client = kPretendPhone;
        discover.sender = kPretendPhone;
        discover.name = "pixel-7.lan";
        discover.bad_option = true;
        list.push_back({3200, dhcp_frame(discover)});
        PretendDhcp request;
        request.client = kPretendPhone;
        request.sender = kPretendPhone;
        request.requested = ipv4(10, 0, 0, 42);
        request.name = "pixel-7";
        list.push_back({3300, dhcp_frame(request)});
        // A speaker checks that an address is free (a probe, no address),
        // then takes it.
        list.push_back({4000, arp_frame(kPretendSpeaker, 0, ipv4(10, 0, 0, 57))});
        list.push_back({7000, arp_frame(kPretendSpeaker, ipv4(10, 0, 0, 57), ipv4(10, 0, 0, 57))});
        return list;
    }();
    return frames;
}

void put_record(std::vector<uint8_t>& out, uint32_t at_ms, const std::vector<uint8_t>& frame) {
    put_native32(out, at_ms / 1000);
    put_native32(out, (at_ms % 1000) * 1000);
    put_native32(out, static_cast<uint32_t>(frame.size()));
    put_native32(out, static_cast<uint32_t>(frame.size()));
    out.insert(out.end(), frame.begin(), frame.end());
}

} // namespace

std::vector<uint8_t> pretend_capture(uint32_t elapsed_ms) {
    std::vector<uint8_t> out;
    put_native32(out, kPcapMagic);
    const uint16_t version[2] = {2, 4};
    uint8_t version_bytes[4];
    std::memcpy(version_bytes, version, sizeof(version_bytes));
    out.insert(out.end(), version_bytes, version_bytes + 4);
    put_native32(out, 0);
    put_native32(out, 0);
    put_native32(out, kCaptureSnapLength);
    put_native32(out, kLinkEthernet);

    // The fixed frames and the router's repeated question, in time order.
    const std::vector<PretendFrame>& frames = pretend_frames();
    std::size_t next = 0;
    uint32_t router_at = kPretendRouterPeriodMs + 300;
    const std::vector<uint8_t> router_question = arp_frame(kPretendRouter, ipv4(10, 0, 0, 1), ipv4(10, 0, 0, 31));
    while (true) {
        const bool fixed_left = next < frames.size() && frames[next].at_ms <= elapsed_ms;
        const bool router_left = router_at <= elapsed_ms;
        if (!fixed_left && !router_left) {
            break;
        }
        if (fixed_left && (!router_left || frames[next].at_ms <= router_at)) {
            put_record(out, frames[next].at_ms, frames[next].bytes);
            ++next;
        } else {
            put_record(out, router_at, router_question);
            router_at += kPretendRouterPeriodMs;
        }
    }
    return out;
}

std::vector<OwnPort> pretend_own_ports() {
    return {{"eth0", hardware_text(kPretendCable.data())}, {"wlan0", hardware_text(kPretendWifi.data())}};
}

// ---------------------------------------------------------------------------
// The capture helper (device builds).

#if !USE_DESKTOP

namespace {

constexpr auto kReapStep = std::chrono::milliseconds(10);

} // namespace

CaptureProcess::~CaptureProcess() {
    stop();
}

bool CaptureProcess::start(const std::string& interface_name,
                           int32_t seconds,
                           model::ToolError& error,
                           std::string& reason) {
    if (pid_ > 0 || !model::valid_interface_name(interface_name)) {
        error = model::ToolError::Failed;
        reason = "interface not usable";
        return false;
    }
    int ends[2] = {-1, -1};
    if (pipe2(ends, O_CLOEXEC) != 0) {
        error = model::ToolError::Failed;
        reason = "no pipe";
        return false;
    }
    // Only this end stays here; it never blocks the runner's loop.
    fcntl(ends[0], F_SETFL, fcntl(ends[0], F_GETFL) | O_NONBLOCK);

    std::string words[] = {"lokictl-capture", "discover", interface_name, std::to_string(seconds)};
    char* argv[] = {words[0].data(), words[1].data(), words[2].data(), words[3].data(), nullptr};
    char* environment[] = {nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    // dup2 clears close-on-exec on the copy, so only fd 1 reaches the
    // helper; both ends of the pipe themselves are closed on exec.
    posix_spawn_file_actions_adddup2(&actions, ends[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    sigset_t no_signals;
    sigset_t default_signals;
    sigemptyset(&no_signals);
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGTERM);
    sigaddset(&default_signals, SIGINT);
    sigaddset(&default_signals, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    posix_spawnattr_setsigdefault(&attributes, &default_signals);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    pid_t child = -1;
    const int spawned = posix_spawn(&child, APP_CAPTURE_PATH, &actions, &attributes, argv, environment);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(ends[1]);
    if (spawned != 0) {
        close(ends[0]);
        if (spawned == EACCES || spawned == EPERM) {
            error = model::ToolError::NotAllowed;
            reason.clear();
        } else {
            error = model::ToolError::Failed;
            reason = spawned == ENOENT ? "capture helper not installed" : "capture helper could not start";
        }
        return false;
    }
    fd_ = ends[0];
    pid_ = child;
    return true;
}

void CaptureProcess::close_pipe() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

int CaptureProcess::reap(int timeout_ms) {
    if (pid_ <= 0) {
        return -1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(pid_, &status, WNOHANG);
        if (done == pid_) {
            break;
        }
        if (done < 0 && errno != EINTR) {
            pid_ = -1;
            return -1;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid_, SIGKILL);
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
            }
            pid_ = -1;
            return -1;
        }
        std::this_thread::sleep_for(kReapStep);
    }
    pid_ = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void CaptureProcess::stop() {
    close_pipe();
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        reap(100);
    }
}

int CaptureProcess::finish() {
    close_pipe();
    return reap(500);
}

bool capture_exit_ok(int status, model::ToolError& error, std::string& reason) {
    switch (status) {
        case 0:
            return true;
        case 2:
            error = model::ToolError::Failed;
            reason = "interface not usable";
            return false;
        case 3:
            error = model::ToolError::NotAllowed;
            reason.clear();
            return false;
        case 4:
            error = model::ToolError::Failed;
            reason = "capture could not start";
            return false;
        default:
            error = model::ToolError::Failed;
            reason = "capture helper ended";
            return false;
    }
}

#endif

} // namespace platform

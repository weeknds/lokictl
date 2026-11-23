/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "talk.h"

#include "dns_message.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace platform {

namespace {

constexpr std::size_t kEthernetHeader = 14;
constexpr std::size_t kIpv4MinHeader = 20;
constexpr std::size_t kUdpHeader = 8;
constexpr std::size_t kTcpMinHeader = 20;
constexpr std::size_t kDnsHeader = 12;
constexpr uint16_t kEtherTypeIpv4 = 0x0800;
constexpr uint8_t kProtocolTcp = 6;
constexpr uint8_t kProtocolUdp = 17;
constexpr uint16_t kDnsPort = 53;

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

std::string hardware_text(const uint8_t* bytes) {
    char text[18] = {};
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3],
                  bytes[4], bytes[5]);
    return text;
}

std::string ipv4_text(const uint8_t* bytes) {
    char text[16] = {};
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
    return text;
}

// The first question's name of the DNS message `data`, when it is a
// standard query (not an answer, opcode 0) with a question that is not the
// root.
bool read_query(const uint8_t* data, std::size_t size, std::string& name) {
    if (size < kDnsHeader) {
        return false;
    }
    const uint8_t flags = data[2];
    if ((flags & 0x80) != 0 || ((flags >> 3) & 0x0F) != 0 || read_u16(data + 4) == 0) {
        return false;
    }
    dns::Message message;
    if (!dns::parse_message(data, size, message) || message.question_name.empty()) {
        return false;
    }
    name = model::talk_clean_name(message.question_name);
    return true;
}

} // namespace

bool read_talk_question(const uint8_t* frame, std::size_t size, TalkQuestion& out) {
    out = TalkQuestion{};
    if (frame == nullptr || size < kEthernetHeader + kIpv4MinHeader || read_u16(frame + 12) != kEtherTypeIpv4) {
        return false;
    }
    const uint8_t* sender = frame + 6;
    if ((sender[0] & 0x01) != 0 || std::all_of(sender, sender + 6, [](uint8_t byte) { return byte == 0; })) {
        return false;
    }

    const uint8_t* ip = frame + kEthernetHeader;
    const std::size_t ip_room = size - kEthernetHeader;
    const std::size_t header = static_cast<std::size_t>(ip[0] & 0x0F) * 4u;
    if ((ip[0] >> 4) != 4 || header < kIpv4MinHeader || header > ip_room) {
        return false;
    }
    const std::size_t total = read_u16(ip + 2);
    if (total < header || (read_u16(ip + 6) & 0x3FFF) != 0) {
        return false;
    }
    // A frame may carry padding after the packet, or be cut short.
    const std::size_t end = std::min(total, ip_room);
    const uint8_t* transport = ip + header;
    const std::size_t transport_size = end - header;

    const uint8_t* message = nullptr;
    std::size_t message_size = 0;
    if (ip[9] == kProtocolUdp) {
        if (transport_size < kUdpHeader || read_u16(transport + 2) != kDnsPort) {
            return false;
        }
        const std::size_t length = read_u16(transport + 4);
        if (length < kUdpHeader) {
            return false;
        }
        message = transport + kUdpHeader;
        message_size = std::min(length, transport_size) - kUdpHeader;
    } else if (ip[9] == kProtocolTcp) {
        if (transport_size < kTcpMinHeader || read_u16(transport + 2) != kDnsPort) {
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(transport[12] >> 4) * 4u;
        if (offset < kTcpMinHeader || offset > transport_size) {
            return false;
        }
        const uint8_t* data = transport + offset;
        const std::size_t data_size = transport_size - offset;
        // The two-byte length, then the whole message in this segment.
        if (data_size < 2) {
            return false;
        }
        const std::size_t length = read_u16(data);
        if (length > data_size - 2) {
            return false;
        }
        message = data + 2;
        message_size = length;
    } else {
        return false;
    }

    std::string name;
    if (!read_query(message, message_size, name)) {
        return false;
    }
    out.hardware = hardware_text(sender);
    out.address = ipv4_text(ip + 12);
    out.name = std::move(name);
    return true;
}

void TalkCounter::note_frame(const uint8_t* frame, std::size_t size, int64_t at_ms) {
    TalkQuestion question;
    if (read_talk_question(frame, size, question)) {
        note_question(question, at_ms);
    }
}

void TalkCounter::note_question(const TalkQuestion& question, int64_t at_ms) {
    if (question.hardware.empty() || question.name.empty()) {
        return;
    }
    ++frames_;
    changed_ = true;

    auto device = std::find_if(devices_.begin(), devices_.end(),
                               [&question](const Device& entry) { return entry.hardware == question.hardware; });
    if (device == devices_.end()) {
        if (devices_.size() >= model::kTalkDevicesMax) {
            devices_.erase(std::min_element(devices_.begin(), devices_.end(),
                                            [](const Device& left, const Device& right) {
                                                return left.last_ms < right.last_ms;
                                            }));
        }
        devices_.push_back(Device{question.hardware, {}, {}, 0, 0});
        device = devices_.end() - 1;
    }
    device->address = question.address;
    device->last_ms = at_ms;

    auto name = std::find_if(device->names.begin(), device->names.end(),
                             [&question](const Name& entry) { return entry.name == question.name; });
    if (name != device->names.end()) {
        if (at_ms >= name->counted_ms && at_ms - name->counted_ms < model::kTalkSameLookupMs) {
            return;  // the same lookup (A and AAAA, a repeat)
        }
        ++name->count;
        name->counted_ms = at_ms;
        std::rotate(device->names.begin(), name, name + 1);
    } else {
        device->names.insert(device->names.begin(), Name{question.name, 1, at_ms});
        if (device->names.size() > model::kTalkNamesMax) {
            device->names.pop_back();
        }
    }
    ++device->lookups;
    ++lookups_;
}

void TalkCounter::fill(model::TalkRun& run) const {
    run.frames = frames_;
    run.lookups = lookups_;
    run.devices.clear();
    run.devices.reserve(devices_.size());
    for (const Device& device : devices_) {
        model::TalkDevice shown;
        shown.hardware = device.hardware;
        shown.address = device.address;
        shown.lookups = device.lookups;
        shown.names.reserve(device.names.size());
        for (const Name& name : device.names) {
            shown.names.push_back(model::TalkName{name.name, name.count});
        }
        run.devices.push_back(std::move(shown));
    }
}

bool TalkCounter::take_changed() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

std::vector<uint8_t> talk_question_frame(const uint8_t* hardware, uint32_t source, uint32_t destination,
                                         const std::string& name, uint16_t type, bool tcp) {
    std::vector<uint8_t> message;
    if (!dns::build_query(0x4c4b, name, type, message)) {
        return {};
    }
    std::vector<uint8_t> frame;
    // Ethernet: to the hotspot (a made-up, locally administered address),
    // from the device.
    const uint8_t hotspot[6] = {0x02, 0x00, 0x00, 0x00, 0x7f, 0x01};
    frame.insert(frame.end(), hotspot, hotspot + 6);
    frame.insert(frame.end(), hardware, hardware + 6);
    frame.push_back(0x08);
    frame.push_back(0x00);

    const std::size_t transport_size = tcp ? kTcpMinHeader + 2 : kUdpHeader;
    const std::size_t total = kIpv4MinHeader + transport_size + message.size();
    const auto put16 = [&frame](std::size_t value) {
        frame.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(value & 0xFF));
    };
    const auto put32 = [&frame](uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
        }
    };
    frame.push_back(0x45);
    frame.push_back(0x00);
    put16(total);
    put16(0x1234);  // identification
    put16(0x4000);  // don't fragment
    frame.push_back(64);
    frame.push_back(tcp ? kProtocolTcp : kProtocolUdp);
    put16(0);  // checksum: not read
    put32(source);
    put32(destination);
    if (tcp) {
        put16(40001);
        put16(kDnsPort);
        put32(1);           // sequence
        put32(1);           // acknowledgement
        frame.push_back(0x50);  // data offset: 20 bytes
        frame.push_back(0x18);  // PSH, ACK
        put16(64240);       // window
        put16(0);           // checksum
        put16(0);           // urgent
        put16(message.size());
    } else {
        put16(40001);
        put16(kDnsPort);
        put16(kUdpHeader + message.size());
        put16(0);
    }
    frame.insert(frame.end(), message.begin(), message.end());
    return frame;
}

const std::vector<TalkFrame>& pretend_talk_frames() {
    static const std::vector<TalkFrame> frames = [] {
        const uint8_t phone[6] = {0x02, 0x5e, 0x11, 0x7a, 0xc3, 0x01};
        const uint8_t laptop[6] = {0x02, 0x5e, 0x11, 0x7a, 0xc3, 0x02};
        const uint8_t tv[6] = {0x02, 0x5e, 0x11, 0x7a, 0xc3, 0x03};
        constexpr uint32_t kHotspot = 0x0a2a0001;  // 10.42.0.1, the sample's hotspot
        constexpr uint32_t kPhone = 0x0a2a000b;    // 10.42.0.11
        constexpr uint32_t kLaptop = 0x0a2a000e;   // 10.42.0.14
        constexpr uint32_t kTv = 0x0a2a0017;       // 10.42.0.23
        struct Ask {
            uint32_t at_ms;
            const uint8_t* hardware;
            uint32_t source;
            const char* name;
            uint16_t type;
            bool tcp;
        };
        const Ask asks[] = {
            {0, phone, kPhone, "example.com", 1, false},
            {0, phone, kPhone, "example.com", 28, false},  // AAAA: the same lookup
            {0, phone, kPhone, "www.example.org", 1, false},
            {0, laptop, kLaptop, "updates.example.net", 1, false},
            {0, phone, kPhone, "time.example.net", 1, false},
            {0, tv, kTv, "stream.example.org", 1, false},
            {0, phone, kPhone, "photos.example.com", 1, false},
            {0, laptop, kLaptop, "docs.example.org", 1, false},
            {0, phone, kPhone, "a1b2c3d4e5f6a7b8c9d0.metrics.cdn.example.net", 1, false},
            {0, tv, kTv, "ads.example.com", 1, false},
            {0, phone, kPhone, "mail.example.org", 1, false},
            {0, tv, kTv, "config.example.net", 1, true},  // over TCP
            {2500, phone, kPhone, "example.com", 1, false},
            {5000, phone, kPhone, "example.com", 1, false},
            {5000, laptop, kLaptop, "example.com", 1, false},
            {8000, phone, kPhone, "chat.example.com", 1, false},
            {12000, tv, kTv, "ads.example.com", 1, false},
        };
        std::vector<TalkFrame> out;
        for (const Ask& ask : asks) {
            out.push_back(TalkFrame{ask.at_ms, talk_question_frame(ask.hardware, ask.source, kHotspot, ask.name,
                                                                   ask.type, ask.tcp)});
        }
        return out;
    }();
    return frames;
}

} // namespace platform

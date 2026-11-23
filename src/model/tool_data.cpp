/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_data.h"

#include <algorithm>

namespace model {
namespace {

// An IPv4 address in dotted form as a number, for sorting; 0 when `text`
// is not one.
uint32_t address_number(const std::string& text) {
    if (!is_ipv4_text(text)) {
        return 0;
    }
    uint32_t number = 0;
    uint32_t part = 0;
    for (const char character : text) {
        if (character == '.') {
            number = (number << 8) | part;
            part = 0;
        } else {
            part = part * 10 + static_cast<uint32_t>(character - '0');
        }
    }
    return (number << 8) | part;
}

} // namespace

const char* tool_name(ToolKind kind) {
    switch (kind) {
        case ToolKind::Ping:
            return "ping";
        case ToolKind::Dns:
            return "dns";
        case ToolKind::Trace:
            return "trace";
        case ToolKind::Listen:
            return "listen";
        case ToolKind::Probe:
            return "probe";
    }
    return "ping";
}

bool operator==(const PingReply& left, const PingReply& right) {
    return left.sequence == right.sequence && left.time_us == right.time_us;
}

bool operator!=(const PingReply& left, const PingReply& right) {
    return !(left == right);
}

bool operator==(const PingRun& left, const PingRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.target == right.target && left.address == right.address && left.sent == right.sent &&
           left.received == right.received && left.lost == right.lost && left.min_us == right.min_us &&
           left.avg_us == right.avg_us && left.max_us == right.max_us && left.replies == right.replies;
}

bool operator!=(const PingRun& left, const PingRun& right) {
    return !(left == right);
}

bool operator==(const DnsRun& left, const DnsRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.name == right.name && left.server == right.server && left.addresses == right.addresses &&
           left.time_ms == right.time_ms;
}

bool operator!=(const DnsRun& left, const DnsRun& right) {
    return !(left == right);
}

bool operator==(const TraceHop& left, const TraceHop& right) {
    return left.ttl == right.ttl && left.address == right.address && left.name == right.name &&
           left.time_us == right.time_us;
}

bool operator!=(const TraceHop& left, const TraceHop& right) {
    return !(left == right);
}

bool operator==(const TraceRun& left, const TraceRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.target == right.target && left.address == right.address && left.hops == right.hops &&
           left.end == right.end;
}

bool operator!=(const TraceRun& left, const TraceRun& right) {
    return !(left == right);
}

bool operator==(const HeardDevice& left, const HeardDevice& right) {
    return left.hardware_address == right.hardware_address && left.address == right.address &&
           left.name == right.name && left.kind == right.kind && left.own_port == right.own_port &&
           left.heard_s == right.heard_s;
}

bool operator!=(const HeardDevice& left, const HeardDevice& right) {
    return !(left == right);
}

bool operator==(const ListenRun& left, const ListenRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.interface_name == right.interface_name && left.elapsed_s == right.elapsed_s &&
           left.frames == right.frames && left.devices == right.devices && left.save == right.save &&
           left.save_name == right.save_name && left.save_reason == right.save_reason &&
           left.save_bytes == right.save_bytes && left.save_full == right.save_full;
}

bool operator!=(const ListenRun& left, const ListenRun& right) {
    return !(left == right);
}

bool operator==(const ProbeRun& left, const ProbeRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.interface_name == right.interface_name && left.network == right.network &&
           left.step == right.step && left.total == right.total && left.asked == right.asked &&
           left.replied == right.replied && left.elapsed_s == right.elapsed_s && left.devices == right.devices;
}

bool operator!=(const ProbeRun& left, const ProbeRun& right) {
    return !(left == right);
}

bool operator==(const ToolsData& left, const ToolsData& right) {
    return left.ping == right.ping && left.dns == right.dns && left.trace == right.trace &&
           left.listen == right.listen && left.probe == right.probe && left.latest == right.latest &&
           left.sent == right.sent;
}

bool operator!=(const ToolsData& left, const ToolsData& right) {
    return !(left == right);
}

bool tool_running(const ToolsData& data) {
    return data.ping.state == ToolState::Running || data.dns.state == ToolState::Running ||
           data.trace.state == ToolState::Running || data.listen.state == ToolState::Running ||
           data.probe.state == ToolState::Running;
}

ToolState tool_state(const ToolsData& data, ToolKind kind) {
    switch (kind) {
        case ToolKind::Ping:
            return data.ping.state;
        case ToolKind::Dns:
            return data.dns.state;
        case ToolKind::Trace:
            return data.trace.state;
        case ToolKind::Listen:
            return data.listen.state;
        case ToolKind::Probe:
            return data.probe.state;
    }
    return ToolState::Idle;
}

std::vector<DiscoverDevice> discover_devices(const ListenRun& listen, const ProbeRun& probe) {
    std::vector<DiscoverDevice> list;
    list.reserve(listen.devices.size() + probe.devices.size());
    for (const HeardDevice& device : listen.devices) {
        list.push_back({device, std::max<int32_t>(listen.elapsed_s - device.heard_s, 0)});
    }
    const std::size_t heard = list.size();
    for (const HeardDevice& device : probe.devices) {
        const int32_t age = std::max<int32_t>(probe.elapsed_s - device.heard_s, 0);
        const auto end = list.begin() + static_cast<std::ptrdiff_t>(heard);
        const auto found = std::find_if(list.begin(), end, [&device](const DiscoverDevice& entry) {
            return entry.device.hardware_address == device.hardware_address;
        });
        if (found == end) {
            list.push_back({device, age});
            continue;
        }
        HeardDevice& known = found->device;
        if (!device.address.empty()) {
            known.address = device.address;
        }
        if (known.name.empty()) {
            known.name = device.name;
        }
        if (known.kind.empty()) {
            known.kind = device.kind;
        }
        if (known.own_port.empty()) {
            known.own_port = device.own_port;
        }
        found->age_s = std::min(found->age_s, age);
    }
    std::sort(list.begin(), list.end(), [](const DiscoverDevice& left, const DiscoverDevice& right) {
        const uint32_t left_address = address_number(left.device.address);
        const uint32_t right_address = address_number(right.device.address);
        if ((left_address == 0) != (right_address == 0)) {
            return left_address != 0;
        }
        if (left_address != right_address) {
            return left_address < right_address;
        }
        return left.device.hardware_address < right.device.hardware_address;
    });
    return list;
}

bool is_ipv4_text(std::string_view text) {
    int parts = 0;
    std::size_t index = 0;
    while (index <= text.size()) {
        // One number: 1 to 3 digits, 0-255, no leading zero.
        const std::size_t start = index;
        int value = 0;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            value = value * 10 + (text[index] - '0');
            ++index;
            if (index - start > 3) {
                return false;
            }
        }
        const std::size_t digits = index - start;
        if (digits == 0 || value > 255 || (digits > 1 && text[start] == '0')) {
            return false;
        }
        ++parts;
        if (index == text.size()) {
            break;
        }
        if (text[index] != '.' || parts == 4) {
            return false;
        }
        ++index;
    }
    return parts == 4;
}

bool valid_interface_name(std::string_view text) {
    if (text.empty() || text.size() > kInterfaceNameMax || text == "." || text == "..") {
        return false;
    }
    for (const char character : text) {
        const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                             (character >= '0' && character <= '9') || character == '.' || character == '_' ||
                             character == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

bool valid_tool_target(ToolKind kind, std::string_view text) {
    if (kind == ToolKind::Listen || kind == ToolKind::Probe) {
        return valid_interface_name(text);
    }
    if (text.empty() || text.size() > kToolTargetMax) {
        return false;
    }
    bool only_numbers = true;
    char previous = '.';
    for (const char character : text) {
        const bool digit = character >= '0' && character <= '9';
        const bool letter = character >= 'a' && character <= 'z';
        if (character == '.') {
            if (previous == '.') {
                return false;  // a dot first, or two in a row
            }
        } else if (!digit && !letter) {
            return false;
        }
        if (letter) {
            only_numbers = false;
        }
        previous = character;
    }
    if (previous == '.') {
        return false;
    }
    if (only_numbers) {
        return kind != ToolKind::Dns && is_ipv4_text(text);
    }
    return true;
}

} // namespace model

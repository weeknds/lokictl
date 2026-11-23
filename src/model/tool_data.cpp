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
        case ToolKind::Ports:
            return "ports";
        case ToolKind::Speed:
            return "speed";
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

const std::vector<CommonPort>& common_ports() {
    static const std::vector<CommonPort> ports{
        {21, "ftp"},         {22, "ssh"},        {23, "telnet"},     {25, "smtp"},        {53, "dns"},
        {80, "http"},        {110, "pop3"},      {111, "rpcbind"},   {135, "msrpc"},      {139, "netbios"},
        {143, "imap"},       {443, "https"},     {445, "smb"},       {465, "smtps"},      {515, "lpd"},
        {548, "afp"},        {554, "rtsp"},      {587, "submission"}, {631, "ipp"},       {853, "dns-tls"},
        {873, "rsync"},      {993, "imaps"},     {995, "pop3s"},     {1080, "socks"},     {1883, "mqtt"},
        {1900, "upnp"},      {2049, "nfs"},      {3000, "web"},      {3306, "mysql"},     {3389, "rdp"},
        {5000, "web"},       {5432, "postgres"}, {5555, "adb"},      {5900, "vnc"},       {6379, "redis"},
        {7000, "airplay"},   {7547, "tr-069"},   {8000, "http-alt"}, {8008, "http-alt"},  {8009, "cast"},
        {8080, "http-proxy"}, {8123, "hass"},    {8443, "https-alt"}, {8883, "mqtts"},    {8888, "http-alt"},
        {9100, "jetdirect"}, {10000, "webmin"},  {32400, "plex"},    {49152, "upnp"},     {62078, "iphone-sync"},
    };
    return ports;
}

const char* port_service(uint16_t port) {
    for (const CommonPort& entry : common_ports()) {
        if (entry.port == port) {
            return entry.service;
        }
    }
    return "";
}

bool operator==(const PortRun& left, const PortRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.target == right.target && left.total == right.total && left.asked == right.asked &&
           left.closed == right.closed && left.silent == right.silent && left.open == right.open;
}

bool operator!=(const PortRun& left, const PortRun& right) {
    return !(left == right);
}

bool operator==(const SpeedRun& left, const SpeedRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.server == right.server && left.phase == right.phase && left.phase_ms == right.phase_ms &&
           left.down_bytes == right.down_bytes && left.up_bytes == right.up_bytes &&
           left.down_kbps == right.down_kbps && left.up_kbps == right.up_kbps &&
           left.down_samples == right.down_samples && left.up_samples == right.up_samples;
}

bool operator!=(const SpeedRun& left, const SpeedRun& right) {
    return !(left == right);
}

bool valid_speed_server(std::string_view text) {
    if (text.size() > kSpeedServerMax) {
        return false;
    }
    std::string_view rest;
    if (text.substr(0, 8) == "https://") {
        rest = text.substr(8);
    } else if (text.substr(0, 7) == "http://") {
        rest = text.substr(7);
    } else {
        return false;
    }
    for (const char character : rest) {
        if (character <= 0x20 || character > 0x7E || character == '@' || character == '?' || character == '#' ||
            character == '\\') {
            return false;
        }
    }
    // A host is needed: something before the first '/' or ':'.
    const std::size_t host_end = rest.find_first_of("/:");
    return host_end != 0 && !rest.empty();
}

std::string speed_server_host(std::string_view text) {
    const std::size_t scheme = text.find("://");
    std::string_view rest = scheme == std::string_view::npos ? text : text.substr(scheme + 3);
    return std::string(rest.substr(0, rest.find_first_of("/:")));
}

bool operator==(const ToolsData& left, const ToolsData& right) {
    return left.ping == right.ping && left.dns == right.dns && left.trace == right.trace &&
           left.listen == right.listen && left.probe == right.probe && left.ports == right.ports &&
           left.speed == right.speed && left.latest == right.latest && left.sent == right.sent;
}

bool operator!=(const ToolsData& left, const ToolsData& right) {
    return !(left == right);
}

bool tool_running(const ToolsData& data) {
    return data.ping.state == ToolState::Running || data.dns.state == ToolState::Running ||
           data.trace.state == ToolState::Running || data.listen.state == ToolState::Running ||
           data.probe.state == ToolState::Running || data.ports.state == ToolState::Running ||
           data.speed.state == ToolState::Running;
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
        case ToolKind::Ports:
            return data.ports.state;
        case ToolKind::Speed:
            return data.speed.state;
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

bool port_scan_address(std::string_view text) {
    if (!is_ipv4_text(text)) {
        return false;
    }
    const uint32_t address = address_number(std::string(text));
    const uint32_t first = address >> 24;
    const uint32_t second = (address >> 16) & 0xFF;
    return first == 10 || (first == 172 && second >= 16 && second <= 31) || (first == 192 && second == 168) ||
           (first == 169 && second == 254);
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
    if (kind == ToolKind::Ports) {
        return port_scan_address(text);
    }
    if (kind == ToolKind::Speed) {
        return valid_speed_server(text);
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

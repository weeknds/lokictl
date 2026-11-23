/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_data.h"

namespace model {

const char* tool_name(ToolKind kind) {
    switch (kind) {
        case ToolKind::Ping:
            return "ping";
        case ToolKind::Dns:
            return "dns";
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

bool operator==(const ToolsData& left, const ToolsData& right) {
    return left.ping == right.ping && left.dns == right.dns && left.sent == right.sent;
}

bool operator!=(const ToolsData& left, const ToolsData& right) {
    return !(left == right);
}

bool tool_running(const ToolsData& data) {
    return data.ping.state == ToolState::Running || data.dns.state == ToolState::Running;
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

bool valid_tool_target(ToolKind kind, std::string_view text) {
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
        return kind == ToolKind::Ping && is_ipv4_text(text);
    }
    return true;
}

} // namespace model

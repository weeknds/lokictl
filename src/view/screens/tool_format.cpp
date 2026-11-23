/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_format.h"

#include <cstdio>

namespace screen::tools {
namespace {

constexpr const char* kSeparator = " · ";

std::string failed_text(const std::string& reason) {
    return "Failed: " + (reason.empty() ? std::string("unknown error") : reason);
}

// The texts for the errors both tools share.
ToneText error_status(model::ToolError error, const std::string& reason) {
    switch (error) {
        case model::ToolError::NotAllowed:
            return {"Ping not allowed for this user", kToneError};
        case model::ToolError::NoServer:
            return {"No DNS server", kToneError};
        case model::ToolError::NoSuchName:
            return {"No such name", kToneWarning};
        case model::ToolError::NoAnswer:
            return {"No answer", kToneWarning};
        case model::ToolError::NoAddress:
            return {"No address for this name", kToneWarning};
        case model::ToolError::BadTarget:
            return {"Not a valid name or address", kToneError};
        case model::ToolError::Failed:
        case model::ToolError::None:
            break;
    }
    return {failed_text(reason), kToneError};
}

} // namespace

std::string time_text(int32_t micros) {
    if (micros < 0) {
        return "--";
    }
    char text[16];
    if (micros < 10000) {
        std::snprintf(text, sizeof(text), "%d.%02d", micros / 1000, (micros % 1000) / 10);
    } else if (micros < 100000) {
        std::snprintf(text, sizeof(text), "%d.%d", micros / 1000, (micros % 1000) / 100);
    } else {
        std::snprintf(text, sizeof(text), "%d", micros / 1000);
    }
    return text;
}

ToneText reply_text(const model::PingReply& reply, bool running) {
    if (reply.time_us == model::kPingLost) {
        return {"lost", kToneWarning};
    }
    if (reply.time_us == model::kPingWaiting) {
        return {running ? "…" : "--", kToneMuted};
    }
    return {time_text(reply.time_us), kToneText};
}

std::string ping_counts_text(const model::PingRun& run) {
    if (run.sent == 0 && run.lost == 0) {
        return {};
    }
    const int32_t settled = run.received + run.lost;
    std::string text = std::to_string(run.sent) + " sent" + kSeparator + std::to_string(run.received) + " received";
    if (settled > 0) {
        text += kSeparator + std::to_string(run.lost * 100 / settled) + "% lost";
    }
    return text;
}

std::string ping_times_text(const model::PingRun& run) {
    if (run.received == 0) {
        return {};
    }
    return "min " + time_text(run.min_us) + kSeparator + "avg " + time_text(run.avg_us) + kSeparator + "max " +
           time_text(run.max_us) + " ms";
}

TraceRow trace_row(const model::TraceHop& hop, bool running) {
    TraceRow row;
    row.number = std::to_string(hop.ttl);
    if (hop.time_us == model::kPingWaiting) {
        row.address = {running ? "…" : "*", kToneMuted};
        row.time = {"", kToneMuted};
    } else if (hop.time_us == model::kPingLost || hop.address.empty()) {
        row.address = {"*", kToneMuted};
        row.name = "no answer";
        row.time = {"", kToneMuted};
    } else {
        row.address = {hop.address, kToneText};
        row.name = hop.name;
        row.time = {time_text(hop.time_us) + "ms", kToneText};
    }
    return row;
}

ToneText ping_status(const model::PingRun& run) {
    switch (run.state) {
        case model::ToolState::Idle:
            return {"10 echoes, one a second", kToneMuted};
        case model::ToolState::Running:
            if (run.address.empty()) {
                return {"Looking up the name…", kToneText};
            }
            // The newest echo is the last one of the table.
            return {"Echo " + std::to_string(run.replies.empty() ? 0 : run.replies.back().sequence) + " of " +
                        std::to_string(model::kPingCount),
                    kToneText};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            return error_status(run.error, run.reason);
        case model::ToolState::Done:
            break;
    }
    if (run.received == 0) {
        if (!run.reason.empty()) {
            return {failed_text(run.reason), kToneError};
        }
        return {"No replies", kToneWarning};
    }
    return {"Done", kToneSuccess};
}

ToneText dns_status(const model::DnsRun& run) {
    switch (run.state) {
        case model::ToolState::Idle:
            return {"IPv4 and IPv6 addresses", kToneMuted};
        case model::ToolState::Running:
            return {"Looking up…", kToneText};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            return error_status(run.error, run.reason);
        case model::ToolState::Done:
            break;
    }
    if (run.error != model::ToolError::None) {
        return error_status(run.error, run.reason);
    }
    const std::size_t count = run.addresses.size();
    return {std::to_string(count) + (count == 1 ? " address" : " addresses"), kToneSuccess};
}

ToneText trace_status(const model::TraceRun& run) {
    const std::string hops = std::to_string(run.hops.size());
    switch (run.state) {
        case model::ToolState::Idle:
            return {"One probe per hop, up to " + std::to_string(model::kTraceMaxHops), kToneMuted};
        case model::ToolState::Running:
            if (run.address.empty()) {
                return {"Looking up the name…", kToneText};
            }
            if (run.end == model::TraceEnd::None) {
                return {"Hop " + hops + "…", kToneText};
            }
            return {"Looking up names…", kToneText};
        case model::ToolState::Stopped:
            return {"Stopped", kToneMuted};
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                return {"Trace not allowed for this user", kToneError};
            }
            if (run.hops.empty()) {
                return error_status(run.error, run.reason);
            }
            return {failed_text(run.reason) + " at hop " + std::to_string(run.hops.size() + 1), kToneError};
        case model::ToolState::Done:
            break;
    }
    switch (run.end) {
        case model::TraceEnd::Reached:
            return {"Reached in " + hops + (run.hops.size() == 1 ? " hop" : " hops"), kToneSuccess};
        case model::TraceEnd::Unreachable:
            return {"Unreachable at hop " + hops, kToneWarning};
        case model::TraceEnd::MaxHops:
        case model::TraceEnd::None:
            break;
    }
    return {"Not reached in " + hops + " hops", kToneWarning};
}

ToneText ping_tag(const model::PingRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            if (run.received > 0) {
                return {time_text(run.avg_us) + " ms", kToneMuted};
            }
            return {"no reply", kToneWarning};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

ToneText trace_tag(const model::TraceRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            if (run.end == model::TraceEnd::Reached) {
                return {std::to_string(run.hops.size()) + (run.hops.size() == 1 ? " hop" : " hops"), kToneMuted};
            }
            return {"not reached", kToneWarning};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

ToneText dns_tag(const model::DnsRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            if (run.error == model::ToolError::None) {
                return {std::to_string(run.addresses.size()) + " found", kToneMuted};
            }
            return {"none", kToneWarning};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

std::vector<std::string> open_port_items(const model::PortRun& run) {
    std::vector<std::string> items;
    items.reserve(run.open.size());
    for (const uint16_t port : run.open) {
        const char* service = model::port_service(port);
        items.push_back(std::to_string(port) + (service[0] != '\0' ? std::string(" ") + service : std::string()));
    }
    return items;
}

std::string ports_counts_text(const model::PortRun& run) {
    const int32_t answered = static_cast<int32_t>(run.open.size()) + run.closed + run.silent;
    if (answered == 0) {
        return {};
    }
    return std::to_string(run.open.size()) + " open · " + std::to_string(run.closed) + " closed · " +
           std::to_string(run.silent) + " no answer";
}

ToneText ports_status(const model::PortRun& run) {
    const std::string progress = std::to_string(run.asked) + " of " + std::to_string(run.total);
    switch (run.state) {
        case model::ToolState::Idle:
            return {std::to_string(model::common_ports().size()) + " common ports of one device", kToneMuted};
        case model::ToolState::Running:
            return {run.total > 0 ? "Scanning " + progress : std::string("Scanning"), kToneAccent};
        case model::ToolState::Done:
            return {"Scanned " + std::to_string(run.asked) + ", " + std::to_string(run.open.size()) + " open",
                    run.open.empty() ? kToneMuted : kToneSuccess};
        case model::ToolState::Stopped:
            return {"Scan stopped at " + progress, kToneMuted};
        case model::ToolState::Failed:
            if (run.error == model::ToolError::BadTarget) {
                return {"Not a private address", kToneError};
            }
            if (!run.reason.empty()) {
                std::string text = run.reason;
                if (text[0] >= 'a' && text[0] <= 'z') {
                    text[0] = static_cast<char>(text[0] - 'a' + 'A');
                }
                return {text, kToneError};
            }
            return error_status(run.error, run.reason);
    }
    return {};
}

ToneText ports_tag(const model::PortRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {"running", kToneAccent};
        case model::ToolState::Done:
            return {std::to_string(run.open.size()) + " open", kToneMuted};
        case model::ToolState::Failed:
            return {"failed", kToneWarning};
        case model::ToolState::Idle:
        case model::ToolState::Stopped:
            break;
    }
    return {};
}

} // namespace screen::tools

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

} // namespace screen::tools

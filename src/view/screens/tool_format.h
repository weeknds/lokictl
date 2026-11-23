/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <string>

// Texts shared by the Tools, Ping and DNS pages.
namespace screen::tools {

// The colours a text of these pages can take.
enum Tone : int32_t {
    kToneText = 0,
    kToneMuted,
    kToneSuccess,
    kToneWarning,
    kToneError,
    kToneAccent,
};

struct ToneText {
    std::string text;
    int32_t tone{kToneMuted};
};

// A round trip in milliseconds without the unit: "0.84" below 10 ms,
// "12.4" below 100 ms, "104" from there on; "--" when not known.
std::string time_text(int32_t micros);

// One reply of the Ping page's table: its time, "lost", "…" while it
// waits (or "--" once the run is over).
ToneText reply_text(const model::PingReply& reply, bool running);

// "10 sent · 9 received · 10% lost"; empty before the first echo.
std::string ping_counts_text(const model::PingRun& run);
// "min 11.8 · avg 23.1 · max 104 ms"; empty without a reply.
std::string ping_times_text(const model::PingRun& run);

// The status line of the Ping and DNS pages for their latest run.
ToneText ping_status(const model::PingRun& run);
ToneText dns_status(const model::DnsRun& run);

// The short state on the Tools page next to each tool: "running", the
// last result, or nothing.
ToneText ping_tag(const model::PingRun& run);
ToneText dns_tag(const model::DnsRun& run);

} // namespace screen::tools

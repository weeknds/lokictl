/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <string>

// Texts shared by the Tools, Ping, DNS and Trace pages.
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

// One hop of the Trace page: its number, who answered ("…" while it
// waits, "*" with no answer), the reverse name ("no answer" for none) and
// the time ("12.4ms").
struct TraceRow {
    std::string number;
    ToneText address;
    std::string name;
    ToneText time;
};
TraceRow trace_row(const model::TraceHop& hop, bool running);

// The status line of the Ping, DNS and Trace pages for their latest run.
ToneText ping_status(const model::PingRun& run);
ToneText dns_status(const model::DnsRun& run);
ToneText trace_status(const model::TraceRun& run);

// The short state on the Tools page next to each tool: "running", the
// last result, or nothing.
ToneText ping_tag(const model::PingRun& run);
ToneText dns_tag(const model::DnsRun& run);
ToneText trace_tag(const model::TraceRun& run);

} // namespace screen::tools

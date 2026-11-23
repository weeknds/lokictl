/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstdint>
#include <string>
#include <vector>

// Texts shared by the Tools, Ping, DNS, Trace, Ports and Speed pages.
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

// The status line of the Ping, DNS, Trace and Ports pages for their latest
// run.
ToneText ping_status(const model::PingRun& run);
ToneText dns_status(const model::DnsRun& run);
ToneText trace_status(const model::TraceRun& run);
ToneText ports_status(const model::PortRun& run);
// The Speed page: before a run what it uses ("Uses up to 100 MB of
// data"), then "Connecting", "Download 4 of 10 s", "Upload 7 of 10 s",
// "Done", "Stopped", or why it failed.
ToneText speed_status(const model::SpeedRun& run);
// The most one direction of a speed test moves: "up to 50 MB".
std::string speed_cap_text();

// A byte count, short: "850 kB", "15.2 MB", "109 MB", "1.2 GB"; "0 kB"
// for nothing.
std::string bytes_text(uint64_t bytes);

// The Ports page: each open port with its service ("22 ssh"), ascending,
// and "3 open · 45 closed · 2 no answer" (empty before the first answer).
std::vector<std::string> open_port_items(const model::PortRun& run);
std::string ports_counts_text(const model::PortRun& run);

// The short state on the Tools page next to each tool: "running", the
// last result, or nothing.
ToneText ping_tag(const model::PingRun& run);
ToneText dns_tag(const model::DnsRun& run);
ToneText trace_tag(const model::TraceRun& run);
ToneText ports_tag(const model::PortRun& run);
// "87.3 / 12.1": the download and upload speeds in Mbit/s.
ToneText speed_tag(const model::SpeedRun& run);

} // namespace screen::tools

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

// The short tag of an open port after the check against the list of known
// holes, the worst one winning: "Check" (warning) when the port named the
// system that built it, whose updates may already have fixed what was
// found; "Attacked" (error) for a hole attackers are known to use; "3
// holes" or "1 hole" (warning); "Old" (warning) for a version that no longer
// gets security fixes. Empty when nothing was found. The help panel
// explains each.
ToneText port_tag(const model::PortRun& run, uint16_t port);

// One open port of the Ports page's list: "22 ssh" with its tag.
struct PortItem {
    uint16_t port{0};
    std::string text;
    ToneText tag;
};
std::vector<PortItem> port_items(const model::PortRun& run);

// One line of the Ports page's host detail: a caption or an open port at
// the left, its value at the right.
struct DetailLine {
    std::string left;  // "name", "MAC", or a port with its service ("22 ssh"); empty on a note's line
    ToneText right;    // the value, what the port is and said about itself, or a note
    // Shorter texts for `right`, tried in order when it does not fit the
    // line; the last one is used when none does.
    std::vector<std::string> shorter;
    // Said after `right` in the muted colour ("CVE-2024-6387"); left out
    // when the line would not fit.
    std::string aside;
    bool optional{false};  // the whole line is left out when it does not fit
    int32_t finding{-1};   // a finding's line: its place in run.findings; -1 otherwise
};
// The host detail of `run`: the host's name and hardware address when
// known (an empty text is left out), then each open port with what it is
// in everyday words and what it said, in short when a known product was
// read ("remote login · OpenSSH 10.0", "web page · encrypted (TLS)"; the
// whole text on a line of its own when it fits), followed by its note from
// model::service_rules() in the tone of its level (info muted, warning,
// risk as an error) and what the check found, worst first: "Debian may
// have fixed these" for a vendor's build, one line per hole kept ("Hole
// attackers use", "Very serious hole", "Serious hole", "Known hole", with
// its official number aside), "+5 more holes", "Old: no more security
// fixes". "No open ports" when there are none. A finished scan ends with the
// list's date ("Holes list from 2026-11-23"), or "No holes list: versions
// not checked".
std::vector<DetailLine> host_detail_lines(const model::PortRun& run,
                                          const std::string& name,
                                          const std::string& hardware_address);

// One finding of the check, as its own view shows it.
struct FindingView {
    std::string place;    // "1 of 12 · port 22"
    std::string product;  // "OpenSSH 10.0"
    ToneText severity;    // "Serious hole, 8.1 of 10", "Very serious hole", "Old version"
    std::vector<ToneText> sentences;  // at most two: "Attackers are using it", "Fix: update its software"
    std::string official;  // "Official number CVE-2024-6387"; "Read more on this page" for an old version
    std::string link_text; // the link without "https://": "nvd.nist.gov/vuln/detail/CVE-2024-6387"
    std::string link;      // the whole link, for the QR code
};
// The finding at `index` of run.findings (clamped); empty without findings.
FindingView finding_view(const model::PortRun& run, std::size_t index);

// The short state on the Tools page next to each tool: "running", the
// last result, or nothing.
ToneText ping_tag(const model::PingRun& run);
ToneText dns_tag(const model::DnsRun& run);
ToneText trace_tag(const model::TraceRun& run);
ToneText ports_tag(const model::PortRun& run);
// "87.3 / 12.1": the download and upload speeds in Mbit/s.
ToneText speed_tag(const model::SpeedRun& run);

} // namespace screen::tools

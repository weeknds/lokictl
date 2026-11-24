/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "talk_data.h"
#include "tool_data.h"
#include "trap_data.h"
#include "watch_data.h"

#include <string>
#include <string_view>

// A report: what LokiCTL has found so far, as one web page to open on a PC
// and a table of the devices (CSV). Everything is put together from what
// is in memory; nothing is sent or read here. Every text that came from
// the network (names, banners, alerts) goes through html_text() or
// csv_field(), so a device that calls itself "<script>" shows that name
// and nothing else. The page needs nothing from elsewhere: its style is in
// the page, it has no script, and it forbids both.
namespace model {

// What one report is made from: the app's values when it was saved.
struct ReportSource {
    const HomeData& home;
    const ToolsData& tools;
    const WatchData& watch;
    const TrapRun& trap;
    const TalkRun& talk;   // the names the hotspot's devices looked up
    std::string saved_at;  // local time, "2026-11-23 07:51"
    std::string version;   // the app's version, "1.0.0"
};

// The web page: what was found, in plain words first and the official
// numbers second, with a short "what to do" for each finding.
std::string report_html(const ReportSource& source);

// The devices as a table (CSV, RFC 4180, CRLF line ends): a header line,
// then one line per device of Discover and of the hotspot.
std::string report_devices_csv(const ReportSource& source);

// `text` made safe inside an HTML page, as text or inside a quoted
// attribute: & < > " ' become entities, control characters a space, and
// bytes that are not UTF-8 the replacement character.
std::string html_text(std::string_view text);

// One CSV field: always in quotes, quotes doubled, control characters a
// space, and a ' in front of a text that starts with = + - @ (so a
// spreadsheet never reads it as a formula).
std::string csv_field(std::string_view text);

// One JSON string, with its quotes: " and \ escaped, < > & as \u escapes
// (so it is safe even inside a page), control characters a space, and
// bytes that are not UTF-8 the replacement character.
std::string json_text(std::string_view text);

} // namespace model

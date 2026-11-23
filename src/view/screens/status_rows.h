/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <cstddef>
#include <string>
#include <vector>

// Turns model::HomeData and its Status page details into the rows the
// Status page lists. Nothing here knows LVGL or the theme, so the rows are
// the same in every theme; the page only styles them. A value that is not
// known reads "--".
namespace screen::status {

// How a heading's note is coloured.
enum class NoteTone {
    Plain, // the main text colour
    Good,  // a working link: the success colour
    Quiet, // nothing going on, or not known: the muted colour
    Warning, // a low battery (model::battery_low()): the warning colour
};

struct StatusRow {
    bool heading{false};  // a section heading rather than a value row
    std::string label;    // heading: the section's name; row: what the value is
    std::string value;    // heading: a short note at the right end; row: the value; line: the whole text
    NoteTone tone{NoteTone::Plain}; // headings and lines only
    bool line{false};     // one text across the whole row, without a label (the summary at the top)
};

// The most characters of a value that fit in a row. A Wi-Fi network's name
// or an IPv6 address that is longer goes on in a second row with an empty
// label (an address is cut after a colon).
constexpr std::size_t kValueChars = 26;

// The rows, in this order:
// - the summary lines: one per connected cable port, "Cable: " (or the
//   interface's name and ": " when there are several) and
//   model::cable_summary(), in the warning tone when model::cable_link_poor();
//   then "Battery: " and model::battery_summary(), in the warning tone while
//   the battery is low;
// - per port (HomeData::details.ports): a heading "wi-fi <name>" or
//   "cable <name>" (with " · uplink" on the uplink) and its state as the
//   note; then, while it is connected or runs the access point, for Wi-Fi
//   network, security, signal, speed, IPv4, IPv6, link-local, router and
//   hardware, for a cable speed, duplex ("full, both ways at once"), drops
//   ("0 since LokiCTL started"), IPv4, IPv6, link-local, router and
//   hardware; otherwise only hardware. Without any port: a heading
//   "network" and one row "ports --". A port that is a USB Wi-Fi adapter's
//   interface gets " · usb" after its name.
// - "vpn" with the tunnel's state as the note (model::vpn_state_name()):
//   while it runs file, server and (when on) reply; otherwise
//   "files" with the number of WireGuard files, or "none";
// - "usb wi-fi" with the number of adapters as the note
//   (HomeData::usb_wifi): per adapter interface, name, driver, usb id and
//   managed; without one the note "none" and the rows "adapter none
//   (optional)" and "built-in Wi-Fi is used", because LokiCTL needs none;
// - "dns": one "server" row per DNS server, or one "server --";
// - "battery" with the charge as the note (in the warning tone while the
//   battery is low): state, voltage, current, temp, health, cycles, charge
//   and time left (while discharging model::battery_time_text() or
//   "estimating...", while charging the time to full);
// - "device": processor, memory and uptime;
// - "holes list" with the date of the list of known security holes the
//   Ports page checks against (`list_date`; "missing" when the app found
//   none) as the note: where it comes from and NVD's notice.
std::vector<StatusRow> status_rows(const model::HomeData& data, const std::string& list_date);

// The values as text, as the rows show them; each reads "--" when not known.
std::string voltage_text(int32_t millivolts);      // 3920 -> "3.92 V"
std::string current_text(int32_t milliamps);       // -480 -> "−480 mA"
std::string temperature_text(int32_t tenths);      // 314 -> "31.4 °C"
std::string duration_text(int32_t minutes);        // 199 -> "3 h 19 min"; 0 -> "0 min"
std::string uptime_long_text(int32_t seconds);     // 191820 -> "2 d 5 h 17 min"

} // namespace screen::status

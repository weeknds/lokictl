/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// Turns the numbers of model::HomeData into the text the home page shows.
// Every layout uses the same functions, so a value reads the same in every
// theme. Values outside a sensible range are clamped, never printed as
// garbage. A value that is not known reads "--" (kUnknownText), never 0 and
// never an old number.
namespace screen::home {

// What the page shows for a value that is not known.
inline constexpr const char* kUnknownText = "--";

// Minutes since midnight as "HH:MM": 125 -> "02:05". Clamped to 00:00-23:59.
std::string clock_text(int32_t minutes);

// The battery charge as the home page shows it: "91%"; "91%+" while the
// battery charges; "--%" whenever the charge is not known (a negative
// `percent`), whatever the state. A known charge is clamped to 0-100. Idle
// and discharging read the same.
std::string battery_text(int32_t percent, model::BatteryState state);

// A duration as "HH:MM:SS": 3725 -> "01:02:05". Negative values give
// "00:00:00"; hours go past 99 when they must.
std::string uptime_text(int32_t seconds);

// A rate in kbit/s as Mbit/s with one decimal, rounded: 2450 -> "2.5",
// 40 -> "0.0". From 100 Mbit/s on (99950 and up) it is whole Mbit/s, so
// below 1000 Mbit/s the number is never wider than "99.9": 120000 -> "120".
// A rate that is not known (any negative value, such as
// model::kRateUnknown) gives "--".
std::string rate_text(int32_t kbps);

// The same rate with the unit's short form after it, as Terminal writes it:
// 2450 -> "2.5M", 120000 -> "120M". Not known gives "--", never "--M".
std::string rate_short_text(int32_t kbps);

// A signal level in dBm: -55 gives "55 dBm" with a minus sign in front. It
// is the real minus sign (U+2212), not a hyphen; every theme font has it.
// Clamped to -999..999. model::kSignalUnknown (0) gives "--".
std::string signal_text(int32_t dbm);

// The signal level as a bare number, for a big number with its unit set
// apart: -55 -> "55" with the real minus sign in front. Clamped like
// signal_text(); model::kSignalUnknown (0) gives "--".
std::string signal_number_text(int32_t dbm);

// A link speed in Mbit/s, short, as a compact line shows it: 100 -> "100M".
// A speed that is not known (0 or less, such as model::kSpeedUnknown)
// gives "--", never "0M" or "--M".
std::string speed_text(int32_t mbps);

// A link speed in Mbit/s in words: 100 -> "100 Mbit/s". Not known gives
// "--".
std::string speed_long_text(int32_t mbps);

// A link speed in Mbit/s as a bare number, for a big number with its unit
// set apart: 100 -> "100". Not known gives "--".
std::string speed_number_text(int32_t mbps);

// A link speed in words, as one part of a line made with join_parts():
// 100 -> "100 Mbit/s", like speed_long_text(), but an empty text when the
// speed is not known, so the line leaves it out instead of showing "--".
std::string speed_part_text(int32_t mbps);

// `text` as it is, or "--" when it is empty: for names and addresses that
// may not be known.
std::string text_or_unknown(std::string_view text);

// The words for the network the uplink leads to: the Wi-Fi network's name,
// or "wi-fi network" when the name is not known; "wired network" on a
// cable. Without a link they say why, as far as it is known (see
// model::OfflineState): "connecting", "wi-fi off", "network off", else "no
// network".
std::string network_text(const model::HomeData& data);

// A short note when the internet can not be reached through the link:
// "login page" (model::Internet::Portal), "no internet" (Limited or None).
// An empty text when there is nothing to say: the internet answers, it is
// not known, or there is no link.
std::string internet_note(const model::HomeData& data);

// The network the uplink leads to in one short word or name, for a value
// set beside its own label: the Wi-Fi network's name ("--" when it is not
// known), "wired" on a cable, "--" without a link.
std::string network_name_text(const model::HomeData& data);

// The word for a kind of link: "wi-fi" or "cable"; an empty text for no
// link, so a line made with join_parts() leaves it out.
std::string link_kind_text(model::LinkKind kind);

// The uplink's interface and kind of link: "wlan0 · wi-fi", "eth0 · cable";
// a part that is not known is left out ("wi-fi"), and "--" when neither is
// known, as without a link.
std::string link_text(const model::HomeData& data);

// True when the device has no network at all: no link and no access point
// of its own. The page then says "offline" where the mode word stands.
bool is_offline(const model::HomeData& data);

// The word where the page names the mode: the mode's name ("Client",
// "Router", ...; see model::net_mode_name()), or "Offline" when is_offline().
// Pages write it in capitals or lowercase as their theme asks.
std::string mode_word(const model::HomeData& data);

// The parts that are not empty, joined by " · " (a middle dot, U+00B7, with
// a space on each side): {"wlan0", "wi-fi"} -> "wlan0 · wi-fi", {"", "wi-fi"}
// -> "wi-fi". A line made of parts so never starts or ends with the dot, and
// never has two in a row. No part at all gives an empty text.
std::string join_parts(std::initializer_list<std::string> parts);

// The position of a client among `count` as "n/count", where `index` counts
// from 0: (1, 4) -> "2/4". With no clients it is "0/0".
std::string count_text(int32_t index, int32_t count);

// `text` with the ASCII letters a-z in capitals; every other byte, including
// each byte of a multi-byte UTF-8 character, is kept as it is.
std::string upper_text(std::string_view text);

// `text` with the ASCII letters A-Z in lowercase; every other byte is kept
// as it is, as in upper_text().
std::string lower_text(std::string_view text);

} // namespace screen::home

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Turns the numbers of model::HomeData into the text the home page shows.
// Every layout uses the same functions, so a value reads the same in every
// theme. Values outside a sensible range are clamped, never printed as
// garbage.
namespace screen::home {

// Minutes since midnight as "HH:MM": 125 -> "02:05". Clamped to 00:00-23:59.
std::string clock_text(int32_t minutes);

// A charge or share as "N%": 57 -> "57%". Clamped to 0-100.
std::string percent_text(int32_t percent);

// A duration as "HH:MM:SS": 3725 -> "01:02:05". Negative values give
// "00:00:00"; hours go past 99 when they must.
std::string uptime_text(int32_t seconds);

// A rate in kbit/s as Mbit/s with one decimal, rounded: 2450 -> "2.5",
// 40 -> "0.0". Negative values give "0.0".
std::string rate_text(int32_t kbps);

// A signal level in dBm: -55 gives "55 dBm" with a minus sign in front. It
// is the real minus sign (U+2212), not a hyphen; every theme font has it.
// Clamped to -999..999.
std::string signal_text(int32_t dbm);

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

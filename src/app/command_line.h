/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app {

enum class LaunchMode {
    Interactive,
    Snapshot,
    ListPages,
    Help,
    Invalid,
};

constexpr uint32_t kDefaultSettleMs = 300;
constexpr uint32_t kMaxSettleMs = 60000;

struct LaunchOptions {
    LaunchMode mode{LaunchMode::Interactive};
    std::string snapshot_path;
    std::string page;                       // empty: default page
    std::string theme;                      // empty: not given on the command line
    uint32_t settle_ms{kDefaultSettleMs};
    std::vector<std::string> switch_to;     // themes to switch to live, in order (snapshot only)
    std::string error;                      // set when mode == Invalid
};

// Turns the program arguments into launch options. Does no I/O; page and
// theme names are checked later against their tables.
LaunchOptions parse_command_line(int argc, const char* const* argv);

// Short help text listing every option, ending with a newline.
std::string usage_text();

} // namespace app

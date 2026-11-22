/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "theme.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// One key of snapshot --keys.
struct ScriptedKey {
    uint32_t key;      // the code a real key press delivers (see platform::describe_key)
    bool long_press;   // held for one second of LVGL time instead of 60 ms
};

// The key names --keys accepts, for messages.
constexpr const char* kScriptedKeyNames =
    "0-9, a-z, esc, enter, space, backspace, up, down, left, right or help";

// Turns one --keys name into a key: one of kScriptedKeyNames (any case; a
// letter always gives the lowercase key, as the keyboard sends it),
// optionally followed by ":long". Returns false for anything else.
bool parse_scripted_key(std::string_view text, ScriptedKey& key);

struct SnapshotRequest {
    std::string output_path;
    model::AppPage page;
    view::ThemeId theme;   // the theme the page is drawn in first
    uint32_t settle_ms;
    // Themes to switch to while the app runs, in order, after the first
    // draw and the keys. The switches take the same path as a pick on the
    // Settings page (app::ThemeController) and the picture shows the last
    // one. Empty: no switch.
    std::vector<view::ThemeId> switch_to;
    // Keys to press after the first draw, in order. Each goes through the
    // same path as a real key (platform::inject_key() and
    // app::KeyController): pressed, held for 60 ms (one second with
    // long_press), let go, and then the app runs for settle_ms before the
    // next key. A theme picked on the Settings page really switches;
    // nothing is saved. Empty: no keys.
    std::vector<ScriptedKey> keys;
};

// Renders one page into a PNG without a window, input devices or config access.
// With switch_to, it prints "fonts loaded: <n> (<theme>)" on standard error
// right before the first switch and after every switch. If the keys made the
// app ask to quit, it prints "quit requested" on standard error and still
// takes the picture. The picture shows the page only: the help panel and
// messages float above it.
// Returns the process exit code.
int run_snapshot(const SnapshotRequest& request);

} // namespace app

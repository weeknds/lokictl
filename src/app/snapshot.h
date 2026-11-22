/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "data_source.h"
#include "theme.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// One entry of snapshot --keys: a key, or one further reading of the data
// source.
struct ScriptedKey {
    uint32_t key;      // the code a real key press delivers (see platform::describe_key)
    bool long_press;   // held for one second of LVGL time instead of 60 ms
    bool data_step;    // true: not a key; one further reading is taken here (key is 0)
};

// The names --keys accepts, for messages.
constexpr const char* kScriptedKeyNames =
    "0-9, a-z, esc, enter, space, backspace, up, down, left, right or help, each "
    "optionally followed by :long; or data, which takes one further reading";

// Turns one --keys name into an entry: one of the key names of
// kScriptedKeyNames (any case; a letter always gives the lowercase key, as
// the keyboard sends it), optionally followed by ":long", or the name
// "data" (any case, never with ":long"), which is not a key but one further
// reading of the data source. Returns false for anything else.
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
    // Keys to press after the first draw and the data steps, in order. Each
    // goes through the same path as a real key (platform::inject_key() and
    // app::KeyController): pressed, held for 60 ms (one second with
    // long_press), let go, and then the app runs for settle_ms before the
    // next key. A theme picked on the Settings page really switches;
    // nothing is saved. An entry with data_step takes one further reading
    // instead, whichever page is open, and then the app runs for settle_ms.
    // Empty: no keys.
    std::vector<ScriptedKey> keys;
    // Where the values come from. The first reading is taken before the
    // page is built.
    platform::DataSourceKind data_source;
    // Further readings right after the first draw, before the keys, taken
    // one after the other without a background thread; 0: none.
    uint32_t data_steps;
    // Handed to the data source (see platform::make_data_source()); empty
    // means the real root.
    std::string data_root;
};

// Renders one page into a PNG without a window, input devices or config access.
// The order is: the first reading, build the page, the data steps, the keys,
// the theme switches, the picture. No background thread runs.
// With switch_to, it prints "fonts loaded: <n> (<theme>)" on standard error
// right before the first switch and after every switch. If the keys made the
// app ask to quit, it prints "quit requested" on standard error and still
// takes the picture. The picture shows the page only: the help panel and
// messages float above it. Battery care runs with its normal times in LVGL
// time (app::PowerController): when the settles add up to 60 s without a
// key, the screen dims and the next key only wakes it; each backlight write
// it would make is printed as "backlight: <n>" on standard error (from 100;
// the device's backlight is never touched).
// Returns the process exit code.
int run_snapshot(const SnapshotRequest& request);

} // namespace app

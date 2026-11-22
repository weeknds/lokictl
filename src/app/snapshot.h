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
#include <vector>

namespace app {

struct SnapshotRequest {
    std::string output_path;
    model::AppPage page;
    view::ThemeId theme;   // the theme the page is drawn in first
    uint32_t settle_ms;
    // Themes to switch to while the app runs, in order, after the first
    // draw. The switches take the same path as key 6 (app::ThemeController)
    // and the picture shows the last one. Empty: no switch.
    std::vector<view::ThemeId> switch_to;
};

// Renders one page into a PNG without a window, input devices or config access.
// With switch_to, it prints "fonts loaded: <n> (<theme>)" on standard error
// after the first draw and after every switch.
// Returns the process exit code.
int run_snapshot(const SnapshotRequest& request);

} // namespace app

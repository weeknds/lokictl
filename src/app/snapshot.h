/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "theme.h"

#include <cstdint>
#include <string>

namespace app {

struct SnapshotRequest {
    std::string output_path;
    model::AppPage page;
    view::ThemeId theme;
    uint32_t settle_ms;
};

// Renders one page into a PNG without a window, input devices or config access.
// Returns the process exit code.
int run_snapshot(const SnapshotRequest& request);

} // namespace app

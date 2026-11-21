/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace platform::screenshot {

struct CaptureResult {
    bool success{false};
    std::string path;
    std::string error;
    int32_t width{0};
    int32_t height{0};
};

// Saves the active screen as a timestamped PNG in the user's screenshots folder.
CaptureResult capture_active_screen();

// Saves the active screen as a PNG at exactly this path, creating missing folders.
CaptureResult capture_active_screen_to(const std::filesystem::path& path);

} // namespace platform::screenshot

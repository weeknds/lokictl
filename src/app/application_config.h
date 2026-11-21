/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <optional>
#include <string>

namespace app {

struct ApplicationConfig {
    // Unset when the file has no dark_mode key.
    std::optional<bool> dark_mode;
};

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
// Writes the keys that are set into the [application] section of `path`,
// keeping every other line of the file.
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app

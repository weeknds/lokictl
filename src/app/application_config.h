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
    // The `theme` key of [application]: the theme the user picked last,
    // trimmed and in lower case. Unset when the file has no such key. The
    // name is not checked here; the caller looks it up in the theme table.
    std::optional<std::string> theme;
    // The `dark_mode` key that picked the theme before themes were saved by
    // name (yes: Terminal, no: Instrument). Unset when the file has no such
    // key. It is only read, never written. When the key appears more than
    // once, the last line counts.
    std::optional<bool> dark_mode;
    // True when the `dark_mode` line that counts has a value other than
    // yes/no, true/false or 1/0. dark_mode is then unset; the line does not
    // make the load fail, so a `theme` key in the same file still counts.
    // Like dark_mode, it is only read, never written.
    bool dark_mode_invalid{false};
};

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
// Updates the [application] section of `path`, creating the file, its
// folder and the section when needed:
// - `theme=<name>` is written when config.theme is set. It replaces the
//   first `theme` line of the section, and any further `theme` lines are
//   dropped; with no such line it goes at the end of the section.
// - Every `dark_mode` line of the section is always removed;
//   config.dark_mode is never written.
// Every other line, comment and section is kept as it is.
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app

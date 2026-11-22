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
    // The `dim_when_idle` key of [application]: whether the screen dims
    // when no key was pressed for a while. Unset when the file has no such
    // key or its value is not yes/no, true/false or 1/0. When the key
    // appears more than once, the last line counts.
    std::optional<bool> dim_when_idle;
    // The `hotspot_password` key of [application]: the password of the
    // hotspot that Router starts, as it stands after the '=' (trimmed).
    // Unset when the file has no such key; when it appears more than once,
    // the last line counts. It is not checked here (see
    // valid_hotspot_password()).
    std::optional<std::string> hotspot_password;
};

// True for a password a WPA2 hotspot takes: 8 to 63 printable ASCII
// characters, without a space at either end (the file is read trimmed).
bool valid_hotspot_password(const std::string& password);

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
// Updates the [application] section of `path`, creating the file, its
// folder and the section when needed:
// - `theme=<name>` is written when config.theme is set,
//   `dim_when_idle=yes|no` when config.dim_when_idle is set and
//   `hotspot_password=<password>` when config.hotspot_password is set. Each replaces
//   the first line of its key in the section, and any further lines of
//   that key are dropped; with no such line it goes at the end of the
//   section. A key that is not set is left as it is in the file.
// - Every `dark_mode` line of the section is always removed;
//   config.dark_mode is never written.
// Every other line, comment and section is kept as it is. When the
// password is written, the file is made readable by its owner only (mode
// 0600) before any line goes into it.
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app

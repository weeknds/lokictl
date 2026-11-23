/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
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
    // The `keep_mode` key of [application]: whether the network mode
    // (hotspot, tunnel, kill switch, blocked devices) stays when LokiCTL
    // quits. Unset when the file has no such key or its value is not
    // yes/no, true/false or 1/0. When the key appears more than once, the
    // last line counts.
    std::optional<bool> keep_mode;
    // The `hotspot_password` key of [application]: the password of the
    // hotspot that Router starts, as it stands after the '=' (trimmed).
    // Unset when the file has no such key; when it appears more than once,
    // the last line counts. It is not checked here (see
    // valid_hotspot_password()).
    std::optional<std::string> hotspot_password;
    // The `hotspot_name` key of [application]: the hotspot's network name
    // after the user renamed it, as it stands after the '=' (trimmed); empty
    // means the default name. Unset when the file has no such key; when it
    // appears more than once, the last line counts. It is not checked here
    // (see valid_hotspot_name()).
    std::optional<std::string> hotspot_name;
    // The `blocked_devices` key of [application]: the hardware addresses of
    // the devices LokiCTL blocks on its hotspot, separated by commas, as it
    // stands after the '=' (trimmed); empty for none. Unset when the file
    // has no such key; when it appears more than once, the last line
    // counts. It is not checked here (see platform::parse_blocked_devices()).
    std::optional<std::string> blocked_devices;
    // The `speed_test_server` key of [application]: the speed test's server
    // ("https://speed.cloudflare.com"), as it stands after the '='
    // (trimmed). Unset when the file has no such key; when it appears more
    // than once, the last line counts. It is not checked here (see
    // model::valid_speed_server()). Only read, never written: the user
    // edits it in the file.
    std::optional<std::string> speed_test_server;
};

// True for a password a WPA2 hotspot takes: 8 to 63 printable ASCII
// characters, without a space at either end (the file is read trimmed).
bool valid_hotspot_password(const std::string& password);

// The longest hotspot name, in bytes: the longest name a Wi-Fi network can
// have.
constexpr std::size_t kHotspotNameMax = 32;

// True for a name the hotspot may take: 1 to kHotspotNameMax printable
// ASCII characters, without a space at either end (the file is read
// trimmed).
bool valid_hotspot_name(const std::string& name);

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
// Updates the [application] section of `path`, creating the file, its
// folder and the section when needed:
// - `theme=<name>` is written when config.theme is set,
//   `dim_when_idle=yes|no` when config.dim_when_idle is set,
//   `keep_mode=yes|no` when config.keep_mode is set,
//   `hotspot_password=<password>` when config.hotspot_password is set,
//   `hotspot_name=<name>` when config.hotspot_name is set and
//   `blocked_devices=<list>` when config.blocked_devices is set. Each replaces
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

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_source.h"
#include "theme.h"

#include <optional>
#include <string>

namespace app {

class Application {
public:
    // Reads the command line, then starts the app, writes a snapshot or
    // prints help. Returns the process exit code.
    int run(int argc, char** argv);

private:
    // `theme_override` comes from --theme; it is used for this run only.
    // `data_source` is where the values come from (--data, or the default of
    // a start) and `data_root` is handed to it (LOKICTL_DATA_ROOT).
    int run_interactive(std::optional<view::ThemeId> theme_override,
                        platform::DataSourceKind data_source,
                        const std::string& data_root);
};

} // namespace app

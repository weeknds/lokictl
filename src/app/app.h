/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "theme.h"

#include <optional>

namespace app {

class Application {
public:
    // Reads the command line, then starts the app, writes a snapshot or
    // prints help. Returns the process exit code.
    int run(int argc, char** argv);

private:
    // `theme_override` comes from --theme; it is used for this run only.
    int run_interactive(std::optional<view::ThemeId> theme_override);
};

} // namespace app

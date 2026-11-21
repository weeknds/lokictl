/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace app {

class Application {
public:
    // Reads the command line, then starts the app, writes a snapshot or
    // prints help. Returns the process exit code.
    int run(int argc, char** argv);

private:
    int run_interactive();
};

} // namespace app

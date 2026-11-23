/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "help_popup.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// The commands of the command bar (`:` on any page), as plain functions:
// app::KeyController reads the keys and carries out what these return.

// The longest line the bar takes; it fits the 320 px screen in every theme.
constexpr std::size_t kCommandMaxLength = 36;

// True for a key the bar types into the line: printable ASCII.
bool is_command_character(uint32_t key);

enum class CommandAction {
    None,     // an empty line: just close the bar
    Help,     // show the command list (command_help())
    Theme,    // switch to the theme in table row `theme_row`
    OpenPage, // open `page`
    Error,    // keep the bar open and show `message`
};

struct CommandResult {
    CommandAction action{CommandAction::None};
    int32_t theme_row{-1};
    model::AppPage page{model::AppPage::Home};
    std::string message;
};

// Reads one line: `help`, `theme <name>`, or a page name (`home`, `map`,
// `tools`, `discover`, `wifi`, `status`, `settings`). Words are separated by
// spaces and read without regard to case. Anything else is an Error with a
// message such as "Unknown command: <word>".
CommandResult parse_command(std::string_view line);

struct CommandCompletion {
    std::string line;                 // the line after completing
    std::vector<std::string> choices; // several commands fit: their names
    bool matched{false};              // at least one command fits
};

// Completes the first word of `line`: a prefix that fits one command becomes
// that command (with a space after it when it takes a name); a prefix that
// fits several grows to the part they share, and all of them are listed. A
// line that already has a second word is left as it is (not matched).
CommandCompletion complete_command(std::string_view line);

// The command list for the help panel.
view::widgets::HelpContent command_help();

} // namespace app

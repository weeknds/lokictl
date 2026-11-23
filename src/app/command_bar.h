/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "help_popup.h"
#include "home_data.h"
#include "tool_data.h"

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
    Mode,     // switch to `mode` the way the Modes page does
    Tool,     // start `tool` on its page, on `target` when there is one
    Error,    // keep the bar open and show `message`
};

struct CommandResult {
    CommandAction action{CommandAction::None};
    int32_t theme_row{-1};
    model::AppPage page{model::AppPage::Home};
    model::NetMode mode{model::NetMode::Client};
    model::ToolKind tool{model::ToolKind::Ping};
    std::string target; // a checked target (model::valid_tool_target()); empty: the page's choice
    std::string message;
};

// Reads one line: `help`, `theme <name>`, `mode <name>` (client, router or
// share), a page name (`home`, `map`, `tools`, `discover`, `wifi`,
// `status`, `settings`), or a tool: `ping`, `dns`, `trace` and `ports` with
// an optional target, `scan` (Listen) and `probe`. Words are separated by
// spaces and read without regard to case. As the target of `ping`, `trace`
// and `ports`, `router` and `this` stand for home's gateway and uplink
// addresses. Anything else is an Error with a message such as "Unknown
// command: <word>"; so is a target the tool would not take, and `router`
// or `this` without an address to stand for.
CommandResult parse_command(std::string_view line, const model::HomeData& home);

// Why `mode` can't be switched to now, or empty when it can: the Modes
// page's key 6 rules (not while a switch runs, not the active mode). What
// only the switch itself finds out (no cable, Back to Client first, ...)
// shows on the Modes page afterwards.
std::string mode_command_refusal(model::NetMode mode, const model::HomeData& home);

// Why a tool command can't start now, or empty when it can: while any tool
// runs ("Ping is running: stop it first"), and for `scan` and `probe`
// without a network to listen on.
std::string tool_command_refusal(model::ToolKind kind,
                                 const model::ToolsData& tools,
                                 const model::HomeData& home);

// The page a tool command opens.
model::AppPage tool_command_page(model::ToolKind kind);

struct CommandCompletion {
    std::string line;                 // the line after completing
    std::vector<std::string> choices; // several commands fit: their names
    bool matched{false};              // at least one command fits
};

// Completes the first word of `line`: a prefix that fits one command becomes
// that command (with a space after it when it takes a name or a target); a
// prefix that fits several grows to the part they share, and all of them are
// listed. The second word is completed the same way from the command's
// names: theme and mode names; for `ping` and `trace` `router`, `this`, the
// presets and the addresses Discover found; for `ports` `router`, `this` and
// Discover's private addresses; for `dns` the presets. `router` and `this`
// are offered only when home has their address. A line with a third word,
// or a command that takes no name, is left as it is (not matched).
CommandCompletion complete_command(std::string_view line,
                                   const model::ToolsData& tools,
                                   const model::HomeData& home);

// The choices joined by two spaces for the bar's message line, kept within
// kCommandMaxLength characters: those that don't fit are counted at the end
// ("+3").
std::string command_choices_text(const std::vector<std::string>& choices);

// The lines that ran from the command bar, for Up and Down. Kept in memory
// only: never saved and never logged, since a line can hold a target.
class CommandHistory {
public:
    static constexpr std::size_t kMaxLines = 8;

    // Adds a line that ran (spaces at either end dropped). An empty line or
    // a repeat of the newest one is not added; past kMaxLines the oldest
    // goes. Also ends any stepping (reset()).
    void add(std::string_view line);
    // Ends stepping: the next older() starts again at the newest line.
    void reset();
    // Up: the line before the one shown, starting at the newest; `current`
    // is kept as the line being typed when this is the first step. Stops
    // at the oldest. False (and `line` unchanged) when there is none.
    bool older(const std::string& current, std::string& line);
    // Down: the line after the one shown, and past the newest the line
    // being typed, which ends stepping. False when not stepping.
    bool newer(std::string& line);

private:
    std::vector<std::string> lines_; // oldest first
    std::size_t position_{0};        // the line shown; lines_.size() while not stepping
    std::string typed_;              // the line being typed before the first step
};

// The command list for the help panel.
view::widgets::HelpContent command_help();

} // namespace app

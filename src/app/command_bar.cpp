/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "command_bar.h"

#include "theme.h"

#include <utility>

namespace app {
namespace {

enum class CommandKind {
    Help,
    Theme,
    Page,
};

struct CommandSpec {
    const char* name;
    CommandKind kind;
    model::AppPage page; // for CommandKind::Page
};

// In the order TAB lists them.
constexpr CommandSpec kCommands[] = {
    {"help", CommandKind::Help, model::AppPage::Home},
    {"theme", CommandKind::Theme, model::AppPage::Home},
    {"home", CommandKind::Page, model::AppPage::Home},
    {"map", CommandKind::Page, model::AppPage::Map},
    {"tools", CommandKind::Page, model::AppPage::Tools},
    {"discover", CommandKind::Page, model::AppPage::Discover},
    {"wifi", CommandKind::Page, model::AppPage::Wifi},
    {"status", CommandKind::Page, model::AppPage::Status},
    {"settings", CommandKind::Page, model::AppPage::Settings},
};

std::string lowercase(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

// The words of `line`, split at spaces; empty words are skipped.
std::vector<std::string> split_words(std::string_view line) {
    std::vector<std::string> words;
    std::size_t start = 0;
    while (start < line.size()) {
        const std::size_t space = line.find(' ', start);
        const std::size_t end = space == std::string_view::npos ? line.size() : space;
        if (end > start) {
            words.push_back(lowercase(line.substr(start, end - start)));
        }
        start = end + 1;
    }
    return words;
}

const CommandSpec* find_command(std::string_view name) {
    for (const auto& command : kCommands) {
        if (name == command.name) {
            return &command;
        }
    }
    return nullptr;
}

CommandResult error(std::string message) {
    CommandResult result;
    result.action = CommandAction::Error;
    result.message = std::move(message);
    return result;
}

} // namespace

bool is_command_character(uint32_t key) {
    return key >= 0x20 && key <= 0x7E;
}

CommandResult parse_command(std::string_view line) {
    const std::vector<std::string> words = split_words(line);
    if (words.empty()) {
        return {};
    }

    const CommandSpec* command = find_command(words.front());
    if (!command) {
        return error("Unknown command: " + words.front());
    }

    CommandResult result;
    switch (command->kind) {
        case CommandKind::Help:
            if (words.size() != 1) {
                return error("Use: help");
            }
            result.action = CommandAction::Help;
            return result;
        case CommandKind::Theme: {
            if (words.size() != 2) {
                return error("Use: theme " + view::theme_names());
            }
            const view::ThemeSpec* theme = view::find_theme(words[1]);
            if (!theme) {
                return error("Unknown theme: " + words[1]);
            }
            result.action = CommandAction::Theme;
            result.theme_row = static_cast<int32_t>(theme->id);
            return result;
        }
        case CommandKind::Page:
            if (words.size() != 1) {
                return error(std::string("Use: ") + command->name);
            }
            result.action = CommandAction::OpenPage;
            result.page = command->page;
            return result;
    }
    return result;
}

CommandCompletion complete_command(std::string_view line) {
    CommandCompletion completion;
    completion.line = std::string(line);

    // Leading spaces don't count; a space after the first word means the
    // command is already typed (names are completed in a later version).
    const std::size_t first = line.find_first_not_of(' ');
    const std::string_view word = first == std::string_view::npos ? std::string_view{}
                                                                  : line.substr(first);
    if (word.find(' ') != std::string_view::npos) {
        return completion;
    }

    const std::string prefix = lowercase(word);
    std::vector<const CommandSpec*> fits;
    for (const auto& command : kCommands) {
        if (std::string_view(command.name).substr(0, prefix.size()) == prefix) {
            fits.push_back(&command);
        }
    }
    if (fits.empty()) {
        return completion;
    }
    completion.matched = true;

    if (fits.size() == 1) {
        completion.line = fits.front()->name;
        if (fits.front()->kind == CommandKind::Theme) {
            completion.line += ' ';
        }
        return completion;
    }

    // The part every fitting name shares, at least the prefix typed.
    std::string shared = fits.front()->name;
    for (const CommandSpec* command : fits) {
        const std::string_view name = command->name;
        std::size_t length = 0;
        while (length < shared.size() && length < name.size() && shared[length] == name[length]) {
            ++length;
        }
        shared.resize(length);
    }
    completion.line = shared;
    for (const CommandSpec* command : fits) {
        completion.choices.emplace_back(command->name);
    }
    return completion;
}

view::widgets::HelpContent command_help() {
    view::widgets::HelpContent content;
    content.badge = "COMMANDS";
    content.rows = {
        {"help", "This list"},
        {"theme NAME", "Switch the theme"},
        {"home map tools", "Open that page"},
        {"discover wifi", "Open that page"},
        {"status settings", "Open that page"},
    };
    return content;
}

} // namespace app

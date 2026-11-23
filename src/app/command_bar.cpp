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
    Tool,
};

struct CommandSpec {
    const char* name;
    CommandKind kind;
    model::AppPage page;  // for CommandKind::Page
    model::ToolKind tool; // for CommandKind::Tool
    const char* usage;    // for CommandKind::Tool
};

// In the order TAB lists them.
constexpr CommandSpec kCommands[] = {
    {"help", CommandKind::Help, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"theme", CommandKind::Theme, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"home", CommandKind::Page, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"map", CommandKind::Page, model::AppPage::Map, model::ToolKind::Ping, nullptr},
    {"tools", CommandKind::Page, model::AppPage::Tools, model::ToolKind::Ping, nullptr},
    {"discover", CommandKind::Page, model::AppPage::Discover, model::ToolKind::Ping, nullptr},
    {"wifi", CommandKind::Page, model::AppPage::Wifi, model::ToolKind::Ping, nullptr},
    {"status", CommandKind::Page, model::AppPage::Status, model::ToolKind::Ping, nullptr},
    {"settings", CommandKind::Page, model::AppPage::Settings, model::ToolKind::Ping, nullptr},
    {"ping", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Ping, "ping [target]"},
    {"dns", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Dns, "dns [name]"},
    {"trace", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Trace, "trace [target]"},
    {"ports", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Ports, "ports [address]"},
    {"scan", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Listen, "scan"},
    {"probe", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Probe, "probe"},
};

// Ping, DNS, Trace and Ports take a target; Listen and Probe use the uplink.
bool takes_target(model::ToolKind kind) {
    return kind != model::ToolKind::Listen && kind != model::ToolKind::Probe;
}

// Why `target` is not taken by `kind` (model::valid_tool_target() said no).
std::string target_error(model::ToolKind kind, const std::string& target) {
    if (kind == model::ToolKind::Dns && model::is_ipv4_text(target)) {
        return "DNS needs a name, not an address";
    }
    if (kind == model::ToolKind::Ports) {
        return model::is_ipv4_text(target) ? "Ports needs a private address" : "Ports needs an IPv4 address";
    }
    return "Not a valid target: " + target;
}

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

bool completes_with_space(const CommandSpec& command) {
    return command.kind == CommandKind::Theme || (command.kind == CommandKind::Tool && takes_target(command.tool));
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
        case CommandKind::Tool:
            if (words.size() > (takes_target(command->tool) ? 2U : 1U)) {
                return error(std::string("Use: ") + command->usage);
            }
            if (words.size() == 2 && !model::valid_tool_target(command->tool, words[1])) {
                return error(target_error(command->tool, words[1]));
            }
            result.action = CommandAction::Tool;
            result.tool = command->tool;
            result.page = tool_command_page(command->tool);
            if (words.size() == 2) {
                result.target = words[1];
            }
            return result;
    }
    return result;
}

std::string tool_command_refusal(model::ToolKind kind,
                                 const model::ToolsData& tools,
                                 const model::HomeData& home) {
    if (model::tool_running(tools)) {
        // Worded as the pages' status lines say it.
        struct Running {
            model::ToolKind kind;
            const char* text;
        };
        constexpr Running kRunning[] = {
            {model::ToolKind::Ping, "Ping is running"},
            {model::ToolKind::Dns, "DNS lookup is running"},
            {model::ToolKind::Trace, "Trace is running"},
            {model::ToolKind::Listen, "Discover is listening"},
            {model::ToolKind::Probe, "Discover is probing"},
            {model::ToolKind::Ports, "Port scan is running"},
        };
        for (const Running& running : kRunning) {
            if (model::tool_state(tools, running.kind) == model::ToolState::Running) {
                return std::string(running.text) + ": stop it first";
            }
        }
        return "Another tool is running";
    }
    // As the Discover page's keys 6 and 8: off without a network.
    if (!takes_target(kind) && !model::valid_interface_name(home.uplink_name)) {
        return "No network: join one first";
    }
    return {};
}

model::AppPage tool_command_page(model::ToolKind kind) {
    switch (kind) {
        case model::ToolKind::Ping:
            return model::AppPage::Ping;
        case model::ToolKind::Dns:
            return model::AppPage::Dns;
        case model::ToolKind::Trace:
            return model::AppPage::Trace;
        case model::ToolKind::Ports:
            return model::AppPage::Ports;
        case model::ToolKind::Listen:
        case model::ToolKind::Probe:
            break;
    }
    return model::AppPage::Discover;
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
        if (completes_with_space(*fits.front())) {
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
        {"theme NAME", "Switch the theme"},
        {"PAGE", "home map tools status"},
        {"PAGE", "discover wifi settings"},
        {"TOOL [HOST]", "ping dns trace ports"},
        {"TOOL", "scan probe"},
    };
    return content;
}

} // namespace app

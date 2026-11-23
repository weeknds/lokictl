/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "command_bar.h"

#include "theme.h"

#include <algorithm>
#include <utility>

namespace app {
namespace {

enum class CommandKind {
    Help,
    Report,
    Theme,
    Mode,
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
    {"report", CommandKind::Report, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"theme", CommandKind::Theme, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"mode", CommandKind::Mode, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"home", CommandKind::Page, model::AppPage::Home, model::ToolKind::Ping, nullptr},
    {"map", CommandKind::Page, model::AppPage::Map, model::ToolKind::Ping, nullptr},
    {"tools", CommandKind::Page, model::AppPage::Tools, model::ToolKind::Ping, nullptr},
    {"discover", CommandKind::Page, model::AppPage::Discover, model::ToolKind::Ping, nullptr},
    {"watch", CommandKind::Page, model::AppPage::Watch, model::ToolKind::Ping, nullptr},
    {"trap", CommandKind::Page, model::AppPage::Trap, model::ToolKind::Ping, nullptr},
    {"health", CommandKind::Page, model::AppPage::Health, model::ToolKind::Ping, nullptr},
    {"leak", CommandKind::Page, model::AppPage::Leak, model::ToolKind::Ping, nullptr},
    {"signin", CommandKind::Page, model::AppPage::Signin, model::ToolKind::Ping, nullptr},
    {"wifi", CommandKind::Page, model::AppPage::Wifi, model::ToolKind::Ping, nullptr},
    {"status", CommandKind::Page, model::AppPage::Status, model::ToolKind::Ping, nullptr},
    {"settings", CommandKind::Page, model::AppPage::Settings, model::ToolKind::Ping, nullptr},
    {"ping", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Ping, "ping [target]"},
    {"dns", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Dns, "dns [name]"},
    {"trace", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Trace, "trace [target]"},
    {"ports", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Ports, "ports [address]"},
    {"scan", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Listen, "scan"},
    {"probe", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Probe, "probe"},
    {"speed", CommandKind::Tool, model::AppPage::Home, model::ToolKind::Speed, "speed"},
};

// The modes `mode` switches to, in the order TAB lists them: the ones the
// Modes page's key 6 can use.
constexpr model::NetMode kSwitchModes[] = {
    model::NetMode::Client,
    model::NetMode::Router,
    model::NetMode::Share,
    model::NetMode::Vpn,
};
constexpr const char* kModeUsage = "Use: mode client|router|share|vpn";

// The words that stand for an address as the target of Ping, Trace and Ports.
constexpr std::string_view kRouterWord = "router";
constexpr std::string_view kThisWord = "this";

// Ping, DNS, Trace and Ports take a target; Listen, Probe and Speed use the
// uplink.
bool takes_target(model::ToolKind kind) {
    return kind != model::ToolKind::Listen && kind != model::ToolKind::Probe && kind != model::ToolKind::Speed;
}

// Whether `router` and `this` may stand for an address with `kind`: not
// for DNS, which looks names up.
bool takes_address_word(model::ToolKind kind) {
    return takes_target(kind) && kind != model::ToolKind::Dns;
}

// Whether `address` may be offered (by TAB) to `kind`: Ports only asks
// private addresses, Ping and Trace any IPv4 address.
bool offered_address(model::ToolKind kind, const std::string& address) {
    return kind == model::ToolKind::Ports ? model::port_scan_address(address) : model::is_ipv4_text(address);
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
    return command.kind == CommandKind::Theme || command.kind == CommandKind::Mode ||
           (command.kind == CommandKind::Tool && takes_target(command.tool));
}

std::string lowercase_mode_name(model::NetMode mode) {
    return lowercase(model::net_mode_name(mode));
}

// The names TAB offers as the second word of `command`, in the order it
// lists them; empty for a command that takes none.
std::vector<std::string> argument_choices(const CommandSpec& command,
                                          const model::ToolsData& tools,
                                          const model::HomeData& home) {
    std::vector<std::string> choices;
    const auto add = [&choices](std::string choice) {
        if (std::find(choices.begin(), choices.end(), choice) == choices.end()) {
            choices.push_back(std::move(choice));
        }
    };
    switch (command.kind) {
        case CommandKind::Theme:
            for (int id = 0; id < view::kThemeCount; ++id) {
                add(view::theme_spec(static_cast<view::ThemeId>(id)).name);
            }
            break;
        case CommandKind::Mode:
            for (const model::NetMode mode : kSwitchModes) {
                add(lowercase_mode_name(mode));
            }
            break;
        case CommandKind::Tool:
            if (command.tool == model::ToolKind::Dns) {
                for (const char* preset : model::kDnsPresets) {
                    add(preset);
                }
                break;
            }
            if (!takes_address_word(command.tool)) {
                break;
            }
            if (offered_address(command.tool, home.gateway_address)) {
                add(std::string(kRouterWord));
            }
            if (offered_address(command.tool, home.uplink_address)) {
                add(std::string(kThisWord));
            }
            if (command.tool != model::ToolKind::Ports) {
                for (const char* preset : model::kPingPresets) {
                    add(preset);
                }
            }
            // The router and this device have their words; the Cardputer's
            // own ports are this device too.
            for (const model::DiscoverDevice& entry : model::discover_devices(tools.listen, tools.probe)) {
                const std::string& address = entry.device.address;
                if (entry.device.own_port.empty() && offered_address(command.tool, address) &&
                    address != home.gateway_address && address != home.uplink_address) {
                    add(address);
                }
            }
            break;
        case CommandKind::Help:
        case CommandKind::Report:
        case CommandKind::Page:
            break;
    }
    return choices;
}

// Completes `word`, which follows `head`, from `names`: one fit becomes
// it, several grow to the part they share and are listed.
CommandCompletion complete_word(std::string_view head, std::string_view word, const std::vector<std::string>& names) {
    CommandCompletion completion;
    completion.line = std::string(head) + std::string(word);

    const std::string prefix = lowercase(word);
    std::vector<const std::string*> fits;
    for (const std::string& name : names) {
        if (std::string_view(name).substr(0, prefix.size()) == prefix) {
            fits.push_back(&name);
        }
    }
    if (fits.empty()) {
        return completion;
    }
    completion.matched = true;

    if (fits.size() == 1) {
        completion.line = std::string(head) + *fits.front();
        return completion;
    }

    // The part every fitting name shares, at least the prefix typed.
    std::string shared = *fits.front();
    for (const std::string* name : fits) {
        std::size_t length = 0;
        while (length < shared.size() && length < name->size() && shared[length] == (*name)[length]) {
            ++length;
        }
        shared.resize(length);
    }
    completion.line = std::string(head) + shared;
    for (const std::string* name : fits) {
        completion.choices.push_back(*name);
    }
    return completion;
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

CommandResult parse_command(std::string_view line, const model::HomeData& home) {
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
        case CommandKind::Report:
            if (words.size() != 1) {
                return error("Use: report");
            }
            result.action = CommandAction::Report;
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
        case CommandKind::Mode: {
            if (words.size() != 2) {
                return error(kModeUsage);
            }
            for (const model::NetMode mode : kSwitchModes) {
                if (words[1] == lowercase_mode_name(mode)) {
                    result.action = CommandAction::Mode;
                    result.mode = mode;
                    return result;
                }
            }
            return error("Unknown mode: " + words[1]);
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
            if (words.size() == 2) {
                std::string target = words[1];
                if (takes_address_word(command->tool) && target == kRouterWord) {
                    if (!model::is_ipv4_text(home.gateway_address)) {
                        return error("No router: join a network first");
                    }
                    target = home.gateway_address;
                } else if (takes_address_word(command->tool) && target == kThisWord) {
                    if (!model::is_ipv4_text(home.uplink_address)) {
                        return error("No address for this device");
                    }
                    target = home.uplink_address;
                }
                if (!model::valid_tool_target(command->tool, target)) {
                    return error(target_error(command->tool, target));
                }
                result.target = std::move(target);
            }
            result.action = CommandAction::Tool;
            result.tool = command->tool;
            result.page = tool_command_page(command->tool);
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
            {model::ToolKind::Speed, "Speed test is running"},
            {model::ToolKind::Health, "Health check is running"},
            {model::ToolKind::Leak, "Leak test is running"},
            {model::ToolKind::Signin, "Sign-in check is running"},
        };
        for (const Running& running : kRunning) {
            if (model::tool_state(tools, running.kind) == model::ToolState::Running) {
                return std::string(running.text) + ": stop it first";
            }
        }
        return "Another tool is running";
    }
    // Scan and probe are off without a network, as the Discover page's keys
    // 6 and 8 are; a speed test needs one too.
    if (!takes_target(kind) && !model::valid_interface_name(home.uplink_name)) {
        return "No network: join one first";
    }
    return {};
}

std::string mode_command_refusal(model::NetMode mode, const model::HomeData& home) {
    if (home.mode_switch.state == model::SwitchState::Switching) {
        return "A mode switch is under way";
    }
    if (mode == home.mode || (mode == model::NetMode::Vpn && home.vpn.state != model::VpnState::Off)) {
        return std::string("Already in ") + model::net_mode_name(mode) + " mode";
    }
    // As the Modes page's VPN row: nothing to use without a file.
    if (mode == model::NetMode::Vpn && home.vpn.files.empty()) {
        return "No WireGuard files";
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
        case model::ToolKind::Speed:
            return model::AppPage::Speed;
        case model::ToolKind::Health:
            return model::AppPage::Health;
        case model::ToolKind::Leak:
            return model::AppPage::Leak;
        case model::ToolKind::Signin:
            return model::AppPage::Signin;
        case model::ToolKind::Listen:
        case model::ToolKind::Probe:
            break;
    }
    return model::AppPage::Discover;
}

CommandCompletion complete_command(std::string_view line,
                                   const model::ToolsData& tools,
                                   const model::HomeData& home) {
    CommandCompletion completion;
    completion.line = std::string(line);

    // Leading spaces don't count.
    const std::size_t first = line.find_first_not_of(' ');
    const std::string_view rest = first == std::string_view::npos ? std::string_view{} : line.substr(first);
    const std::size_t space = rest.find(' ');
    if (space == std::string_view::npos) {
        // The first word: the commands, without the spaces before it.
        std::vector<std::string> names;
        for (const auto& command : kCommands) {
            names.emplace_back(command.name);
        }
        CommandCompletion words = complete_word({}, rest, names);
        if (words.matched && words.choices.empty()) {
            if (const CommandSpec* command = find_command(words.line); command && completes_with_space(*command)) {
                words.line += ' ';
            }
        }
        return words.matched ? words : completion;
    }

    // The second word, from the names of the command typed first.
    const CommandSpec* command = find_command(lowercase(rest.substr(0, space)));
    if (!command) {
        return completion;
    }
    const std::size_t second = rest.find_first_not_of(' ', space);
    const std::size_t word_start = second == std::string_view::npos ? rest.size() : second;
    const std::string_view word = rest.substr(word_start);
    if (word.find(' ') != std::string_view::npos) {
        return completion;
    }
    const std::vector<std::string> names = argument_choices(*command, tools, home);
    if (names.empty()) {
        return completion;
    }
    const std::string_view head = line.substr(0, first + word_start);
    CommandCompletion words = complete_word(head, word, names);
    return words.matched ? words : completion;
}

std::string command_choices_text(const std::vector<std::string>& choices) {
    constexpr std::string_view kGap = "  ";
    std::string text;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        const std::size_t after = choices.size() - index - 1;
        // Room for "  +N" while more choices follow.
        const std::size_t count_room = after == 0 ? 0 : kGap.size() + 1 + std::to_string(after).size();
        const std::size_t length = text.size() + (text.empty() ? 0 : kGap.size()) + choices[index].size();
        if (!text.empty() && length + count_room > kCommandMaxLength) {
            text += std::string(kGap) + "+" + std::to_string(choices.size() - index);
            return text;
        }
        if (!text.empty()) {
            text += kGap;
        }
        text += choices[index];
    }
    return text;
}

void CommandHistory::add(std::string_view line) {
    const std::size_t first = line.find_first_not_of(' ');
    if (first != std::string_view::npos) {
        const std::size_t last = line.find_last_not_of(' ');
        const std::string trimmed(line.substr(first, last - first + 1));
        if (lines_.empty() || lines_.back() != trimmed) {
            lines_.push_back(trimmed);
            if (lines_.size() > kMaxLines) {
                lines_.erase(lines_.begin());
            }
        }
    }
    reset();
}

void CommandHistory::reset() {
    position_ = lines_.size();
    typed_.clear();
}

bool CommandHistory::older(const std::string& current, std::string& line) {
    if (position_ == 0) {
        return false;
    }
    if (position_ == lines_.size()) {
        typed_ = current;
    }
    --position_;
    line = lines_[position_];
    return true;
}

bool CommandHistory::newer(std::string& line) {
    if (position_ >= lines_.size()) {
        return false;
    }
    ++position_;
    if (position_ == lines_.size()) {
        line = typed_;
        typed_.clear();
    } else {
        line = lines_[position_];
    }
    return true;
}

view::widgets::HelpContent command_help() {
    view::widgets::HelpContent content;
    content.badge = "COMMANDS";
    content.rows = {
        {"theme, mode", "Switch theme or mode"},
        {"PAGE", "home map tools status"},
        {"PAGE", "discover wifi settings"},
        {"TOOL [HOST]", "ping dns trace ports"},
        {"TOOL", "scan probe speed"},
    };
    return content;
}

} // namespace app

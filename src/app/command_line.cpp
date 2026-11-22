/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "command_line.h"

#include "page_registry.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace app {
namespace {

LaunchOptions invalid(std::string message) {
    LaunchOptions options;
    options.mode = LaunchMode::Invalid;
    options.error = std::move(message);
    return options;
}

// Accepts plain decimal digits only (no sign, spaces or trailing text) and
// stops as soon as the value goes past the limit, so it cannot overflow.
bool parse_settle_ms(std::string_view text, uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    uint32_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        result = result * 10U + static_cast<uint32_t>(character - '0');
        if (result > kMaxSettleMs) {
            return false;
        }
    }
    value = result;
    return true;
}

// Splits a comma-separated list of theme names. Returns false if any name
// is empty ("a,,b", or a comma at either end).
bool split_theme_list(std::string_view text, std::vector<std::string>& names) {
    names.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::string_view name =
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (name.empty()) {
            return false;
        }
        names.emplace_back(name);
        if (comma == std::string_view::npos) {
            return true;
        }
        start = comma + 1;
    }
}

std::string padded(std::string text, std::size_t width) {
    if (text.size() < width) {
        text.append(width - text.size(), ' ');
    }
    return text;
}

} // namespace

LaunchOptions parse_command_line(int argc, const char* const* argv) {
    LaunchOptions options;
    if (argc <= 1 || !argv) {
        return options;
    }

    bool list_pages = false;
    bool has_snapshot = false;
    bool has_theme = false;
    // --page, --settle-ms or --switch-to, which only mean something for a
    // snapshot.
    bool has_snapshot_option = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index] ? argv[index] : "";

        if (argument == "--help" || argument == "-h") {
            LaunchOptions help;
            help.mode = LaunchMode::Help;
            return help;
        }
        if (argument == "--list-pages") {
            list_pages = true;
            continue;
        }

        const bool takes_value = argument == "--snapshot" || argument == "--page" ||
                                 argument == "--theme" || argument == "--settle-ms" ||
                                 argument == "--switch-to";
        if (!takes_value) {
            return invalid("unknown argument '" + std::string(argument) + "'");
        }

        const std::string_view value =
            index + 1 < argc && argv[index + 1] ? std::string_view(argv[index + 1]) : std::string_view();
        if (value.empty() || value.substr(0, 2) == "--") {
            return invalid(std::string(argument) + " needs a value");
        }
        ++index;

        if (argument == "--snapshot") {
            options.snapshot_path = std::string(value);
            has_snapshot = true;
            continue;
        }

        if (argument == "--theme") {
            options.theme = std::string(value);
            has_theme = true;
            continue;
        }

        has_snapshot_option = true;
        if (argument == "--page") {
            options.page = std::string(value);
        } else if (argument == "--switch-to") {
            if (!split_theme_list(value, options.switch_to)) {
                return invalid("--switch-to needs theme names separated by commas, "
                               "for example terminal,instrument");
            }
        } else if (!parse_settle_ms(value, options.settle_ms)) {
            return invalid("--settle-ms must be a whole number from 0 to " +
                           std::to_string(kMaxSettleMs));
        }
    }

    if (list_pages) {
        if (has_snapshot || has_theme || has_snapshot_option) {
            return invalid("--list-pages cannot be combined with other options");
        }
        options.mode = LaunchMode::ListPages;
        return options;
    }
    if (!has_snapshot) {
        if (has_snapshot_option) {
            return invalid(
                "--page, --settle-ms and --switch-to only work together with --snapshot");
        }
        // A normal start, possibly with --theme for this run.
        options.mode = LaunchMode::Interactive;
        return options;
    }

    options.mode = LaunchMode::Snapshot;
    return options;
}

std::string usage_text() {
    const std::string program = APP_NAME;

    const std::string themes = view::theme_names();
    const std::string default_theme = view::theme_spec(view::kDefaultThemeId).name;
    const std::string default_page = page_registry().front().name;

    const std::string start_form = program + " [--theme <name>]";
    const std::string list_form = program + " --list-pages";
    const std::string help_form = program + " --help";
    const std::size_t form_width =
        std::max({start_form.size(), list_form.size(), help_form.size()}) + 3;
    const std::size_t option_width = 21;
    const std::string indent = "  " + std::string(option_width, ' ');

    std::string text;
    text += "Usage:\n";
    text += "  " + padded(start_form, form_width) + "start the app\n";
    text += "  " + padded(list_form, form_width) + "print the page names, one per line\n";
    text += "  " + padded(help_form, form_width) + "show this help\n";
    text += "  " + program + " --snapshot <out.png> [--page <name>] [--theme <name>]\n";
    text += "  " + std::string(program.size() + 1, ' ') + "[--settle-ms <n>] [--switch-to <names>]\n";
    text += "\n";
    text += "  " + padded("--theme <name>", option_width) + "one of: " + themes + "\n";
    text += indent + "When starting the app, the theme is used for this run\n";
    text += indent + "only and is not saved. Snapshot default: " + default_theme + "\n";
    text += "\n";
    text += "--snapshot draws one page into a PNG file of 320x170 pixels without\n";
    text += "opening a window, then exits. Snapshot options:\n";
    text += "  " + padded("--page <name>", option_width) + "page to draw (default: " +
            default_page + ", see --list-pages)\n";
    text += "  " + padded("--settle-ms <n>", option_width) +
            "LVGL time before capture, 0-" + std::to_string(kMaxSettleMs) +
            " ms (default: " + std::to_string(kDefaultSettleMs) + ")\n";
    text += "  " + padded("--switch-to <names>", option_width) +
            "themes to switch to while the app runs, in order and\n";
    text += indent + "separated by commas, the same way key 6 switches. The\n";
    text += indent + "picture is taken after the last switch, and the\n";
    text += indent + "number of loaded fonts is printed to standard error.\n";
    return text;
}

} // namespace app

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "command_line.h"

#include "data_source.h"
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

// Accepts plain decimal digits only (no sign, spaces or trailing text) up
// to `limit`, and stops as soon as the value goes past the limit, so it
// cannot overflow.
bool parse_whole_number(std::string_view text, uint32_t limit, uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    uint32_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        result = result * 10U + static_cast<uint32_t>(character - '0');
        if (result > limit) {
            return false;
        }
    }
    value = result;
    return true;
}

// Splits a comma-separated list of names. Returns false if any name is
// empty ("a,,b", or a comma at either end).
bool split_name_list(std::string_view text, std::vector<std::string>& names) {
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
    bool print_data = false;
    bool has_snapshot = false;
    bool has_theme = false;
    // --page, --settle-ms, --switch-to or --keys, which only mean something
    // for a snapshot.
    bool has_snapshot_option = false;
    bool has_data = false;
    bool has_data_steps = false;

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
        if (argument == "--print-data") {
            print_data = true;
            continue;
        }

        const bool takes_value = argument == "--snapshot" || argument == "--page" ||
                                 argument == "--theme" || argument == "--settle-ms" ||
                                 argument == "--switch-to" || argument == "--keys" ||
                                 argument == "--data" || argument == "--data-steps";
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

        // The name is checked later, against the table of sources.
        if (argument == "--data") {
            options.data = std::string(value);
            has_data = true;
            continue;
        }

        if (argument == "--data-steps") {
            if (!parse_whole_number(value, kMaxDataSteps, options.data_steps)) {
                return invalid("--data-steps must be a whole number from 0 to " +
                               std::to_string(kMaxDataSteps));
            }
            has_data_steps = true;
            continue;
        }

        has_snapshot_option = true;
        if (argument == "--page") {
            options.page = std::string(value);
        } else if (argument == "--switch-to") {
            if (!split_name_list(value, options.switch_to)) {
                return invalid("--switch-to needs theme names separated by commas, "
                               "for example terminal,instrument");
            }
        } else if (argument == "--keys") {
            if (!split_name_list(value, options.keys)) {
                return invalid("--keys needs key names separated by commas, "
                               "for example 8,down,esc");
            }
        } else if (!parse_whole_number(value, kMaxSettleMs, options.settle_ms)) {
            return invalid("--settle-ms must be a whole number from 0 to " +
                           std::to_string(kMaxSettleMs));
        }
    }

    if (list_pages) {
        if (has_snapshot || has_theme || has_snapshot_option || print_data || has_data ||
            has_data_steps) {
            return invalid("--list-pages cannot be combined with other options");
        }
        options.mode = LaunchMode::ListPages;
        return options;
    }
    if (print_data) {
        if (has_snapshot || has_theme || has_snapshot_option) {
            return invalid("--print-data only takes --data and --data-steps");
        }
        options.mode = LaunchMode::PrintData;
        return options;
    }
    if (!has_snapshot) {
        if (has_snapshot_option) {
            return invalid("--page, --settle-ms, --switch-to and --keys only work together "
                           "with --snapshot");
        }
        if (has_data_steps) {
            return invalid("--data-steps only works together with --snapshot or --print-data");
        }
        // A normal start, possibly with --theme and --data for this run.
        options.mode = LaunchMode::Interactive;
        return options;
    }

    // A snapshot; --data and --data-steps are allowed here too.
    options.mode = LaunchMode::Snapshot;
    return options;
}

std::string usage_text() {
    const std::string program = APP_NAME;

    const std::string themes = view::theme_names();
    const std::string default_theme = view::theme_spec(view::kDefaultThemeId).name;
    const std::string default_page = page_registry().front().name;

    const std::string sample_source = platform::data_source_name(platform::DataSourceKind::Sample);
    const std::string demo_source = platform::data_source_name(platform::DataSourceKind::Demo);
    const std::string live_source = platform::data_source_name(platform::DataSourceKind::Live);
    // The default source of a start and of --print-data, as app.cpp picks it.
#if USE_DESKTOP
    const std::string start_source = demo_source;
    const std::string start_source_elsewhere = live_source + " on the device";
#else
    const std::string start_source = live_source;
    const std::string start_source_elsewhere = demo_source + " in the simulator";
#endif

    const std::string start_form = program + " [--theme <name>] [--data <name>]";
    const std::string list_form = program + " --list-pages";
    const std::string help_form = program + " --help";
    const std::size_t form_width =
        std::max({start_form.size(), list_form.size(), help_form.size()}) + 3;
    const std::size_t option_width = 21;
    const std::size_t source_width = 8;
    const std::string indent = "  " + std::string(option_width, ' ');
    const std::string source_indent = indent + "  " + std::string(source_width, ' ');
    const std::string continuation(program.size() + 1, ' ');

    std::string text;
    text += "Usage:\n";
    text += "  " + padded(start_form, form_width) + "start the app\n";
    text += "  " + padded(list_form, form_width) + "print the page names, one per line\n";
    text += "  " + padded(help_form, form_width) + "show this help\n";
    text += "  " + program + " --print-data [--data <name>] [--data-steps <n>]\n";
    text += "  " + program + " --snapshot <out.png> [--page <name>] [--theme <name>]\n";
    text += "  " + continuation + "[--settle-ms <n>] [--switch-to <names>] [--keys <names>]\n";
    text += "  " + continuation + "[--data <name>] [--data-steps <n>]\n";
    text += "\n";
    text += "  " + padded("--theme <name>", option_width) + "one of: " + themes + "\n";
    text += indent + "When starting the app, the theme is used for this run\n";
    text += indent + "only and is not saved. Snapshot default: " + default_theme + "\n";
    text += "  " + padded("--data <name>", option_width) + "where the values come from, one of: " +
            platform::data_source_names() + "\n";
    text += indent + "  " + padded(sample_source, source_width) +
            "fixed made-up values that never change\n";
    text += indent + "  " + padded(demo_source, source_width) +
            "the made-up values, moving on a fixed script\n";
    text += source_indent + "(one step a second when the app runs)\n";
    text += indent + "  " + padded(live_source, source_width) +
            "real readings: the clock and the battery so\n";
    text += source_indent + "far; the other values are still made up\n";
    text += indent + "Default: " + sample_source + " for a snapshot; " + start_source +
            " when starting\n";
    text += indent + "the app and for --print-data (" + start_source_elsewhere + ").\n";
    text += "  " + padded("--data-steps <n>", option_width) +
            "with --snapshot or --print-data only: n further\n";
    text += indent + "readings, 0-" + std::to_string(kMaxDataSteps) +
            " (default: 0). A snapshot takes\n";
    text += indent + "them after the page is built and before --keys.\n";
    text += "\n";
    text += "--print-data takes one reading (and the --data-steps further ones), prints\n";
    text += "the values as name=value lines on standard output and exits. It opens no\n";
    text += "window, reads no settings and takes only --data and --data-steps.\n";
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
    text += indent + "separated by commas, the same way the Settings page\n";
    text += indent + "switches. The picture shows the last theme, and the\n";
    text += indent + "number of loaded fonts is printed to standard error.\n";
    text += "  " + padded("--keys <names>", option_width) +
            "keys to press before the picture (and before any\n";
    text += indent + "--switch-to), in order and separated by commas,\n";
    text += indent + "pressed the way the keyboard does. Names: 0-9, a-z,\n";
    text += indent + "esc, enter, space, backspace, up, down, left, right,\n";
    text += indent + "help. :long after a name holds that key for one\n";
    text += indent + "second, for example esc:long. The name data is not\n";
    text += indent + "a key: it takes one further reading at that point\n";
    text += indent + "(it has no :long).\n";
    return text;
}

} // namespace app

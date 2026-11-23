/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

namespace app {
namespace {

std::string trim(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parse_bool(const std::string& value, bool& result) {
    const auto normalized = lowercase(trim(value));
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        result = true;
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        result = false;
        return true;
    }
    return false;
}

bool is_section(const std::string& line, const std::string& expected) {
    const auto normalized = trim(line);
    return normalized.size() >= 2 && normalized.front() == '[' && normalized.back() == ']' &&
           lowercase(trim(normalized.substr(1, normalized.size() - 2))) == expected;
}

bool is_key(const std::string& line, const std::string& expected) {
    const auto separator = line.find('=');
    return separator != std::string::npos &&
           lowercase(trim(line.substr(0, separator))) == expected;
}

} // namespace

bool valid_hotspot_password(const std::string& password) {
    if (password.size() < 8 || password.size() > 63 || password.front() == ' ' || password.back() == ' ') {
        return false;
    }
    return std::all_of(password.begin(), password.end(), [](char character) {
        return character >= 0x20 && character <= 0x7E;
    });
}

bool valid_hotspot_name(const std::string& name) {
    if (name.empty() || name.size() > kHotspotNameMax || name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char character) {
        return character >= 0x20 && character <= 0x7E;
    });
}

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error) {
    error.clear();
    std::ifstream input(path);
    if (!input) {
        error = "could not open file";
        return false;
    }

    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = lowercase(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = lowercase(trim(line.substr(0, separator)));
        if (section == "application" && key == "theme") {
            config.theme = lowercase(trim(line.substr(separator + 1)));
        } else if (section == "application" && key == "dim_when_idle") {
            bool dim_when_idle = false;
            if (parse_bool(line.substr(separator + 1), dim_when_idle)) {
                config.dim_when_idle = dim_when_idle;
            } else {
                config.dim_when_idle.reset();
            }
        } else if (section == "application" && key == "hotspot_password") {
            config.hotspot_password = trim(line.substr(separator + 1));
        } else if (section == "application" && key == "hotspot_name") {
            config.hotspot_name = trim(line.substr(separator + 1));
        } else if (section == "application" && key == "blocked_devices") {
            config.blocked_devices = trim(line.substr(separator + 1));
        } else if (section == "application" && key == "speed_test_server") {
            config.speed_test_server = trim(line.substr(separator + 1));
        } else if (section == "application" && key == "dark_mode") {
            // A value that cannot be read is ignored instead of failing the
            // load, so the theme key of the same file still counts.
            bool dark_mode = false;
            if (parse_bool(line.substr(separator + 1), dark_mode)) {
                config.dark_mode = dark_mode;
                config.dark_mode_invalid = false;
            } else {
                config.dark_mode.reset();
                config.dark_mode_invalid = true;
            }
        }
    }

    return true;
}

bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error) {
    error.clear();

    const std::filesystem::path config_path(path);
    const auto parent = config_path.parent_path();
    if (!parent.empty()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            error = "could not create parent directory: " + filesystem_error.message();
            return false;
        }
    }

    std::vector<std::string> lines;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    // The keys to write, in this order; a key that is not set is left alone.
    struct KeyLine {
        const char* key;
        std::string line;
        bool written;
    };
    std::vector<KeyLine> keys;
    if (config.theme) {
        keys.push_back({"theme", "theme=" + *config.theme, false});
    }
    if (config.dim_when_idle) {
        keys.push_back({"dim_when_idle",
                        std::string("dim_when_idle=") + (*config.dim_when_idle ? "yes" : "no"),
                        false});
    }
    if (config.hotspot_password) {
        keys.push_back({"hotspot_password", "hotspot_password=" + *config.hotspot_password, false});
    }
    if (config.hotspot_name) {
        keys.push_back({"hotspot_name", "hotspot_name=" + *config.hotspot_name, false});
    }
    if (config.blocked_devices) {
        keys.push_back({"blocked_devices", "blocked_devices=" + *config.blocked_devices, false});
    }

    std::vector<std::string> updated;
    bool in_application_section = false;
    bool found_application_section = false;

    // Puts the key lines not written yet at the end of the [application]
    // section that `updated` currently ends with: after its last line with
    // text, so blank lines before the next section stay where they are. The
    // section header has text, so a line never lands above it.
    const auto append_keys_to_section = [&]() {
        auto position = updated.end();
        while (position != updated.begin() && trim(*(position - 1)).empty()) {
            --position;
        }
        for (auto& key : keys) {
            if (!key.written) {
                position = updated.insert(position, key.line) + 1;
                key.written = true;
            }
        }
    };
    const auto all_written = [&]() {
        return std::all_of(keys.begin(), keys.end(), [](const KeyLine& key) { return key.written; });
    };

    for (const auto& current_line : lines) {
        const auto normalized = trim(current_line);
        const bool section_header = normalized.size() >= 2 && normalized.front() == '[' &&
                                    normalized.back() == ']';
        if (section_header) {
            if (in_application_section) {
                append_keys_to_section();
            }
            in_application_section = is_section(current_line, "application");
            found_application_section = found_application_section || in_application_section;
            updated.push_back(current_line);
            continue;
        }

        if (in_application_section) {
            if (is_key(current_line, "dark_mode")) {
                continue;
            }
            bool replaced = false;
            for (auto& key : keys) {
                if (is_key(current_line, key.key)) {
                    // The first line of the key is replaced; any further
                    // ones are dropped.
                    if (!key.written) {
                        updated.push_back(key.line);
                        key.written = true;
                    }
                    replaced = true;
                    break;
                }
            }
            if (replaced) {
                continue;
            }
        }
        updated.push_back(current_line);
    }

    if (!all_written()) {
        if (in_application_section) {
            // [application] is the last section: the keys go at its end.
            append_keys_to_section();
        } else {
            if (!updated.empty() && !trim(updated.back()).empty()) {
                updated.emplace_back();
            }
            if (!found_application_section) {
                updated.emplace_back("[application]");
            }
            for (auto& key : keys) {
                if (!key.written) {
                    updated.push_back(key.line);
                    key.written = true;
                }
            }
        }
    }

    if (config.hotspot_password) {
        // Made private before the password goes in, and before the old
        // content is dropped, so a failure here changes nothing but an
        // empty new file.
        if (!std::filesystem::exists(config_path)) {
            std::ofstream create(path, std::ios::app);
        }
        std::error_code permissions_error;
        std::filesystem::permissions(config_path,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     permissions_error);
        if (permissions_error) {
            error = "could not make the file private: " + permissions_error.message();
            return false;
        }
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "could not open file for writing";
        return false;
    }
    for (const auto& updated_line : updated) {
        output << updated_line << '\n';
    }
    if (!output) {
        error = "failed while writing file";
        return false;
    }

    return true;
}

} // namespace app

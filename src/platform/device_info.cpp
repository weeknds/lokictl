/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "device_info.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace platform {
namespace {

// A number in these files has at most this many digits.
constexpr std::size_t kMaxDigits = 12;
constexpr int64_t kKibPerMib = 1024;

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

// The first line of a small text file, without the white space around it.
bool read_first_line(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return false;
    }
    value = trim(line);
    return true;
}

// A whole number: an optional minus sign and 1 to kMaxDigits digits, nothing else.
bool parse_whole_number(const std::string& text, int64_t& value) {
    const bool negative = !text.empty() && text[0] == '-';
    const std::size_t first = negative ? 1 : 0;
    const std::size_t digits = text.size() - first;
    if (digits == 0 || digits > kMaxDigits) {
        return false;
    }
    int64_t result = 0;
    for (std::size_t index = first; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        result = result * 10 + (text[index] - '0');
    }
    value = negative ? -result : result;
    return true;
}

// A number as a Status page number: unknown when it does not fit.
int32_t clamp_detail(int64_t value) {
    if (value > std::numeric_limits<int32_t>::max() || value <= model::kDetailUnknown) {
        return model::kDetailUnknown;
    }
    return static_cast<int32_t>(value);
}

int32_t read_cpu_temperature(const std::filesystem::path& thermal_dir) {
    std::error_code error;
    std::filesystem::directory_iterator entry(thermal_dir, error);
    if (error) {
        return model::kDetailUnknown;
    }
    std::vector<std::string> names;
    const std::filesystem::directory_iterator end;
    while (entry != end) {
        const std::string name = entry->path().filename().string();
        if (name.rfind("thermal_zone", 0) == 0) {
            names.push_back(name);
        }
        entry.increment(error);
        if (error) {
            return model::kDetailUnknown;
        }
    }
    std::sort(names.begin(), names.end());

    for (const std::string& name : names) {
        std::string text;
        if (!read_first_line(thermal_dir / name / "type", text) || text != "cpu-thermal") {
            continue;
        }
        int64_t thousandths = 0;
        if (!read_first_line(thermal_dir / name / "temp", text) || !parse_whole_number(text, thousandths)) {
            return model::kDetailUnknown;
        }
        return clamp_detail((thousandths >= 0 ? thousandths + 50 : thousandths - 50) / 100);
    }
    return model::kDetailUnknown;
}

// Lines of proc/meminfo read "MemTotal:  457456 kB".
void read_memory(const std::filesystem::path& meminfo_file, model::StatusDetails& details) {
    std::ifstream file(meminfo_file);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string key, number, unit;
        int64_t kib = 0;
        if (!(fields >> key >> number >> unit) || unit != "kB" || !parse_whole_number(number, kib) ||
            kib < 0) {
            continue;
        }
        if (key == "MemTotal:") {
            details.memory_total_mb = clamp_detail(kib / kKibPerMib);
        } else if (key == "MemAvailable:") {
            details.memory_free_mb = clamp_detail(kib / kKibPerMib);
        }
    }
}

// proc/uptime reads "79172.98 282600.46": seconds since start, then the
// idle time of all processors.
int32_t read_uptime(const std::filesystem::path& uptime_file) {
    std::string text;
    if (!read_first_line(uptime_file, text)) {
        return model::kDetailUnknown;
    }
    const std::string first = text.substr(0, text.find_first_of(" \t"));
    const std::string whole = first.substr(0, first.find('.'));
    int64_t seconds = 0;
    if (!parse_whole_number(whole, seconds) || seconds < 0) {
        return model::kDetailUnknown;
    }
    return clamp_detail(seconds);
}

} // namespace

void read_device_details(const std::filesystem::path& root, model::StatusDetails& details) {
    details.cpu_temp_tenths = read_cpu_temperature(root / "sys/class/thermal");
    details.memory_free_mb = model::kDetailUnknown;
    details.memory_total_mb = model::kDetailUnknown;
    read_memory(root / "proc/meminfo", details);
    details.uptime_seconds = read_uptime(root / "proc/uptime");
}

} // namespace platform

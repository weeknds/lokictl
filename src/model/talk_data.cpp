/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "talk_data.h"

namespace model {

bool operator==(const TalkName& left, const TalkName& right) {
    return left.name == right.name && left.count == right.count;
}

bool operator!=(const TalkName& left, const TalkName& right) {
    return !(left == right);
}

bool operator==(const TalkDevice& left, const TalkDevice& right) {
    return left.hardware == right.hardware && left.address == right.address && left.names == right.names &&
           left.lookups == right.lookups;
}

bool operator!=(const TalkDevice& left, const TalkDevice& right) {
    return !(left == right);
}

bool operator==(const TalkRun& left, const TalkRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.interface_name == right.interface_name && left.frames == right.frames &&
           left.lookups == right.lookups && left.devices == right.devices;
}

bool operator!=(const TalkRun& left, const TalkRun& right) {
    return !(left == right);
}

const TalkDevice* talk_device(const TalkRun& run, std::string_view hardware) {
    if (hardware.empty()) {
        return nullptr;
    }
    for (const TalkDevice& device : run.devices) {
        if (device.hardware == hardware) {
            return &device;
        }
    }
    return nullptr;
}

std::string talk_clean_name(std::string_view name) {
    std::string clean;
    clean.reserve(name.size());
    for (const char raw : name) {
        char character = raw;
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        clean.push_back(character >= '!' && character <= '~' ? character : '?');
    }
    return talk_tail(clean, kTalkNameMax);
}

std::string talk_tail(std::string_view name, std::size_t max_chars) {
    if (name.size() <= max_chars) {
        return std::string(name);
    }
    if (max_chars <= 2) {
        return std::string(name.substr(name.size() - max_chars));
    }
    return ".." + std::string(name.substr(name.size() - (max_chars - 2)));
}

} // namespace model

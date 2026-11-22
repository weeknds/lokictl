/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "network_manager.h"

#include <cstddef>
#include <fstream>
#include <sstream>

namespace platform {
namespace {

// The longest name a Wi-Fi network can have, in bytes.
constexpr std::size_t kMaxNetworkName = 32;
// A speed in the stand-in file has at most this many digits.
constexpr std::size_t kMaxSpeedDigits = 9;

bool parse_switch(const std::string& text, const char* on_word, const char* off_word, bool& value) {
    if (text == on_word) {
        value = true;
        return true;
    }
    if (text == off_word) {
        value = false;
        return true;
    }
    return false;
}

bool parse_internet(const std::string& text, model::Internet& value) {
    constexpr model::Internet kAll[] = {model::Internet::Unknown, model::Internet::None,
                                        model::Internet::Portal, model::Internet::Limited,
                                        model::Internet::Full};
    for (const model::Internet internet : kAll) {
        if (text == model::internet_name(internet)) {
            value = internet;
            return true;
        }
    }
    return false;
}

// "-" (not known) or a whole number of 1 to kMaxSpeedDigits digits above 0.
bool parse_speed(const std::string& text, int32_t& value) {
    if (text == "-") {
        value = model::kSpeedUnknown;
        return true;
    }
    if (text.empty() || text.size() > kMaxSpeedDigits) {
        return false;
    }
    int32_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        result = result * 10 + (character - '0');
    }
    value = result > 0 ? result : model::kSpeedUnknown;
    return true;
}

// The rest of a line after the fields read so far, without the white space
// in front.
std::string rest_of_line(std::istringstream& fields) {
    std::string rest;
    std::getline(fields, rest);
    const auto first = rest.find_first_not_of(" \t");
    return first == std::string::npos ? std::string() : rest.substr(first);
}

} // namespace

void apply_network_manager(const NetworkManagerReading& reading, model::HomeData& data) {
    if (!reading.available) {
        return;
    }

    if (data.uplink_kind == model::LinkKind::Wifi) {
        for (const NetworkManagerReading::WifiLink& link : reading.wifi_links) {
            if (link.interface_name != data.uplink_name) {
                continue;
            }
            data.wifi_name = link.name;
            data.wifi_security = link.security;
            if (link.speed_mbps > model::kSpeedUnknown) {
                data.uplink_speed_mbps = link.speed_mbps;
            }
            break;
        }
    }

    if (reading.hotspot.active && reading.hotspot.interface_name == kShareInterface) {
        show_share(data, reading.hotspot.name, reading.hotspot.security, reading.hotspot.channel,
                   reading.hotspot.uptime_seconds);
    } else if (reading.hotspot.active) {
        show_router(data, reading.hotspot.name, reading.hotspot.security, reading.hotspot.channel,
                    reading.hotspot.uptime_seconds);
    }

    if (data.uplink_kind != model::LinkKind::None) {
        data.internet = reading.internet;
        data.offline_state = model::OfflineState::Unknown;
    } else {
        data.internet = model::Internet::Unknown;
        if (!reading.networking_on) {
            data.offline_state = model::OfflineState::NetworkOff;
        } else if (reading.connecting) {
            data.offline_state = model::OfflineState::Connecting;
        } else if (!reading.wifi_on) {
            data.offline_state = model::OfflineState::WifiOff;
        } else {
            data.offline_state = model::OfflineState::Unknown;
        }
    }
}

void apply_network_manager(const NetworkManagerReading& reading, std::vector<model::StatusPort>& ports) {
    if (!reading.available) {
        return;
    }
    for (model::StatusPort& port : ports) {
        if (reading.hotspot.active && port.name == reading.hotspot.interface_name) {
            port.state = model::PortState::AccessPoint;
            port.network = reading.hotspot.name;
            port.security = reading.hotspot.security;
            continue;
        }
        if (port.kind != model::LinkKind::Wifi || port.state != model::PortState::Connected) {
            continue;
        }
        for (const NetworkManagerReading::WifiLink& link : reading.wifi_links) {
            if (link.interface_name != port.name) {
                continue;
            }
            port.network = link.name;
            port.security = link.security;
            if (link.speed_mbps > model::kSpeedUnknown) {
                port.speed_mbps = link.speed_mbps;
            }
            break;
        }
    }
}

std::string clean_network_name(const std::string& name) {
    std::string result = name.substr(0, kMaxNetworkName);
    if (name.size() > kMaxNetworkName) {
        // Drop a UTF-8 character the cut went through: step back over its
        // continuation bytes (10xxxxxx) and its lead byte.
        std::size_t end = result.size();
        std::size_t back = end;
        while (back > 0 && (static_cast<unsigned char>(result[back - 1]) & 0xC0) == 0x80) {
            --back;
        }
        if (back > 0 && (static_cast<unsigned char>(result[back - 1]) & 0x80) != 0) {
            const auto lead = static_cast<unsigned char>(result[back - 1]);
            const std::size_t length = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3
                                       : (lead & 0xF8) == 0xF0 ? 4 : 1;
            if (end - (back - 1) < length) {
                end = back - 1;
            }
        }
        result.resize(end);
    }
    for (char& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F) {
            character = '?';
        }
    }
    return result;
}

NetworkManagerReading read_network_manager_file(const std::filesystem::path& path) {
    NetworkManagerReading reading;
    std::ifstream file(path);
    std::string line;
    while (file && std::getline(file, line)) {
        std::istringstream fields(line);
        std::string key;
        if (!(fields >> key) || key[0] == '#') {
            continue;
        }
        if (key == "link") {
            NetworkManagerReading::WifiLink link;
            std::string security, speed;
            if (!(fields >> link.interface_name >> security >> speed) || !parse_speed(speed, link.speed_mbps)) {
                continue;
            }
            link.security = security == "-" ? std::string() : security;
            link.name = clean_network_name(rest_of_line(fields));
            reading.wifi_links.push_back(link);
            continue;
        }
        if (key == "ap") {
            NetworkManagerReading::Hotspot hotspot;
            std::string security;
            if (!(fields >> hotspot.interface_name >> security)) {
                continue;
            }
            hotspot.active = true;
            hotspot.security = security == "-" ? std::string() : security;
            hotspot.name = clean_network_name(rest_of_line(fields));
            reading.hotspot = hotspot;
            continue;
        }
        if (key == "fail") {
            const std::string reason = rest_of_line(fields);
            if (reason == "permission") {
                reading.switch_failure = PretendFailure{model::SwitchError::NotAllowed, {}};
            } else if (!reason.empty()) {
                reading.switch_failure = PretendFailure{model::SwitchError::Failed, reason};
            }
            continue;
        }
        std::string value;
        if (!(fields >> value)) {
            continue;
        }
        if (key == "running") {
            parse_switch(value, "yes", "no", reading.available);
        } else if (key == "networking") {
            parse_switch(value, "on", "off", reading.networking_on);
        } else if (key == "wifi") {
            parse_switch(value, "on", "off", reading.wifi_on);
        } else if (key == "connecting") {
            parse_switch(value, "yes", "no", reading.connecting);
        } else if (key == "internet") {
            parse_internet(value, reading.internet);
        }
    }
    return reading;
}

} // namespace platform

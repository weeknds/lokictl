/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "survey_data.h"

#include "health_check.h"

#include <algorithm>
#include <tuple>

namespace model {

bool operator==(const SurveyNetwork& left, const SurveyNetwork& right) {
    return left.bssid == right.bssid && left.name == right.name && left.hidden == right.hidden &&
           left.channel == right.channel && left.security == right.security &&
           left.signal_dbm == right.signal_dbm && left.beacons == right.beacons && left.last_ms == right.last_ms &&
           left.look_alike == right.look_alike;
}

bool operator!=(const SurveyNetwork& left, const SurveyNetwork& right) {
    return !(left == right);
}

bool operator==(const SurveyAttack& left, const SurveyAttack& right) {
    return left.channel == right.channel && left.peak == right.peak && left.bssid == right.bssid;
}

bool operator!=(const SurveyAttack& left, const SurveyAttack& right) {
    return !(left == right);
}

bool operator==(const SurveyChannel& left, const SurveyChannel& right) {
    return left.frames == right.frames && left.kickoffs == right.kickoffs;
}

bool operator!=(const SurveyChannel& left, const SurveyChannel& right) {
    return !(left == right);
}

bool operator==(const SurveyRun& left, const SurveyRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.interface_name == right.interface_name && left.elapsed_ms == right.elapsed_ms &&
           left.frames == right.frames && left.kickoffs == right.kickoffs &&
           left.networks_seen == right.networks_seen && left.channels == right.channels &&
           left.networks == right.networks && left.attacks == right.attacks;
}

bool operator!=(const SurveyRun& left, const SurveyRun& right) {
    return !(left == right);
}

int32_t survey_channel_of(int32_t frequency_mhz) {
    if (frequency_mhz == 2484) {
        return 14;
    }
    if (frequency_mhz >= 2412 && frequency_mhz <= 2472 && (frequency_mhz - 2407) % 5 == 0) {
        return (frequency_mhz - 2407) / 5;
    }
    return 0;
}

namespace {

// The length of the UTF-8 character at `at`, or 0 when it is not a valid
// one (cut short, an overlong form, a surrogate or past U+10FFFF).
std::size_t utf8_length(const uint8_t* bytes, std::size_t size, std::size_t at) {
    const uint8_t lead = bytes[at];
    std::size_t length = 0;
    uint32_t code = 0;
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        code = lead & 0x07;
    } else {
        return 0;
    }
    if (size - at < length) {
        return 0;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const uint8_t next = bytes[at + index];
        if ((next & 0xC0) != 0x80) {
            return 0;
        }
        code = (code << 6) | (next & 0x3F);
    }
    const uint32_t smallest = length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
    if (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        return 0;
    }
    return length;
}

int32_t frames_per_second(const SurveyRun& run, int32_t channel) {
    if (channel < 1 || channel > kSurveyChannels || run.elapsed_ms <= 0) {
        return 0;
    }
    const int64_t frames = run.channels[static_cast<std::size_t>(channel - 1)].frames;
    // Each channel is listened to 1/kSurveyChannels of the run.
    const int64_t listened_ms = std::max<int64_t>(run.elapsed_ms / kSurveyChannels, 1);
    return static_cast<int32_t>(std::min<int64_t>(frames * 1000 / listened_ms, 1000000));
}

} // namespace

std::string survey_clean_name(const uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr) {
        return {};
    }
    size = std::min(size, kSurveyNameMax);
    if (std::all_of(bytes, bytes + size, [](uint8_t byte) { return byte == 0; })) {
        return {};
    }
    bool valid = true;
    for (std::size_t at = 0; at < size && valid;) {
        const std::size_t length = utf8_length(bytes, size, at);
        valid = length > 0;
        at += length;
    }
    std::string name;
    name.reserve(size);
    for (std::size_t at = 0; at < size; ++at) {
        const uint8_t byte = bytes[at];
        if (byte < 0x20 || byte == 0x7F || (!valid && byte >= 0x80)) {
            name += '?';
        } else {
            name += static_cast<char>(byte);
        }
    }
    return name;
}

ChannelLoad survey_channel_load(const SurveyRun& run, int32_t channel) {
    if (channel < 1 || channel > kSurveyChannels || run.elapsed_ms < 1000) {
        return ChannelLoad::Unknown;
    }
    const int32_t rate = frames_per_second(run, channel);
    if (rate >= kSurveyBusyRate) {
        return ChannelLoad::Busy;
    }
    return rate < kSurveyQuietRate ? ChannelLoad::Quiet : ChannelLoad::InUse;
}

int32_t survey_channel_level(const SurveyRun& run, int32_t channel) {
    if (channel < 1 || channel > kSurveyChannels) {
        return 0;
    }
    // Every channel is heard for the same time, so the frame counts compare
    // as they are.
    int64_t busiest = 0;
    for (const SurveyChannel& each : run.channels) {
        busiest = std::max<int64_t>(busiest, each.frames);
    }
    if (busiest <= 0) {
        return 0;
    }
    const int64_t frames = std::max<int64_t>(run.channels[static_cast<std::size_t>(channel - 1)].frames, 0);
    return static_cast<int32_t>(std::clamp<int64_t>(frames * 100 / busiest, 0, 100));
}

int32_t survey_channel_networks(const SurveyRun& run, int32_t channel) {
    return static_cast<int32_t>(std::count_if(run.networks.begin(), run.networks.end(),
                                              [channel](const SurveyNetwork& network) {
                                                  return network.channel == channel;
                                              }));
}

int32_t survey_best_channel(const SurveyRun& run) {
    if (run.elapsed_ms < 1000) {
        return 0;
    }
    int32_t best = 0;
    std::tuple<int32_t, int32_t> best_key{0, 0};
    for (const int32_t channel : {1, 6, 11}) {
        const std::tuple<int32_t, int32_t> key{frames_per_second(run, channel), survey_channel_networks(run, channel)};
        if (best == 0 || key < best_key) {
            best = channel;
            best_key = key;
        }
    }
    return best;
}

bool survey_joined_wifi(const HomeData& home, std::string& name, std::string& security) {
    name.clear();
    security.clear();
    if (home.uplink_kind == LinkKind::Wifi && !home.wifi_name.empty()) {
        name = home.wifi_name;
        security = home.wifi_security;
        return true;
    }
    for (const WifiNetwork& network : home.wifi_scan.networks) {
        if (network.joined && !network.name.empty()) {
            name = network.name;
            security = network.security;
            return true;
        }
    }
    return false;
}

void mark_survey_look_alikes(SurveyRun& run, const std::string& joined_name, const std::string& joined_security) {
    const int32_t joined = wifi_security_rank(joined_security);
    for (SurveyNetwork& network : run.networks) {
        network.look_alike = false;
        if (joined_name.empty() || joined < 0 || network.name != joined_name) {
            continue;
        }
        const int32_t rank = wifi_security_rank(network.security);
        network.look_alike = rank >= 0 && rank <= 2 && rank < joined;
    }
}

std::vector<SurveyFinding> survey_findings(const SurveyRun& run) {
    std::vector<SurveyFinding> findings;
    for (const SurveyAttack& attack : run.attacks) {
        findings.push_back({SurveyFindingKind::Kickoff, "Kick-off attack on ch " + std::to_string(attack.channel),
                            attack.channel});
    }
    for (const SurveyNetwork& network : run.networks) {
        if (!network.look_alike) {
            continue;
        }
        const bool repeat = std::any_of(findings.begin(), findings.end(), [&network](const SurveyFinding& finding) {
            return finding.kind == SurveyFindingKind::LookAlike && finding.channel == network.channel &&
                   finding.text == "Look-alike of " + network.name;
        });
        if (!repeat) {
            findings.push_back({SurveyFindingKind::LookAlike, "Look-alike of " + network.name, network.channel});
        }
    }
    for (int32_t channel = 1; channel <= kSurveyChannels; ++channel) {
        if (survey_channel_load(run, channel) == ChannelLoad::Busy) {
            findings.push_back({SurveyFindingKind::Busy, "Channel " + std::to_string(channel) + ": busy", channel});
        }
    }
    return findings;
}

int32_t survey_warnings(const SurveyRun& run) {
    int32_t count = static_cast<int32_t>(run.attacks.size());
    for (const SurveyFinding& finding : survey_findings(run)) {
        count += finding.kind == SurveyFindingKind::LookAlike ? 1 : 0;
    }
    return count;
}

std::vector<std::size_t> survey_network_order(const SurveyRun& run) {
    std::vector<std::size_t> order(run.networks.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    // An unknown signal (0) sorts after every real one.
    const auto signal = [](const SurveyNetwork& network) {
        return network.signal_dbm == kSignalUnknown ? -1000 : network.signal_dbm;
    };
    std::stable_sort(order.begin(), order.end(), [&run, &signal](std::size_t left, std::size_t right) {
        const SurveyNetwork& a = run.networks[left];
        const SurveyNetwork& b = run.networks[right];
        if (a.look_alike != b.look_alike) {
            return a.look_alike;
        }
        if (signal(a) != signal(b)) {
            return signal(a) > signal(b);
        }
        return a.name < b.name;
    });
    return order;
}

std::string survey_stick(const HomeData& home, std::string& why) {
    why = "none";
    for (const UsbWifiAdapter& adapter : home.usb_wifi) {
        if (!valid_interface_name(adapter.interface_name) || adapter.interface_name == "wlan0" ||
            adapter.interface_name == kSurveyListenInterface) {
            continue;
        }
        if (adapter.managed == Managed::No) {
            why.clear();
            return adapter.interface_name;
        }
        why = adapter.managed == Managed::Yes ? "managed" : "unknown";
    }
    return {};
}

} // namespace model

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Reading the survey's frames and counting them (survey.h). The runners
// are in survey_runner.cpp; this part is built into the checks without
// LVGL.

#include "survey.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform {
namespace {

// The bytes of a Wi-Fi management frame's header, and of the fixed part of
// a beacon's body (time stamp, interval, capabilities).
constexpr std::size_t kWifiHeader = 24;
constexpr std::size_t kBeaconFixed = 12;
// The radiotap fields read: bit, alignment and size, in their order.
struct RadioField {
    uint32_t bit;
    std::size_t align;
    std::size_t size;
};
constexpr RadioField kRadioFields[] = {
    {0, 8, 8},  // TSFT
    {1, 1, 1},  // flags
    {2, 1, 1},  // rate
    {3, 2, 4},  // channel: frequency, flags
    {4, 2, 2},  // FHSS
    {5, 1, 1},  // antenna signal, dBm
};
constexpr uint8_t kFlagFcs = 0x10;
constexpr uint8_t kFlagBadFcs = 0x40;
// Frame kinds (the first byte of the frame control: subtype, type, version).
constexpr uint8_t kBeacon = 0x80;
constexpr uint8_t kProbeAnswer = 0x50;
constexpr uint8_t kDeauthentication = 0xc0;
constexpr uint8_t kDisassociation = 0xa0;

uint16_t le16(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

uint32_t le32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

std::string mac_text(const uint8_t* bytes) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3],
                  bytes[4], bytes[5]);
    return text;
}

// What the security elements of a beacon offer.
struct Security {
    bool privacy{false};
    bool rsn{false};
    bool wpa{false};
    bool rsn_psk{false};
    bool rsn_sae{false};
    bool rsn_eap{false};
    bool rsn_suite_b{false};
    bool rsn_owe{false};
    bool wpa_psk{false};
    bool wpa_eap{false};
};

// Reads the key management suites of an RSN element's body (or a WPA
// vendor element's, after its four-byte type): version, group cipher,
// the pairwise ciphers and then the key management list. Reads only as far
// as the body holds; `oui` is the suites' organisation (00:0f:ac for RSN,
// 00:50:f2 for WPA).
void read_key_management(const uint8_t* body, std::size_t size, const uint8_t oui[3], bool wpa, Security& out) {
    std::size_t at = 2 + 4;  // version, group cipher
    if (size < at + 2) {
        return;
    }
    const std::size_t pairwise = le16(body + at);
    at += 2;
    if (pairwise > (size - at) / 4) {
        return;
    }
    at += pairwise * 4;
    if (size < at + 2) {
        return;
    }
    const std::size_t suites = le16(body + at);
    at += 2;
    for (std::size_t index = 0; index < suites && size - at >= 4; ++index, at += 4) {
        const uint8_t* suite = body + at;
        if (std::memcmp(suite, oui, 3) != 0) {
            continue;
        }
        const uint8_t type = suite[3];
        if (wpa) {
            out.wpa_psk = out.wpa_psk || type == 2;
            out.wpa_eap = out.wpa_eap || type == 1;
            continue;
        }
        out.rsn_psk = out.rsn_psk || type == 2 || type == 6;
        out.rsn_sae = out.rsn_sae || type == 8 || type == 24;
        out.rsn_eap = out.rsn_eap || type == 1 || type == 3 || type == 5;
        out.rsn_suite_b = out.rsn_suite_b || type == 12 || type == 13;
        out.rsn_owe = out.rsn_owe || type == 18;
    }
}

// The strongest security offered, in the words NetworkManager's list uses
// (see security_text() in network_manager_client.cpp).
std::string security_text(const Security& security) {
    if (security.rsn_sae) {
        return security.rsn_psk ? "WPA2/3" : "WPA3";
    }
    if (security.rsn_psk) {
        return "WPA2";
    }
    if (security.wpa_psk) {
        return "WPA";
    }
    if (security.rsn_suite_b) {
        return "WPA3-EAP";
    }
    if (security.rsn_eap) {
        return "WPA2-EAP";
    }
    if (security.wpa_eap) {
        return "WPA-EAP";
    }
    if (security.rsn_owe) {
        return "OWE";
    }
    if (security.rsn) {
        return "WPA2";
    }
    if (security.wpa) {
        return "WPA";
    }
    return security.privacy ? "WEP" : "open";
}

void read_beacon(const uint8_t* wifi, std::size_t size, WifiFrame& out) {
    out.kind = WifiFrame::Kind::Beacon;
    out.probe_answer = wifi[0] == kProbeAnswer;
    out.bssid = mac_text(wifi + 16);
    Security security;
    security.privacy = (le16(wifi + kWifiHeader + 10) & 0x0010) != 0;
    bool named = false;
    static constexpr uint8_t kRsnOui[3] = {0x00, 0x0f, 0xac};
    static constexpr uint8_t kWpaOui[3] = {0x00, 0x50, 0xf2};
    std::size_t at = kWifiHeader + kBeaconFixed;
    while (size - at >= 2) {
        const uint8_t id = wifi[at];
        const std::size_t length = wifi[at + 1];
        if (size - at - 2 < length) {
            break;  // cut short: only whole elements count
        }
        const uint8_t* body = wifi + at + 2;
        if (id == 0 && !named) {
            named = true;
            out.name = model::survey_clean_name(body, length);
            out.hidden = out.name.empty();
        } else if (id == 3 && length >= 1) {
            out.own_channel = body[0] >= 1 && body[0] <= 14 ? body[0] : 0;
        } else if (id == 48 && !security.rsn) {
            security.rsn = true;
            read_key_management(body, length, kRsnOui, false, security);
        } else if (id == 221 && length >= 4 && std::memcmp(body, kWpaOui, 3) == 0 && body[3] == 1 &&
                   !security.wpa) {
            security.wpa = true;
            read_key_management(body + 4, length - 4, kWpaOui, true, security);
        }
        at += 2 + length;
    }
    if (!named) {
        out.hidden = true;
    }
    out.security = security_text(security);
}

} // namespace

bool read_radio_header(const uint8_t* frame, std::size_t size, RadioHeader& out) {
    out = RadioHeader{};
    if (frame == nullptr || size < 8 || frame[0] != 0) {
        return false;
    }
    const std::size_t length = le16(frame + 2);
    if (length < 8 || length > size) {
        return false;
    }
    const uint32_t present = le32(frame + 4);
    // The present words: each with bit 31 set has another after it.
    std::size_t at = 4;
    for (uint32_t word = present; (word & 0x80000000U) != 0;) {
        at += 4;
        if (length - at < 4) {
            return false;
        }
        word = le32(frame + at);
    }
    at += 4;
    uint8_t flags = 0;
    for (const RadioField& field : kRadioFields) {
        if ((present & (1U << field.bit)) == 0) {
            continue;
        }
        at = (at + field.align - 1) / field.align * field.align;
        if (at > length || length - at < field.size) {
            return false;
        }
        const uint8_t* value = frame + at;
        if (field.bit == 1) {
            flags = value[0];
        } else if (field.bit == 3) {
            out.frequency_mhz = le16(value);
        } else if (field.bit == 5) {
            const auto signal = static_cast<int8_t>(value[0]);
            out.signal_dbm = signal < 0 && signal > -120 ? signal : model::kSignalUnknown;
        }
        at += field.size;
    }
    out.length = length;
    out.has_fcs = (flags & kFlagFcs) != 0;
    out.bad_fcs = (flags & kFlagBadFcs) != 0;
    return true;
}

bool read_wifi_frame(const uint8_t* frame, std::size_t size, WifiFrame& out) {
    out = WifiFrame{};
    RadioHeader radio;
    if (!read_radio_header(frame, size, radio) || radio.bad_fcs) {
        return false;
    }
    out.channel = model::survey_channel_of(radio.frequency_mhz);
    out.signal_dbm = radio.signal_dbm;
    const uint8_t* wifi = frame + radio.length;
    std::size_t wifi_size = size - radio.length;
    if (wifi_size == 0) {
        return true;  // only the radio header: still a frame on the channel
    }
    if ((wifi[0] & 0x03) != 0) {
        return false;  // not version 0: noise
    }
    const uint8_t kind = wifi[0] & 0xfc;
    if ((kind == kBeacon || kind == kProbeAnswer || kind == kDeauthentication || kind == kDisassociation) &&
        radio.has_fcs && wifi_size >= kWifiHeader + 4) {
        wifi_size -= 4;  // the checksum is no element
    }
    if ((kind == kBeacon || kind == kProbeAnswer) && wifi_size >= kWifiHeader + kBeaconFixed) {
        read_beacon(wifi, wifi_size, out);
    } else if ((kind == kDeauthentication || kind == kDisassociation) && wifi_size >= kWifiHeader) {
        out.kind = WifiFrame::Kind::Kickoff;
        out.bssid = mac_text(wifi + 16);
    }
    return true;
}

// ---------------------------------------------------------------------------
// SurveyCounter

void SurveyCounter::note_frame(const uint8_t* frame, std::size_t size, uint32_t seconds, int64_t at_ms) {
    WifiFrame read;
    if (read_wifi_frame(frame, size, read)) {
        note(read, seconds, at_ms);
    }
}

void SurveyCounter::note(const WifiFrame& frame, uint32_t seconds, int64_t at_ms) {
    changed_ = true;
    run_.frames = std::min(run_.frames + 1, INT32_MAX - 1);
    const bool on_chart = frame.channel >= 1 && frame.channel <= model::kSurveyChannels;
    const std::size_t slot = on_chart ? static_cast<std::size_t>(frame.channel - 1) : 0;
    if (on_chart) {
        run_.channels[slot].frames = std::min(run_.channels[slot].frames + 1, INT32_MAX - 1);
    }

    if (frame.kind == WifiFrame::Kind::Beacon) {
        auto it = std::find_if(run_.networks.begin(), run_.networks.end(),
                               [&frame](const model::SurveyNetwork& network) { return network.bssid == frame.bssid; });
        if (it == run_.networks.end()) {
            if (run_.networks.size() >= model::kSurveyNetworksMax) {
                run_.networks.erase(std::min_element(run_.networks.begin(), run_.networks.end(),
                                                     [](const model::SurveyNetwork& left,
                                                        const model::SurveyNetwork& right) {
                                                         return left.last_ms < right.last_ms;
                                                     }));
            }
            model::SurveyNetwork network;
            network.bssid = frame.bssid;
            run_.networks.push_back(std::move(network));
            it = run_.networks.end() - 1;
            run_.networks_seen = std::min(run_.networks_seen + 1, INT32_MAX - 1);
        }
        model::SurveyNetwork& network = *it;
        if (!frame.probe_answer) {
            network.hidden = frame.hidden;
        }
        // A probe answer may name a hidden network.
        if (!frame.name.empty()) {
            network.name = frame.name;
        }
        if (frame.own_channel > 0) {
            network.channel = frame.own_channel;
        } else if (network.channel == 0) {
            network.channel = frame.channel;
        }
        network.security = frame.security;
        if (frame.signal_dbm != model::kSignalUnknown &&
            (network.signal_dbm == model::kSignalUnknown || frame.signal_dbm > network.signal_dbm)) {
            network.signal_dbm = frame.signal_dbm;
        }
        network.beacons = std::min(network.beacons + 1, INT32_MAX - 1);
        network.last_ms = at_ms;
        return;
    }

    if (frame.kind != WifiFrame::Kind::Kickoff) {
        return;
    }
    run_.kickoffs = std::min(run_.kickoffs + 1, INT32_MAX - 1);
    if (!on_chart) {
        return;
    }
    run_.channels[slot].kickoffs = std::min(run_.channels[slot].kickoffs + 1, INT32_MAX - 1);
    Second& second = kickoff_seconds_[slot];
    if (!second.valid || second.second != seconds) {
        second = Second{seconds, 0, true};
    }
    second.count = std::min(second.count + 1, INT32_MAX - 1);
    if (second.count <= model::kSurveyKickoffsPerSecond) {
        return;
    }
    auto attack = std::find_if(run_.attacks.begin(), run_.attacks.end(),
                               [&frame](const model::SurveyAttack& known) { return known.channel == frame.channel; });
    if (attack == run_.attacks.end()) {
        run_.attacks.push_back({frame.channel, second.count, frame.bssid});
    } else {
        attack->peak = std::max(attack->peak, second.count);
        if (attack->bssid.empty()) {
            attack->bssid = frame.bssid;
        }
    }
}

void SurveyCounter::fill(model::SurveyRun& run) const {
    run.frames = run_.frames;
    run.kickoffs = run_.kickoffs;
    run.networks_seen = run_.networks_seen;
    run.channels = run_.channels;
    run.networks = run_.networks;
    run.attacks = run_.attacks;
}

bool SurveyCounter::take_changed() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

// ---------------------------------------------------------------------------
// Made-up frames

namespace {

// A radio header of 15 bytes: flags (none), channel and signal.
void put_radio_header(std::vector<uint8_t>& out, int32_t channel, int32_t signal_dbm) {
    const int32_t frequency = channel == 14 ? 2484 : 2407 + channel * 5;
    const uint8_t header[15] = {
        0x00, 0x00, 15, 0x00,                   // version, pad, length
        0x2a, 0x00, 0x00, 0x00,                 // present: flags, channel, antenna signal
        0x00,                                   // flags
        0x00,                                   // pad to 2
        static_cast<uint8_t>(frequency & 0xff), static_cast<uint8_t>(frequency >> 8),
        0xa0, 0x00,                             // 2 GHz, CCK
        static_cast<uint8_t>(static_cast<int8_t>(signal_dbm)),
    };
    out.insert(out.end(), header, header + sizeof(header));
}

void put_header(std::vector<uint8_t>& out, uint8_t kind, const uint8_t* bssid) {
    out.push_back(kind);
    out.push_back(0x00);
    out.push_back(0x00);
    out.push_back(0x00);
    for (int index = 0; index < 6; ++index) {
        out.push_back(0xff);
    }
    out.insert(out.end(), bssid, bssid + 6);
    out.insert(out.end(), bssid, bssid + 6);
    out.push_back(0x00);
    out.push_back(0x00);
}

void put_suites(std::vector<uint8_t>& out, uint8_t element, const uint8_t oui[3], const std::vector<uint8_t>& akms,
                bool wpa) {
    std::vector<uint8_t> body;
    if (wpa) {
        body.insert(body.end(), oui, oui + 3);
        body.push_back(0x01);
    }
    body.push_back(0x01);
    body.push_back(0x00);                                      // version 1
    body.insert(body.end(), oui, oui + 3);
    body.push_back(wpa ? 0x02 : 0x04);                         // group cipher
    body.push_back(0x01);
    body.push_back(0x00);                                      // one pairwise cipher
    body.insert(body.end(), oui, oui + 3);
    body.push_back(wpa ? 0x02 : 0x04);
    body.push_back(static_cast<uint8_t>(akms.size()));
    body.push_back(0x00);
    for (const uint8_t akm : akms) {
        body.insert(body.end(), oui, oui + 3);
        body.push_back(akm);
    }
    if (!wpa) {
        body.push_back(0x00);
        body.push_back(0x00);                                  // capabilities
    }
    out.push_back(element);
    out.push_back(static_cast<uint8_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

} // namespace

std::vector<uint8_t> survey_beacon_frame(const uint8_t* bssid, const std::string& name, int32_t channel,
                                         int32_t signal_dbm, const std::string& security, bool hidden) {
    static constexpr uint8_t kRsnOui[3] = {0x00, 0x0f, 0xac};
    static constexpr uint8_t kWpaOui[3] = {0x00, 0x50, 0xf2};
    std::vector<uint8_t> frame;
    put_radio_header(frame, channel, signal_dbm);
    put_header(frame, kBeacon, bssid);
    frame.insert(frame.end(), 8, 0x00);  // time stamp
    frame.push_back(0x64);
    frame.push_back(0x00);               // interval
    frame.push_back(security == "open" ? 0x01 : 0x11);
    frame.push_back(0x04);               // capabilities: ESS, privacy, short slot
    const std::size_t length = hidden ? 0 : std::min<std::size_t>(name.size(), 32);
    frame.push_back(0x00);
    frame.push_back(static_cast<uint8_t>(length));
    frame.insert(frame.end(), name.begin(), name.begin() + static_cast<std::ptrdiff_t>(length));
    const uint8_t rates[] = {0x01, 0x04, 0x82, 0x84, 0x8b, 0x96};
    frame.insert(frame.end(), rates, rates + sizeof(rates));
    frame.push_back(0x03);
    frame.push_back(0x01);
    frame.push_back(static_cast<uint8_t>(channel));
    if (security == "WPA2") {
        put_suites(frame, 48, kRsnOui, {2}, false);
    } else if (security == "WPA2/3") {
        put_suites(frame, 48, kRsnOui, {2, 8}, false);
    } else if (security == "WPA3") {
        put_suites(frame, 48, kRsnOui, {8}, false);
    } else if (security == "WPA2-EAP") {
        put_suites(frame, 48, kRsnOui, {1}, false);
    } else if (security == "OWE") {
        put_suites(frame, 48, kRsnOui, {18}, false);
    } else if (security == "WPA") {
        put_suites(frame, 221, kWpaOui, {2}, true);
    }
    return frame;
}

std::vector<uint8_t> survey_kickoff_frame(const uint8_t* bssid, int32_t channel) {
    std::vector<uint8_t> frame;
    put_radio_header(frame, channel, -61);
    put_header(frame, kDeauthentication, bssid);
    frame.push_back(0x07);
    frame.push_back(0x00);  // reason: a frame from a device that had not joined
    return frame;
}

std::vector<uint8_t> survey_other_frame(int32_t channel) {
    std::vector<uint8_t> frame;
    put_radio_header(frame, channel, -70);
    frame.push_back(0x08);
    frame.push_back(0x01);  // a data frame to the access point, cut to its kind
    return frame;
}

void pretend_survey_frames(uint32_t second, std::vector<SurveyFrame>& out) {
    // Locally administered, made-up addresses.
    struct Made {
        uint8_t bssid[6];
        const char* name;
        int32_t channel;
        int32_t signal_dbm;
        const char* security;
        bool hidden;
    };
    static const Made kNetworks[] = {
        {{0x02, 0x00, 0x5e, 0x10, 0x00, 0x01}, "Harbour-Cafe", 6, -48, "WPA2", false},
        {{0x02, 0x00, 0x5e, 0x10, 0x00, 0x02}, "Neighbour-Flat", 6, -67, "WPA2/3", false},
        {{0x02, 0x00, 0x5e, 0x10, 0x00, 0x03}, "", 1, -71, "WPA2", true},
        {{0x02, 0x00, 0x5e, 0x10, 0x00, 0x04}, "Library-Guest", 1, -75, "open", false},
        {{0x02, 0x00, 0x5e, 0x66, 0x00, 0x09}, "Harbour-Cafe", 11, -58, "open", false},
        {{0x02, 0x00, 0x5e, 0x10, 0x00, 0x05}, "Office-Staff", 11, -73, "WPA2-EAP", false},
        {{0x02, 0x00, 0x5e, 0x10, 0x00, 0x06}, "Printer-Setup", 6, -80, "open", false},
    };
    const uint32_t base = second * 1000;
    // Every access point heard once a second, from second k on (one more
    // each second at the start).
    for (std::size_t index = 0; index < sizeof(kNetworks) / sizeof(kNetworks[0]); ++index) {
        if (second < index) {
            continue;
        }
        const Made& made = kNetworks[index];
        out.push_back({base + 50 + static_cast<uint32_t>(index) * 100,
                       survey_beacon_frame(made.bssid, made.name, made.channel, made.signal_dbm, made.security,
                                           made.hidden)});
    }
    // Other frames: channel 6 busy, 1 and 11 in use, the rest quiet.
    const struct {
        int32_t channel;
        uint32_t count;
    } kTraffic[] = {{6, 18}, {1, 4}, {11, 3}, {3, 1}, {9, 1}};
    for (const auto& traffic : kTraffic) {
        for (uint32_t index = 0; index < traffic.count; ++index) {
            out.push_back({base + 20 + index * (900 / traffic.count), survey_other_frame(traffic.channel)});
        }
    }
    // A kick-off attack on channel 11: 30 frames a second, from second 20
    // to 22, naming the office network.
    if (second >= 20 && second <= 22) {
        for (uint32_t index = 0; index < 30; ++index) {
            out.push_back({base + 100 + index * 25, survey_kickoff_frame(kNetworks[5].bssid, 11)});
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SurveyFrame& left, const SurveyFrame& right) { return left.at_ms < right.at_ms; });
}

} // namespace platform

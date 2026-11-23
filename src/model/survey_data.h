/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "tool_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The Wi-Fi survey: what goes on in the air around the Cardputer, heard
// with an optional USB Wi-Fi stick (never the built-in Wi-Fi). The
// firewall helper adds a listen-only (monitor mode) interface on the
// stick and hops over the 2.4 GHz channels; the capture helper passes on
// the management frames (beacons, probe answers, kick-offs) and a count of
// every other frame. Nothing is ever sent, and nothing is kept after the
// run.
namespace model {

// One survey runs for this long, then the listening interface goes.
constexpr int32_t kSurveySeconds = 300;
// The listen-only interface the firewall helper adds on the stick for a
// survey, and removes after it (the stick's own interface stays down and
// in managed mode). The capture helper listens only on this name. It is
// never a stick of its own: read_usb_wifi() and survey_stick() skip it.
constexpr std::string_view kSurveyListenInterface = "lokimon0";
// The firewall helper keeps the stick this long on each channel.
constexpr int32_t kSurveyHopMs = 250;
// The chart shows channels 1 to this one (2.4 GHz; 14 is allowed almost
// nowhere, and the helper only hops what the country allows).
constexpr int32_t kSurveyChannels = 13;
// At most this many access points are kept; when full, the one heard from
// longest ago makes room, so a flood of made-up ones cannot make it grow.
constexpr std::size_t kSurveyNetworksMax = 48;
// A network name is at most this many bytes (the most Wi-Fi allows).
constexpr std::size_t kSurveyNameMax = 32;
// More kick-off frames (deauthentication or disassociation) than this
// heard on one channel within one second count as a kick-off attack. The
// stick hears each channel only 1/13 of the time, so this is a lower bound:
// a real flood must be much faster to be noticed.
constexpr int32_t kSurveyKickoffsPerSecond = 20;
// How busy a channel is: frames heard per second of listening on it.
// Below kSurveyQuietRate it is quiet, from kSurveyBusyRate on it is busy.
constexpr int32_t kSurveyQuietRate = 30;
constexpr int32_t kSurveyBusyRate = 200;

// One access point heard, by its hardware address (BSSID).
struct SurveyNetwork {
    std::string bssid;       // "02:00:5e:10:00:01", lowercase; never logged
    std::string name;        // see survey_clean_name(); empty while hidden and not learned
    bool hidden{false};      // its beacons hide the name
    int32_t channel{0};      // from the beacon itself, else where it was heard; 0 when not known
    std::string security;    // as WifiNetwork::security writes it: "open", "WEP", "WPA", "WPA2", "WPA2/3", ...
    int32_t signal_dbm{kSignalUnknown}; // the strongest heard, dBm (negative)
    int32_t beacons{0};      // beacons and probe answers heard
    int64_t last_ms{0};      // when it was last heard (the runner's clock)
    bool look_alike{false};  // see mark_survey_look_alikes()
};

bool operator==(const SurveyNetwork& left, const SurveyNetwork& right);
bool operator!=(const SurveyNetwork& left, const SurveyNetwork& right);

// A kick-off attack seen on one channel.
struct SurveyAttack {
    int32_t channel{0};
    int32_t peak{0};       // most kick-off frames heard within one second
    std::string bssid;     // the network the frames named, when they named one
};

bool operator==(const SurveyAttack& left, const SurveyAttack& right);
bool operator!=(const SurveyAttack& left, const SurveyAttack& right);

// What one channel heard.
struct SurveyChannel {
    int32_t frames{0};    // every frame heard while the stick was on it
    int32_t kickoffs{0};  // kick-off frames among them
};

bool operator==(const SurveyChannel& left, const SurveyChannel& right);
bool operator!=(const SurveyChannel& left, const SurveyChannel& right);

// What the Survey page shows. Idle until the user starts it; Running while
// the stick listens; Done after kSurveySeconds; Stopped by the user;
// Failed when the stick or a helper could not be used (reason says why).
struct SurveyRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;          // with ToolError::Failed: a short reason
    std::string interface_name;  // the stick's interface, for example "wlan1"
    int32_t elapsed_ms{0};       // listening time so far
    int32_t frames{0};           // every frame heard
    int32_t kickoffs{0};         // every kick-off frame heard
    int32_t networks_seen{0};    // access points heard, also those no longer kept
    std::array<SurveyChannel, kSurveyChannels> channels{}; // channel 1 first
    std::vector<SurveyNetwork> networks;  // in the order first heard, at most kSurveyNetworksMax
    std::vector<SurveyAttack> attacks;    // one per channel, in the order found
};

bool operator==(const SurveyRun& left, const SurveyRun& right);
bool operator!=(const SurveyRun& left, const SurveyRun& right);

// The channel of a 2.4 GHz frequency in MHz (2412 is 1, 2484 is 14), or 0.
int32_t survey_channel_of(int32_t frequency_mhz);

// A network name as it is kept and shown: at most kSurveyNameMax bytes;
// valid UTF-8 is kept with control characters as '?'; otherwise every
// byte that is not printable ASCII becomes '?'. A name of only zero bytes
// (how some access points hide it) is empty. Names come from the air, so
// nothing is taken on trust.
std::string survey_clean_name(const uint8_t* bytes, std::size_t size);

// How busy a channel is.
enum class ChannelLoad {
    Unknown = 0,  // not listened to long enough to say
    Quiet,
    InUse,
    Busy,
};

// How busy `channel` (1 to kSurveyChannels) was: its frames per second of
// listening, where the stick listens to each channel 1/kSurveyChannels of
// the run. Unknown before a second of the run has passed.
ChannelLoad survey_channel_load(const SurveyRun& run, int32_t channel);
// The bar of `channel` as 0 to 100: its frames against the busiest
// channel's (100), so the bars differ even when few frames arrive while
// hopping; 0 before any frame. How busy a channel really is stays with
// survey_channel_load().
int32_t survey_channel_level(const SurveyRun& run, int32_t channel);
// The access points heard on `channel`.
int32_t survey_channel_networks(const SurveyRun& run, int32_t channel);
// Of channels 1, 6 and 11 (the three that do not overlap), the one with the
// fewest frames per second, then the fewest networks; 0 before the run
// says anything.
int32_t survey_best_channel(const SurveyRun& run);

// The network the built-in Wi-Fi joined, as the health check finds it: the
// uplink's name and security when the uplink is Wi-Fi, else the network
// NetworkManager's last scan list (HomeData::wifi_scan, read while the
// Wi-Fi, Health or Survey page is on top) marks as joined, so it is known
// while the cable is the uplink too. False (and both empty) when neither
// says.
bool survey_joined_wifi(const HomeData& home, std::string& name, std::string& security);

// Marks the look-alikes of the network the built-in Wi-Fi joined
// (`joined_name`, `joined_security`, see survey_joined_wifi()): an access point
// with exactly that name whose security is weak (open, OWE, WEP or WPA) and
// weaker than the joined one's (see wifi_security_rank()). Another access
// point with the same name and as strong a security is not marked: a home
// with several access points has those. Clears every mark first; marks
// nothing without a joined name.
void mark_survey_look_alikes(SurveyRun& run, const std::string& joined_name, const std::string& joined_security);

// What the list shows above the networks, most serious first.
enum class SurveyFindingKind {
    Kickoff,    // "Kick-off attack on ch 11"
    LookAlike,  // "Look-alike of HomeNet"
    Busy,       // "Channel 6: busy"
};

struct SurveyFinding {
    SurveyFindingKind kind{SurveyFindingKind::Busy};
    std::string text;      // one short line
    int32_t channel{0};
};

// The findings of `run`: every attack, every look-alike (once per name and
// channel) and every busy channel.
std::vector<SurveyFinding> survey_findings(const SurveyRun& run);
// The number of attacks and look-alikes (the warnings).
int32_t survey_warnings(const SurveyRun& run);

// The networks in the order the list shows them: look-alikes first, then
// the strongest first (on equal signal by name). Indexes into run.networks.
std::vector<std::size_t> survey_network_order(const SurveyRun& run);

// The USB Wi-Fi stick a survey may use: the first adapter in
// `home.usb_wifi` that NetworkManager does not manage (Managed::No), whose
// interface is a valid_interface_name() and neither the built-in "wlan0"
// nor kSurveyListenInterface.
// Empty when there is none; `why` then says what is missing: "none" (no
// stick), "managed" (the system's network service uses it) or "unknown"
// (no network service to ask).
std::string survey_stick(const HomeData& home, std::string& why);

} // namespace model

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// "What is it talking to?": the names each device on LokiCTL's own hotspot
// looks up. The capture helper's fixed `names` command passes the DNS
// questions the devices send to the hotspot's own address (its DNS
// server); nothing is sent, and what is heard is kept in memory only and
// forgotten when the hotspot stops.
namespace model {

// The newest this many names are kept per device.
constexpr std::size_t kTalkNamesMax = 50;
// At most this many devices are kept; the one heard from longest ago goes
// first.
constexpr std::size_t kTalkDevicesMax = 32;
// A name is kept with at most this many characters: the end, which says
// whose site it is, with ".." in front of what was cut.
constexpr std::size_t kTalkNameMax = 100;
// A question for the same name from the same device within this long of
// the last one counted is the same lookup (A and AAAA, a repeat).
constexpr int64_t kTalkSameLookupMs = 2000;
// The helper is run for this long at a time, then run again.
constexpr int32_t kTalkHelperSeconds = 1800;

// One name a device looked up.
struct TalkName {
    std::string name;  // see talk_clean_name()
    int32_t count{0};  // lookups counted (see kTalkSameLookupMs)
};

bool operator==(const TalkName& left, const TalkName& right);
bool operator!=(const TalkName& left, const TalkName& right);

// The names one device looked up, by its hardware address.
struct TalkDevice {
    std::string hardware;         // "02:5e:11:7a:c3:01", lowercase
    std::string address;          // the IPv4 address it asked from, as text
    std::vector<TalkName> names;  // the newest first, at most kTalkNamesMax
    int32_t lookups{0};           // every lookup counted, also of names no longer kept
};

bool operator==(const TalkDevice& left, const TalkDevice& right);
bool operator!=(const TalkDevice& left, const TalkDevice& right);

// What the Talk page shows. Idle while the hotspot is off; Running while
// it runs and the helper listens (also while it waits for the hotspot's
// address); Failed when the helper cannot run.
struct TalkRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;               // with ToolError::Failed: a short reason
    std::string interface_name;       // the hotspot's interface
    int32_t frames{0};                // questions heard
    int32_t lookups{0};               // lookups counted, all devices
    std::vector<TalkDevice> devices;  // in the order first heard, at most kTalkDevicesMax
};

bool operator==(const TalkRun& left, const TalkRun& right);
bool operator!=(const TalkRun& left, const TalkRun& right);

// The device with hardware address `hardware`, or nullptr.
const TalkDevice* talk_device(const TalkRun& run, std::string_view hardware);

// A name as it is kept and shown: lowercase, every byte that is not
// printable ASCII ('!' to '~') becomes '?', and a name longer than
// kTalkNameMax keeps its end with ".." in front. Names come from the
// devices, so nothing is taken on trust.
std::string talk_clean_name(std::string_view name);

// `name` cut from the left to at most `max_chars` characters, with ".." in
// front when something was cut (the end says whose site it is). `name` is
// ASCII (see talk_clean_name()).
std::string talk_tail(std::string_view name, std::size_t max_chars);

} // namespace model

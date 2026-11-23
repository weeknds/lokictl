/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <cstdint>
#include <string>
#include <vector>

// The two short lines at the top of the Status page: how healthy the cable
// link is, and how long the battery lasts at the current use. Also the
// battery clock that averages that time over about a minute.
namespace model {

// A cable link that went down more than this many times since LokiCTL
// started needs a look.
constexpr int32_t kCableMostDrops = 3;

// True when the connected cable `port` needs a look: it runs at
// kHealthSlowCableMbps or slower (the health check's rule), at half duplex,
// or went down more than kCableMostDrops times.
bool cable_link_poor(const StatusPort& port);

// The cable line's text after "Cable: ", at most 26 characters:
// - a healthy link: "100 Mbit, full, 0 drops" (a value that is not known
//   is left out; "connected" when none is known);
// - a poor one: "slow link, check it", "5 drops, check it" or
//   "slow, 5 drops, check it". More than 99 drops read "99+ drops".
std::string cable_summary(const StatusPort& port);

// A time left, rounded so it does not jump about: to 5 minutes under an
// hour (at least 5), to 10 minutes under 10 hours, to the hour above.
// "about 45 min", "about 3 h 20 min", "about 12 h", "over 99 h"; empty
// when `minutes` is not known or negative.
std::string battery_time_text(int32_t minutes);

// The battery line's text after "Battery: ": "about 3 h 20 min left"
// (StatusDetails::battery_empty_minutes), "estimating..." during the first
// minute on battery, "time left not known", "charging", "on the charger"
// or "not known".
std::string battery_summary(const HomeData& data);

// The battery clock averages over the readings of the last
// kBatteryAverageMs and gives a time once they span kBatteryEnoughMs.
constexpr int64_t kBatteryAverageMs = 60000;
constexpr int64_t kBatteryEnoughMs = 50000;
// The gauge's own time to empty counts when it is 1 minute to this long.
constexpr int32_t kBatteryMostGaugeMinutes = 48 * 60;

// One reading of the battery while it discharges.
struct BatterySample {
    int64_t at_ms{0};                      // a steady clock, ms
    int32_t gauge_minutes{kDetailUnknown}; // the gauge's time to empty
    int32_t current_ma{kDetailUnknown};    // either sign; the size is used
    int32_t charge_mah{kDetailUnknown};    // charge left
};

// How long the battery lasts at the current use. Only readings while it
// discharges count; any other state, or a clock that went back, starts
// afresh.
class BatteryClock {
public:
    void add(BatteryState state, const BatterySample& sample);

    // True while discharging and the readings since it started span less
    // than kBatteryEnoughMs. Once they did, a later gap in the readings
    // does not bring it back; only a fresh start does.
    bool estimating() const;

    // The minutes left, or kDetailUnknown while estimating() or when not
    // discharging. When the newest reading has a sensible gauge time, the
    // average of those times; otherwise the newest charge divided by the
    // average size of the current (the voltage cancels out). kDetailUnknown
    // when neither can be worked out.
    int32_t minutes() const;

private:
    std::vector<BatterySample> samples_; // oldest first, the last kBatteryAverageMs
    bool ready_{false};                  // the readings once spanned kBatteryEnoughMs
};

} // namespace model

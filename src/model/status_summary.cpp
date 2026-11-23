/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "status_summary.h"

#include "tool_data.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace model {
namespace {

// Drops above this read "99+ drops".
constexpr int32_t kMostDropsShown = 99;
// A reading keeps at most this many samples (one every 10 s fills 7).
constexpr std::size_t kMostSamples = 32;

bool known(int32_t value) {
    return value != kDetailUnknown;
}

bool slow_link(const StatusPort& port) {
    return (known(port.speed_mbps) && port.speed_mbps > 0 && port.speed_mbps <= kHealthSlowCableMbps) ||
           port.duplex == "half";
}

bool many_drops(const StatusPort& port) {
    return known(port.drops) && port.drops > kCableMostDrops;
}

std::string drops_text(int32_t drops) {
    if (drops > kMostDropsShown) {
        return std::to_string(kMostDropsShown) + "+ drops";
    }
    return std::to_string(drops) + (drops == 1 ? " drop" : " drops");
}

bool sane_gauge(int32_t minutes) {
    return known(minutes) && minutes >= 1 && minutes <= kBatteryMostGaugeMinutes;
}

// `value` rounded to the nearest multiple of `step`.
int32_t rounded(int32_t value, int32_t step) {
    return (value + step / 2) / step * step;
}

} // namespace

bool cable_link_poor(const StatusPort& port) {
    return slow_link(port) || many_drops(port);
}

std::string cable_summary(const StatusPort& port) {
    const bool slow = slow_link(port);
    const bool drops = many_drops(port);
    if (slow && drops) {
        return "slow, " + drops_text(port.drops) + ", check it";
    }
    if (slow) {
        return "slow link, check it";
    }
    if (drops) {
        return drops_text(port.drops) + ", check it";
    }
    std::string text;
    const auto add = [&text](const std::string& part) {
        text += (text.empty() ? "" : ", ") + part;
    };
    if (known(port.speed_mbps) && port.speed_mbps > 0) {
        add(std::to_string(port.speed_mbps) + " Mbit");
    }
    if (!port.duplex.empty()) {
        add(port.duplex);
    }
    if (known(port.drops) && port.drops >= 0) {
        add(drops_text(port.drops));
    }
    return text.empty() ? std::string("connected") : text;
}

std::string battery_time_text(int32_t minutes) {
    if (!known(minutes) || minutes < 0) {
        return {};
    }
    if (minutes < 60) {
        const int32_t shown = std::max(rounded(minutes, 5), 5);
        return shown >= 60 ? std::string("about 1 h") : "about " + std::to_string(shown) + " min";
    }
    if (minutes < 10 * 60) {
        const int32_t shown = rounded(minutes, 10);
        const int32_t rest = shown % 60;
        return "about " + std::to_string(shown / 60) + " h" + (rest == 0 ? "" : " " + std::to_string(rest) + " min");
    }
    if (minutes < 100 * 60 - 30) {
        return "about " + std::to_string(rounded(minutes, 60) / 60) + " h";
    }
    return "over 99 h";
}

std::string battery_summary(const HomeData& data) {
    switch (data.battery_state) {
        case BatteryState::Charging:
            return "charging";
        case BatteryState::Idle:
            return "on the charger";
        case BatteryState::Discharging: {
            if (data.details.battery_estimating) {
                return "estimating...";
            }
            const std::string time = battery_time_text(data.details.battery_empty_minutes);
            return time.empty() ? std::string("time left not known") : time + " left";
        }
        case BatteryState::Unknown:
            break;
    }
    return "not known";
}

void BatteryClock::add(BatteryState state, const BatterySample& sample) {
    if (state != BatteryState::Discharging) {
        samples_.clear();
        ready_ = false;
        return;
    }
    if (!samples_.empty() && sample.at_ms < samples_.back().at_ms) {
        samples_.clear();
        ready_ = false;
    }
    samples_.push_back(sample);
    if (sample.at_ms - samples_.front().at_ms >= kBatteryEnoughMs) {
        ready_ = true;
    }
    const int64_t oldest = sample.at_ms - kBatteryAverageMs;
    samples_.erase(samples_.begin(),
                   std::find_if(samples_.begin(), samples_.end(),
                                [oldest](const BatterySample& kept) { return kept.at_ms >= oldest; }));
    if (samples_.size() > kMostSamples) {
        samples_.erase(samples_.begin(), samples_.end() - kMostSamples);
    }
}

bool BatteryClock::estimating() const {
    return !samples_.empty() && !ready_;
}

int32_t BatteryClock::minutes() const {
    if (samples_.empty() || estimating()) {
        return kDetailUnknown;
    }
    if (sane_gauge(samples_.back().gauge_minutes)) {
        int64_t sum = 0;
        int64_t count = 0;
        for (const BatterySample& sample : samples_) {
            if (sane_gauge(sample.gauge_minutes)) {
                sum += sample.gauge_minutes;
                ++count;
            }
        }
        return static_cast<int32_t>((sum + count / 2) / count);
    }

    int64_t current_sum = 0;
    int64_t count = 0;
    int32_t charge = kDetailUnknown;
    for (const BatterySample& sample : samples_) {
        if (known(sample.current_ma) && sample.current_ma != 0) {
            current_sum += std::llabs(static_cast<int64_t>(sample.current_ma));
            ++count;
        }
        if (known(sample.charge_mah) && sample.charge_mah >= 0) {
            charge = sample.charge_mah;
        }
    }
    if (count == 0 || !known(charge)) {
        return kDetailUnknown;
    }
    // charge (mAh) × 60 / average current (mA), rounded.
    const int64_t minutes = (static_cast<int64_t>(charge) * 60 * count + current_sum / 2) / current_sum;
    return static_cast<int32_t>(std::min<int64_t>(minutes, std::numeric_limits<int32_t>::max() - 1));
}

} // namespace model

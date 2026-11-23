/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the Status page's summary (status_summary.h): the cable line,
// the battery line, how the time left is rounded and how the battery clock
// averages. Built on the Mac only, as lokictl_status_tests; exits 1 when a
// check fails.

#include "status_summary.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

void expect_text(const std::string& got, const std::string& want, const char* what) {
    expect(got == want, std::string(what) + ": got \"" + got + "\", want \"" + want + "\"");
}

// The longest line the page shows: "Cable: " or "Battery: " and the text.
constexpr std::size_t kMostLine = 34;

model::StatusPort cable(int32_t mbps, const char* duplex, int32_t drops) {
    model::StatusPort port;
    port.kind = model::LinkKind::Cable;
    port.name = "eth0";
    port.state = model::PortState::Connected;
    port.speed_mbps = mbps;
    port.duplex = duplex;
    port.drops = drops;
    return port;
}

void check_cable() {
    expect_text(model::cable_summary(cable(100, "full", 0)), "100 Mbit, full, 0 drops", "normal");
    expect(!model::cable_link_poor(cable(100, "full", 0)), "normal is fine");
    expect_text(model::cable_summary(cable(1000, "full", 1)), "1000 Mbit, full, 1 drop", "one drop");
    expect_text(model::cable_summary(cable(100, "full", 3)), "100 Mbit, full, 3 drops", "three drops");
    expect(!model::cable_link_poor(cable(100, "full", 3)), "three drops are fine");
    expect_text(model::cable_summary(cable(10, "full", 0)), "slow link, check it", "10 Mbit");
    expect_text(model::cable_summary(cable(100, "half", 0)), "slow link, check it", "half duplex");
    expect(model::cable_link_poor(cable(100, "half", 0)), "half duplex is poor");
    expect_text(model::cable_summary(cable(100, "full", 4)), "4 drops, check it", "flapping");
    expect(model::cable_link_poor(cable(100, "full", 4)), "four drops are poor");
    expect_text(model::cable_summary(cable(10, "half", 250)), "slow, 99+ drops, check it", "both");
    expect_text(model::cable_summary(cable(model::kDetailUnknown, "", model::kDetailUnknown)), "connected",
                "nothing known");
    expect_text(model::cable_summary(cable(100, "", model::kDetailUnknown)), "100 Mbit", "speed only");
    for (const int32_t drops : {0, 1, 3, 4, 99, 100, 2000000000}) {
        for (const char* duplex : {"full", "half", ""}) {
            const std::string line = "Cable: " + model::cable_summary(cable(10000, duplex, drops));
            expect(line.size() <= kMostLine, "cable line too long: " + line);
        }
    }
}

void check_time_text() {
    expect_text(model::battery_time_text(model::kDetailUnknown), "", "unknown");
    expect_text(model::battery_time_text(0), "about 5 min", "0");
    expect_text(model::battery_time_text(43), "about 45 min", "43");
    expect_text(model::battery_time_text(58), "about 1 h", "58");
    expect_text(model::battery_time_text(199), "about 3 h 20 min", "199");
    expect_text(model::battery_time_text(240), "about 4 h", "240");
    expect_text(model::battery_time_text(596), "about 10 h", "596");
    expect_text(model::battery_time_text(725), "about 12 h", "725");
    expect_text(model::battery_time_text(5969), "about 99 h", "5969");
    expect_text(model::battery_time_text(5970), "over 99 h", "5970");
}

model::HomeData battery(model::BatteryState state, int32_t minutes, bool estimating) {
    model::HomeData data;
    data.battery_state = state;
    data.details.battery_empty_minutes = minutes;
    data.details.battery_estimating = estimating;
    return data;
}

void check_battery_line() {
    using model::BatteryState;
    expect_text(model::battery_summary(battery(BatteryState::Discharging, 199, false)), "about 3 h 20 min left",
                "discharging");
    expect_text(model::battery_summary(battery(BatteryState::Discharging, model::kDetailUnknown, true)),
                "estimating...", "estimating");
    expect_text(model::battery_summary(battery(BatteryState::Discharging, model::kDetailUnknown, false)),
                "time left not known", "no time");
    expect_text(model::battery_summary(battery(BatteryState::Charging, 0, false)), "charging", "charging");
    expect_text(model::battery_summary(battery(BatteryState::Idle, 0, false)), "on the charger", "idle");
    expect_text(model::battery_summary(battery(BatteryState::Unknown, 0, false)), "not known", "unknown");
    for (const int32_t minutes : {0, 199, 725, 5969, 100000}) {
        const std::string line = "Battery: " + model::battery_summary(battery(BatteryState::Discharging, minutes, false));
        expect(line.size() <= kMostLine, "battery line too long: " + line);
    }
}

model::BatterySample sample(int64_t seconds, int32_t gauge, int32_t current, int32_t charge) {
    model::BatterySample out;
    out.at_ms = seconds * 1000;
    out.gauge_minutes = gauge;
    out.current_ma = current;
    out.charge_mah = charge;
    return out;
}

void check_clock() {
    using model::BatteryState;
    constexpr int32_t kNone = model::kDetailUnknown;

    // From the current: 1000 mAh at an average of 500 mA is 120 minutes,
    // whatever the sign; a time only after 50 s.
    model::BatteryClock clock;
    expect(!clock.estimating() && clock.minutes() == kNone, "empty clock");
    for (int64_t second = 0; second <= 40; second += 10) {
        clock.add(BatteryState::Discharging, sample(second, kNone, second % 20 == 0 ? -400 : -600, 1000));
    }
    expect(clock.estimating() && clock.minutes() == kNone, "estimating for 40 s");
    clock.add(BatteryState::Discharging, sample(50, kNone, -600, 1000));
    expect(!clock.estimating(), "ready at 50 s");
    expect(clock.minutes() == 120, "120 minutes from the current, got " + std::to_string(clock.minutes()));

    // Only the last minute counts.
    for (int64_t second = 60; second <= 130; second += 10) {
        clock.add(BatteryState::Discharging, sample(second, kNone, 1000, 1000));
    }
    expect(clock.minutes() == 60, "the last minute at 1000 mA, got " + std::to_string(clock.minutes()));

    // A gap in the readings later on does not bring "estimating" back.
    model::BatteryClock gap;
    for (int64_t second = 0; second <= 60; second += 10) {
        gap.add(BatteryState::Discharging, sample(second, kNone, -500, 1000));
    }
    for (int64_t second = 85; second <= 125; second += 10) {
        gap.add(BatteryState::Discharging, sample(second, kNone, -500, 1000));
    }
    expect(!gap.estimating() && gap.minutes() == 120, "ready after a gap, got " + std::to_string(gap.minutes()));

    // The gauge's own time, averaged, when the newest reading has one.
    model::BatteryClock gauge;
    for (int64_t second = 0; second <= 60; second += 10) {
        gauge.add(BatteryState::Discharging, sample(second, second < 30 ? 200 : 210, -500, 1000));
    }
    expect(gauge.minutes() == 206, "gauge average, got " + std::to_string(gauge.minutes()));
    // A gauge value out of range falls back to the current.
    gauge.add(BatteryState::Discharging, sample(70, 65535, -500, 1000));
    expect(gauge.minutes() == 120, "insane gauge, got " + std::to_string(gauge.minutes()));

    // Charging starts afresh, and so does a clock that went back.
    gauge.add(BatteryState::Charging, sample(80, kNone, 500, 1000));
    expect(!gauge.estimating() && gauge.minutes() == kNone, "charging clears");
    gauge.add(BatteryState::Discharging, sample(90, kNone, -500, 1000));
    expect(gauge.estimating(), "estimating again after charging");
    gauge.add(BatteryState::Discharging, sample(10, kNone, -500, 1000));
    expect(gauge.estimating(), "clock back starts afresh");

    // No current and no gauge: not known once the minute is over.
    model::BatteryClock nothing;
    for (int64_t second = 0; second <= 60; second += 10) {
        nothing.add(BatteryState::Discharging, sample(second, kNone, 0, 1000));
    }
    expect(!nothing.estimating() && nothing.minutes() == kNone, "no current");
}

} // namespace

int main() {
    check_cable();
    check_time_text();
    check_battery_line();
    check_clock();
    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("status summary: all checks passed\n");
    return 0;
}

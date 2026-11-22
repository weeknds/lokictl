/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_source.h"

#include "home_examples.h"
#include "live_data_source.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace platform {
namespace {

using std::chrono::milliseconds;

struct DataSourceName {
    DataSourceKind kind;
    const char* name;
};

// In the order the help text lists them.
constexpr DataSourceName kDataSourceNames[] = {
    {DataSourceKind::Sample, "sample"},
    {DataSourceKind::Demo, "demo"},
    {DataSourceKind::Live, "live"},
    {DataSourceKind::Wifi, "wifi"},
    {DataSourceKind::Cable, "cable"},
    {DataSourceKind::Offline, "offline"},
    {DataSourceKind::Tour, "tour"},
};

// Every made-up block carries made-up Status page values that fit it
// (model::example_status_details()), whichever page is shown, so the Status
// page has values in every made-up source.
void add_example_details(model::HomeData& data) {
    data.details = model::example_status_details(data);
}

// The fixed made-up values. They never change, so the reader never wakes up
// for them.
class SampleDataSource final : public DataSource {
public:
    DataSourceKind kind() const override {
        return DataSourceKind::Sample;
    }

    void read(model::HomeData& data) override {
        data = model::sample_home_data();
        add_example_details(data);
    }

    milliseconds next_read_delay() override {
        return kNever;
    }
};

// One of the fixed made-up situations of home_examples.h: Wi-Fi, cable or
// no network. Like the sample, its values never change.
class ExampleDataSource final : public DataSource {
public:
    using Fill = model::HomeData (*)();

    ExampleDataSource(DataSourceKind kind, Fill fill)
        : kind_(kind),
          fill_(fill) {}

    DataSourceKind kind() const override {
        return kind_;
    }

    void read(model::HomeData& data) override {
        data = fill_();
        add_example_details(data);
    }

    milliseconds next_read_delay() override {
        return kNever;
    }

private:
    const DataSourceKind kind_;
    const Fill fill_;
};

// The tour (see model::home_tour_stop()). Each reading is one stop; step 0
// is stop 0, and after the last stop it starts again. In a window it takes
// one stop every two seconds, so a person can watch the page change.
constexpr milliseconds kTourStepTime{2000};

class TourDataSource final : public DataSource {
public:
    DataSourceKind kind() const override {
        return DataSourceKind::Tour;
    }

    // Uses the current stop, then moves on to the next one.
    void read(model::HomeData& data) override {
        const auto stop = static_cast<int>(step_ % static_cast<uint64_t>(model::kHomeTourStops));
        data = model::home_tour_stop(stop);
        add_example_details(data);
        ++step_;
    }

    milliseconds next_read_delay() override {
        return kTourStepTime;
    }

private:
    uint64_t step_{0};
};

// The demo script. Each reading is one step; step 0 is the sample exactly.
// From there the clock goes on by one minute per step (after 23:59 comes
// 00:00), and the battery runs down from the sample's charge by one per
// cent per step to the floor, then charges by one per step to full, stays
// idle on the charger for a few steps, runs down to the floor again, and
// so on. The traffic moves too (see demo_traffic()). Every value not named
// here stays the sample's; the clients keep their rates.
constexpr int32_t kMinutesPerDay = 24 * 60;
constexpr int64_t kDemoBatteryFloor = 5;    // the lowest charge of the script, per cent
constexpr int64_t kDemoBatteryFull = 100;   // the highest charge of the script, per cent
constexpr int64_t kDemoIdleSteps = 3;       // steps on the charger at full charge
constexpr milliseconds kDemoStepTime{1000}; // one step per second in a window

// Steps from one charge above the floor to full, charging.
constexpr int64_t kDemoRiseSteps = kDemoBatteryFull - kDemoBatteryFloor;
// One whole cycle after the first run down: up to full, idle, down to the floor.
constexpr int64_t kDemoCycleSteps = 2 * kDemoRiseSteps + kDemoIdleSteps;

// `rate` scaled by share / reference, rounded; `rate` itself when the
// reference is 0.
int32_t scaled_rate(int32_t rate, uint8_t share, uint8_t reference) {
    if (reference == 0) {
        return rate;
    }
    return static_cast<int32_t>((static_cast<int64_t>(rate) * share + reference / 2) / reference);
}

// The demo's traffic at `step`: the sample's two histories turned left by
// one place per step, so the oldest value comes back as the newest and the
// graph scrolls, round and round. Each rate matches its newest value: the
// sample's rate times that value over the sample's own newest value. At
// step 0 it is the sample's traffic as it is.
void demo_traffic(model::HomeData& data, uint64_t step) {
    const auto shift = static_cast<std::ptrdiff_t>(step % model::kHomeHistoryLength);
    const uint8_t down_reference = data.down_history.back();
    const uint8_t up_reference = data.up_history.back();
    std::rotate(data.down_history.begin(), data.down_history.begin() + shift, data.down_history.end());
    std::rotate(data.up_history.begin(), data.up_history.begin() + shift, data.up_history.end());
    data.down_kbps = scaled_rate(data.down_kbps, data.down_history.back(), down_reference);
    data.up_kbps = scaled_rate(data.up_kbps, data.up_history.back(), up_reference);
}

model::HomeData demo_step(uint64_t step) {
    model::HomeData data = model::sample_home_data();
    if (step == 0) {
        return data;
    }

    const int64_t minutes =
        (data.clock_minutes + static_cast<int64_t>(step % kMinutesPerDay)) % kMinutesPerDay;
    data.clock_minutes = static_cast<int32_t>((minutes + kMinutesPerDay) % kMinutesPerDay);

    // The first run down takes the charge from the sample's value to the
    // floor; steps up to `last_first_fall` belong to it.
    const int64_t start_percent = data.battery_percent;
    const int64_t last_first_fall = start_percent - kDemoBatteryFloor;
    const auto n = static_cast<int64_t>(step);
    int64_t percent = 0;
    model::BatteryState state = model::BatteryState::Discharging;
    if (n <= last_first_fall) {
        percent = start_percent - n;
    } else {
        const int64_t k = (n - last_first_fall - 1) % kDemoCycleSteps;
        if (k < kDemoRiseSteps) {
            percent = kDemoBatteryFloor + 1 + k;
            state = model::BatteryState::Charging;
        } else if (k < kDemoRiseSteps + kDemoIdleSteps) {
            percent = kDemoBatteryFull;
            state = model::BatteryState::Idle;
        } else {
            percent = kDemoBatteryFull - 1 - (k - kDemoRiseSteps - kDemoIdleSteps);
        }
    }
    data.battery_percent = static_cast<int32_t>(percent);
    data.battery_state = state;
    demo_traffic(data, step);
    return data;
}

class DemoDataSource final : public DataSource {
public:
    DataSourceKind kind() const override {
        return DataSourceKind::Demo;
    }

    // Uses the current step, then moves on to the next one.
    void read(model::HomeData& data) override {
        data = demo_step(step_);
        add_example_details(data);
        ++step_;
    }

    milliseconds next_read_delay() override {
        return kDemoStepTime;
    }

private:
    uint64_t step_{0};
};

// A made-up source with the pretend switch between modes on top (see
// PretendModeSwitch): it reads the source it wraps, then puts the pretend
// mode and the switch's state into the block.
class PretendSwitchSource final : public DataSource {
public:
    explicit PretendSwitchSource(std::unique_ptr<DataSource> source)
        : source_(std::move(source)) {}

    DataSourceKind kind() const override {
        return source_->kind();
    }

    void read(model::HomeData& data) override {
        source_->read(data);
        pretend_.apply(data, PretendFailure{});
    }

    milliseconds next_read_delay() override {
        return std::min(source_->next_read_delay(), pretend_.next_delay());
    }

    void set_traffic_shown(bool shown) override {
        source_->set_traffic_shown(shown);
    }

    void set_details_shown(bool shown) override {
        source_->set_details_shown(shown);
    }

    void wait_until_ready(milliseconds limit) override {
        source_->wait_until_ready(limit);
    }

    void request_mode(const ModeRequest& request) override {
        pretend_.request(request.mode);
    }

private:
    std::unique_ptr<DataSource> source_;
    PretendModeSwitch pretend_;
};

// The made-up source of `kind` (every kind but Live).
std::unique_ptr<DataSource> make_made_up_source(DataSourceKind kind) {
    switch (kind) {
        case DataSourceKind::Sample:
        case DataSourceKind::Live:
            break;
        case DataSourceKind::Demo:
            return std::make_unique<DemoDataSource>();
        case DataSourceKind::Wifi:
            return std::make_unique<ExampleDataSource>(kind, &model::wifi_home_data);
        case DataSourceKind::Cable:
            return std::make_unique<ExampleDataSource>(kind, &model::cable_home_data);
        case DataSourceKind::Offline:
            return std::make_unique<ExampleDataSource>(kind, &model::offline_home_data);
        case DataSourceKind::Tour:
            return std::make_unique<TourDataSource>();
    }
    return std::make_unique<SampleDataSource>();
}

} // namespace

const char* data_source_name(DataSourceKind kind) {
    for (const auto& entry : kDataSourceNames) {
        if (entry.kind == kind) {
            return entry.name;
        }
    }
    return "sample";
}

bool find_data_source(std::string_view name, DataSourceKind& kind) {
    for (const auto& entry : kDataSourceNames) {
        if (name == entry.name) {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

std::string data_source_names() {
    std::string names;
    for (const auto& entry : kDataSourceNames) {
        if (!names.empty()) {
            names += ", ";
        }
        names += entry.name;
    }
    return names;
}

std::unique_ptr<DataSource> make_data_source(DataSourceKind kind,
                                             const std::string& data_root,
                                             bool may_change_network) {
    if (kind == DataSourceKind::Live) {
        return std::make_unique<LiveDataSource>(data_root, may_change_network);
    }
    return std::make_unique<PretendSwitchSource>(make_made_up_source(kind));
}

} // namespace platform

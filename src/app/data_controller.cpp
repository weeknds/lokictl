/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_controller.h"

#include "base_viewmodel.h"
#include "logger.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace app {
namespace {

// How often the UI thread looks whether the reader has a new block: four
// times a second. A look that finds nothing new does nothing else.
constexpr uint32_t kPollPeriodMs = 250;

// The charge as the log and the comparison use it: unknown for any negative
// number, at most 100 otherwise, as the page shows it.
int32_t logged_charge(int32_t percent) {
    return percent < 0 ? model::kBatteryPercentUnknown : std::min<int32_t>(percent, 100);
}

} // namespace

DataController::DataController(viewmodel::BaseViewModel& view_model,
                               std::unique_ptr<platform::DataSource> source)
    : view_model_(view_model),
      reader_(std::move(source)) {}

DataController::~DataController() {
    stop();
}

void DataController::read_first() {
    const auto started = std::chrono::steady_clock::now();
    reader_.read_now();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    LOG_INFO("data: {} (first reading in {} ms)",
             platform::data_source_name(reader_.kind()),
             static_cast<long long>(elapsed_ms));

    if (!poll()) {
        // Only when the reading failed: the page keeps the values the view
        // model already holds, and the battery line says what they are.
        LOG_WARN("data: the first reading failed; the values stay as they were");
        const model::HomeData& shown = view_model_.home();
        log_battery(logged_charge(shown.battery_percent), shown.battery_state);
    }
}

void DataController::start() {
    if (reader_.running()) {
        return;
    }

    reader_.start();
    if (!reader_.running()) {
        LOG_WARN("data: the reader could not start; the values stay as they are");
        return;
    }

    poll_timer_ = lv_timer_create(poll_timer_cb, kPollPeriodMs, this);
    if (!poll_timer_) {
        // Nothing would fetch the readings, so the reader is not left running.
        reader_.stop();
        LOG_WARN("data: no timer to fetch new values; the values stay as they are");
    }
}

void DataController::step() {
    if (reader_.running()) {
        return;
    }
    reader_.read_now();
    poll();
}

void DataController::stop() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }

    const bool was_running = reader_.running();
    reader_.stop();
    if (was_running) {
        LOG_INFO("data: reader stopped");
    }
}

void DataController::poll_timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<DataController*>(lv_timer_get_user_data(timer));
    if (controller) {
        controller->poll();
    }
}

bool DataController::poll() {
    if (!reader_.take(block_)) {
        return false;
    }
    deliver(block_);
    return true;
}

void DataController::deliver(const model::HomeData& data) {
    view_model_.set_home_data(data);

    const int32_t charge = logged_charge(data.battery_percent);
    if (!battery_logged_ || charge != logged_percent_ || data.battery_state != logged_state_) {
        log_battery(charge, data.battery_state);
    }
}

void DataController::log_battery(int32_t percent, model::BatteryState state) {
    battery_logged_ = true;
    logged_percent_ = percent;
    logged_state_ = state;

    if (percent < 0 && state == model::BatteryState::Unknown) {
        LOG_INFO("data: battery unknown");
        return;
    }
    const std::string charge = percent < 0 ? std::string("--%") : std::to_string(percent) + "%";
    LOG_INFO("data: battery {} {}", charge, model::battery_state_name(state));
}

} // namespace app

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "power_controller.h"

#include "base_viewmodel.h"
#include "data_controller.h"
#include "home_data.h"
#include "logger.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <utility>

namespace app {
namespace {

// How often the timer looks whether a stage is due.
constexpr uint32_t kCheckPeriodMs = 250;
constexpr uint32_t kLongestIdleSeconds = 86400;

// A fifth of `level`, at least 10, never above `level`: 100 -> 20, 30 -> 10,
// 8 -> 8.
int32_t dim_level(int32_t level) {
    return std::max(std::min<int32_t>(level, 10), level / 5);
}

const char* stage_name(PowerController::Stage stage) {
    switch (stage) {
        case PowerController::Stage::Awake:
            return "awake";
        case PowerController::Stage::Dim:
            return "dim";
        case PowerController::Stage::Dark:
            return "dark";
    }
    return "awake";
}

bool parse_seconds(std::string_view text, uint32_t& seconds) {
    if (text.empty()) {
        return false;
    }
    uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return false;
    }
    seconds = value;
    return true;
}

} // namespace

PowerController::PowerController(viewmodel::BaseViewModel& view_model,
                                 DataController& data,
                                 std::unique_ptr<platform::Backlight> backlight,
                                 Times times)
    : view_model_(view_model),
      data_(data),
      backlight_(std::move(backlight)),
      times_(times) {}

PowerController::~PowerController() {
    stop();
}

void PowerController::set_warning_listener(WarningListener listener) {
    warning_listener_ = std::move(listener);
}

void PowerController::set_setting_listener(SettingListener listener) {
    setting_listener_ = std::move(listener);
}

void PowerController::start() {
    if (timer_) {
        return;
    }
    last_activity_ = lv_tick_get();
    // LVGL calls each observer once right away: the battery is checked (a
    // low battery at start warns at once) and the setting's first value is
    // taken without saving it.
    home_observer_ = lv_subject_add_observer(view_model_.home_data_subject(), home_observer_cb, this);
    dimming_observer_ =
        lv_subject_add_observer(view_model_.screen_dimming_subject(), dimming_observer_cb, this);
    timer_ = lv_timer_create(timer_cb, kCheckPeriodMs, this);
}

void PowerController::stop() {
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (home_observer_) {
        lv_observer_remove(home_observer_);
        home_observer_ = nullptr;
    }
    if (dimming_observer_) {
        lv_observer_remove(dimming_observer_);
        dimming_observer_ = nullptr;
    }
    // The backlight goes back first thing on the way out: quitting with ESC,
    // SIGTERM and SIGINT all end the main loop, which calls this.
    apply_backlight(Stage::Awake);
    stage_ = Stage::Awake;
}

bool PowerController::on_key() {
    last_activity_ = lv_tick_get();
    if (stage_ == Stage::Awake) {
        return false;
    }
    set_stage(Stage::Awake, "a key");
    return true;
}

PowerController::Stage PowerController::stage() const {
    return stage_;
}

bool PowerController::parse_times(std::string_view text, Times& times) {
    const auto comma = text.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    uint32_t dim = 0;
    uint32_t dark = 0;
    if (!parse_seconds(text.substr(0, comma), dim) || !parse_seconds(text.substr(comma + 1), dark) ||
        dim < 1 || dark <= dim || dark > kLongestIdleSeconds) {
        return false;
    }
    times = Times{dim * 1000, dark * 1000};
    return true;
}

void PowerController::timer_cb(lv_timer_t* timer) {
    auto* controller = static_cast<PowerController*>(lv_timer_get_user_data(timer));
    if (!controller) {
        return;
    }
    const Stage due = controller->due_stage();
    if (due != controller->stage_) {
        controller->set_stage(due, nullptr);
    }
}

void PowerController::home_observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* controller = static_cast<PowerController*>(lv_observer_get_user_data(observer));
    if (controller) {
        controller->check_battery();
    }
}

void PowerController::dimming_observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* controller = static_cast<PowerController*>(lv_observer_get_user_data(observer));
    if (!controller) {
        return;
    }
    const bool on = controller->view_model_.screen_dimming();
    if (!controller->dimming_seen_) {
        controller->dimming_seen_ = true;
        LOG_INFO("screen: dimming {}", on ? "on" : "off");
        return;
    }
    LOG_INFO("screen: dimming switched {}", on ? "on" : "off");
    // The idle time starts again either way; switched off, the screen comes
    // back at once (it can only be dim if the switch came without a key).
    controller->wake("dimming switched");
    if (controller->setting_listener_) {
        controller->setting_listener_(on);
    }
}

PowerController::Stage PowerController::due_stage() const {
    if (!view_model_.screen_dimming()) {
        return Stage::Awake;
    }
    const uint32_t idle = lv_tick_elaps(last_activity_);
    if (idle >= times_.dark_ms && !on_charger_) {
        return Stage::Dark;
    }
    if (idle >= times_.dim_ms) {
        return Stage::Dim;
    }
    return Stage::Awake;
}

void PowerController::set_stage(Stage stage, const char* reason) {
    if (stage == stage_) {
        return;
    }
    if (stage == Stage::Awake) {
        LOG_INFO("screen: awake ({})", reason ? reason : "due");
    } else {
        LOG_INFO("screen: {}", stage_name(stage));
    }
    stage_ = stage;
    apply_backlight(stage);
    data_.set_screen_dark(stage == Stage::Dark);
}

void PowerController::apply_backlight(Stage stage) {
    if (!backlight_ || (stage == Stage::Awake && !owned_)) {
        return;
    }
    int32_t current = 0;
    if (!backlight_->read(current)) {
        if (!read_failure_logged_) {
            read_failure_logged_ = true;
            LOG_WARN("backlight: cannot be read; the screen's brightness is left alone");
        }
        owned_ = false;
        return;
    }
    // Ours only while it still holds what was written here.
    const bool ours = owned_ && current == written_level_;

    if (stage == Stage::Awake) {
        if (ours && current != saved_level_) {
            if (backlight_->write(saved_level_)) {
                LOG_INFO("backlight: {} -> {}", current, saved_level_);
            } else {
                LOG_WARN("backlight: could not put {} back", saved_level_);
            }
        } else if (owned_ && !ours) {
            LOG_INFO("backlight: left at {}, it was changed elsewhere", current);
        }
        owned_ = false;
        return;
    }

    if (!ours) {
        owned_ = false;
        if (current <= 0) {
            // Already off (the launcher's sleep, say): left alone.
            return;
        }
        saved_level_ = current;
    }
    const int32_t target = stage == Stage::Dim ? dim_level(saved_level_) : 0;
    if (target != current) {
        if (!backlight_->write(target)) {
            LOG_WARN("backlight: could not write {}", target);
            owned_ = false;
            return;
        }
        LOG_INFO("backlight: {} -> {}", current, target);
    }
    written_level_ = target;
    owned_ = true;
}

void PowerController::wake(const char* reason) {
    last_activity_ = lv_tick_get();
    set_stage(Stage::Awake, reason);
}

void PowerController::check_battery() {
    const model::HomeData& data = view_model_.home();
    on_charger_ = model::on_charger(data.battery_state);
    if (on_charger_) {
        warned_low_ = false;
        warned_very_low_ = false;
        return;
    }
    const int32_t percent = data.battery_percent;
    if (model::battery_very_low(percent, data.battery_state)) {
        if (!warned_very_low_) {
            warned_very_low_ = true;
            warned_low_ = true;
            warn(percent, true);
        }
    } else if (model::battery_low(percent, data.battery_state) && !warned_low_) {
        warned_low_ = true;
        warn(percent, false);
    }
}

void PowerController::warn(int32_t percent, bool very_low) {
    LOG_INFO("battery: {} warning at {}%", very_low ? "very low" : "low", percent);
    wake("battery warning");
    if (warning_listener_) {
        warning_listener_(percent, very_low);
    }
}

} // namespace app

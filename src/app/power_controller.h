/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "backlight.h"

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

class DataController;

// Battery care: dims the screen when no key was pressed for a while, turns
// it dark later, slows the reading down while it is dark and warns when the
// battery runs low.
//
// - The screen dims Times::dim_ms after the last key and goes dark (the
//   backlight off) Times::dark_ms after it. On the charger (charging or
//   idle) it only dims. With the "Dim when idle" setting off
//   (BaseViewModel::screen_dimming()) it stays bright.
// - Any key while the screen is dim or dark only brings it back: on_key()
//   says so, and the key does nothing else.
// - The backlight's value is read when it is first lowered, not at start,
//   so a backlight that is already off is left alone. Dim is a fifth of
//   that value, at least 10 (never above the value). Waking and stop() put
//   the value back, but only if the backlight still holds what was written
//   here: when something else changed it meanwhile (the launcher's own
//   sleep), it is left as it is.
// - While the screen is dark, DataController::set_screen_dark() stops the
//   reading of the traffic and of the Status page's details.
// - At kBatteryLowPercent or less while discharging the warning listener is
//   called once, and once more at kBatteryVeryLowPercent; each warning
//   wakes the screen. Being on the charger arms both again.
//
// Everything runs on the UI thread: an LVGL timer looks four times a second
// whether a stage is due, and LVGL time counts, so snapshots cross the
// times with --settle-ms. Log lines: "screen: dim", "screen: dark",
// "screen: awake (<why>)", "backlight: <old> -> <new>", "battery: low
// warning at <n>%", "battery: very low warning at <n>%".
//
// Create it after the view model and the data controller and destroy it
// before them; call stop() right after the main loop.
class PowerController {
public:
    struct Times {
        uint32_t dim_ms;
        uint32_t dark_ms;
    };
    static constexpr Times kDefaultTimes{60000, 300000};

    enum class Stage {
        Awake,
        Dim,
        Dark,
    };

    // `backlight` may be null: then the stages only change what the keys
    // and the reading do.
    PowerController(viewmodel::BaseViewModel& view_model,
                    DataController& data,
                    std::unique_ptr<platform::Backlight> backlight,
                    Times times = kDefaultTimes);
    // Calls stop().
    ~PowerController();

    PowerController(const PowerController&) = delete;
    PowerController& operator=(const PowerController&) = delete;

    // Called with the charge for each battery warning; `very_low` for the
    // second one.
    using WarningListener = std::function<void(int32_t percent, bool very_low)>;
    void set_warning_listener(WarningListener listener);
    // Called with the new value each time the "Dim when idle" setting
    // changes after start().
    using SettingListener = std::function<void(bool on)>;
    void set_setting_listener(SettingListener listener);

    // Starts the timer and follows the battery and the setting; the idle
    // time counts from here. Set the listeners first: a low battery at
    // start warns at once.
    void start();

    // Puts the backlight back (see above) and stops the timer and the
    // observers. Safe to call more than once, and without start().
    void stop();

    // For every key press, before the key does anything: counts as use.
    // Returns true when the screen was dim or dark; the key only woke it
    // then and must do nothing else.
    bool on_key();

    Stage stage() const;

    // Reads "<dim>,<dark>" in whole seconds, 1 <= dim < dark <= 86400
    // (LOKICTL_IDLE_SECONDS). Returns false and leaves `times` alone for
    // anything else.
    static bool parse_times(std::string_view text, Times& times);

private:
    static void timer_cb(lv_timer_t* timer);
    static void home_observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void dimming_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    // The stage the idle time, the charger and the setting ask for.
    Stage due_stage() const;
    void set_stage(Stage stage, const char* reason);
    void apply_backlight(Stage stage);
    // Counts as use and brings the screen back, without a key.
    void wake(const char* reason);
    void check_battery();
    void warn(int32_t percent, bool very_low);

    viewmodel::BaseViewModel& view_model_;
    DataController& data_;
    std::unique_ptr<platform::Backlight> backlight_;
    const Times times_;
    WarningListener warning_listener_;
    SettingListener setting_listener_;

    lv_timer_t* timer_{nullptr};
    lv_observer_t* home_observer_{nullptr};
    lv_observer_t* dimming_observer_{nullptr};
    bool dimming_seen_{false};     // the setting's first value was seen
    uint32_t last_activity_{0};    // lv_tick_get() at the last key or warning
    Stage stage_{Stage::Awake};
    bool on_charger_{false};
    bool warned_low_{false};
    bool warned_very_low_{false};

    // The backlight while it is lowered: the value to put back and the
    // value written last. `owned_` is false while nothing was written.
    bool owned_{false};
    int32_t saved_level_{0};
    int32_t written_level_{0};
    bool read_failure_logged_{false};
};

} // namespace app

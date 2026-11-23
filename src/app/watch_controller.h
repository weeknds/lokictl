/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "watch.h"
#include "watch_data.h"

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The screen's side of Watch (passive alerts, model::WatchData). It owns a
// platform::WatchRunner and the list of known devices, carries out the
// Watch page's requests (BaseViewModel::request_watch()) and, from an LVGL
// timer four times a second, takes the latest run from the runner and gives
// it to the view model with the known list. The log it hands over also
// holds the alerts added from elsewhere (add_alert(): the trap), in the
// order they came. Watch runs next to any tool and
// keeps going when its page is left; it ends when LokiCTL quits (there is
// no background service).
//
// Each new alert is handed to the alert listener (a short message on any
// page); several in one take become one message ("3 new alerts"). Devices
// learned on a first run and devices marked known (key 7) join the known
// list (at most model::kWatchKnownMax, the oldest dropped first), which is
// handed to the saver: at once for a device marked known, once learning is
// over (or the run or the app stops) for learned ones.
//
// Log lines: "watch: started", "watch: stopped by the user", "watch:
// ended, alerts: <n>, frames: <f>", "watch: failed: <why>", "watch: alert:
// <kind>", "watch: learned devices: <n>", "watch: device marked known" and
// "watch: runner stopped" (never an address, a hardware address or a name).
//
// Create it after the view model and destroy it before; it is not copied.
class WatchController {
public:
    static constexpr uint32_t kPollPeriodMs = 250;

    using AlertListener = std::function<void(const std::string& text)>;
    using KnownSaver = std::function<void(const std::vector<std::string>& known)>;

    WatchController(viewmodel::BaseViewModel& view_model,
                    std::unique_ptr<platform::WatchRunner> runner,
                    std::vector<std::string> known);
    // Calls stop().
    ~WatchController();

    WatchController(const WatchController&) = delete;
    WatchController& operator=(const WatchController&) = delete;

    // Gets each new alert's message ("Scan on you", "3 new alerts").
    void set_alert_listener(AlertListener listener);
    // Gets the known list after each change; empty: nothing is saved.
    void set_known_saver(KnownSaver saver);

    // Adds an alert from elsewhere (the trap's connections) to the log, as
    // the newest. It shows with Watch on or off, stays when a new Watch run
    // starts, and gives no message (its sender gives its own).
    void add_alert(const model::WatchAlert& alert);

    // Starts following the requests and the timer. Does nothing when it
    // already runs.
    void start();
    // Deletes the timer, stops following the requests, stops a run and
    // ends the runner's thread. Safe to call more than once, and without
    // start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void handle_request();
    void poll();
    void add_known(const std::string& hardware);
    void keep_learned(const model::WatchRun& run);
    void save_known();
    void publish();

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<platform::WatchRunner> runner_;
    std::vector<std::string> known_;
    AlertListener alert_listener_;
    KnownSaver known_saver_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* request_observer_{nullptr};
    int32_t requests_seen_{0};
    bool stopped_{false};
    model::WatchRun run_;          // the run handed over last
    std::size_t learned_taken_{0}; // how many of run_.learned are in known_
    // Alerts from elsewhere, the newest first, each with how many of run_'s
    // alerts were raised before it, so the log shows both in order.
    struct OtherAlert {
        model::WatchAlert alert;
        int32_t after{0};
    };
    std::vector<OtherAlert> others_;
    int32_t others_raised_{0};
};

} // namespace app

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "trap.h"
#include "trap_data.h"

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

class WatchController;

// Adds the trap's new connections (`fresh`, the newest first) to Watch's
// log as Trap alerts, the oldest first, so the newest ends on top.
void add_trap_alerts(WatchController& watch, const std::vector<model::TrapHit>& fresh);

// The screen's side of the trap (fake services, model::TrapRun). It owns a
// platform::TrapRunner, carries out the Trap page's requests
// (BaseViewModel::request_trap()) and, from an LVGL timer four times a
// second, takes the latest run from the runner and gives it to the view
// model. The trap is off at every start, keeps going when its page is left
// and ends when LokiCTL quits (also when the mode is kept on quitting).
//
// Each new connection is handed to the hit listener (a short message on
// any page and a line in Watch's log); several in one take give one
// message ("3 connections").
//
// Log lines: "trap: set by the user", "trap: taken away by the user",
// "trap: on, ports open: <n>", "trap: connection to port <p>", "trap: off,
// connections: <n>, closed at once: <m>", "trap: failed: <why>", "trap:
// firewall helper: <why>" and "trap: runner stopped" (never an address or
// what a connection sent).
//
// Create it after the view model and destroy it before; it is not copied.
class TrapController {
public:
    static constexpr uint32_t kPollPeriodMs = 250;

    // `message` is "192.0.2.7 tried web" or "3 connections"; `fresh` the
    // new connections, the newest first (those the log kept).
    using HitListener =
        std::function<void(const std::string& message, const std::vector<model::TrapHit>& fresh)>;

    TrapController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::TrapRunner> runner);
    // Calls stop().
    ~TrapController();

    TrapController(const TrapController&) = delete;
    TrapController& operator=(const TrapController&) = delete;

    void set_hit_listener(HitListener listener);

    // Starts following the requests and the timer. Does nothing when it
    // already runs.
    void start();
    // Deletes the timer, stops following the requests, stops a run (the
    // redirect goes) and ends the runner's thread. Safe to call more than
    // once, and without start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void handle_request();
    void poll();
    void log_changes(const model::TrapRun& next) const;

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<platform::TrapRunner> runner_;
    HitListener hit_listener_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* request_observer_{nullptr};
    int32_t requests_seen_{0};
    bool stopped_{false};
    model::TrapRun run_;  // the run handed over last
};

} // namespace app

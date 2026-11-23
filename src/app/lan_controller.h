/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lan_runner.h"
#include "lan_speed.h"

#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The screen's side of the LAN speed test (model::LanRun). It owns a
// platform::LanRunner, carries out the LAN page's requests
// (BaseViewModel::request_lan()) and, from an LVGL timer four times a
// second, takes the latest run from the runner and gives it to the view
// model, with the Cardputer's addresses a server may listen on
// (model::LanRun::places: the uplink's private address as "network", the
// hotspot's as "hotspot" while it runs), looked up while the page is open.
//
// A test from the Cardputer keeps going when its page is left. The server
// only runs while the LAN page is on top: leaving the page, or its address
// going away, turns it off. Nothing starts while another tool runs.
//
// Log lines: "lan: test started", "lan: test done, down <d> kbit/s (<n>
// bytes), up <u> kbit/s (<m> bytes)", "lan: test stopped by the user",
// "lan: test failed: <why>", "lan: server on (<network|hotspot>)", "lan:
// server test done, <k> kbit/s, <in|out|both ways>", "lan: server off,
// tests: <n>" (", page left" or ", address gone" when it was not the
// user), "lan: server failed: <why>" and "lan: runner stopped" (never an
// address).
//
// Create it after the view model and destroy it before; it is not copied.
class LanController {
public:
    static constexpr uint32_t kPollPeriodMs = 250;

    LanController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::LanRunner> runner);
    // Calls stop().
    ~LanController();

    LanController(const LanController&) = delete;
    LanController& operator=(const LanController&) = delete;

    // Starts following the requests and the timer. Does nothing when it
    // already runs.
    void start();
    // Asks a run to end without waiting (the iperf3 child is ended on the
    // runner's thread), so the app's other teardown runs alongside.
    // stop() still has to follow.
    void begin_stop();
    // Deletes the timer, stops following the requests, stops a run (the
    // iperf3 child ends) and ends the runner's thread. Safe to call more
    // than once, and without start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void handle_request();
    void poll();
    std::vector<model::LanPlace> find_places() const;
    const char* place_label(const std::string& address) const;
    void log_change(const model::LanRun& next) const;

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<platform::LanRunner> runner_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* request_observer_{nullptr};
    int32_t requests_seen_{0};
    int32_t ticks_{0};
    bool stopped_{false};
    const char* off_why_{""};  // why the server was turned off, for the log
    std::vector<model::LanPlace> places_;
    model::LanRun run_;  // the run handed over last
};

} // namespace app

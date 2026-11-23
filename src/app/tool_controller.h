/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"
#include "tool_runner.h"

#include "lvgl.h"

#include <cstdint>
#include <memory>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The screen's side of the tools (ping, DNS lookup, trace, and listening
// on the Discover page). It owns a
// platform::ToolRunner, hands it the tool pages' requests
// (BaseViewModel::request_tool_run() and request_tool_stop()) and, from an
// LVGL timer four times a second, takes the latest runs from it and gives
// them to the view model with the app-wide Sent count
// (platform::sent_packets()). Everything here runs on the UI thread; the
// runner does the network work on its own thread, never this one.
//
// A run keeps going when its page is left. Only one tool runs at a time: a
// request to run while one runs is ignored (the pages switch Run off then).
//
// Log lines: "tools: <ping|dns|trace|listen> started", "tools:
// <ping|dns|trace|listen> stopped by the user", "tools: ping done, <r>/<s>
// replies", "tools: ping failed: <why>", "tools: dns done, <n> addresses",
// "tools: dns failed: <why>", "tools: trace done, reached in <n> hops"
// (or "unreachable at hop <n>", "not reached in <n> hops"), "tools: trace
// failed: <why>", "tools: listen done, devices heard: <n>, frames: <f>",
// "tools: listen ended, devices heard: <n>" (after a stop), "tools: listen
// failed: <why>" (never a target, an address, a hardware address or a
// name), and "tools: runner stopped" at the end.
//
// Create it after the view model and destroy it before; it is not copied.
class ToolController {
public:
    static constexpr uint32_t kPollPeriodMs = 250;

    ToolController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::ToolRunner> runner);
    // Calls stop().
    ~ToolController();

    ToolController(const ToolController&) = delete;
    ToolController& operator=(const ToolController&) = delete;

    // Starts following the requests and the timer that takes the runs.
    // Does nothing when it already runs.
    void start();
    // Deletes the timer, stops following the requests, stops a run and
    // ends the runner's thread (within about 0.2 s). Safe to call more than
    // once, and without start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void handle_request();
    void poll();
    void log_change(const model::ToolsData& next) const;

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<platform::ToolRunner> runner_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* request_observer_{nullptr};
    int32_t requests_seen_{0};
    bool stopped_{false};
    model::ToolsData data_;  // the data handed over last
};

} // namespace app

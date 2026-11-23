/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "talk.h"
#include "talk_data.h"

#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <string>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The screen's side of "What is it talking to?" (model::TalkRun). It owns a
// platform::TalkRunner and, from an LVGL timer four times a second, follows
// the hotspot: while LokiCTL's hotspot runs (HomeData::ap_active, in Router
// on wlan0, in Share on lokiap0) the runner listens on its interface; when
// the hotspot stops, or moves to another interface, the run is stopped and
// everything it heard is forgotten (the view model gets an empty, Idle
// run). Nothing is ever written to disk; a report holds the names only
// when the user saves one.
//
// Log lines: "talk: started on <interface>", "talk: stopped, lookups: <n>",
// "talk: failed: <why>" and "talk: runner stopped" (never a name, an
// address or a hardware address).
//
// Create it after the view model and destroy it before; it is not copied.
class TalkController {
public:
    static constexpr uint32_t kPollPeriodMs = 250;

    TalkController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::TalkRunner> runner);
    // Calls stop().
    ~TalkController();

    TalkController(const TalkController&) = delete;
    TalkController& operator=(const TalkController&) = delete;

    // Starts the timer (and takes a first look at once). Does nothing when
    // it already runs.
    void start();
    // Deletes the timer, stops a run and ends the runner's thread. Safe to
    // call more than once, and without start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    void poll();
    // The hotspot's interface while the hotspot runs; empty otherwise.
    std::string hotspot_interface() const;

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<platform::TalkRunner> runner_;
    lv_timer_t* poll_timer_{nullptr};
    bool stopped_{false};
    std::string interface_;  // where the run listens; empty: no run wanted
    bool started_{false};    // the runner took the run for interface_
    model::TalkRun run_;     // the run handed over last
};

} // namespace app

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "survey.h"
#include "survey_data.h"

#include "lvgl.h"

#include <cstdint>
#include <memory>
#include <string>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The screen's side of the Wi-Fi survey (model::SurveyRun). It owns a
// platform::SurveyRunner and carries out the Survey page's requests
// (BaseViewModel::request_survey()): a start takes the USB Wi-Fi stick
// model::survey_stick() picks (one NetworkManager does not manage, never
// the built-in wlan0), and nothing starts without one. It never starts on
// its own. From an LVGL timer four times a second it takes the latest run
// from the runner, marks the look-alikes of the network the built-in
// Wi-Fi joined (model::mark_survey_look_alikes()) and gives it to the view
// model; the joined network (model::survey_joined_wifi()) is remembered
// from the last reading that said it, since the scan list it may come from
// is read only while the page is on top. A survey keeps going when the
// page is left; it stops when its
// stick is pulled out or no longer free, and at the end of
// model::kSurveySeconds.
//
// At start() it asks the runner to remove the listening interface a
// killed run left on the stick (platform::SurveyRunner::recover()).
//
// Log lines: "survey: started on <interface>", "survey: done (stopped,
// failed: <why>), networks: <n>, warnings: <w>, frames: <f>", "survey:
// no stick to use (<none|managed|unknown>)", "survey: stick gone, stopping"
// and "survey: runner stopped" (never a network name or address).
//
// Create it after the view model and destroy it before; it is not copied.
class SurveyController {
public:
    static constexpr uint32_t kPollPeriodMs = 250;

    SurveyController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::SurveyRunner> runner);
    // Calls stop().
    ~SurveyController();

    SurveyController(const SurveyController&) = delete;
    SurveyController& operator=(const SurveyController&) = delete;

    // Starts following the requests and the timer. Does nothing when it
    // already runs.
    void start();
    // Deletes the timer, stops following the requests, stops a run (its
    // listening interface goes) and ends the runner's thread. Safe to call
    // more than once, and without start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void handle_request();
    void poll();
    void log_end(const model::SurveyRun& run) const;

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<platform::SurveyRunner> runner_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* request_observer_{nullptr};
    int32_t requests_seen_{0};
    bool stopped_{false};
    bool stick_gone_logged_{false};
    std::string joined_name_;      // the joined network, last known
    std::string joined_security_;
    model::SurveyRun run_;  // the runner's latest run, without the look-alike marks
};

} // namespace app

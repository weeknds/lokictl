/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <functional>
#include <string>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// Saves reports (model/report.h): on every request from the pages and the
// command bar (BaseViewModel::request_report()) it puts together what the
// view model holds (home, tools, Watch, trap) and writes it as a new
// report into its folder (platform::save_report()), then hands the result
// to the listener (a short message). The page and the devices table are a
// few tens of kilobytes, written at once on the UI thread without waiting
// for the disk, as the settings file is.
//
// The folder comes from platform::report_folder(): ~/Documents/LokiCTL/
// reports on the device, none in the desktop simulator unless
// LOKICTL_REPORT_DIR names one (then a request only says "no folder").
//
// Log lines: "report: saved <name> (<n> bytes)" and "report: not saved
// (<why>)"; never what the report holds.
//
// Create it after the view model and destroy it before.
class ReportController {
public:
    // `saved` and, when saved, the page's time ("07:51:30"); otherwise why
    // it was not ("disk full").
    using Listener = std::function<void(bool saved, const std::string& text)>;

    ReportController(viewmodel::BaseViewModel& view_model, std::string folder);
    // Calls stop().
    ~ReportController();

    ReportController(const ReportController&) = delete;
    ReportController& operator=(const ReportController&) = delete;

    void set_listener(Listener listener);

    // Starts following the requests. Does nothing when it already does.
    void start();
    // Stops following the requests. Safe to call more than once.
    void stop();

private:
    static void request_cb(lv_observer_t* observer, lv_subject_t* subject);
    void save();

    viewmodel::BaseViewModel& view_model_;
    std::string folder_;
    Listener listener_;
    lv_observer_t* request_observer_{nullptr};
    int32_t requests_seen_{0};
};

} // namespace app

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_reader.h"
#include "data_source.h"
#include "home_data.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// The screen's side of the data layer. It owns a platform::DataReader,
// fetches the newest values from it and hands them to the view model, which
// tells the pages. It is the only place that gives the view model new
// values, and the only place that logs about the data layer: everything here
// runs on the UI thread, the background reader never draws, never logs and
// never touches the view model.
//
// In a normal run, read_first() takes the first reading before the first
// page is built, start() starts the background reader and an LVGL timer that
// looks four times a second whether a new block is there, and stop() ends
// both. Snapshot runs never start the reader: step() takes each further
// reading on the UI thread, one after the other, through the same path, so
// the same command always gives the same picture.
//
// Log lines: "data: <source> (first reading in <n> ms)" once at the start;
// "data: battery <n>% <state>" for the first reading and then only when the
// charge or the state changed ("--%" for an unknown charge, "data: battery
// unknown" when neither is known); "data: link <none|wifi|cable>, access
// point <on|off>, <n> clients" for the first reading and then only when the
// kind of link, the access point or the number of clients changed, always
// after the battery line of the same reading; "data: reader stopped" when a
// background reader that ran has ended.
//
// It also follows the current page and tells the reader whether the home
// page, the only page that shows the traffic, is on top
// (DataReader::set_traffic_shown()), so the live source reads the traffic
// once a second only while it is seen.
//
// Create it after the view model and destroy it before; like
// app::ThemeController, it is not copied.
class DataController {
public:
    // Starts following the current page at once.
    DataController(viewmodel::BaseViewModel& view_model, std::unique_ptr<platform::DataSource> source);
    // Calls stop() and stops following the current page.
    ~DataController();

    DataController(const DataController&) = delete;
    DataController& operator=(const DataController&) = delete;

    // Takes the first reading on the calling thread, hands it to the view
    // model and logs how long it took, then the first battery and link
    // lines. Call it
    // once, before the first page is built, so the page never shows values
    // that are about to change.
    void read_first();

    // For a normal run: starts the background reader and the timer that
    // fetches its values. Does nothing while the reader already runs.
    void start();

    // For runs without the background reader (snapshots): takes one further
    // reading on the calling thread and hands it over at once. Does nothing
    // while the background reader runs.
    void step();

    // Deletes the timer, stops the background reader and waits for it to
    // end. Logs "data: reader stopped" when the reader ran. Safe to call
    // more than once, and without start().
    void stop();

private:
    static void poll_timer_cb(lv_timer_t* timer);
    // Tells the reader whether the page on top shows the traffic. The user
    // data is the controller.
    static void page_observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    // Fetches the newest block from the reader, if one arrived since the
    // last time, and hands it over. Returns false when nothing new was there;
    // nothing else happens then.
    bool poll();
    // Gives `data` to the view model, and logs the battery line when the
    // charge or the state differs from the one logged last, then the link
    // line when the kind of link, the access point or the number of clients
    // does.
    void deliver(const model::HomeData& data);
    void log_battery(int32_t percent, model::BatteryState state);
    void log_link(model::LinkKind kind, bool ap_active, std::size_t clients);

    viewmodel::BaseViewModel& view_model_;
    platform::DataReader reader_;
    lv_timer_t* poll_timer_{nullptr};
    lv_observer_t* page_observer_{nullptr};
    model::HomeData block_;  // the block fetched last; kept to reuse its memory
    bool battery_logged_{false};
    int32_t logged_percent_{model::kBatteryPercentUnknown};
    model::BatteryState logged_state_{model::BatteryState::Unknown};
    bool link_logged_{false};
    model::LinkKind logged_kind_{model::LinkKind::None};
    bool logged_ap_active_{false};
    std::size_t logged_clients_{0};
};

} // namespace app

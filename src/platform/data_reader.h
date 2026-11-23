/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_source.h"
#include "home_data.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace platform {

// Asks a data source for values and keeps the newest block where the screen
// can fetch it.
//
// With start(), a background thread (the "reader") sleeps until the source
// says its values can have changed, asks it, and hands the block over. The
// reader thread only ever touches the source and the hand-over slot below,
// which a mutex guards; it never draws, never logs and knows nothing about
// the screen. Without start() nothing runs in the background: read_now()
// takes one reading on the caller's thread, through the same hand-over.
//
// Threads: every public function is called by the thread that owns the
// reader (the screen's thread in the app). The background thread runs only
// between start() and stop(), and while it runs the owner never calls the
// source itself: read_now() does nothing then.
//
// The first reading is not taken by the background thread: the owner takes
// it with read_now() before start(), so the first page never shows values
// that are about to change.
class DataReader {
public:
    explicit DataReader(std::unique_ptr<DataSource> source);
    // Stops the background thread, if it runs.
    ~DataReader();

    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;

    DataSourceKind kind() const;

    // Takes one reading on the caller's thread and hands it over like the
    // background thread does. Does nothing while the background thread runs.
    void read_now();

    // Lets the source wait, at most `limit`, for values it collects in the
    // background after it was made (DataSource::wait_until_ready()). It
    // blocks; the owner calls it before the first read_now(). Does nothing
    // while the background thread runs.
    void wait_until_ready(std::chrono::milliseconds limit);

    // Starts the background thread. Does nothing while it already runs. If
    // the system cannot start a thread, nothing runs and running() stays
    // false. SIGTERM and SIGINT are blocked in the new thread, so those
    // signals are always handled by the other threads of the app.
    void start();

    // Wakes the background thread and waits until it has ended. Before it
    // ends, the thread lets the source switch back to Client
    // (DataSource::leave_modes(), at most kLeaveModesLimit), so a normal
    // quit never leaves a mode on. Safe to call more than once, and without
    // start(); without the background thread nothing is left.
    void stop();

    // Whether the background thread runs.
    bool running() const;

    // Tells the source whether a page that shows the traffic is on screen
    // (see DataSource::set_traffic_shown()); it starts as shown. While the
    // background thread runs, it wakes up, hands the flag to the source and
    // asks it again how long to sleep, without taking a reading for that
    // alone. Without the background thread the flag reaches the source with
    // the next read_now().
    void set_traffic_shown(bool shown);

    // Tells the source whether the Status page is on screen (see
    // DataSource::set_details_shown()); it starts as not shown. Handed over
    // the same way as the traffic flag.
    void set_details_shown(bool shown);

    // Tells the source whether the Wi-Fi page is on screen (see
    // DataSource::set_wifi_shown()); it starts as not shown. Handed over
    // the same way as the traffic flag.
    void set_wifi_shown(bool shown);

    // Hands a Wi-Fi scan the user asked for to the source (DataSource::
    // request_wifi_scan()). It never waits: while the background thread
    // runs, it wakes up, hands the request over and takes a reading at
    // once. Without the background thread the request reaches the source
    // with the next read_now().
    void request_wifi_scan();

    // Hands a switch between modes to the source (DataSource::
    // request_mode()). It never waits: while the background thread runs,
    // it wakes up, hands the request over and takes a reading at once, so
    // the switch's first state follows quickly. Without the background
    // thread the request reaches the source with the next read_now(). A
    // newer request replaces one that was not handed over yet.
    void request_mode(const ModeRequest& request);

    // Hands the list of devices to block to the source (DataSource::
    // set_blocked()), the whole list each time. It never waits: while the
    // background thread runs, it wakes up, hands the list over and takes a
    // reading at once, so the page shows the result quickly. Without the
    // background thread the list reaches the source with the next
    // read_now(). A newer list replaces one that was not handed over yet,
    // which loses nothing, because each list is the whole wanted state.
    void set_blocked(const std::vector<std::string>& addresses);

    // Whether the source changes the real network (DataSource::
    // changes_network()).
    bool changes_network() const;

    // How long the background thread, on its way out, lets the source
    // switch back to Client (DataSource::leave_modes()).
    static constexpr std::chrono::milliseconds kLeaveModesLimit{3000};

    // Copies the newest block into `data` and returns true, but only if it
    // changed since the last take(); otherwise leaves `data` alone and
    // returns false.
    bool take(model::HomeData& data);

    // A copy of the newest block. Before the first reading it is a
    // default block, with every value unknown or empty.
    model::HomeData latest() const;

private:
    void run();
    // Asks the source for one reading. A reading that fails with an
    // exception is dropped, so the previous values stay; returns false then.
    bool read_source(model::HomeData& data);
    // Stores the block as the newest one, unless it equals the newest one.
    void publish(model::HomeData data);
    // Gives the source the newest traffic, details and Wi-Fi flags, and a
    // waiting switch request, list of devices to block or scan request.
    // Called on the thread that asks the source. Returns true when it
    // handed over a request or a list.
    bool hand_over_shown();

    std::unique_ptr<DataSource> source_;
    const DataSourceKind kind_;
    std::thread thread_;

    // The hand-over slot, the stop flag, the three page flags, the switch
    // request, the devices to block and the scan request; the mutex guards
    // all of them.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_requested_{false};
    model::HomeData latest_;
    bool has_latest_{false};
    bool fresh_{false};            // latest_ changed since the last take()
    bool traffic_shown_{true};     // see set_traffic_shown()
    bool details_shown_{false};    // see set_details_shown()
    bool wifi_shown_{false};       // see set_wifi_shown()
    bool shown_changed_{false};    // a flag changed since the source got them
    bool scan_requested_{false};   // a Wi-Fi scan waits for the source
    bool mode_requested_{false};   // mode_request_ waits for the source
    ModeRequest mode_request_;
    bool blocked_changed_{false};  // blocked_ waits for the source
    std::vector<std::string> blocked_;
};

} // namespace platform

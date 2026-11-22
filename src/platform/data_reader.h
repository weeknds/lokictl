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
#include <thread>

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

    // Wakes the background thread and waits until it has ended. Safe to call
    // more than once, and without start().
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
    // Gives the source the newest traffic and details flags. Called on the
    // thread that asks the source.
    void hand_over_shown();

    std::unique_ptr<DataSource> source_;
    const DataSourceKind kind_;
    std::thread thread_;

    // The hand-over slot, the stop flag and the two page flags; the mutex
    // guards all seven.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_requested_{false};
    model::HomeData latest_;
    bool has_latest_{false};
    bool fresh_{false};            // latest_ changed since the last take()
    bool traffic_shown_{true};     // see set_traffic_shown()
    bool details_shown_{false};    // see set_details_shown()
    bool shown_changed_{false};    // a flag changed since the source got them
};

} // namespace platform

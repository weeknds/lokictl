/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "data_source.h"
#include "home_data.h"

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

    std::unique_ptr<DataSource> source_;
    const DataSourceKind kind_;
    std::thread thread_;

    // The hand-over slot and the stop flag; the mutex guards all four.
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_requested_{false};
    model::HomeData latest_;
    bool has_latest_{false};
    bool fresh_{false}; // latest_ changed since the last take()
};

} // namespace platform

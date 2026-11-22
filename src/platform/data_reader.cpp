/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_reader.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <pthread.h>
#include <signal.h>
#include <utility>

namespace platform {
namespace {

using std::chrono::milliseconds;

// A source that asks for no wait at all (or a negative one) would keep a
// core busy; this is the shortest sleep between two readings.
constexpr milliseconds kShortestWait{10};
// Waits are cut to this length. After it the reader just asks the source
// again; it also keeps a very long wait from overflowing the clock.
constexpr milliseconds kLongestWait = std::chrono::hours(24);

} // namespace

DataReader::DataReader(std::unique_ptr<DataSource> source)
    : source_(std::move(source)),
      kind_(source_ ? source_->kind() : DataSourceKind::Sample) {}

DataReader::~DataReader() {
    stop();
}

DataSourceKind DataReader::kind() const {
    return kind_;
}

void DataReader::read_now() {
    if (thread_.joinable()) {
        return;
    }
    hand_over_traffic_shown();
    model::HomeData data;
    if (read_source(data)) {
        publish(std::move(data));
    }
}

void DataReader::wait_until_ready(milliseconds limit) {
    if (thread_.joinable() || !source_) {
        return;
    }
    source_->wait_until_ready(limit);
}

void DataReader::start() {
    if (thread_.joinable() || !source_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = false;
    }

    // A new thread starts with the signal mask of the thread that creates
    // it. Blocking SIGTERM and SIGINT around the creation keeps them out of
    // the reader for good; the old mask is put back right after.
    sigset_t stop_signals;
    sigset_t old_mask;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals, SIGTERM);
    sigaddset(&stop_signals, SIGINT);
    const bool masked = pthread_sigmask(SIG_BLOCK, &stop_signals, &old_mask) == 0;
    try {
        thread_ = std::thread(&DataReader::run, this);
    } catch (...) {
        // No thread: nothing runs, and running() says so. The mask is put
        // back below all the same.
    }
    if (masked) {
        pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
    }
}

void DataReader::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool DataReader::running() const {
    return thread_.joinable();
}

void DataReader::set_traffic_shown(bool shown) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (traffic_shown_ == shown) {
            return;
        }
        traffic_shown_ = shown;
        traffic_changed_ = true;
    }
    wake_.notify_all();
}

void DataReader::hand_over_traffic_shown() {
    bool shown = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shown = traffic_shown_;
        traffic_changed_ = false;
    }
    if (source_) {
        source_->set_traffic_shown(shown);
    }
}

bool DataReader::take(model::HomeData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!fresh_) {
        return false;
    }
    data = latest_;
    fresh_ = false;
    return true;
}

model::HomeData DataReader::latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

// The background thread. The source is asked with the mutex released, so
// take() never waits for a reading. A new traffic flag ends the wait early:
// the loop then starts again, hands the flag over and asks the source how
// long to sleep now (the live source asks for a reading at once).
void DataReader::run() {
    const auto woken = [this] { return stop_requested_ || traffic_changed_; };
    while (true) {
        hand_over_traffic_shown();
        milliseconds delay = DataSource::kNever;
        try {
            delay = source_->next_read_delay();
        } catch (...) {
            delay = kLongestWait;
        }

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (delay == DataSource::kNever) {
                // The values never change: sleep until stop() (or a new
                // traffic flag), and ask the source nothing else. kNever
                // itself cannot go to wait_for(), whose deadline would
                // overflow, so it waits in slices of kLongestWait. Not
                // condition_variable::wait(): with GCC 12 and later that
                // function needs a newer libstdc++ than the rest of the
                // app, which would change the package's dependencies.
                while (!wake_.wait_for(lock, kLongestWait, woken)) {
                    // A slice ended without stop() or a new flag: wait again.
                }
            } else {
                wake_.wait_for(lock, std::clamp(delay, kShortestWait, kLongestWait), woken);
            }
            if (stop_requested_) {
                return;
            }
            if (traffic_changed_) {
                continue;
            }
        }

        model::HomeData data;
        if (read_source(data)) {
            publish(std::move(data));
        }
    }
}

bool DataReader::read_source(model::HomeData& data) {
    if (!source_) {
        return false;
    }
    try {
        source_->read(data);
        return true;
    } catch (...) {
        return false;
    }
}

void DataReader::publish(model::HomeData data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (has_latest_ && data == latest_) {
        return;
    }
    latest_ = std::move(data);
    has_latest_ = true;
    fresh_ = true;
}

} // namespace platform

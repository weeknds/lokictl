/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

// Frame statistics for development tools.
//
// FrameStats times every frame LVGL draws on one display (from the start of
// rendering until the last part has been handed to the screen) and keeps
// running totals. About once a second, and only when new frames were drawn,
// it writes them to a small text file, one key=value per line:
//
//   frames=<frames drawn since start>
//   render_us_total=<sum of all frame times in microseconds>
//   render_us_max=<slowest frame in microseconds>
//   render_us_last=<latest frame in microseconds>
//
// It is opt-in: the app only creates it when a development tool asks for it
// (scripts/device.sh sets NETDECK_FRAME_STATS_FILE). Everything runs on the UI
// thread, and the file is removed again when the object is destroyed.

#include "lvgl.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace platform {

class FrameStats {
public:
    // Receives a one-line description when the statistics file cannot be
    // written. It is called at most once; after that the file is no longer
    // updated.
    using ErrorReporter = void (*)(const std::string& message);

    FrameStats(lv_display_t* display, std::string output_path, ErrorReporter report_error = nullptr);
    ~FrameStats();

    FrameStats(const FrameStats&) = delete;
    FrameStats& operator=(const FrameStats&) = delete;
    FrameStats(FrameStats&&) = delete;
    FrameStats& operator=(FrameStats&&) = delete;

    uint64_t frames() const { return frames_; }

private:
    static void display_event_cb(lv_event_t* event);
    static void write_timer_cb(lv_timer_t* timer);

    void on_render_start();
    void on_render_ready();
    void write_if_changed();
    void write_file();
    void remove_files();

    lv_display_t* display_{nullptr};
    lv_timer_t* write_timer_{nullptr};
    std::string output_path_;
    std::string temp_path_;
    ErrorReporter report_error_{nullptr};
    bool write_failed_{false};
    bool render_started_{false};
    std::chrono::steady_clock::time_point render_start_{};

    uint64_t frames_{0};
    uint64_t render_us_total_{0};
    uint64_t render_us_max_{0};
    uint64_t render_us_last_{0};
    uint64_t written_frames_{0};
};

} // namespace platform

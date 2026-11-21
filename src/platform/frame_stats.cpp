/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "frame_stats.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace platform {
namespace {

constexpr uint32_t kWritePeriodMs = 1000;

} // namespace

FrameStats::FrameStats(lv_display_t* display, std::string output_path, ErrorReporter report_error)
    : display_(display),
      output_path_(std::move(output_path)),
      temp_path_(output_path_ + ".tmp"),
      report_error_(report_error) {
    if (display_) {
        lv_display_add_event_cb(display_, display_event_cb, LV_EVENT_RENDER_START, this);
        lv_display_add_event_cb(display_, display_event_cb, LV_EVENT_RENDER_READY, this);
        lv_display_add_event_cb(display_, display_event_cb, LV_EVENT_DELETE, this);
    }
    write_timer_ = lv_timer_create(write_timer_cb, kWritePeriodMs, this);

    // One write straight away, so tools can tell the app has started even
    // before the first frame is finished.
    write_file();
}

FrameStats::~FrameStats() {
    if (display_) {
        lv_display_remove_event_cb_with_user_data(display_, display_event_cb, this);
        display_ = nullptr;
    }
    if (write_timer_) {
        lv_timer_delete(write_timer_);
        write_timer_ = nullptr;
    }
    remove_files();
}

void FrameStats::display_event_cb(lv_event_t* event) {
    auto* self = static_cast<FrameStats*>(lv_event_get_user_data(event));
    if (!self) {
        return;
    }

    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START:
            self->on_render_start();
            break;
        case LV_EVENT_RENDER_READY:
            self->on_render_ready();
            break;
        case LV_EVENT_DELETE:
            // LVGL drops the display's callbacks itself after this event.
            self->display_ = nullptr;
            break;
        default:
            break;
    }
}

void FrameStats::write_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<FrameStats*>(lv_timer_get_user_data(timer));
    if (self) {
        self->write_if_changed();
    }
}

void FrameStats::on_render_start() {
    render_start_ = std::chrono::steady_clock::now();
    render_started_ = true;
}

void FrameStats::on_render_ready() {
    if (!render_started_) {
        return;
    }
    render_started_ = false;

    const auto elapsed = std::chrono::steady_clock::now() - render_start_;
    const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    const uint64_t frame_us = elapsed_us > 0 ? static_cast<uint64_t>(elapsed_us) : 0U;

    ++frames_;
    render_us_total_ += frame_us;
    render_us_last_ = frame_us;
    if (frame_us > render_us_max_) {
        render_us_max_ = frame_us;
    }
}

void FrameStats::write_if_changed() {
    // An idle screen draws nothing, so nothing is written either.
    if (frames_ == written_frames_) {
        return;
    }
    write_file();
}

void FrameStats::write_file() {
    if (write_failed_ || output_path_.empty()) {
        return;
    }

    std::string error;
    {
        std::ofstream file(temp_path_, std::ios::out | std::ios::trunc);
        if (file) {
            file << "frames=" << frames_ << '\n'
                 << "render_us_total=" << render_us_total_ << '\n'
                 << "render_us_max=" << render_us_max_ << '\n'
                 << "render_us_last=" << render_us_last_ << '\n';
            file.close();
        }
        if (!file) {
            error = "cannot write " + temp_path_;
        }
    }

    if (error.empty()) {
        std::error_code rename_error;
        std::filesystem::rename(temp_path_, output_path_, rename_error);
        if (rename_error) {
            error = "cannot replace " + output_path_ + ": " + rename_error.message();
        }
    }

    if (!error.empty()) {
        write_failed_ = true;
        std::error_code ignored;
        std::filesystem::remove(temp_path_, ignored);
        if (report_error_) {
            report_error_(error);
        } else {
            LV_LOG_WARN("frame stats: %s", error.c_str());
        }
        return;
    }

    written_frames_ = frames_;
}

void FrameStats::remove_files() {
    if (output_path_.empty()) {
        return;
    }
    std::error_code ignored;
    std::filesystem::remove(output_path_, ignored);
    std::filesystem::remove(temp_path_, ignored);
}

} // namespace platform

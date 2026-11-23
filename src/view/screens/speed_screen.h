/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "tool_data.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The Speed page, opened from the Tools page. Under the title bar:
// - the server row: the caption "server" and the speed server's host;
// - two columns, DOWN and UP, each with the speed in Mbit/s ("--" before
//   its phase), the bytes moved and a bar graph with one bar per half
//   second (download in the accent colour, upload in the second accent),
//   both on one scale, the fastest half second of the run;
// - the status line: "Connecting", "Download 4 of 10 s", "Upload 7 of
//   10 s", "Done", "Stopped" or why it failed, with the app-wide Sent count
//   at its right.
//
// Keys: 4 (Back) or a short ESC goes back, and a run keeps going; 6 (Run)
// or Enter starts a test (a 10 s download, then a 10 s upload) and stops it
// while it runs (Stop). While another tool runs, Run is switched off. The
// command bar's `speed` opens the page and asks for a start
// (BaseViewModel::request_tool_start()); it never stops a running test.
//
// The run lives in the view model (tools().speed), so it is kept when the
// page is left and across a theme switch.
class SpeedScreen : public BaseScreen {
public:
    SpeedScreen(viewmodel::BaseViewModel& view_model,
                app::AssetManager& assets,
                view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    // One column: download or upload.
    struct Column {
        lv_obj_t* caption{nullptr};
        lv_obj_t* speed{nullptr};
        lv_obj_t* unit{nullptr};
        lv_obj_t* bytes{nullptr};
        lv_obj_t* graph{nullptr};
        std::string speed_shown;
        std::string bytes_shown;
        std::array<uint8_t, model::kSpeedSamples> bars_shown{};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;
    void build_column(lv_obj_t* parent, Column& column, const char* caption, int32_t left, int32_t top,
                      lv_color_t bar_color);

    bool this_running() const;
    bool any_running() const;
    void run_or_stop();
    // The command bar asked for a test (BaseViewModel::take_tool_start()):
    // does what the first 6 does, and nothing while any tool runs.
    void start_from_command();

    void refresh();
    void refresh_keys();
    void refresh_column(Column& column, int32_t kbps, uint64_t bytes, const std::vector<int32_t>& samples,
                        int32_t scale);
    void refresh_status();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void start_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* server_caption_{nullptr};
    lv_obj_t* server_{nullptr};
    Column down_;
    Column up_;
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};
    std::string server_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    int32_t shown_keys_{-1};  // what refresh_keys() set last
};

} // namespace screen

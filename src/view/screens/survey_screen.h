/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "survey_data.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The Survey page (Wi-Fi key 7, or the command `survey`): what an optional
// USB Wi-Fi stick hears in the air (model::SurveyRun, in survey()). It
// only receives. Under the title bar:
// - the summary line: the stick ("wlan1"), how many networks were heard,
//   and at the right the warnings ("2 warnings") or the best channel;
// - the list (the start view), one line each, as many as fit: first the
//   findings ("Kick-off attack on ch 11", "Look-alike of HomeNet",
//   "Channel 6: busy"), then the networks, look-alikes first, then the
//   strongest ("Hidden network" for a hidden one) with their channel and
//   security at the right; a thin mark at the right edge shows where the
//   lines lie when there are more. Or the chart: one bar per channel, 1 to
//   13, as busy as it was (warning colour when busy, error colour where
//   a kick-off attack was seen), the channel under it. In place of either,
//   a note: "Plug in a USB Wi-Fi stick", "Press 6 to start listening", ...;
// - the status line: "Needs a USB Wi-Fi stick", "Ready on wlan1, sends
//   nothing", "Listening, 4:12 left", "Done, listened 5:00", "Stopped after
//   1:23", or what went wrong.
//
// Keys: 4 (Back) or a short ESC goes back; 5 switches between the list and
// the chart (its label names the other one); 6 (Start / Stop) or Enter
// starts or stops the survey (off without a free stick); up and down (F/X)
// scroll the list.
//
// The view and the first line shown live in the view model
// (survey_chart(), survey_scroll()), so they are kept across a theme
// switch.
class SurveyScreen : public BaseScreen {
public:
    SurveyScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets, view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

private:
    // The list shows at most this many lines (fewer when the theme's lines
    // are taller).
    static constexpr std::size_t kRows = 5;

    struct Row {
        lv_obj_t* left{nullptr};
        lv_obj_t* right{nullptr};
        std::string left_shown;
        std::string right_shown;
        int32_t tone{-1};
        int32_t wide{-1};  // the left text takes the whole line (no right text)
        bool hidden{true};
    };

    struct Bar {
        lv_obj_t* bar{nullptr};
        lv_obj_t* label{nullptr};
        int32_t height{-1};
        int32_t tone{-1};
    };

    // One line of the list.
    struct Line {
        std::string left;
        std::string right;
        int32_t tone{0};
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The lines of the list, findings first.
    std::vector<Line> lines() const;
    // True when key 6 may start a survey now or stop the one running.
    bool can_toggle() const;
    void toggle();
    void switch_view();
    void scroll(int32_t delta);

    void refresh();
    void refresh_list(const std::vector<Line>& all, bool shown);
    void refresh_chart(bool shown);
    void refresh_keys();

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* summary_{nullptr};
    lv_obj_t* summary_right_{nullptr};
    std::array<Row, kRows> rows_{};
    int32_t visible_rows_{0};
    int32_t list_top_{0};
    int32_t row_height_{0};
    int32_t left_width_{0};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
    std::array<Bar, model::kSurveyChannels> bars_{};
    int32_t bar_bottom_{0};
    int32_t bar_max_height_{0};
    lv_obj_t* note_{nullptr};
    lv_obj_t* status_{nullptr};

    std::string summary_shown_;
    std::string summary_right_shown_;
    int32_t summary_right_tone_{-1};
    std::string note_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    int32_t chart_shown_{-1};
    int32_t keys_shown_{-1};
};

} // namespace screen

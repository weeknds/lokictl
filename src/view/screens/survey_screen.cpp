/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "survey_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the summary line at the top, the list or the chart under it and the
// status line at the bottom, like the Talk page.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kSummaryTop    = 1;
constexpr int32_t kListGap       = 3;
constexpr int32_t kStatusHeight  = 18;
constexpr int32_t kStatusTop     = kContentHeight - kStatusHeight - 1;
constexpr int32_t kColumnGap     = 6;
constexpr int32_t kTrackX        = view::kScreenWidth - 6;
constexpr int32_t kThumbX        = kTrackX - 1;
constexpr int32_t kThumbWidth    = 3;
constexpr int32_t kThumbMinHeight = 6;
constexpr int32_t kTrackRoom     = 6;
// The chart: one column per channel across the width, a bar in each.
constexpr int32_t kColumnWidth   = kWidth / model::kSurveyChannels;
constexpr int32_t kBarWidth      = kColumnWidth - 8;
constexpr int32_t kLabelGap      = 1;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, "");
    return label;
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

int32_t text_width(const std::string& text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

lv_color_t tone_color(const view::Theme& theme, int32_t tone) {
    switch (tone) {
        case tools::kToneMuted:
            return theme.colors.muted;
        case tools::kToneWarning:
            return theme.colors.warning;
        case tools::kToneError:
            return theme.colors.error;
        case tools::kToneAccent:
            return theme.colors.accent;
        default:
            return theme.colors.text;
    }
}

void set_tone(lv_obj_t* label, int32_t& shown, int32_t tone, const view::Theme& theme) {
    if (label && tone != shown) {
        lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        shown = tone;
    }
}

// "4:12" from milliseconds.
std::string minutes_text(int32_t ms) {
    const int32_t seconds = std::max<int32_t>(ms, 0) / 1000;
    const int32_t rest = seconds % 60;
    return std::to_string(seconds / 60) + (rest < 10 ? ":0" : ":") + std::to_string(rest);
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

} // namespace

SurveyScreen::SurveyScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool SurveyScreen::handles(model::AppPage page) {
    return page == model::AppPage::Survey;
}

view::widgets::HelpContent SurveyScreen::help_content() {
    return {"SURVEY",
            {
                {"ESC / 4", "Back"},
                {"5 / 6", "View / start, stop"},
                {"Kick-off", "Kicks you off Wi-Fi"},
                {"Look-alike", "Fake of your Wi-Fi"},
                {"Stick", "Listens, sends none"},
            }};
}

view::widgets::HelpContent SurveyScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet SurveyScreen::soft_keys() {
    // refresh_keys() sets keys 5 and 6 as the page changes.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Chart", [this] { switch_view(); }},
        {"Start", [this] { toggle(); }, true, false},
        {},
        {},
    }};
}

std::vector<SurveyScreen::Line> SurveyScreen::lines() const {
    const model::SurveyRun& run = view_model().survey();
    std::vector<Line> all;
    for (const model::SurveyFinding& finding : model::survey_findings(run)) {
        const int32_t tone = finding.kind == model::SurveyFindingKind::Kickoff     ? tools::kToneError
                             : finding.kind == model::SurveyFindingKind::LookAlike ? tools::kToneWarning
                                                                                   : tools::kToneAccent;
        all.push_back({finding.text, "", tone});
    }
    for (const std::size_t index : model::survey_network_order(run)) {
        const model::SurveyNetwork& network = run.networks[index];
        std::string name = network.name;
        int32_t tone = network.look_alike ? tools::kToneWarning : tools::kToneText;
        if (name.empty()) {
            name = "Hidden network";
            tone = tools::kToneMuted;
        } else if (network.hidden) {
            name += " (hidden)";
        }
        std::string right = network.channel > 0 ? "ch " + std::to_string(network.channel) : std::string("ch ?");
        right += " " + network.security;
        all.push_back({name, right, tone});
    }
    return all;
}

bool SurveyScreen::can_toggle() const {
    if (view_model().survey().state == model::ToolState::Running) {
        return true;
    }
    std::string why;
    return !model::survey_stick(view_model().home(), why).empty();
}

void SurveyScreen::toggle() {
    if (!can_toggle()) {
        return;
    }
    const bool running = view_model().survey().state == model::ToolState::Running;
    if (!running) {
        view_model().set_survey_scroll(0);
    }
    view_model().request_survey(running ? viewmodel::BaseViewModel::SurveyRequest::Stop
                                        : viewmodel::BaseViewModel::SurveyRequest::Start);
}

void SurveyScreen::switch_view() {
    view_model().set_survey_chart(!view_model().survey_chart());
}

void SurveyScreen::scroll(int32_t delta) {
    if (view_model().survey_chart()) {
        return;
    }
    const auto total = static_cast<int32_t>(lines().size());
    const int32_t last = std::max<int32_t>(total - visible_rows_, 0);
    const int32_t first = std::clamp<int32_t>(view_model().survey_scroll(), 0, last);
    view_model().set_survey_scroll(std::clamp<int32_t>(first + delta, 0, last));
}

bool SurveyScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            scroll(-1);
            return true;
        case LV_KEY_DOWN:
            scroll(1);
            return true;
        case LV_KEY_ENTER:
            toggle();
            return true;
        default:
            return false;
    }
}

void SurveyScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);

    // The summary, and the warnings or the best channel at the right, on
    // its baseline.
    const int32_t right_width = text_width("00 warnings", small) + 4;
    summary_ = plain_label(content);
    view::style::text(summary_, theme, view::FontRole::Strong);
    fix_width(summary_, kWidth - right_width - kColumnGap, strong);
    lv_obj_set_pos(summary_, kSideMargin, kSummaryTop);
    summary_right_ = plain_label(content);
    view::style::text(summary_right_, theme, view::FontRole::Small, theme.colors.muted);
    fix_width(summary_right_, right_width, small);
    lv_obj_set_style_text_align(summary_right_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(summary_right_, kSideMargin + kWidth - right_width,
                   kSummaryTop + std::max<int32_t>(baseline_from_top(strong) - baseline_from_top(small), 0));

    // The list: one line per finding or network, ending above the status
    // line whatever the theme's line heights.
    row_height_ = lv_font_get_line_height(body);
    list_top_ = kSummaryTop + lv_font_get_line_height(strong) + kListGap;
    visible_rows_ = std::clamp<int32_t>((kStatusTop - list_top_) / row_height_, 1, static_cast<int32_t>(kRows));
    const int32_t right_column = text_width("ch 13 WPA2-EAP", small);
    const int32_t left_column = std::max<int32_t>(kWidth - kTrackRoom - right_column - kColumnGap, 0);
    left_width_ = left_column;
    const int32_t small_down = std::max<int32_t>(baseline_from_top(body) - baseline_from_top(small), 0);
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        const int32_t y = list_top_ + index * row_height_;
        row.left = plain_label(content);
        view::style::text(row.left, theme, view::FontRole::Body);
        fix_width(row.left, left_column, body);
        lv_obj_set_pos(row.left, kSideMargin, y);
        row.right = plain_label(content);
        view::style::text(row.right, theme, view::FontRole::Small, theme.colors.muted);
        fix_width(row.right, right_column, small);
        lv_obj_set_style_text_align(row.right, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.right, kSideMargin + kWidth - kTrackRoom - right_column, y + small_down);
        set_hidden(row.left, true);
        set_hidden(row.right, true);
        row.hidden = true;
    }

    track_ = plain_object(content);
    lv_obj_set_pos(track_, kTrackX, list_top_);
    lv_obj_set_size(track_, 1, visible_rows_ * row_height_);
    lv_obj_set_style_bg_color(track_, theme.colors.border_subtle, 0);
    lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);
    thumb_ = plain_object(content);
    lv_obj_set_style_bg_color(thumb_, theme.colors.muted, 0);
    lv_obj_set_style_bg_opa(thumb_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb_, std::min<int32_t>(theme.metrics.radius, 1), 0);
    set_hidden(track_, true);
    set_hidden(thumb_, true);

    // The chart: bars grow up from a common bottom, the channel under each.
    const int32_t label_height = lv_font_get_line_height(small);
    bar_bottom_ = kStatusTop - 2 - label_height - kLabelGap;
    bar_max_height_ = std::max<int32_t>(bar_bottom_ - list_top_, 4);
    for (int32_t index = 0; index < model::kSurveyChannels; ++index) {
        Bar& bar = bars_[static_cast<std::size_t>(index)];
        const int32_t column_x = kSideMargin + index * kColumnWidth;
        bar.bar = plain_object(content);
        lv_obj_set_style_bg_opa(bar.bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar.bar, std::min<int32_t>(theme.metrics.radius, 2), 0);
        lv_obj_set_pos(bar.bar, column_x + (kColumnWidth - kBarWidth) / 2, bar_bottom_ - 1);
        lv_obj_set_size(bar.bar, kBarWidth, 1);
        bar.label = plain_label(content);
        view::style::text(bar.label, theme, view::FontRole::Small, theme.colors.muted);
        lv_obj_set_size(bar.label, kColumnWidth, label_height);
        lv_obj_set_style_text_align(bar.label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(bar.label, column_x, bar_bottom_ + kLabelGap);
        lv_label_set_text(bar.label, std::to_string(index + 1).c_str());
        set_hidden(bar.bar, true);
        set_hidden(bar.label, true);
    }

    note_ = plain_label(content);
    view::style::text(note_, theme, view::FontRole::Mono, theme.colors.muted);
    fix_width(note_, kWidth, mono);
    lv_obj_set_pos(note_, kSideMargin, list_top_);
    set_hidden(note_, true);

    status_ = plain_label(content);
    view::style::text(status_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(status_, kWidth, kStatusHeight);
    lv_obj_set_pos(status_, kSideMargin, kStatusTop);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().survey_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().survey_view_subject(), observer_cb, content, this);
    // The stick comes and goes with the network readings.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
}

void SurveyScreen::refresh() {
    const auto& theme = this->theme();
    const model::HomeData& home = view_model().home();
    const model::SurveyRun& run = view_model().survey();
    std::string why;
    const std::string stick = model::survey_stick(home, why);
    const bool started = run.state != model::ToolState::Idle;
    const bool running = run.state == model::ToolState::Running;

    // The summary line.
    std::string summary = "Wi-Fi survey";
    if (started && !run.interface_name.empty()) {
        summary = run.interface_name + " · " + std::to_string(run.networks_seen) +
                  (run.networks_seen == 1 ? " network" : " networks");
    } else if (!stick.empty()) {
        summary = stick + " · USB stick";
    }
    set_text_if_changed(summary_, summary_shown_, summary);
    const int32_t warnings = model::survey_warnings(run);
    std::string right;
    int32_t right_tone = tools::kToneMuted;
    if (warnings > 0) {
        right = std::to_string(warnings) + (warnings == 1 ? " warning" : " warnings");
        right_tone = tools::kToneWarning;
    } else if (const int32_t best = model::survey_best_channel(run); best > 0) {
        right = "Best ch " + std::to_string(best);
    }
    set_text_if_changed(summary_right_, summary_right_shown_, right);
    set_tone(summary_right_, summary_right_tone_, right_tone, theme);

    // The list or the chart, or a note in their place.
    const std::vector<Line> all = lines();
    std::string note;
    if (!started && stick.empty()) {
        note = why == "managed" ? "The system uses the stick" : why == "unknown" ? "No network service to ask"
                                                                                  : "Plug in a USB Wi-Fi stick";
    } else if (!started) {
        note = "Press 6 to start listening";
    } else if (all.empty()) {
        note = running ? "Listening…" : "Nothing heard";
    }
    const bool chart = view_model().survey_chart();
    refresh_list(all, note.empty() && !chart);
    refresh_chart(note.empty() && chart);
    if (set_text_if_changed(note_, note_shown_, note)) {
        set_hidden(note_, note.empty());
    }

    // The status line.
    tools::ToneText status;
    switch (run.state) {
        case model::ToolState::Running:
            status = {"Listening, " + minutes_text(model::kSurveySeconds * 1000 - run.elapsed_ms) + " left",
                      tools::kToneAccent};
            break;
        case model::ToolState::Done:
            status = {"Done, listened " + minutes_text(run.elapsed_ms), tools::kToneMuted};
            break;
        case model::ToolState::Stopped:
            status = {"Stopped after " + minutes_text(run.elapsed_ms), tools::kToneMuted};
            break;
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                status = {"Survey not allowed", tools::kToneError};
            } else {
                status = {capitalized(run.reason.empty() ? std::string("survey failed") : run.reason),
                          tools::kToneError};
            }
            break;
        case model::ToolState::Idle:
            break;
    }
    if (run.state == model::ToolState::Idle) {
        if (!stick.empty()) {
            status = {"Ready on " + stick + ", sends nothing", tools::kToneMuted};
        } else if (why == "managed") {
            status = {"Stick is in use", tools::kToneWarning};
        } else if (why == "unknown") {
            status = {"Can't check the stick", tools::kToneWarning};
        } else {
            status = {"Needs a USB Wi-Fi stick", tools::kToneWarning};
        }
    }
    set_text_if_changed(status_, status_shown_, status.text);
    set_tone(status_, status_tone_, status.tone, theme);

    refresh_keys();
}

void SurveyScreen::refresh_list(const std::vector<Line>& all, bool shown) {
    const auto& theme = this->theme();
    const auto total = shown ? static_cast<int32_t>(all.size()) : 0;
    const int32_t first =
        std::clamp<int32_t>(view_model().survey_scroll(), 0, std::max<int32_t>(total - visible_rows_, 0));
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        const int32_t place = first + index;
        const bool hidden = place >= total;
        if (hidden != row.hidden) {
            set_hidden(row.left, hidden);
            set_hidden(row.right, hidden);
            row.hidden = hidden;
        }
        if (hidden) {
            continue;
        }
        const Line& line = all[static_cast<std::size_t>(place)];
        const int32_t wide = line.right.empty() ? 1 : 0;
        if (wide != row.wide) {
            row.wide = wide;
            lv_obj_set_width(row.left, wide ? kWidth - kTrackRoom : left_width_);
            row.left_shown.clear();  // LVGL cut the old text to the old width
            lv_label_set_text(row.left, "");
        }
        set_text_if_changed(row.left, row.left_shown, line.left);
        set_text_if_changed(row.right, row.right_shown, line.right);
        set_tone(row.left, row.tone, line.tone, theme);
    }

    // The scroll mark, only when there are more lines than fit.
    if (total > visible_rows_) {
        const int32_t list_height = visible_rows_ * row_height_;
        const int32_t thumb_height = std::max<int32_t>(kThumbMinHeight, list_height * visible_rows_ / total);
        const int32_t thumb_y = list_top_ + (list_height - thumb_height) * first / (total - visible_rows_);
        lv_obj_set_pos(thumb_, kThumbX, thumb_y);
        lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
        set_hidden(track_, false);
        set_hidden(thumb_, false);
    } else {
        set_hidden(track_, true);
        set_hidden(thumb_, true);
    }
}

void SurveyScreen::refresh_chart(bool shown) {
    const auto& theme = this->theme();
    const model::SurveyRun& run = view_model().survey();
    if (static_cast<int32_t>(shown) != chart_shown_) {
        chart_shown_ = shown ? 1 : 0;
        for (Bar& bar : bars_) {
            set_hidden(bar.bar, !shown);
            set_hidden(bar.label, !shown);
        }
    }
    if (!shown) {
        return;
    }
    for (int32_t index = 0; index < model::kSurveyChannels; ++index) {
        Bar& bar = bars_[static_cast<std::size_t>(index)];
        const int32_t channel = index + 1;
        const int32_t level = model::survey_channel_level(run, channel);
        const bool heard = run.channels[static_cast<std::size_t>(index)].frames > 0;
        const int32_t height = heard ? std::max<int32_t>(level * bar_max_height_ / 100, 2) : 1;
        const bool attacked = std::any_of(run.attacks.begin(), run.attacks.end(),
                                          [channel](const model::SurveyAttack& attack) {
                                              return attack.channel == channel;
                                          });
        const int32_t tone = attacked ? tools::kToneError
                             : model::survey_channel_load(run, channel) == model::ChannelLoad::Busy
                                 ? tools::kToneWarning
                                 : heard ? tools::kToneAccent
                                         : tools::kToneMuted;
        if (height != bar.height) {
            bar.height = height;
            lv_obj_set_pos(bar.bar, kSideMargin + index * kColumnWidth + (kColumnWidth - kBarWidth) / 2,
                           bar_bottom_ - height);
            lv_obj_set_size(bar.bar, kBarWidth, height);
        }
        if (tone != bar.tone) {
            bar.tone = tone;
            lv_obj_set_style_bg_color(bar.bar, tone == tools::kToneMuted ? theme.colors.border_subtle
                                                                         : tone_color(theme, tone),
                                      0);
        }
    }
}

void SurveyScreen::refresh_keys() {
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    const bool running = view_model().survey().state == model::ToolState::Running;
    const bool chart = view_model().survey_chart();
    const int32_t keys = (running ? 1 : 0) | (can_toggle() ? 2 : 0) | (chart ? 4 : 0);
    if (keys == keys_shown_) {
        return;
    }
    keys_shown_ = keys;
    bar->set_key(1, {chart ? "List" : "Chart", [this] { switch_view(); }});
    bar->set_key(2, {running ? "Stop" : "Start", [this] { toggle(); }, true, can_toggle()});
}

void SurveyScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<SurveyScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

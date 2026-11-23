/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "trap_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar,
// like the Watch page: the summary row at the top, one line per connection
// under it and the status line at the bottom. The detail view takes the
// whole area.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kSummaryTop    = 1;
constexpr int32_t kListGap       = 3;
constexpr int32_t kRowGap        = 1;
constexpr int32_t kStatusHeight  = 18;
constexpr int32_t kStatusTop     = kContentHeight - kStatusHeight - 1;
constexpr int32_t kCaptionGap    = 5;
constexpr int32_t kColumnGap     = 6;
constexpr int32_t kDetailGap     = 2;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    return label;
}

lv_color_t tone_color(const view::Theme& theme, int32_t tone) {
    switch (tone) {
        case tools::kToneMuted:
            return theme.colors.muted;
        case tools::kToneSuccess:
            return theme.colors.success;
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

int32_t text_width(const char* text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// "4:12": minutes and seconds; "1:02:03" from an hour on.
std::string duration_text(int32_t seconds) {
    seconds = std::max<int32_t>(seconds, 0);
    const int32_t hours = seconds / 3600;
    const int32_t minutes = (seconds / 60) % 60;
    const int32_t rest = seconds % 60;
    std::string text;
    if (hours > 0) {
        text = std::to_string(hours) + (minutes < 10 ? ":0" : ":");
    }
    text += std::to_string(minutes) + (rest < 10 ? ":0" : ":") + std::to_string(rest);
    return text;
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

const char* service_word(uint16_t port) {
    const model::TrapService* service = model::trap_service(port);
    return service ? service->word : "a port";
}

// "Ports 21 23 80 445 3389 8080": every fake port while the trap is off,
// the open ones while it is on.
std::string ports_text(const model::TrapRun& run) {
    std::string text = "Ports";
    const bool on = run.state == model::ToolState::Running;
    for (const model::TrapService& service : model::trap_services()) {
        bool shown = !on;
        for (const model::TrapPort& port : run.ports) {
            if (port.port == service.port) {
                shown = !on || port.state == model::TrapPortState::Open;
            }
        }
        if (shown) {
            text += " " + std::to_string(service.port);
        }
    }
    return text;
}

} // namespace

TrapScreen::TrapScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool TrapScreen::handles(model::AppPage page) {
    return page == model::AppPage::Trap;
}

view::widgets::HelpContent TrapScreen::help_content() {
    return {"TRAP",
            {
                {"Trap", "Fake services (honeypot)"},
                {"Caught", "Someone is probing"},
                {"6", "Set trap, it replies"},
                {"ENTER / 5", "What they sent"},
                {"ESC / 4", "Back, trap stays on"},
            }};
}

view::widgets::HelpContent TrapScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet TrapScreen::soft_keys() {
    auto& view_model = this->view_model();
    return {{
        {"Back",
         [&view_model] {
             if (view_model.trap_detail()) {
                 view_model.set_trap_detail(false);
             } else {
                 view_model.go_back();
             }
         }},
        {"Info", [this] { toggle_detail(); }, false, false},
        {"Start", [this] { start_or_stop(); }, true},
        {},
        {},
    }};
}

bool TrapScreen::running() const {
    return view_model().trap().state == model::ToolState::Running;
}

void TrapScreen::start_or_stop() {
    if (running()) {
        view_model().request_trap(viewmodel::BaseViewModel::TrapRequest::Stop);
    } else {
        view_model().set_trap_selection(0);
        view_model().set_trap_detail(false);
        view_model().request_trap(viewmodel::BaseViewModel::TrapRequest::Start);
    }
}

int32_t TrapScreen::selected() const {
    const auto count = static_cast<int32_t>(view_model().trap().hits.size());
    return std::clamp<int32_t>(view_model().trap_selection(), 0, std::max<int32_t>(count - 1, 0));
}

void TrapScreen::toggle_detail() {
    if (view_model().trap_detail()) {
        view_model().set_trap_detail(false);
    } else if (!view_model().trap().hits.empty()) {
        view_model().set_trap_detail(true);
    }
}

void TrapScreen::select(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().trap().hits.size());
    if (count == 0) {
        return;
    }
    view_model().set_trap_selection(std::clamp<int32_t>(selected() + delta, 0, count - 1));
}

bool TrapScreen::on_back() {
    if (view_model().trap_detail()) {
        view_model().set_trap_detail(false);
        return true;
    }
    return false;
}

bool TrapScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            select(-1);
            return true;
        case LV_KEY_DOWN:
            select(1);
            return true;
        case LV_KEY_ENTER:
            toggle_detail();
            return true;
        default:
            return false;
    }
}

void TrapScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    // The summary row: the captions stand on the state's baseline.
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    state_caption_ = plain_label(content, "");
    view::style::caption(state_caption_, theme, "Trap");
    lv_obj_set_pos(state_caption_, kSideMargin, kSummaryTop + small_drop);
    lv_obj_update_layout(state_caption_);
    state_ = plain_label(content, "");
    view::style::text(state_, theme, view::FontRole::Mono, theme.colors.muted);
    lv_obj_set_pos(state_, kSideMargin + lv_obj_get_width(state_caption_) + kCaptionGap, kSummaryTop);
    caught_caption_ = plain_label(content, "");
    view::style::caption(caught_caption_, theme, "");
    lv_obj_align(caught_caption_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kSummaryTop + small_drop);

    // One line per connection; the rows end above the status line, whatever
    // the theme's line heights.
    const int32_t row_height = std::max(lv_font_get_line_height(body), lv_font_get_line_height(strong)) + 2;
    const int32_t list_top = kSummaryTop + lv_font_get_line_height(mono) + kListGap;
    visible_rows_ = std::clamp<int32_t>((kStatusTop - 1 - list_top + kRowGap) / (row_height + kRowGap), 1,
                                        static_cast<int32_t>(kRows));
    const int32_t pad = theme.metrics.space_xs + theme.metrics.border_width;
    const int32_t inner = kWidth - 2 * pad;
    const int32_t time_width = text_width("00:00", small);
    const int32_t what_width = text_width("tried sharing", small);
    const int32_t who_x = time_width + kColumnGap;
    const int32_t who_width = std::max<int32_t>(inner - who_x - kColumnGap - what_width, 0);
    const int32_t small_down = baseline_from_top(body) - baseline_from_top(small);
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        row.box = plain_object(content);
        lv_obj_set_pos(row.box, kSideMargin, list_top + index * (row_height + kRowGap));
        lv_obj_set_size(row.box, kWidth, row_height);
        view::style::list_row(row.box, theme, false);
        row.time = plain_label(row.box, "");
        lv_obj_set_style_text_font(row.time, small, 0);
        lv_obj_set_pos(row.time, 0, 1 + std::max<int32_t>(small_down, 0));
        row.who = plain_label(row.box, "");
        lv_label_set_long_mode(row.who, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.who, who_width, row_height - 2);
        lv_obj_set_pos(row.who, who_x, 1);
        row.what = plain_label(row.box, "");
        lv_obj_set_style_text_font(row.what, small, 0);
        lv_label_set_long_mode(row.what, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.what, what_width, lv_font_get_line_height(small));
        lv_obj_set_style_text_align(row.what, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.what, inner - what_width, 1 + std::max<int32_t>(small_down, 0));
        lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
        row.hidden = true;
    }

    // In place of the list: what the trap is, and its ports.
    note_ = plain_label(content, "");
    view::style::text(note_, theme, view::FontRole::Body, theme.colors.text);
    lv_obj_set_width(note_, kWidth);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(note_, kSideMargin, list_top);
    ports_note_ = plain_label(content, "");
    view::style::text(ports_note_, theme, view::FontRole::Mono, theme.colors.muted);
    lv_obj_set_width(ports_note_, kWidth);
    lv_label_set_long_mode(ports_note_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(ports_note_, kSideMargin, list_top + lv_font_get_line_height(body) + kListGap);

    status_ = plain_label(content, "");
    view::style::text(status_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(status_, kWidth, kStatusHeight);
    lv_obj_set_pos(status_, kSideMargin, kStatusTop);

    // The detail view, over the whole area.
    detail_ = plain_object(content);
    lv_obj_set_pos(detail_, 0, 0);
    lv_obj_set_size(detail_, view::kScreenWidth, kContentHeight);
    detail_name_ = plain_label(detail_, "");
    view::style::text(detail_name_, theme, view::FontRole::Strong, theme.colors.warning);
    lv_obj_set_pos(detail_name_, kSideMargin, kSummaryTop);
    detail_time_ = plain_label(detail_, "");
    view::style::text(detail_time_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(detail_time_, LV_ALIGN_TOP_RIGHT, -kSideMargin,
                 kSummaryTop + std::max<int32_t>(baseline_from_top(strong) - baseline_from_top(small), 0));
    int32_t top = kSummaryTop + lv_font_get_line_height(strong) + kDetailGap;
    detail_who_ = plain_label(detail_, "");
    view::style::text(detail_who_, theme, view::FontRole::Mono);
    lv_label_set_long_mode(detail_who_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(detail_who_, kWidth, lv_font_get_line_height(mono));
    lv_obj_set_pos(detail_who_, kSideMargin, top);
    top += lv_font_get_line_height(mono) + kDetailGap;
    detail_fact_ = plain_label(detail_, "");
    view::style::text(detail_fact_, theme, view::FontRole::Small, theme.colors.accent2);
    lv_label_set_long_mode(detail_fact_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(detail_fact_, kWidth, lv_font_get_line_height(small));
    lv_obj_set_pos(detail_fact_, kSideMargin, top);
    top += lv_font_get_line_height(small) + kDetailGap + 1;
    detail_about_ = plain_label(detail_, "");
    view::style::text(detail_about_, theme, view::FontRole::Small);
    lv_label_set_long_mode(detail_about_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail_about_, kWidth);
    lv_obj_set_pos(detail_about_, kSideMargin, top);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().trap_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().trap_selection_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().trap_detail_subject(), observer_cb, content, this);
}

void TrapScreen::refresh() {
    const model::TrapRun& run = view_model().trap();
    // New connections go on top; a picked one other than the newest stays
    // picked.
    if (raised_seen_ >= 0 && run.raised > raised_seen_ &&
        (view_model().trap_selection() > 0 || view_model().trap_detail())) {
        const int32_t moved = run.raised - raised_seen_;
        raised_seen_ = run.raised;
        view_model().set_trap_selection(view_model().trap_selection() + moved);  // calls refresh() again
        return;
    }
    raised_seen_ = run.raised;
    if (view_model().trap_detail() && run.hits.empty()) {
        view_model().set_trap_detail(false);  // calls refresh() again
        return;
    }
    const bool detail = view_model().trap_detail();
    if (static_cast<int32_t>(detail) != detail_shown_) {
        detail_shown_ = detail ? 1 : 0;
        set_hidden(detail_, !detail);
        set_hidden(state_caption_, detail);
        set_hidden(state_, detail);
        set_hidden(caught_caption_, detail);
        set_hidden(status_, detail);
    }
    if (detail) {
        for (Row& row : rows_) {
            if (row.box && !row.hidden) {
                set_hidden(row.box, true);
                row.hidden = true;
            }
        }
        set_hidden(note_, true);
        set_hidden(ports_note_, true);
        note_shown_.clear();
        ports_note_shown_.clear();
        refresh_detail();
    } else {
        refresh_list();
        refresh_status();
    }
    refresh_keys();
}

void TrapScreen::refresh_list() {
    const auto& theme = this->theme();
    const model::TrapRun& run = view_model().trap();

    const bool on = run.state == model::ToolState::Running;
    const std::string state = on ? "On" : "Off";
    const int32_t state_tone = on ? tools::kToneAccent : tools::kToneMuted;
    set_text_if_changed(state_, state_shown_, state);
    if (state_tone != state_tone_) {
        lv_obj_set_style_text_color(state_, on ? theme.colors.accent2 : theme.colors.muted, 0);
        state_tone_ = state_tone;
    }
    const std::string caught = std::to_string(run.raised) + " caught";
    if (caught != caught_shown_) {
        caught_shown_ = caught;
        view::style::caption(caught_caption_, theme, caught.c_str());
    }

    const auto total = static_cast<int32_t>(run.hits.size());
    const int32_t picked = selected();
    // The first row shown moves only as far as needed to show the pick.
    first_row_ = std::clamp<int32_t>(first_row_, std::max<int32_t>(picked - visible_rows_ + 1, 0), picked);
    first_row_ = std::clamp<int32_t>(first_row_, 0, std::max<int32_t>(total - visible_rows_, 0));
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        const int32_t place = first_row_ + index;
        const bool hidden = place >= total;
        if (hidden != row.hidden) {
            set_hidden(row.box, hidden);
            row.hidden = hidden;
        }
        if (hidden) {
            continue;
        }
        const model::TrapHit& hit = run.hits[static_cast<std::size_t>(place)];
        const int32_t style = place == picked ? 1 : 0;
        if (style != row.style_shown) {
            view::style::list_row(row.box, theme, style == 1);
            row.style_shown = style;
            // On the highlight every part takes the row's colour.
            if (style == 1) {
                lv_obj_remove_local_style_prop(row.time, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.who, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.what, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(row.time, theme.colors.muted, 0);
                lv_obj_set_style_text_color(row.who, theme.colors.text, 0);
                lv_obj_set_style_text_color(row.what, theme.colors.warning, 0);
            }
        }
        set_text_if_changed(row.time, row.time_shown, hit.time);
        set_text_if_changed(row.who, row.who_shown, hit.address);
        set_text_if_changed(row.what, row.what_shown, std::string("tried ") + service_word(hit.port));
    }

    std::string note;
    std::string ports;
    if (total == 0) {
        note = on ? "Waiting for snoopers" : "Fake services that catch snoopers";
        ports = ports_text(run);
    }
    if (note != note_shown_) {
        note_shown_ = note;
        lv_label_set_text(note_, note.c_str());
        set_hidden(note_, note.empty());
    }
    if (ports != ports_note_shown_) {
        ports_note_shown_ = ports;
        lv_label_set_text(ports_note_, ports.c_str());
        set_hidden(ports_note_, ports.empty());
    }
}

void TrapScreen::refresh_detail() {
    const model::TrapRun& run = view_model().trap();
    const model::TrapHit& hit = run.hits[static_cast<std::size_t>(selected())];
    const model::TrapService* service = model::trap_service(hit.port);
    set_text_if_changed(detail_name_, detail_name_shown_, std::string("Tried ") + service_word(hit.port));
    set_text_if_changed(detail_time_, detail_time_shown_, hit.time);
    set_text_if_changed(detail_who_, detail_who_shown_, hit.address + " port " + std::to_string(hit.source_port));
    set_text_if_changed(detail_fact_, detail_fact_shown_,
                        std::string(service ? service->name : "Fake service") + ", port " + std::to_string(hit.port));
    const std::string sent = hit.first_bytes.empty() ? std::string("Sent nothing") : "Sent: " + hit.first_bytes;
    set_text_if_changed(detail_about_, detail_about_shown_,
                        sent + "\nNothing real runs here: someone is probing.");
}

void TrapScreen::refresh_status() {
    const model::TrapRun& run = view_model().trap();
    tools::ToneText status;
    const auto open = model::trap_open_ports(run);
    const auto all = static_cast<int32_t>(model::kTrapServiceCount);
    switch (run.state) {
        case model::ToolState::Running: {
            const std::string time = "On " + duration_text(run.elapsed_s) + ", ";
            if (!run.helper_problem.empty()) {
                status = {time + std::to_string(open) + " of " + std::to_string(all) + " (no helper)",
                          tools::kToneWarning};
            } else if (open == all) {
                status = {time + std::to_string(open) + " fake ports", tools::kToneAccent};
            } else {
                status = {time + std::to_string(open) + " of " + std::to_string(all) + " ports", tools::kToneAccent};
            }
            break;
        }
        case model::ToolState::Stopped:
        case model::ToolState::Done:
            status = {"Off after " + duration_text(run.elapsed_s), tools::kToneMuted};
            break;
        case model::ToolState::Failed:
            status = {capitalized(run.reason.empty() ? std::string("trap failed") : run.reason), tools::kToneError};
            break;
        case model::ToolState::Idle:
            status = {"Off, no port open", tools::kToneMuted};
            break;
    }
    set_text_if_changed(status_, status_shown_, status.text);
    if (status.tone != status_tone_) {
        lv_obj_set_style_text_color(status_, tone_color(theme(), status.tone), 0);
        status_tone_ = status.tone;
    }
}

void TrapScreen::refresh_keys() {
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    const int32_t run_key = running() ? 1 : 0;
    if (run_key != shown_run_key_) {
        shown_run_key_ = run_key;
        bar->set_key(2, {run_key == 1 ? "Stop" : "Start", [this] { start_or_stop(); }, true});
    }
    const int32_t info_key = view_model().trap().hits.empty() ? 0 : 1;
    if (info_key != shown_info_key_) {
        shown_info_key_ = info_key;
        bar->set_key(1, {"Info", [this] { toggle_detail(); }, false, info_key != 0});
    }
}

void TrapScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<TrapScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

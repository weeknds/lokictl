/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "watch_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"
#include "trap_data.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the summary row at the top, one line per alert under it and the status
// line at the bottom, like the Discover page. The detail view takes the
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

// Who an alert row names: its IPv4 address, else its hardware address (an
// IPv6 address is too long for a row; the detail shows it).
std::string row_who(const model::WatchAlert& alert) {
    if (!alert.address.empty() && alert.address.find(':') == std::string::npos) {
        return alert.address;
    }
    return alert.hardware.empty() ? alert.address : alert.hardware;
}

// The one line of facts the detail shows under who it is about.
std::string fact_text(const model::WatchAlert& alert, bool known) {
    switch (alert.kind) {
        case model::WatchAlertKind::NewDevice:
            return known ? "Marked as known" : "Not on your known list";
        case model::WatchAlertKind::FakeRouter:
            return "Router was " + alert.before;
        case model::WatchAlertKind::SameAddress:
            return "Also " + alert.before;
        case model::WatchAlertKind::SecondDhcp:
            return "First server " + alert.before;
        case model::WatchAlertKind::Ipv6Router:
            return alert.hardware.empty() ? std::string("Router announcement") : "Sent by " + alert.hardware;
        case model::WatchAlertKind::Scan:
            return "Tried " + std::to_string(alert.count) + " ports";
        case model::WatchAlertKind::ArpFlood:
            return std::to_string(alert.count) + " ARP frames in 1 s";
        case model::WatchAlertKind::Trap: {
            const model::TrapService* service = model::trap_service(static_cast<uint16_t>(alert.count));
            return std::string("Tried ") + (service ? service->word : "a port") + ", port " +
                   std::to_string(alert.count);
        }
    }
    return {};
}

bool serious(model::WatchAlertKind kind) {
    return kind != model::WatchAlertKind::NewDevice;
}

} // namespace

WatchScreen::WatchScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool WatchScreen::handles(model::AppPage page) {
    return page == model::AppPage::Watch;
}

view::widgets::HelpContent WatchScreen::help_content() {
    return {"WATCH",
            {
                {"ESC / 4", "Back (Watch goes on)"},
                {"R", "Save a report for a PC"},
                {"ENTER / 5", "What the alert means"},
                {"6", "Watch, sends nothing"},
                {"7", "Mark device as known"},
            }};
}

view::widgets::HelpContent WatchScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet WatchScreen::soft_keys() {
    auto& view_model = this->view_model();
    return {{
        {"Back",
         [&view_model] {
             if (view_model.watch_detail()) {
                 view_model.set_watch_detail(false);
             } else {
                 view_model.go_back();
             }
         }},
        {"Info", [this] { toggle_detail(); }, false, false},
        {"Start", [this] { watch_or_stop(); }, true},
        {"Known", [this] { mark_known(); }, false, false},
        {"Trap", [&view_model] { view_model.open_trap(); }},
    }};
}

std::string WatchScreen::uplink() const {
    const std::string& name = view_model().home().uplink_name;
    return model::valid_interface_name(name) ? name : std::string();
}

bool WatchScreen::watching() const {
    return view_model().watch().run.state == model::ToolState::Running;
}

bool WatchScreen::can_watch() const {
    return watching() || !uplink().empty();
}

void WatchScreen::watch_or_stop() {
    if (watching()) {
        view_model().request_watch(viewmodel::BaseViewModel::WatchRequest::Stop);
    } else if (can_watch()) {
        view_model().set_watch_selection(0);
        view_model().set_watch_detail(false);
        view_model().request_watch(viewmodel::BaseViewModel::WatchRequest::Start);
    }
}

int32_t WatchScreen::selected() const {
    const auto count = static_cast<int32_t>(view_model().watch().run.alerts.size());
    return std::clamp<int32_t>(view_model().watch_selection(), 0, std::max<int32_t>(count - 1, 0));
}

void WatchScreen::toggle_detail() {
    if (view_model().watch_detail()) {
        view_model().set_watch_detail(false);
    } else if (!view_model().watch().run.alerts.empty()) {
        view_model().set_watch_detail(true);
    }
}

bool WatchScreen::can_mark_known() const {
    const model::WatchData& data = view_model().watch();
    if (data.run.alerts.empty()) {
        return false;
    }
    const model::WatchAlert& alert = data.run.alerts[static_cast<std::size_t>(selected())];
    return alert.kind == model::WatchAlertKind::NewDevice && !alert.hardware.empty() &&
           !model::watch_known(data, alert.hardware);
}

void WatchScreen::mark_known() {
    if (!can_mark_known()) {
        return;
    }
    const model::WatchAlert& alert = view_model().watch().run.alerts[static_cast<std::size_t>(selected())];
    view_model().request_watch(viewmodel::BaseViewModel::WatchRequest::MarkKnown, alert.hardware);
}

void WatchScreen::select(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().watch().run.alerts.size());
    if (count == 0) {
        return;
    }
    view_model().set_watch_selection(std::clamp<int32_t>(selected() + delta, 0, count - 1));
}

bool WatchScreen::on_back() {
    if (view_model().watch_detail()) {
        view_model().set_watch_detail(false);
        return true;
    }
    return false;
}

bool WatchScreen::on_key(uint32_t key) {
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
        case 'r':
        case 'R':
            view_model().request_report();
            return true;
        default:
            return false;
    }
}

void WatchScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    // The summary row: the captions stand on the count's baseline.
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    interface_caption_ = plain_label(content, "");
    view::style::caption(interface_caption_, theme, "");
    lv_obj_set_pos(interface_caption_, kSideMargin, kSummaryTop + small_drop);
    count_ = plain_label(content, "");
    view::style::text(count_, theme, view::FontRole::Mono, theme.colors.accent2);
    known_caption_ = plain_label(content, "");
    view::style::caption(known_caption_, theme, "");
    lv_obj_align(known_caption_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kSummaryTop + small_drop);

    // One line per alert; the rows end above the status line, whatever the
    // theme's line heights. A selected row uses the Strong font, which has
    // the Body font's size.
    const int32_t row_height = std::max(lv_font_get_line_height(body), lv_font_get_line_height(strong)) + 2;
    const int32_t list_top = kSummaryTop + lv_font_get_line_height(mono) + kListGap;
    visible_rows_ = std::clamp<int32_t>((kStatusTop - 1 - list_top + kRowGap) / (row_height + kRowGap), 1,
                                        static_cast<int32_t>(kRows));
    const int32_t pad = theme.metrics.space_xs + theme.metrics.border_width;
    const int32_t inner = kWidth - 2 * pad;
    const int32_t time_width = text_width("00:00", small);
    const int32_t who_width = text_width("00:00:00:00:00:00", small);
    const int32_t name_x = time_width + kColumnGap;
    const int32_t name_width = std::max<int32_t>(inner - name_x - kColumnGap - who_width, 0);
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
        row.name = plain_label(row.box, "");
        lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.name, name_width, row_height - 2);
        lv_obj_set_pos(row.name, name_x, 1);
        row.who = plain_label(row.box, "");
        lv_obj_set_style_text_font(row.who, small, 0);
        lv_label_set_long_mode(row.who, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.who, who_width, lv_font_get_line_height(small));
        lv_obj_set_style_text_align(row.who, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(row.who, inner - who_width, 1 + std::max<int32_t>(small_down, 0));
        lv_obj_add_flag(row.box, LV_OBJ_FLAG_HIDDEN);
        row.hidden = true;
    }

    note_ = plain_label(content, "");
    view::style::text(note_, theme, view::FontRole::Mono, theme.colors.muted);
    lv_obj_set_width(note_, kWidth);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(note_, kSideMargin, list_top);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

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
    lv_subject_add_observer_obj(view_model().watch_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().watch_selection_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().watch_detail_subject(), observer_cb, content, this);
    // The uplink comes and goes with the network.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
}

void WatchScreen::refresh() {
    const model::WatchRun& run = view_model().watch().run;
    // New alerts go on top; a picked alert other than the newest stays
    // picked.
    if (raised_seen_ >= 0 && run.raised > raised_seen_ &&
        (view_model().watch_selection() > 0 || view_model().watch_detail())) {
        const int32_t moved = run.raised - raised_seen_;
        raised_seen_ = run.raised;
        view_model().set_watch_selection(view_model().watch_selection() + moved);  // calls refresh() again
        return;
    }
    raised_seen_ = run.raised;
    if (view_model().watch_detail() && run.alerts.empty()) {
        view_model().set_watch_detail(false);  // calls refresh() again
        return;
    }
    const bool detail = view_model().watch_detail();
    if (static_cast<int32_t>(detail) != detail_shown_) {
        detail_shown_ = detail ? 1 : 0;
        set_hidden(detail_, !detail);
        set_hidden(interface_caption_, detail);
        set_hidden(count_, detail);
        set_hidden(known_caption_, detail);
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
        note_shown_.clear();
        refresh_detail();
    } else {
        refresh_list();
        refresh_status();
    }
    refresh_keys();
}

void WatchScreen::refresh_list() {
    const auto& theme = this->theme();
    const model::WatchData& data = view_model().watch();
    const model::WatchRun& run = data.run;

    const std::string interface_name = run.interface_name.empty() ? uplink() : run.interface_name;
    const std::string count = std::to_string(run.alerts.size()) + (run.alerts.size() == 1 ? " alert" : " alerts");
    const bool interface_changed = interface_name != interface_shown_;
    if (interface_changed) {
        interface_shown_ = interface_name;
        view::style::caption(interface_caption_, theme, interface_name.c_str());
    }
    if (set_text_if_changed(count_, count_shown_, count) || interface_changed) {
        lv_obj_update_layout(interface_caption_);
        const int32_t left = interface_name.empty() ? 0 : lv_obj_get_width(interface_caption_) + kCaptionGap;
        lv_obj_set_pos(count_, kSideMargin + left, kSummaryTop);
    }
    const std::string known = std::to_string(data.known.size()) + " known";
    if (known != known_shown_) {
        known_shown_ = known;
        view::style::caption(known_caption_, theme, known.c_str());
    }

    const auto total = static_cast<int32_t>(run.alerts.size());
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
        const model::WatchAlert& alert = run.alerts[static_cast<std::size_t>(place)];
        const int32_t style = place == picked ? 1 : 0;
        if (style != row.style_shown) {
            view::style::list_row(row.box, theme, style == 1);
            row.style_shown = style;
            row.tone_shown = -1;
        }
        set_text_if_changed(row.time, row.time_shown, alert.time);
        set_text_if_changed(row.name, row.name_shown, model::watch_alert_name(alert.kind));
        set_text_if_changed(row.who, row.who_shown, row_who(alert));
        // On the highlight every part takes the row's colour.
        const int32_t tone = style == 1 ? -2 : (serious(alert.kind) ? tools::kToneWarning : tools::kToneText);
        if (tone != row.tone_shown) {
            if (tone == -2) {
                lv_obj_remove_local_style_prop(row.time, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.name, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.who, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(row.time, theme.colors.muted, 0);
                lv_obj_set_style_text_color(row.name, tone_color(theme, tone), 0);
                lv_obj_set_style_text_color(row.who, theme.colors.text, 0);
            }
            row.tone_shown = tone;
        }
    }

    std::string note;
    if (total == 0) {
        if (run.state == model::ToolState::Running) {
            note = "No alerts yet";
        } else if (run.state == model::ToolState::Idle) {
            note = uplink().empty() ? "No network to watch" : "Not watching";
        } else {
            note = "No alerts";
        }
    }
    if (note != note_shown_) {
        note_shown_ = note;
        lv_label_set_text(note_, note.c_str());
        set_hidden(note_, note.empty());
    }
}

void WatchScreen::refresh_detail() {
    const model::WatchData& data = view_model().watch();
    const model::WatchAlert& alert = data.run.alerts[static_cast<std::size_t>(selected())];
    const auto& theme = this->theme();
    if (set_text_if_changed(detail_name_, detail_name_shown_, model::watch_alert_name(alert.kind))) {
        lv_obj_set_style_text_color(detail_name_, serious(alert.kind) ? theme.colors.warning : theme.colors.accent,
                                    0);
    }
    set_text_if_changed(detail_time_, detail_time_shown_, alert.time);
    std::string who = alert.address;
    // An IPv6 address fills the line; its hardware address is in the facts.
    if (!alert.hardware.empty() && alert.address.find(':') == std::string::npos) {
        who += (who.empty() ? "" : "  ") + alert.hardware;
    }
    set_text_if_changed(detail_who_, detail_who_shown_, who.empty() ? std::string("not known") : who);
    set_text_if_changed(detail_fact_, detail_fact_shown_,
                        fact_text(alert, model::watch_known(data, alert.hardware)));
    set_text_if_changed(detail_about_, detail_about_shown_, model::watch_alert_about(alert.kind));
}

void WatchScreen::refresh_status() {
    const model::WatchRun& run = view_model().watch().run;
    tools::ToneText status;
    switch (run.state) {
        case model::ToolState::Running:
            if (run.learn_left_s > 0) {
                status = {"Learning devices, " + duration_text(run.learn_left_s) + " left", tools::kToneAccent};
            } else {
                status = {"Watching " + duration_text(run.elapsed_s) + ", sends none", tools::kToneAccent};
            }
            break;
        case model::ToolState::Stopped:
        case model::ToolState::Done:
            status = {"Stopped after " + duration_text(run.elapsed_s), tools::kToneMuted};
            break;
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                status = {"Capture not allowed", tools::kToneError};
            } else {
                status = {capitalized(run.reason.empty() ? std::string("watch failed") : run.reason),
                          tools::kToneError};
            }
            break;
        case model::ToolState::Idle:
            status = uplink().empty() ? tools::ToneText{"Join a network first", tools::kToneWarning}
                                      : tools::ToneText{"Listens only, sends nothing", tools::kToneMuted};
            break;
    }
    set_text_if_changed(status_, status_shown_, status.text);
    if (status.tone != status_tone_) {
        lv_obj_set_style_text_color(status_, tone_color(theme(), status.tone), 0);
        status_tone_ = status.tone;
    }
}

void WatchScreen::refresh_keys() {
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    const int32_t watch_key = watching() ? 2 : (can_watch() ? 1 : 0);
    if (watch_key != shown_watch_key_) {
        shown_watch_key_ = watch_key;
        bar->set_key(2, {watch_key == 2 ? "Stop" : "Start", [this] { watch_or_stop(); }, true, watch_key != 0});
    }
    const int32_t info_key = view_model().watch().run.alerts.empty() ? 0 : 1;
    if (info_key != shown_info_key_) {
        shown_info_key_ = info_key;
        bar->set_key(1, {"Info", [this] { toggle_detail(); }, false, info_key != 0});
    }
    const int32_t known_key = can_mark_known() ? 1 : 0;
    if (known_key != shown_known_key_) {
        shown_known_key_ = known_key;
        bar->set_key(3, {"Known", [this] { mark_known(); }, false, known_key != 0});
    }
}

void WatchScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<WatchScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

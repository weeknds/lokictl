/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "health_screen.h"

#include "health_check.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the summary row at the top, one line per finding under it and the status
// line at the bottom, like the Watch page. The detail view takes the whole
// area.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kSummaryTop    = 1;
constexpr int32_t kListGap       = 3;
constexpr int32_t kRowGap        = 1;
constexpr int32_t kStatusHeight  = 18;
constexpr int32_t kStatusTop     = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap     = 8;
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

void set_tone_if_changed(lv_obj_t* label, int32_t& shown, int32_t tone, const view::Theme& theme) {
    if (label && tone != shown) {
        lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        shown = tone;
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

int32_t level_tone(model::RiskLevel level) {
    switch (level) {
        case model::RiskLevel::Risk:
            return tools::kToneError;
        case model::RiskLevel::Warning:
            return tools::kToneWarning;
        case model::RiskLevel::Info:
            return tools::kToneMuted;
    }
    return tools::kToneMuted;
}

// The summary's colour: the worst level found, green when a finished check
// found nothing.
int32_t summary_tone(const model::HealthRun& run, const model::HealthCounts& counts) {
    if (counts.risks > 0) {
        return tools::kToneError;
    }
    if (counts.warnings > 0) {
        return tools::kToneWarning;
    }
    return run.state == model::ToolState::Done ? tools::kToneSuccess : tools::kToneAccent;
}

// What the status line says while another tool runs.
const char* other_tool_text(const model::ToolsData& data) {
    if (data.ping.state == model::ToolState::Running) {
        return "Ping is running";
    }
    if (data.dns.state == model::ToolState::Running) {
        return "DNS lookup is running";
    }
    if (data.trace.state == model::ToolState::Running) {
        return "Trace is running";
    }
    if (data.listen.state == model::ToolState::Running) {
        return "Discover is listening";
    }
    if (data.probe.state == model::ToolState::Running) {
        return "Discover is probing";
    }
    if (data.speed.state == model::ToolState::Running) {
        return "Speed test is running";
    }
    return "Port scan is running";
}

} // namespace

HealthScreen::HealthScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

HealthScreen::~HealthScreen() {
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
        confirm_timer_ = nullptr;
    }
}

bool HealthScreen::handles(model::AppPage page) {
    return page == model::AppPage::Health;
}

view::widgets::HelpContent HealthScreen::help_content() {
    return {"HEALTH",
            {
                {"6 twice", "Check network, sends"},
                {"ENTER / 5", "What it means, the fix"},
                {"8 / R", "Save a report for a PC"},
                {"Risk/Warn", "Fix soon / worth a look"},
                {"Info", "Good to know"},
            }};
}

view::widgets::HelpContent HealthScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet HealthScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh_keys() switches them on and off and turns Check into Stop.
    return {{
        {"Back",
         [this] {
             if (!on_back()) {
                 view_model().go_back();
             }
         }},
        {"Info", [this] { toggle_detail(); }, false, false},
        {"Check", [this] { check_or_stop(); }, true},
        {},
        {"Save", [this] { save_report(); }},
    }};
}

std::string HealthScreen::uplink() const {
    const std::string& name = view_model().home().uplink_name;
    return model::valid_interface_name(name) ? name : std::string();
}

std::string HealthScreen::home_address() const {
    const std::string& address = view_model().home().uplink_address;
    if (!model::is_ipv4_text(address)) {
        return {};
    }
    const int first = std::stoi(address);
    const int second = std::stoi(address.substr(address.find('.') + 1));
    const bool home = first == 10 || (first == 172 && second >= 16 && second <= 31) || (first == 192 && second == 168);
    return home ? address : std::string();
}

bool HealthScreen::checking() const {
    return view_model().tools().health.state == model::ToolState::Running;
}

bool HealthScreen::can_check() const {
    if (checking()) {
        return true;
    }
    return !model::tool_running(view_model().tools()) && !uplink().empty();
}

void HealthScreen::check_or_stop() {
    if (checking()) {
        cancel_confirm();
        view_model().request_tool_stop();
        return;
    }
    if (!can_check()) {
        cancel_confirm();
        return;
    }
    if (!confirming_) {
        // What would be asked; nothing is sent.
        confirming_ = true;
        if (!confirm_timer_) {
            confirm_timer_ = lv_timer_create(confirm_timer_cb, kConfirmMs, this);
        }
        view_model().set_health_detail(false);
        refresh();
        return;
    }
    // The second 6: the confirmation, given for this run only.
    const bool may_check = !home_address().empty();
    cancel_confirm();
    if (may_check) {
        view_model().set_health_selection(0);
        view_model().set_health_detail(false);
        view_model().request_tool_run(model::ToolKind::Health, uplink());
    }
}

void HealthScreen::cancel_confirm() {
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
        confirm_timer_ = nullptr;
    }
    if (confirming_) {
        confirming_ = false;
        refresh();
    }
}

void HealthScreen::confirm_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<HealthScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->cancel_confirm();
    }
}

int32_t HealthScreen::selected() const {
    const auto count = static_cast<int32_t>(view_model().tools().health.findings.size());
    return std::clamp<int32_t>(view_model().health_selection(), 0, std::max<int32_t>(count - 1, 0));
}

void HealthScreen::toggle_detail() {
    if (confirming_) {
        cancel_confirm();  // like any other key: only cancels
        return;
    }
    if (view_model().health_detail()) {
        view_model().set_health_detail(false);
    } else if (!view_model().tools().health.findings.empty()) {
        view_model().set_health_detail(true);
    }
}

void HealthScreen::select(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().tools().health.findings.size());
    if (count == 0) {
        return;
    }
    view_model().set_health_selection(std::clamp<int32_t>(selected() + delta, 0, count - 1));
}

bool HealthScreen::on_back() {
    if (confirming_) {
        cancel_confirm();
        return true;
    }
    if (view_model().health_detail()) {
        view_model().set_health_detail(false);
        return true;
    }
    return false;
}

bool HealthScreen::on_key(uint32_t key) {
    if (confirming_) {
        // Any other key cancels the confirmation, and does nothing else.
        cancel_confirm();
        return true;
    }
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
            save_report();
            return true;
        default:
            return false;
    }
}

void HealthScreen::save_report() {
    if (confirming_) {
        cancel_confirm();  // like any other key: only cancels
        return;
    }
    view_model().request_report();
}

void HealthScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    // The summary row: the caption stands on the summary's baseline.
    const int32_t small_drop = baseline_from_top(mono) - baseline_from_top(small);
    interface_caption_ = plain_label(content, "");
    view::style::caption(interface_caption_, theme, "");
    lv_obj_set_pos(interface_caption_, kSideMargin, kSummaryTop + small_drop);
    summary_ = plain_label(content, "");
    view::style::text(summary_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_label_set_long_mode(summary_, LV_LABEL_LONG_DOT);

    // One line per finding; the rows end above the status line, whatever
    // the theme's line heights. A selected row uses the Strong font, which
    // has the Body font's size.
    const int32_t row_height = std::max(lv_font_get_line_height(body), lv_font_get_line_height(strong)) + 2;
    const int32_t list_top = kSummaryTop + lv_font_get_line_height(mono) + kListGap;
    visible_rows_ = std::clamp<int32_t>((kStatusTop - 1 - list_top + kRowGap) / (row_height + kRowGap), 1,
                                        static_cast<int32_t>(kRows));
    const int32_t pad = theme.metrics.space_xs + theme.metrics.border_width;
    const int32_t inner = kWidth - 2 * pad;
    const int32_t tag_width = std::max({text_width("Risk", small), text_width("Warn", small),
                                        text_width("Info", small)});
    const int32_t who_width = std::max(text_width("255.255.255.255", small), text_width("this device", small));
    const int32_t name_x = tag_width + kColumnGap;
    row_inner_ = inner;
    name_x_ = name_x;
    // The name gets what the row's own "who" leaves (refresh_list()).
    const int32_t name_width = std::max<int32_t>(inner - name_x - kColumnGap - who_width, 0);
    const int32_t small_down = baseline_from_top(body) - baseline_from_top(small);
    for (int32_t index = 0; index < visible_rows_; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        row.box = plain_object(content);
        lv_obj_set_pos(row.box, kSideMargin, list_top + index * (row_height + kRowGap));
        lv_obj_set_size(row.box, kWidth, row_height);
        view::style::list_row(row.box, theme, false);
        row.tag = plain_label(row.box, "");
        lv_obj_set_style_text_font(row.tag, small, 0);
        lv_obj_set_pos(row.tag, 0, 1 + std::max<int32_t>(small_down, 0));
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

    // In place of the list: a note, or the confirmation under it.
    note_ = plain_label(content, "");
    view::style::text(note_, theme, view::FontRole::Mono, theme.colors.muted);
    lv_obj_set_width(note_, kWidth);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(note_, kSideMargin, list_top);
    lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
    confirm_ = plain_label(content, "");
    view::style::text(confirm_, theme, view::FontRole::Mono, theme.colors.warning);
    lv_obj_set_width(confirm_, kWidth);
    lv_label_set_long_mode(confirm_, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(confirm_, LV_OBJ_FLAG_HIDDEN);

    // The status line, with the Sent count and its caption at the right.
    status_ = plain_label(content, "");
    view::style::text(status_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_, kSideMargin, kStatusTop);
    sent_ = plain_label(content, "");
    view::style::text(sent_, theme, view::FontRole::Mono, theme.colors.text);
    lv_obj_align(sent_, LV_ALIGN_TOP_RIGHT, -kSideMargin, kStatusTop);
    sent_caption_ = plain_label(content, "");
    view::style::caption(sent_caption_, theme, "sent");

    // The detail view, over the whole area.
    detail_ = plain_object(content);
    lv_obj_set_pos(detail_, 0, 0);
    lv_obj_set_size(detail_, view::kScreenWidth, kContentHeight);
    detail_name_ = plain_label(detail_, "");
    view::style::text(detail_name_, theme, view::FontRole::Strong, theme.colors.warning);
    lv_obj_set_pos(detail_name_, kSideMargin, kSummaryTop);
    detail_level_ = plain_label(detail_, "");
    view::style::text(detail_level_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(detail_level_, LV_ALIGN_TOP_RIGHT, -kSideMargin,
                 kSummaryTop + std::max<int32_t>(baseline_from_top(strong) - baseline_from_top(small), 0));
    int32_t top = kSummaryTop + lv_font_get_line_height(strong) + kDetailGap;
    detail_where_ = plain_label(detail_, "");
    view::style::text(detail_where_, theme, view::FontRole::Mono);
    lv_label_set_long_mode(detail_where_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(detail_where_, kWidth, lv_font_get_line_height(mono));
    lv_obj_set_pos(detail_where_, kSideMargin, top);
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
    detail_fix_ = plain_label(detail_, "");
    view::style::text(detail_fix_, theme, view::FontRole::Small, theme.colors.accent);
    lv_label_set_long_mode(detail_fix_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail_fix_, kWidth);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().health_selection_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().health_detail_subject(), observer_cb, content, this);
    // The uplink comes and goes with the network.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
}

void HealthScreen::refresh() {
    const model::HealthRun& run = view_model().tools().health;
    if (view_model().health_detail() && (run.findings.empty() || confirming_)) {
        view_model().set_health_detail(false);  // calls refresh() again
        return;
    }
    const bool detail = view_model().health_detail();
    if (static_cast<int32_t>(detail) != detail_shown_) {
        detail_shown_ = detail ? 1 : 0;
        set_hidden(detail_, !detail);
        set_hidden(interface_caption_, detail);
        set_hidden(summary_, detail);
        set_hidden(status_, detail);
        set_hidden(sent_, detail);
        set_hidden(sent_caption_, detail);
    }
    if (detail) {
        for (Row& row : rows_) {
            if (row.box && !row.hidden) {
                set_hidden(row.box, true);
                row.hidden = true;
            }
        }
        set_hidden(note_, true);
        set_hidden(confirm_, true);
        note_shown_.clear();
        confirm_shown_.clear();
        refresh_detail();
    } else {
        refresh_list();
        refresh_status();
    }
    refresh_keys();
}

void HealthScreen::refresh_list() {
    const auto& theme = this->theme();
    const model::HealthRun& run = view_model().tools().health;
    const model::HealthCounts counts = model::health_counts(run.findings);

    std::string interface_name = run.state != model::ToolState::Idle ? run.interface_name : uplink();
    if (interface_name.empty()) {
        interface_name = "no network";
    }
    if (interface_name != interface_shown_) {
        interface_shown_ = interface_name;
        view::style::caption(interface_caption_, theme, interface_name.c_str());
        lv_obj_update_layout(interface_caption_);
        const int32_t left = lv_obj_get_width(interface_caption_) + kCaptionGap;
        lv_obj_set_pos(summary_, kSideMargin + left, kSummaryTop);
        lv_obj_set_width(summary_, std::max<int32_t>(kWidth - left, 0));
    }
    std::string summary;
    int32_t tone = tools::kToneMuted;
    if (run.state == model::ToolState::Idle) {
        summary = "not checked yet";
    } else if (run.state == model::ToolState::Failed && run.findings.empty()) {
        summary = "check failed";
        tone = tools::kToneError;
    } else if (run.state == model::ToolState::Running && run.findings.empty()) {
        summary = "checking";
    } else {
        summary = model::health_summary(counts);
        tone = summary_tone(run, counts);
    }
    set_text_if_changed(summary_, summary_shown_, summary);
    set_tone_if_changed(summary_, summary_tone_, tone, theme);

    const auto total = confirming_ ? 0 : static_cast<int32_t>(run.findings.size());
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
        const model::HealthFinding& finding = run.findings[static_cast<std::size_t>(place)];
        const int32_t style = place == picked ? 1 : 0;
        if (style != row.style_shown) {
            view::style::list_row(row.box, theme, style == 1);
            row.style_shown = style;
            row.tone_shown = -1;
        }
        set_text_if_changed(row.tag, row.tag_shown, model::health_level_tag(finding.level));
        set_text_if_changed(row.name, row.name_shown, finding.title);
        if (set_text_if_changed(row.who, row.who_shown, finding.who) || row.name_width < 0) {
            const int32_t who_width = text_width(finding.who.c_str(), theme.font(view::FontRole::Small));
            row.name_width = std::max<int32_t>(row_inner_ - name_x_ - kColumnGap - who_width, 0);
            lv_obj_set_width(row.name, row.name_width);
        }
        // On the highlight every part takes the row's colour.
        const int32_t row_tone = style == 1 ? -2 : level_tone(finding.level);
        if (row_tone != row.tone_shown) {
            if (row_tone == -2) {
                lv_obj_remove_local_style_prop(row.tag, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.name, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.who, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(row.tag, tone_color(theme, row_tone), 0);
                lv_obj_set_style_text_color(row.name, theme.colors.text, 0);
                lv_obj_set_style_text_color(row.who, theme.colors.muted, 0);
            }
            row.tone_shown = row_tone;
        }
    }

    // In place of the list: what the second 6 would do, else a note when
    // there is no finding to show.
    std::string note;
    std::string confirm;
    int32_t note_tone = tools::kToneMuted;
    if (confirming_) {
        const std::string& address = view_model().home().uplink_address;
        const std::string allowed = home_address();
        note_tone = tools::kToneText;
        if (!model::is_ipv4_text(address)) {
            note = "No IPv4 address on " + uplink() + " yet.";
        } else if (allowed.empty()) {
            note = "Not a private home network,\nso the check stays off.";
        } else {
            const std::string prefix = allowed.substr(0, allowed.rfind('.') + 1);
            note = "Asks " + prefix + "1-254, then 50\nports on up to " + std::to_string(model::kHealthMaxHosts) +
                   " devices.";
            confirm = "Only networks you own or may test.\nPress 6 again to check.";
        }
    } else if (total == 0) {
        switch (run.state) {
            case model::ToolState::Idle:
                note = uplink().empty() ? "No network to check" : "Press 6 to check your network";
                break;
            case model::ToolState::Running:
                note = "Nothing found so far";
                break;
            case model::ToolState::Done:
                note = "Nothing to flag";
                note_tone = tools::kToneSuccess;
                break;
            case model::ToolState::Stopped:
                note = "Nothing found before the stop";
                break;
            case model::ToolState::Failed:
                break;  // the status line says why
        }
    }
    set_hidden(note_, note.empty());
    bool note_changed = false;
    if (!note.empty()) {
        note_changed = set_text_if_changed(note_, note_shown_, note);
        set_tone_if_changed(note_, note_tone_, note_tone, theme);
    }
    set_hidden(confirm_, confirm.empty());
    // The confirmation stands right under the note, however many lines it took.
    if (!confirm.empty() && (set_text_if_changed(confirm_, confirm_shown_, confirm) || note_changed)) {
        lv_obj_update_layout(note_);
        lv_obj_set_pos(confirm_, kSideMargin, lv_obj_get_y(note_) + lv_obj_get_height(note_));
    }
}

void HealthScreen::refresh_detail() {
    const model::HealthRun& run = view_model().tools().health;
    const model::HealthFinding& finding = run.findings[static_cast<std::size_t>(selected())];
    const auto& theme = this->theme();
    set_text_if_changed(detail_name_, detail_name_shown_, finding.title);
    // Info's name in the text colour; the others in their level's.
    set_tone_if_changed(detail_name_, detail_tone_,
                        finding.level == model::RiskLevel::Info ? tools::kToneText : level_tone(finding.level), theme);
    set_text_if_changed(detail_level_, detail_level_shown_, model::risk_level_name(finding.level));
    set_text_if_changed(detail_where_, detail_where_shown_, finding.where);
    set_text_if_changed(detail_fact_, detail_fact_shown_, finding.fact);
    const bool about_changed = set_text_if_changed(detail_about_, detail_about_shown_, model::health_about(finding));
    const std::string fix = model::health_fix(finding);
    if (set_text_if_changed(detail_fix_, detail_fix_shown_, fix.empty() ? std::string() : "Fix: " + fix) ||
        about_changed) {
        // The fix stands right under the explanation, however many lines it took.
        lv_obj_update_layout(detail_about_);
        lv_obj_set_pos(detail_fix_, kSideMargin,
                       lv_obj_get_y(detail_about_) + lv_obj_get_height(detail_about_) + kDetailGap);
    }
}

void HealthScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    const model::HealthRun& run = data.health;
    tools::ToneText status;
    switch (run.state) {
        case model::ToolState::Running:
            if (run.step == model::HealthStep::Sweep) {
                status = {"Finding devices, " + std::to_string(run.asked) + " of " + std::to_string(run.total),
                          tools::kToneAccent};
            } else {
                const int32_t at = std::min(run.scanned + 1, std::max(run.hosts, 1));
                status = {"Checking device " + std::to_string(at) + " of " + std::to_string(run.hosts),
                          tools::kToneAccent};
            }
            break;
        case model::ToolState::Done:
            status = {"Checked " + std::to_string(run.scanned) + (run.scanned == 1 ? " device in " : " devices in ") +
                          duration_text(run.elapsed_s),
                      tools::kToneMuted};
            break;
        case model::ToolState::Stopped:
            if (run.step == model::HealthStep::Sweep) {
                status = {"Stopped while finding devices", tools::kToneMuted};
            } else if (run.hosts == 0) {
                status = {"Stopped", tools::kToneMuted};
            } else {
                status = {"Stopped at device " + std::to_string(std::min(run.scanned + 1, run.hosts)) + " of " +
                              std::to_string(run.hosts),
                          tools::kToneMuted};
            }
            break;
        case model::ToolState::Failed:
            if (run.error == model::ToolError::NotAllowed) {
                status = {"Sweep not allowed", tools::kToneError};
            } else {
                status = {capitalized(run.reason.empty() ? std::string("check failed") : run.reason),
                          tools::kToneError};
            }
            break;
        case model::ToolState::Idle:
            status = uplink().empty() ? tools::ToneText{"Join a network first", tools::kToneWarning}
                                      : tools::ToneText{"Looks for weak spots", tools::kToneMuted};
            break;
    }
    if (confirming_) {
        status = {"Nothing sent yet", tools::kToneMuted};
    } else if (!checking() && model::tool_running(data)) {
        status = {other_tool_text(data), tools::kToneMuted};
    }
    const bool sent_changed = set_text_if_changed(sent_, sent_shown_, std::to_string(data.sent));
    if (set_text_if_changed(status_, status_shown_, status.text) || sent_changed) {
        // The caption stands left of the count, on its baseline; the status
        // gets the rest of the line and ends in dots when it is longer.
        lv_obj_update_layout(sent_);
        lv_obj_align_to(sent_caption_, sent_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Mono)->base_line);
        lv_obj_update_layout(sent_caption_);
        const int32_t width =
            kWidth - lv_obj_get_width(sent_) - kCaptionGap - lv_obj_get_width(sent_caption_) - kStatusGap;
        lv_obj_set_size(status_, std::max<int32_t>(width, 0), kStatusHeight);
    }
    set_tone_if_changed(status_, status_tone_, status.tone, theme);
}

void HealthScreen::refresh_keys() {
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    const int32_t check_key = checking() ? 2 : (can_check() ? 1 : 0);
    if (check_key != shown_check_key_) {
        shown_check_key_ = check_key;
        bar->set_key(2, {check_key == 2 ? "Stop" : "Check", [this] { check_or_stop(); }, true, check_key != 0});
    }
    const int32_t info_key = view_model().tools().health.findings.empty() ? 0 : 1;
    if (info_key != shown_info_key_) {
        shown_info_key_ = info_key;
        bar->set_key(1, {"Info", [this] { toggle_detail(); }, false, info_key != 0});
    }
}

void HealthScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<HealthScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

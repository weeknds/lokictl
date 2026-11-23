/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "leak_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// one line per check from the top, the status line at the bottom. The
// detail view takes the whole area.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kListTop       = 2;
constexpr int32_t kRowGap        = 2;
constexpr int32_t kStatusHeight  = 18;
constexpr int32_t kStatusTop     = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap     = 8;
constexpr int32_t kCaptionGap    = 5;
constexpr int32_t kDetailTop     = 1;
constexpr int32_t kDetailGap     = 3;

// The tunnel's interface, the leak test's target (platform::kVpnInterface).
constexpr const char* kTunnel = "lokiwg0";

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
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

int32_t verdict_tone(model::LeakVerdict verdict) {
    switch (verdict) {
        case model::LeakVerdict::Pass:
            return tools::kToneSuccess;
        case model::LeakVerdict::Fail:
            return tools::kToneError;
        case model::LeakVerdict::Unknown:
            return tools::kToneWarning;
        case model::LeakVerdict::Info:
            return tools::kToneText;
        case model::LeakVerdict::Waiting:
            break;
    }
    return tools::kToneMuted;
}

// The detail's word for a verdict, right of the check's name.
const char* verdict_word(model::LeakVerdict verdict) {
    switch (verdict) {
        case model::LeakVerdict::Pass:
            return "no leak";
        case model::LeakVerdict::Fail:
            return "LEAK";
        case model::LeakVerdict::Unknown:
            return "can't tell";
        case model::LeakVerdict::Info:
            return "info";
        case model::LeakVerdict::Waiting:
            break;
    }
    return "not checked";
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
    if (data.health.state == model::ToolState::Running) {
        return "Health check is running";
    }
    if (data.signin.state == model::ToolState::Running) {
        return "Sign-in check is running";
    }
    return "Port scan is running";
}

} // namespace

LeakScreen::LeakScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool LeakScreen::handles(model::AppPage page) {
    return page == model::AppPage::Leak;
}

view::widgets::HelpContent LeakScreen::help_content() {
    return {"LEAK",
            {
                {"ESC / 4", "Back (a test goes on)"},
                {"ENTER / 5", "More, and what to do"},
                {"6", "Run / stop"},
                {"VPN off", "6 notes your own address"},
                {"Sent", "One request per run"},
            }};
}

view::widgets::HelpContent LeakScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet LeakScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh_keys() sets key 6 for the run.
    auto& view_model = this->view_model();
    return {{
        {"Back", [this, &view_model] {
             if (!on_back()) {
                 view_model.go_back();
             }
         }},
        {"Info", [this] { toggle_detail(); }},
        {"Run", [this] { run_or_stop(); }, true},
        {},
        {},
    }};
}

const model::LeakRun& LeakScreen::shown_run() const {
    static const model::LeakRun kNone;
    const model::LeakRun& run = view_model().tools().leak;
    if (run.state != model::ToolState::Running && run.state != model::ToolState::Idle &&
        !model::leak_run_current(run, model::leak_local(view_model().home()))) {
        return kNone;
    }
    return run;
}

bool LeakScreen::this_running() const {
    return view_model().tools().leak.state == model::ToolState::Running;
}

bool LeakScreen::any_running() const {
    return model::tool_running(view_model().tools());
}

bool LeakScreen::can_run() const {
    return !any_running() && view_model().home().vpn.state != model::VpnState::Connecting;
}

void LeakScreen::run_or_stop() {
    if (this_running()) {
        view_model().request_tool_stop();
        return;
    }
    if (!can_run()) {
        return;
    }
    view_model().request_tool_run(model::ToolKind::Leak, kTunnel);
}

void LeakScreen::toggle_detail() {
    view_model().set_leak_detail(!view_model().leak_detail());
}

int32_t LeakScreen::selected() const {
    return std::clamp<int32_t>(view_model().leak_selection(), 0, static_cast<int32_t>(model::kLeakCheckCount) - 1);
}

void LeakScreen::select(int32_t delta) {
    const int32_t next =
        std::clamp<int32_t>(selected() + delta, 0, static_cast<int32_t>(model::kLeakCheckCount) - 1);
    view_model().set_leak_selection(next);
}

bool LeakScreen::on_back() {
    if (view_model().leak_detail()) {
        view_model().set_leak_detail(false);
        return true;
    }
    return false;
}

bool LeakScreen::on_key(uint32_t key) {
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

void LeakScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);

    // One line per check. A selected row uses the Strong font, which has
    // the Body font's size.
    const int32_t row_height = std::max(lv_font_get_line_height(body), lv_font_get_line_height(strong)) + 2;
    const int32_t pad = theme.metrics.space_xs + theme.metrics.border_width;
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        Row& row = rows_[index];
        row.box = plain_object(content);
        lv_obj_set_pos(row.box, kSideMargin, kListTop + static_cast<int32_t>(index) * (row_height + kRowGap));
        lv_obj_set_size(row.box, kWidth, row_height);
        view::style::list_row(row.box, theme, false);
        row.text = plain_label(row.box, "");
        lv_label_set_long_mode(row.text, LV_LABEL_LONG_DOT);
        lv_obj_set_size(row.text, kWidth - 2 * pad, row_height - 2);
        lv_obj_set_pos(row.text, 0, 1);
    }

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

    // The detail view, over the whole area: the check's name with its
    // verdict at the right, the facts, then what it means and what to do.
    detail_ = plain_object(content);
    lv_obj_set_pos(detail_, 0, 0);
    lv_obj_set_size(detail_, view::kScreenWidth, kContentHeight);
    detail_title_ = plain_label(detail_, "");
    view::style::text(detail_title_, theme, view::FontRole::Strong, theme.colors.text);
    lv_obj_set_pos(detail_title_, kSideMargin, kDetailTop);
    detail_verdict_ = plain_label(detail_, "");
    view::style::text(detail_verdict_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_align(detail_verdict_, LV_ALIGN_TOP_RIGHT, -kSideMargin,
                 kDetailTop + std::max<int32_t>(baseline_from_top(strong) - baseline_from_top(small), 0));
    int32_t top = kDetailTop + lv_font_get_line_height(strong) + kDetailGap;
    detail_fact_ = plain_label(detail_, "");
    view::style::text(detail_fact_, theme, view::FontRole::Mono, theme.colors.accent2);
    fix_width(detail_fact_, kWidth, mono);
    lv_obj_set_pos(detail_fact_, kSideMargin, top);
    top += lv_font_get_line_height(mono) + kDetailGap;
    detail_advice_ = plain_label(detail_, "");
    view::style::text(detail_advice_, theme, view::FontRole::Small);
    lv_label_set_long_mode(detail_advice_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail_advice_, kWidth);
    lv_obj_set_pos(detail_advice_, kSideMargin, top);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().leak_selection_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().leak_detail_subject(), observer_cb, content, this);
}

void LeakScreen::refresh_keys() {
    // 0: off (the VPN is starting, or another tool runs); 1: Run; 2: Stop.
    int32_t keys = 1;
    if (this_running()) {
        keys = 2;
    } else if (!can_run()) {
        keys = 0;
    }
    if (keys == shown_run_key_) {
        return;
    }
    shown_run_key_ = keys;
    if (auto* bar = soft_key_bar()) {
        bar->set_key(2, {keys == 2 ? "Stop" : "Run", [this] { run_or_stop(); }, true, keys != 0});
    }
}

void LeakScreen::refresh_list() {
    const auto& theme = this->theme();
    const model::LeakRun& run = shown_run();
    const int32_t picked = selected();
    for (std::size_t index = 0; index < rows_.size(); ++index) {
        Row& row = rows_[index];
        const model::LeakLine line = model::leak_line(run, static_cast<model::LeakCheck>(index));
        const int32_t style = static_cast<int32_t>(index) == picked ? 1 : 0;
        if (style != row.style_shown) {
            view::style::list_row(row.box, theme, style == 1);
            row.style_shown = style;
            row.tone_shown = -1;
        }
        set_text_if_changed(row.text, row.text_shown, line.text);
        // On the highlight the text takes the row's colour.
        const int32_t tone = style == 1 ? -2 : verdict_tone(line.verdict);
        if (tone != row.tone_shown) {
            if (tone == -2) {
                lv_obj_remove_local_style_prop(row.text, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(row.text, tone_color(theme, tone), 0);
            }
            row.tone_shown = tone;
        }
    }
}

void LeakScreen::refresh_detail() {
    const auto& theme = this->theme();
    const model::LeakLine line = model::leak_line(shown_run(), static_cast<model::LeakCheck>(selected()));
    set_text_if_changed(detail_title_, detail_title_shown_, line.title);
    set_text_if_changed(detail_verdict_, detail_verdict_shown_, verdict_word(line.verdict));
    set_text_if_changed(detail_fact_, detail_fact_shown_, line.fact);
    set_text_if_changed(detail_advice_, detail_advice_shown_, line.advice);
    const int32_t tone = verdict_tone(line.verdict);
    if (tone != detail_tone_) {
        lv_obj_set_style_text_color(detail_verdict_, tone_color(theme, tone), 0);
        detail_tone_ = tone;
    }
}

void LeakScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    const model::LeakRun& run = shown_run();
    const model::VpnStatus& vpn = view_model().home().vpn;
    tools::ToneText status{"", tools::kToneMuted};
    if (!this_running() && any_running()) {
        status = {other_tool_text(data), tools::kToneMuted};
    } else {
        switch (run.state) {
            case model::ToolState::Running:
                status = {"Checking…", tools::kToneAccent};
                break;
            case model::ToolState::Done: {
                const int32_t fails = model::leak_fail_count(run);
                const int32_t unknown = model::leak_unknown_count(run);
                if (!run.vpn_on) {
                    status = run.public_address.empty()
                                 ? tools::ToneText{"VPN off: address not noted", tools::kToneWarning}
                                 : tools::ToneText{"VPN off: your address noted", tools::kToneMuted};
                } else if (fails > 0) {
                    status = {fails == 1 ? std::string("1 leak found") : std::to_string(fails) + " leaks found",
                              tools::kToneError};
                } else if (unknown > 0) {
                    status = {"No leak seen, " + std::to_string(unknown) + " unclear", tools::kToneWarning};
                } else {
                    status = {"No leaks found", tools::kToneSuccess};
                }
                break;
            }
            case model::ToolState::Stopped:
                status = {"Stopped", tools::kToneMuted};
                break;
            case model::ToolState::Failed:
                status = {"Failed: " + (run.reason.empty() ? std::string("internal error") : run.reason),
                          tools::kToneError};
                break;
            case model::ToolState::Idle:
                if (vpn.state == model::VpnState::On) {
                    status = {"Press 6 to test the VPN", tools::kToneText};
                } else if (vpn.state == model::VpnState::Connecting) {
                    status = {"VPN is starting…", tools::kToneMuted};
                } else {
                    status = {"VPN is off: nothing to test", tools::kToneMuted};
                }
                break;
        }
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
    if (status.tone != status_tone_) {
        lv_obj_set_style_text_color(status_, tone_color(theme, status.tone), 0);
        status_tone_ = status.tone;
    }
}

void LeakScreen::refresh() {
    refresh_keys();
    const int32_t detail = view_model().leak_detail() ? 1 : 0;
    if (detail != detail_shown_) {
        detail_shown_ = detail;
        set_hidden(detail_, detail == 0);
        for (Row& row : rows_) {
            set_hidden(row.box, detail == 1);
        }
        set_hidden(status_, detail == 1);
        set_hidden(sent_, detail == 1);
        set_hidden(sent_caption_, detail == 1);
    }
    if (detail == 1) {
        refresh_detail();
    } else {
        refresh_list();
        refresh_status();
    }
}

void LeakScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<LeakScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

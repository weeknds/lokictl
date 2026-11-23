/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "signin_screen.h"

#include "home_layout.h"
#include "signin_check.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace screen {
namespace {

// Layout of the 320x140 area above the soft-key bar, like the Hotspot
// page's: the code at the left, as big as the height allows, and a column
// at its right. The detail view takes the whole area.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kCodeSize      = 136;
constexpr int32_t kCodeTop       = (kContentHeight - kCodeSize) / 2;
constexpr int32_t kColumnGap     = 10;
constexpr int32_t kPanelLeft     = kSideMargin + kCodeSize + kColumnGap;
constexpr int32_t kPanelWidth    = view::kScreenWidth - kSideMargin - kPanelLeft;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kCaptionGap    = 5;
constexpr int32_t kDetailTop     = 4;
constexpr int32_t kDetailGap     = 4;
// The detail shows at most this many lines of an address.
constexpr int32_t kAddressLines  = 3;
// The column's hint line holds about this many characters in every theme.
constexpr std::size_t kHintMax   = 20;

static_assert(kCodeTop >= 0, "the code must fit the height");

constexpr const char* kShareTip =
    "In Share mode, sign in from a phone on LokiCTL's hotspot and every device on it gets internet.";
constexpr const char* kCheckOffNote =
    "The system's own check is off, so LokiCTL never notices a sign-in by itself: open this page.";

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

// A label one line high and as wide as its parent that ends in dots when
// the text is longer.
lv_obj_t* value_label(lv_obj_t* parent, const view::Theme& theme, view::FontRole role, lv_color_t color) {
    auto* label = plain_label(parent, "");
    view::style::text(label, theme, role, color);
    lv_obj_set_size(label, LV_PCT(100), lv_font_get_line_height(theme.font(role)));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

lv_obj_t* caption_label(lv_obj_t* parent, const view::Theme& theme, const char* text) {
    auto* label = plain_label(parent, "");
    view::style::caption(label, theme, text);
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
    if (tone != shown) {
        lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        shown = tone;
    }
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj || hidden == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Why a check got no answer, in a few words.
std::string failure_text(const model::SigninRun& run) {
    switch (run.error) {
        case model::ToolError::NoServer:
            return "No DNS server";
        case model::ToolError::NoAnswer:
            return "DNS server is silent";
        case model::ToolError::NoSuchName:
        case model::ToolError::NoAddress:
            return "Name not found";
        default:
            break;
    }
    // The fetch's reasons are short; anything longer (a system error text)
    // would not fit the column.
    if (run.reason.empty() || run.reason.size() > kHintMax) {
        return "Connection failed";
    }
    std::string text = run.reason;
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

// The verdict line and the hint under it.
struct ColumnText {
    tools::ToneText verdict;
    std::string hint;
};

// The verdict and hint of `run`. Every hint has at most kHintMax
// characters, so it fits the column in every theme.
ColumnText run_text(const model::SigninRun& run) {
    switch (run.state) {
        case model::ToolState::Running:
            return {{"Checking…", tools::kToneAccent}, "One web request"};
        case model::ToolState::Stopped:
            return {{"Stopped", tools::kToneMuted}, "Press 6 to check"};
        case model::ToolState::Failed:
            return {{"No answer", tools::kToneWarning}, failure_text(run)};
        case model::ToolState::Idle:
            return {{"Not checked", tools::kToneMuted}, "Press 6 to check"};
        case model::ToolState::Done:
            break;
    }
    switch (run.verdict) {
        case model::SigninVerdict::Clear:
            return {{"No sign-in needed", tools::kToneSuccess}, "The internet is open"};
        case model::SigninVerdict::Needed:
            return {{"Sign-in needed", tools::kToneWarning}, "Scan with your phone"};
        case model::SigninVerdict::Unclear:
        case model::SigninVerdict::Waiting:
            break;
    }
    return {{"Can't tell", tools::kToneWarning},
            run.status > 0 ? "Odd answer, code " + std::to_string(run.status) : std::string("Not a web answer")};
}

// The column's text: the run's, with another hint while another tool runs
// (key 6 is off until it ends).
ColumnText column_text(const model::SigninRun& run, const model::ToolsData& data, bool running_here) {
    ColumnText text = run_text(run);
    if (!running_here && model::tool_running(data)) {
        text.hint = "Another tool runs";
    }
    return text;
}

// The detail's word for the verdict, right of the title.
tools::ToneText verdict_word(const model::SigninRun& run) {
    if (run.state == model::ToolState::Done) {
        switch (run.verdict) {
            case model::SigninVerdict::Clear:
                return {"not needed", tools::kToneSuccess};
            case model::SigninVerdict::Needed:
                return {"needed", tools::kToneWarning};
            default:
                return {"can't tell", tools::kToneWarning};
        }
    }
    if (run.state == model::ToolState::Running) {
        return {"checking", tools::kToneAccent};
    }
    if (run.state == model::ToolState::Failed) {
        return {"no answer", tools::kToneWarning};
    }
    return {"not checked", tools::kToneMuted};
}

} // namespace

SigninScreen::SigninScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init(Layout::NoTitleBar);
}

bool SigninScreen::handles(model::AppPage page) {
    return page == model::AppPage::Signin;
}

view::widgets::HelpContent SigninScreen::help_content() {
    return {"SIGN IN",
            {
                {"ESC / 4", "Back"},
                {"ENTER / 5", "Address, what to do"},
                {"6", "Check again"},
                {"Share", "One sign-in serves all"},
                {"Auto check", "Off: no sign-in alert"},
            }};
}

view::widgets::HelpContent SigninScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet SigninScreen::soft_keys() {
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
        {"Check", [this] { check_or_stop(); }, true},
        {},
        {},
    }};
}

bool SigninScreen::this_running() const {
    return view_model().tools().signin.state == model::ToolState::Running;
}

bool SigninScreen::any_running() const {
    return model::tool_running(view_model().tools());
}

void SigninScreen::check_or_stop() {
    if (this_running()) {
        view_model().request_tool_stop();
        return;
    }
    if (any_running()) {
        return;
    }
    view_model().request_tool_run(model::ToolKind::Signin, model::kSigninCheckHost);
}

void SigninScreen::toggle_detail() {
    view_model().set_signin_detail(!view_model().signin_detail());
}

bool SigninScreen::on_back() {
    if (view_model().signin_detail()) {
        view_model().set_signin_detail(false);
        return true;
    }
    return false;
}

bool SigninScreen::on_key(uint32_t key) {
    if (key == LV_KEY_ENTER) {
        toggle_detail();
        return true;
    }
    return false;
}

void SigninScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    main_panel_ = plain_object(content);
    lv_obj_set_size(main_panel_, view::kScreenWidth, kContentHeight);

    // Black on white with a white quiet zone in every theme: phone cameras
    // often fail on light-on-dark codes.
    code_ = lv_qrcode_create(main_panel_);
    lv_qrcode_set_size(code_, kCodeSize);
    lv_qrcode_set_dark_color(code_, lv_color_black());
    lv_qrcode_set_light_color(code_, lv_color_white());
    lv_qrcode_set_quiet_zone(code_, true);
    lv_obj_set_pos(code_, kSideMargin, kCodeTop);
    lv_obj_add_flag(code_, LV_OBJ_FLAG_HIDDEN);

    code_box_ = plain_object(main_panel_);
    view::style::card(code_box_, theme);
    lv_obj_set_pos(code_box_, kSideMargin, kCodeTop);
    lv_obj_set_size(code_box_, kCodeSize, kCodeSize);
    code_note_ = plain_label(code_box_, "");
    view::style::text(code_note_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_set_width(code_note_, LV_PCT(100));
    lv_label_set_long_mode(code_note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(code_note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(code_note_);

    // The column: the name, the verdict and its hint, the sign-in page's
    // host, the system's check, and the Sent count at the bottom.
    auto* panel = plain_object(main_panel_);
    lv_obj_set_pos(panel, kPanelLeft, kCodeTop);
    lv_obj_set_size(panel, kPanelWidth, kCodeSize);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    auto* title = plain_label(panel, "Wi-Fi sign-in");
    view::style::text(title, theme, view::FontRole::Strong);
    lv_obj_set_style_margin_bottom(title, theme.metrics.space_xs, 0);
    verdict_ = value_label(panel, theme, view::FontRole::Body, theme.colors.text);
    hint_ = value_label(panel, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_set_style_margin_bottom(hint_, theme.metrics.space_xs, 0);
    page_caption_ = caption_label(panel, theme, "sign-in page");
    page_ = value_label(panel, theme, view::FontRole::Mono, theme.colors.accent2);
    caption_label(panel, theme, "auto check");
    check_ = value_label(panel, theme, view::FontRole::Body, theme.colors.text);

    sent_ = plain_label(main_panel_, "");
    view::style::text(sent_, theme, view::FontRole::Mono, theme.colors.text);
    sent_caption_ = plain_label(main_panel_, "");
    view::style::caption(sent_caption_, theme, "sent");

    // The detail view, over the whole area: the title with the verdict at
    // the right, the address, then what it means and what to do.
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const lv_font_t* mono = theme.font(view::FontRole::Mono);
    const lv_font_t* strong = theme.font(view::FontRole::Strong);
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
    detail_address_ = plain_label(detail_, "");
    view::style::text(detail_address_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_label_set_long_mode(detail_address_, LV_LABEL_LONG_DOT);
    lv_obj_set_size(detail_address_, kWidth, kAddressLines * lv_font_get_line_height(mono));
    lv_obj_set_pos(detail_address_, kSideMargin, top);
    detail_advice_ = plain_label(detail_, "");
    view::style::text(detail_advice_, theme, view::FontRole::Small);
    lv_label_set_long_mode(detail_advice_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(detail_advice_, kWidth);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().signin_detail_subject(), observer_cb, content, this);
}

void SigninScreen::refresh_keys() {
    // 0: off (another tool runs); 1: Check; 2: Stop.
    int32_t keys = 1;
    if (this_running()) {
        keys = 2;
    } else if (any_running()) {
        keys = 0;
    }
    if (keys == shown_check_key_) {
        return;
    }
    shown_check_key_ = keys;
    if (auto* bar = soft_key_bar()) {
        bar->set_key(2, {keys == 2 ? "Stop" : "Check", [this] { check_or_stop(); }, true, keys != 0});
    }
}

void SigninScreen::refresh_code() {
    const model::SigninRun& run = view_model().tools().signin;
    const std::string text = run.state == model::ToolState::Done ? model::signin_code_text(run) : std::string();
    // The code is made again only when its text changes; the address never
    // goes to the log.
    if (text != code_text_shown_) {
        code_text_shown_ = text;
        code_ok_ = !text.empty() &&
                   lv_qrcode_update(code_, text.data(), static_cast<uint32_t>(text.size())) == LV_RESULT_OK;
    }
    set_hidden(code_, !code_ok_);
    set_hidden(code_box_, code_ok_);
    if (!code_ok_) {
        const std::string note = run.state == model::ToolState::Running
                                     ? "Checking…"
                                     : "The code shows when Wi-Fi wants a sign-in";
        if (set_text_if_changed(code_note_, note_shown_, note)) {
            lv_obj_center(code_note_);
        }
    }
}

void SigninScreen::refresh_column() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    const model::SigninRun& run = data.signin;
    const ColumnText text = column_text(run, data, this_running());
    set_text_if_changed(verdict_, verdict_shown_, text.verdict.text);
    set_tone_if_changed(verdict_, verdict_tone_, text.verdict.tone, theme);
    set_text_if_changed(hint_, hint_shown_, text.hint);

    // The sign-in page's host, while one is needed.
    const bool needed = run.state == model::ToolState::Done && run.verdict == model::SigninVerdict::Needed;
    set_hidden(page_caption_, !needed);
    set_hidden(page_, !needed);
    if (needed) {
        const std::string host = model::signin_host(run.target);
        set_text_if_changed(page_, page_shown_, host.empty() ? std::string("not given") : host);
        const int32_t tone = host.empty() ? tools::kToneMuted : -2;
        if (tone != page_tone_) {
            lv_obj_set_style_text_color(page_, tone == -2 ? theme.colors.accent2 : tone_color(theme, tone), 0);
            page_tone_ = tone;
        }
    }

    // The system's own check: on, or off on this system.
    const bool check_on = view_model().home().internet_check;
    set_text_if_changed(check_, check_shown_, check_on ? "on" : "off on this system");
    set_tone_if_changed(check_, check_tone_, check_on ? tools::kToneText : tools::kToneWarning, theme);

    // The Sent count at the bottom right, its caption left of it.
    if (set_text_if_changed(sent_, sent_shown_, std::to_string(data.sent))) {
        lv_obj_update_layout(sent_);
        lv_obj_align(sent_, LV_ALIGN_BOTTOM_RIGHT, -kSideMargin, -kCodeTop);
        lv_obj_update_layout(sent_);
        lv_obj_align_to(sent_caption_, sent_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Mono)->base_line);
    }
}

void SigninScreen::refresh_detail() {
    const auto& theme = this->theme();
    const model::SigninRun& run = view_model().tools().signin;
    const bool done = run.state == model::ToolState::Done;
    const bool needed = done && run.verdict == model::SigninVerdict::Needed;
    const bool check_on = view_model().home().internet_check;

    std::string title = "Check page";
    std::string address = model::kSigninCheckUrl;
    std::string advice;
    if (needed && !run.target.empty()) {
        title = "Sign-in page";
        address = run.target;
        advice = "Open it on your phone and sign in there. ";
    } else if (needed) {
        title = "Sign-in page";
        advice = "The network answered this page with its own. Scan the code to open it on your phone. ";
    } else if (done && run.verdict == model::SigninVerdict::Clear) {
        advice = "It answered \"no content\", as it does when the internet is open. ";
    } else if (done) {
        advice = "Its answer fits neither an open internet nor a sign-in page. Press 6 to check again. ";
    } else {
        advice = "LokiCTL asks this page once as Sign in opens, and again on 6. ";
    }
    advice += needed || check_on ? kShareTip : kCheckOffNote;

    set_text_if_changed(detail_title_, detail_title_shown_, title);
    const tools::ToneText word = verdict_word(run);
    set_text_if_changed(detail_verdict_, detail_verdict_shown_, word.text);
    set_tone_if_changed(detail_verdict_, detail_tone_, word.tone, theme);
    if (set_text_if_changed(detail_address_, detail_address_shown_, address)) {
        // The advice starts under the address's lines, however many it takes.
        lv_obj_update_layout(detail_address_);
        const lv_font_t* mono = theme.font(view::FontRole::Mono);
        const int32_t line = lv_font_get_line_height(mono);
        lv_point_t size{};
        lv_text_get_size(&size, address.c_str(), mono, 0, 0, kWidth, LV_TEXT_FLAG_NONE);
        const int32_t lines = std::clamp<int32_t>((size.y + line - 1) / line, 1, kAddressLines);
        lv_obj_set_height(detail_address_, lines * line);
        lv_obj_set_pos(detail_advice_, kSideMargin, lv_obj_get_y(detail_address_) + lines * line + kDetailGap);
    }
    set_text_if_changed(detail_advice_, detail_advice_shown_, advice);
}

void SigninScreen::refresh() {
    refresh_keys();
    const int32_t detail = view_model().signin_detail() ? 1 : 0;
    if (detail != detail_shown_) {
        detail_shown_ = detail;
        set_hidden(detail_, detail == 0);
        set_hidden(main_panel_, detail == 1);
    }
    if (detail == 1) {
        refresh_detail();
    } else {
        refresh_code();
        refresh_column();
    }
}

void SigninScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<SigninScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

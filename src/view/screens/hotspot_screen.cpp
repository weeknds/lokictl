/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "hotspot_screen.h"

#include "application_config.h"
#include "home_format.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace screen {
namespace {

// Layout of the 320x140 area above the soft-key bar: the code at the left,
// as big as the height allows, and the values in a column at its right.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kCodeSize      = 136;
constexpr int32_t kCodeTop       = (kContentHeight - kCodeSize) / 2;
constexpr int32_t kColumnGap     = 10;
constexpr int32_t kPanelLeft     = kSideMargin + kCodeSize + kColumnGap;
constexpr int32_t kPanelWidth    = view::kScreenWidth - kSideMargin - kPanelLeft;
constexpr int32_t kTypingWidth   = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kDraftHeight   = 30;

static_assert(kCodeTop >= 0, "the code must fit the height");

// The colours a label of the page can take.
enum Tone : int32_t {
    kToneText = 0,
    kToneMuted,
    kToneSuccess,
    kToneWarning,
    kToneError,
};

lv_color_t tone_color(const view::Theme& theme, int32_t tone) {
    switch (tone) {
        case kToneMuted:
            return theme.colors.muted;
        case kToneSuccess:
            return theme.colors.success;
        case kToneWarning:
            return theme.colors.warning;
        case kToneError:
            return theme.colors.error;
        default:
            return theme.colors.text;
    }
}

// Gives `label` the colour of `tone` when it differs from `shown`.
void set_tone_if_changed(lv_obj_t* label, int32_t& shown, int32_t tone, const view::Theme& theme) {
    if (tone != shown) {
        lv_obj_set_style_text_color(label, tone_color(theme, tone), 0);
        shown = tone;
    }
}

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

// How long the hotspot has run, in whole minutes: "Up 12 min", "Up 2 h 5
// min". Changes once a minute, so the line is seldom drawn again.
std::string up_text(int32_t seconds) {
    const int32_t minutes = std::max<int32_t>(seconds, 0) / 60;
    if (minutes < 60) {
        return "Up " + std::to_string(minutes) + " min";
    }
    return "Up " + std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
}

lv_obj_t* caption_label(lv_obj_t* parent, const view::Theme& theme, const char* text) {
    auto* label = plain_label(parent, "");
    view::style::caption(label, theme, text);
    return label;
}

// Hides or shows `obj`; nothing is redrawn when it stays as it is.
void set_hidden(lv_obj_t* obj, bool hidden) {
    if (hidden == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

std::string escaped(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        if (character == '\\' || character == ';' || character == ',' || character == ':' || character == '"') {
            out += '\\';
        }
        out += character;
    }
    return out;
}

constexpr const char* kSeparator = " · ";

} // namespace

HotspotScreen::HotspotScreen(viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets,
                             view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init(Layout::NoTitleBar);
}

bool HotspotScreen::handles(model::AppPage page) {
    return page == model::AppPage::Hotspot;
}

view::widgets::HelpContent HotspotScreen::help_content() {
    return {"HOTSPOT",
            {
                {"ESC / 4", "Back"},
                {"5", "Rename (ENTER saves)"},
                {"6 twice", "New password"},
                {"Phone", "Scan the code to join"},
            }};
}

view::widgets::HelpContent HotspotScreen::help() const {
    return help_content();
}

std::string HotspotScreen::join_text(const std::string& name, const std::string& password) {
    return "WIFI:T:WPA;S:" + escaped(name) + ";P:" + escaped(password) + ";;";
}

bool HotspotScreen::router_on() const {
    const model::HomeData& data = view_model().home();
    return model::is_hotspot_mode(data.mode) && data.ap_active;
}

bool HotspotScreen::switching() const {
    return view_model().home().mode_switch.state == model::SwitchState::Switching;
}

std::string HotspotScreen::shown_name() const {
    const model::HomeData& data = view_model().home();
    if (router_on() && !data.ap_name.empty()) {
        return data.ap_name;
    }
    return view_model().hotspot_name();
}

view::widgets::SoftKeySet HotspotScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh_keys() switches them off while typing or switching.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Name", [this] { start_rename(); }},
        {"Pass", [this] { press_new_password(); }},
        {},
        {},
    }};
}

void HotspotScreen::start_rename() {
    cancel_question();
    if (switching() || view_model().hotspot_typing()) {
        return;
    }
    draft_invalid_ = false;
    view_model().set_hotspot_draft(view_model().hotspot_name());
    view_model().set_hotspot_typing(true);
}

void HotspotScreen::press_new_password() {
    if (switching() || view_model().hotspot_typing()) {
        return;
    }
    if (!question_) {
        question_ = true;
        refresh();
        return;
    }
    question_ = false;
    view_model().request_new_hotspot_password();
    refresh();
}

void HotspotScreen::cancel_question() {
    if (question_) {
        question_ = false;
        refresh();
    }
}

bool HotspotScreen::text_entry() const {
    return view_model().hotspot_typing();
}

// While typing: printable characters, Backspace and Enter; every key is
// taken. Otherwise any key ends the question of key 6.
bool HotspotScreen::on_key(uint32_t key) {
    if (!view_model().hotspot_typing()) {
        const bool asked = question_;
        cancel_question();
        return asked;
    }
    std::string draft = view_model().hotspot_draft();
    if (key == LV_KEY_ENTER) {
        // An empty name puts the default name back.
        if (!draft.empty() && !app::valid_hotspot_name(draft)) {
            draft_invalid_ = true;
            refresh();
            return true;
        }
        draft_invalid_ = false;
        view_model().set_hotspot_typing(false);
        if (draft != view_model().hotspot_name()) {
            view_model().request_hotspot_rename(draft);
        }
        return true;
    }
    if (key == LV_KEY_BACKSPACE) {
        if (!draft.empty()) {
            draft.pop_back();
        }
    } else if (key >= 0x20 && key <= 0x7E) {
        if (draft.size() < app::kHotspotNameMax) {
            draft += static_cast<char>(key);
        }
    } else {
        return true;
    }
    draft_invalid_ = false;
    view_model().set_hotspot_draft(draft);
    refresh();
    return true;
}

// A short ESC cancels typing or the question of key 6; otherwise the app
// goes back.
bool HotspotScreen::on_back() {
    if (view_model().hotspot_typing()) {
        draft_invalid_ = false;
        view_model().set_hotspot_typing(false);
        return true;
    }
    if (question_) {
        cancel_question();
        return true;
    }
    return false;
}

void HotspotScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    // The code and the values.
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

    auto* panel = plain_object(main_panel_);
    lv_obj_set_pos(panel, kPanelLeft, kCodeTop);
    lv_obj_set_size(panel, kPanelWidth, kCodeSize);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    auto* heading = plain_object(panel);
    lv_obj_set_size(heading, LV_PCT(100), LV_SIZE_CONTENT);
    auto* title = plain_label(heading, "Hotspot");
    view::style::text(title, theme, view::FontRole::Strong);
    state_ = plain_label(heading, "");
    view::style::text(state_, theme, view::FontRole::Strong);
    lv_obj_align(state_, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_margin_bottom(heading, theme.metrics.space_xs, 0);

    caption_label(panel, theme, "name");
    name_ = value_label(panel, theme, view::FontRole::Body, theme.colors.text);
    caption_label(panel, theme, "password");
    password_ = value_label(panel, theme, view::FontRole::Mono, theme.colors.accent2);
    details_ = value_label(panel, theme, view::FontRole::Small, theme.colors.muted);
    details_tone_ = kToneMuted;
    lv_obj_set_style_margin_top(details_, theme.metrics.space_xs, 0);
    message_ = plain_label(panel, "");
    view::style::text(message_, theme, view::FontRole::Small);
    lv_obj_set_width(message_, LV_PCT(100));
    lv_label_set_long_mode(message_, LV_LABEL_LONG_WRAP);

    // The rename panel, shown instead while typing.
    typing_panel_ = plain_object(content);
    lv_obj_set_size(typing_panel_, view::kScreenWidth, kContentHeight);
    lv_obj_add_flag(typing_panel_, LV_OBJ_FLAG_HIDDEN);
    auto* typing_column = plain_object(typing_panel_);
    lv_obj_set_pos(typing_column, kSideMargin, theme.metrics.space_md);
    lv_obj_set_size(typing_column, kTypingWidth, kContentHeight - theme.metrics.space_md);
    lv_obj_set_flex_flow(typing_column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(typing_column, theme.metrics.space_sm, 0);

    auto* typing_title = plain_label(typing_column, "Rename the hotspot");
    view::style::text(typing_title, theme, view::FontRole::Strong);
    auto* box = plain_object(typing_column);
    view::style::card(box, theme);
    lv_obj_set_style_border_color(box, theme.colors.accent, 0);
    lv_obj_set_size(box, LV_PCT(100), kDraftHeight);
    draft_ = plain_label(box, "");
    view::style::text(draft_, theme, view::FontRole::Mono);
    lv_obj_align(draft_, LV_ALIGN_LEFT_MID, 0, 0);
    count_ = plain_label(typing_column, "");
    view::style::text(count_, theme, view::FontRole::Small, theme.colors.muted);
    typing_note_ = plain_label(typing_column, "");
    view::style::text(typing_note_, theme, view::FontRole::Small);
    lv_obj_set_width(typing_note_, LV_PCT(100));
    lv_label_set_long_mode(typing_note_, LV_LABEL_LONG_WRAP);

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().hotspot_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().hotspot_typing_subject(), observer_cb, content, this);
}

void HotspotScreen::refresh_keys() {
    // 0: all on; 1: a switch runs (Name and Pass off); 2: typing (all off,
    // keys 4-8 type digits).
    const int32_t keys = view_model().hotspot_typing() ? 2 : (switching() ? 1 : 0);
    if (keys == shown_keys_) {
        return;
    }
    shown_keys_ = keys;
    auto* bar = soft_key_bar();
    if (!bar) {
        return;
    }
    auto& view_model = this->view_model();
    bar->set_key(0, {"Back", [&view_model] { view_model.go_back(); }, false, keys != 2});
    bar->set_key(1, {"Name", [this] { start_rename(); }, false, keys == 0});
    bar->set_key(2, {"Pass", [this] { press_new_password(); }, false, keys == 0});
}

void HotspotScreen::refresh_code(bool on, bool busy, const std::string& name) {
    // While a switch runs the hotspot may still send the old name and
    // password, so no code is shown.
    on = on && !busy;
    if (on) {
        // The code is made again only when its text changes; the text and
        // the password in it never go to the log.
        const std::string text = join_text(name, view_model().hotspot_password());
        if (text != code_text_shown_) {
            code_text_shown_ = text;
            code_ok_ = !view_model().hotspot_password().empty() &&
                       lv_qrcode_update(code_, text.data(), static_cast<uint32_t>(text.size())) == LV_RESULT_OK;
        }
    }
    const bool show_code = on && code_ok_;
    set_hidden(code_, !show_code);
    set_hidden(code_box_, show_code);
    if (!show_code) {
        const char* note = on     ? "No code for this name and password"
                           : busy ? "The join code shows once the hotspot runs"
                                  : "The join code shows while the hotspot runs";
        if (std::strcmp(lv_label_get_text(code_note_), note) != 0) {
            lv_label_set_text(code_note_, note);
        }
    }
}

void HotspotScreen::refresh_typing() {
    const auto& theme = this->theme();
    const std::string& draft = view_model().hotspot_draft();
    set_text_if_changed(draft_, draft_shown_, draft + "_");
    set_text_if_changed(count_, count_shown_,
                              std::to_string(draft.size()) + "/" + std::to_string(app::kHotspotNameMax) +
                                  kSeparator + "ENTER saves" + kSeparator + "ESC cancels");
    std::string note;
    int32_t tone = kToneWarning;
    if (draft_invalid_) {
        note = "1 to 32 characters, no space at either end";
        tone = kToneError;
    } else if (draft.empty()) {
        note = "Empty: ENTER puts back " + view_model().default_hotspot_name();
        tone = kToneMuted;
    } else if (router_on()) {
        note = "Devices on the hotspot must join again";
    }
    set_text_if_changed(typing_note_, typing_note_shown_, note);
    set_tone_if_changed(typing_note_, typing_note_tone_, tone, theme);
}

void HotspotScreen::refresh() {
    const auto& theme = this->theme();
    const model::HomeData& data = view_model().home();
    const bool typing = view_model().hotspot_typing();
    const bool on = router_on();
    const bool busy = switching();
    if (busy || typing) {
        question_ = false;
    }

    if ((typing ? 1 : 0) != shown_typing_) {
        set_hidden(main_panel_, typing);
        set_hidden(typing_panel_, !typing);
        shown_typing_ = typing ? 1 : 0;
    }
    refresh_keys();
    if (typing) {
        refresh_typing();
        return;
    }

    const std::string name = shown_name();
    refresh_code(on, busy, name);

    std::string state = "off";
    int32_t state_tone = kToneMuted;
    if (busy) {
        state = "…";
        state_tone = kToneText;
    } else if (on) {
        state = "on";
        state_tone = kToneSuccess;
    }
    set_text_if_changed(state_, state_shown_, state);
    set_tone_if_changed(state_, state_tone_, state_tone, theme);

    const std::string& password = view_model().hotspot_password();
    set_text_if_changed(name_, name_shown_, name.empty() ? std::string(home::kUnknownText) : name);
    set_text_if_changed(password_, password_shown_,
                              password.empty() ? std::string(home::kUnknownText) : password);

    std::string details;
    int32_t details_tone = kToneMuted;
    if (question_ && on) {
        details = "Devices must rejoin";
        details_tone = kToneWarning;
    } else if (on) {
        details = "channel " +
                  (data.ap_channel > 0 ? std::to_string(data.ap_channel) : std::string(home::kUnknownText)) +
                  kSeparator + (data.ap_security.empty() ? std::string("WPA2") : data.ap_security);
    } else {
        details = std::string("WPA2") + kSeparator + "2.4 GHz";
    }
    set_text_if_changed(details_, details_shown_, details);
    set_tone_if_changed(details_, details_tone_, details_tone, theme);

    std::string message;
    int32_t tone = kToneMuted;
    const model::ModeSwitch& change = data.mode_switch;
    if (question_) {
        message = "Press 6 again for a new password";
        tone = kToneWarning;
    } else if (busy) {
        message = "Applying…";
        tone = kToneText;
    } else if (change.state == model::SwitchState::Failed) {
        switch (change.error) {
            case model::SwitchError::NoCable:
                message = "Plug in a cable first";
                tone = kToneWarning;
                break;
            case model::SwitchError::NoWifi:
                message = "Join a Wi-Fi network first";
                tone = kToneWarning;
                break;
            case model::SwitchError::ClientFirst:
                message = "Back to Client first";
                tone = kToneWarning;
                break;
            case model::SwitchError::NotAllowed:
                message = "Not allowed: permission missing";
                tone = kToneError;
                break;
            case model::SwitchError::Failed:
            case model::SwitchError::None:
                message = "Failed: " + (change.reason.empty() ? std::string("unknown error") : change.reason);
                tone = kToneError;
                break;
        }
    } else if (on) {
        message = up_text(data.uptime_seconds) + kSeparator + "scan to join";
    } else {
        message = "Starts with Router or Share in Modes";
    }
    set_text_if_changed(message_, message_shown_, message);
    set_tone_if_changed(message_, message_tone_, tone, theme);
}

void HotspotScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<HotspotScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

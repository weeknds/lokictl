/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "modes_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the four rows at the top and the status line under them, all 304 px wide.
constexpr int32_t kSideMargin  = 8;
constexpr int32_t kWidth       = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kListTop     = 2;
constexpr int32_t kRowHeight   = 19;
constexpr int32_t kRowGap      = 1;
constexpr int32_t kRowCount    = model::kNetModeCount;
constexpr int32_t kListHeight  = kRowCount * kRowHeight + (kRowCount - 1) * kRowGap;
constexpr int32_t kHintLeft    = 78;
constexpr int32_t kStatusTop   = kListTop + kListHeight + 5;
constexpr int32_t kStatusHeight = 20;
constexpr int32_t kStatusGap   = 8;
constexpr int32_t kCaptionGap  = 5;

static_assert(kStatusTop + kStatusHeight <=
                  view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight,
              "the status line must end above the soft-key bar");

// Drawn at the right end of the row of the mode that is on.
constexpr const char* kActiveMark = "✓"; // check mark

// The short hint after each mode's name, in model::NetMode order.
constexpr const char* kHints[kRowCount] = {
    "join a Wi-Fi network",
    "share the cable over Wi-Fi",
    "share Wi-Fi as a hotspot",
    "comes later",
};

// Index 2 of the soft-key set is key 6.
constexpr std::size_t kUseKeyIndex = 2;

// The colours of the status line's left part.
enum StatusTone : int32_t {
    kToneText = 0,
    kToneMuted,
    kToneWarning,
    kToneError,
};

bool switchable(int32_t row) {
    const auto mode = static_cast<model::NetMode>(row);
    return mode == model::NetMode::Client || model::is_hotspot_mode(mode);
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

} // namespace

ModesScreen::ModesScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool ModesScreen::handles(model::AppPage page) {
    return page == model::AppPage::Modes;
}

view::widgets::HelpContent ModesScreen::help_content() {
    return {"MODES",
            {
                {"ESC / 4", "Back"},
                {"F / X", "Up / down"},
                {"ENTER / 6", "Switch to the mode"},
                {"7", "Hotspot: join code, name"},
            }};
}

view::widgets::HelpContent ModesScreen::help() const {
    return help_content();
}

int32_t ModesScreen::active_row() const {
    return std::clamp<int32_t>(static_cast<int32_t>(view_model().home().mode), 0, kRowCount - 1);
}

int32_t ModesScreen::selected_row() const {
    const int32_t selection = view_model().modes_selection();
    if (selection >= 0 && selection < kRowCount) {
        return selection;
    }
    return active_row();
}

bool ModesScreen::can_use(int32_t row) const {
    return switchable(row) && row != active_row() &&
           view_model().home().mode_switch.state != model::SwitchState::Switching;
}

void ModesScreen::use_selected() {
    const int32_t row = selected_row();
    if (can_use(row)) {
        view_model().request_mode(static_cast<model::NetMode>(row));
    }
}

view::widgets::SoftKeySet ModesScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it. Key 6
    // starts switched off; refresh() switches it on where Use works.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Use", [this] { use_selected(); }, true, false},
        {"QR", [&view_model] { view_model.open_hotspot(); }},
        {},
    }};
}

// Up and down move the highlight and stop at the first and the last row;
// Enter is Use.
bool ModesScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_DOWN: {
            const int32_t current = selected_row();
            const int32_t next = std::clamp<int32_t>(current + (key == LV_KEY_UP ? -1 : 1), 0, kRowCount - 1);
            if (next != current) {
                view_model().select_mode_row(next);
            }
            return true;
        }
        case LV_KEY_ENTER:
            use_selected();
            return true;
        default:
            return false;
    }
}

void ModesScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    auto* list = plain_object(content);
    lv_obj_set_pos(list, kSideMargin, kListTop);
    lv_obj_set_size(list, kWidth, kListHeight);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, kRowGap, 0);

    // Fixed row heights and places, so nothing moves when the highlight
    // switches a row to the Strong font. The name and the tick inherit the
    // row's font and colour (view::style::list_row()); the hint keeps the
    // Small font of its own.
    for (int32_t index = 0; index < kRowCount; ++index) {
        Row& row = rows_[static_cast<std::size_t>(index)];
        row.row = plain_object(list);
        lv_obj_set_size(row.row, LV_PCT(100), kRowHeight);

        row.name = plain_label(row.row, model::net_mode_name(static_cast<model::NetMode>(index)));
        lv_obj_align(row.name, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);

        row.hint = plain_label(row.row, kHints[index]);
        lv_obj_set_style_text_font(row.hint, theme.font(view::FontRole::Small), 0);
        lv_obj_align(row.hint, LV_ALIGN_LEFT_MID, kHintLeft, 0);

        row.mark = plain_label(row.row, kActiveMark);
        lv_obj_align(row.mark, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
        lv_obj_add_flag(row.mark, LV_OBJ_FLAG_HIDDEN);
    }

    // The status line: one text across the line, or the hotspot's name on
    // the left and its password, with a caption, on the right.
    const int32_t inset = theme.metrics.border_width + theme.metrics.space_xs + theme.metrics.space_sm;
    status_left_ = plain_label(content, "");
    view::style::text(status_left_, theme, view::FontRole::Body);
    lv_label_set_long_mode(status_left_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_left_, kSideMargin + inset, kStatusTop);
    status_right_ = plain_label(content, "");
    view::style::text(status_right_, theme, view::FontRole::Mono, theme.colors.accent2);
    lv_obj_align(status_right_, LV_ALIGN_TOP_RIGHT, -(kSideMargin + inset), kStatusTop);
    status_caption_ = plain_label(content, "");
    view::style::caption(status_caption_, theme, "password");
    lv_obj_add_flag(status_caption_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away; the first call draws the
    // rows, the tick, the status line and key 6.
    lv_subject_add_observer_obj(view_model().home_data_subject(), data_observer, content, this);
    lv_subject_add_observer_obj(view_model().modes_selection_subject(), selection_observer, content, this);
}

void ModesScreen::refresh() {
    const auto& theme = this->theme();
    const int32_t selected = selected_row();
    const int32_t active = active_row();

    if (selected != shown_selected_) {
        for (int32_t index = 0; index < kRowCount; ++index) {
            const Row& row = rows_[static_cast<std::size_t>(index)];
            const bool is_selected = index == selected;
            view::style::list_row(row.row, theme, is_selected);
            // Off rows and hints are muted, except on the highlight, whose
            // colours come from the row.
            if (is_selected) {
                lv_obj_remove_local_style_prop(row.hint, LV_STYLE_TEXT_COLOR, 0);
                lv_obj_remove_local_style_prop(row.name, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(row.hint, theme.colors.muted, 0);
                if (switchable(index)) {
                    lv_obj_remove_local_style_prop(row.name, LV_STYLE_TEXT_COLOR, 0);
                } else {
                    lv_obj_set_style_text_color(row.name, theme.colors.muted, 0);
                }
            }
        }
        shown_selected_ = selected;
    }

    if (active != shown_active_) {
        for (int32_t index = 0; index < kRowCount; ++index) {
            lv_obj_t* mark = rows_[static_cast<std::size_t>(index)].mark;
            if (index == active) {
                lv_obj_clear_flag(mark, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
            }
        }
        shown_active_ = active;
    }

    const int32_t use = can_use(selected) ? 1 : 0;
    if (use != shown_use_) {
        if (auto* bar = soft_key_bar()) {
            bar->set_key(kUseKeyIndex, {"Use", [this] { use_selected(); }, true, use == 1});
        }
        shown_use_ = use;
    }

    refresh_status();
}

void ModesScreen::refresh_status() {
    const auto& theme = this->theme();
    const model::HomeData& data = view_model().home();
    const model::ModeSwitch& change = data.mode_switch;

    std::string left;
    std::string right;
    int32_t tone = kToneText;
    if (change.state == model::SwitchState::Switching) {
        // The highlight stays on the mode asked for, so the line need not
        // name it.
        left = "Switching\u2026";
    } else if (change.state == model::SwitchState::Failed) {
        switch (change.error) {
            case model::SwitchError::NoCable:
                left = "Plug in a cable first";
                tone = kToneWarning;
                break;
            case model::SwitchError::NoWifi:
                left = "Join a Wi-Fi network first";
                tone = kToneWarning;
                break;
            case model::SwitchError::ClientFirst:
                left = "Back to Client first";
                tone = kToneWarning;
                break;
            case model::SwitchError::NotAllowed:
                left = "Not allowed: permission missing";
                tone = kToneError;
                break;
            case model::SwitchError::Failed:
            case model::SwitchError::None:
                left = "Failed: " + (change.reason.empty() ? std::string("unknown error") : change.reason);
                tone = kToneError;
                break;
        }
    } else if (model::is_hotspot_mode(data.mode) && data.ap_active) {
        left = data.ap_name.empty() ? std::string("Hotspot on") : data.ap_name;
        right = view_model().hotspot_password();
    } else {
        tone = kToneMuted;
        switch (data.uplink_kind) {
            case model::LinkKind::Wifi:
                left = data.wifi_name.empty() ? std::string("On Wi-Fi") : "On Wi-Fi " + data.wifi_name;
                break;
            case model::LinkKind::Cable:
                left = "On the cable " + data.uplink_name;
                break;
            case model::LinkKind::None:
                left = "No network";
                break;
        }
    }

    const bool right_changed = set_text_if_changed(status_right_, status_right_shown_, right);
    if (set_text_if_changed(status_left_, status_left_shown_, left) || right_changed || status_tone_ < 0) {
        // The left part gets the room the right part leaves; longer text
        // ends in dots.
        const int32_t inset = theme.metrics.border_width + theme.metrics.space_xs + theme.metrics.space_sm;
        int32_t width = kWidth - 2 * inset;
        if (!right.empty()) {
            // The caption stands left of the password, on its baseline.
            lv_obj_clear_flag(status_caption_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_update_layout(status_right_);
            lv_obj_align_to(status_caption_, status_right_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                            theme.font(view::FontRole::Small)->base_line -
                                theme.font(view::FontRole::Mono)->base_line);
            lv_obj_update_layout(status_caption_);
            width -= lv_obj_get_width(status_right_) + kCaptionGap + lv_obj_get_width(status_caption_) + kStatusGap;
        } else {
            lv_obj_add_flag(status_caption_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_size(status_left_, std::max<int32_t>(width, 0), kStatusHeight);
    }
    if (tone != status_tone_) {
        const lv_color_t colors[] = {theme.colors.text, theme.colors.muted, theme.colors.warning, theme.colors.error};
        lv_obj_set_style_text_color(status_left_, colors[tone], 0);
        status_tone_ = tone;
    }
}

void ModesScreen::selection_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ModesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void ModesScreen::data_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ModesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

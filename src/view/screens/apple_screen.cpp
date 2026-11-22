/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "apple_screen.h"

#include "asset_manager.h"
#include "style.h"
#include "theme.h"
#include "theme_engine.h"

namespace screen {
namespace {

constexpr const char* kIconKbFn          = "\uE080";
constexpr const char* kIconKbH           = "\uE084";
constexpr const char* kIconKbJ           = "\uE08C";
constexpr const char* kIconKbPrintScreen = "\uE0B1";
constexpr const char* kIconKbHelp        = "\uEA28";

// The hint rows are shorter than the 34 px key-cap glyphs, which may draw
// past their row; the visible caps are about 16 px tall. Kept small so the
// greeting in the Display font, the version line and both hints fit the
// 110 px content area in every theme.
constexpr int32_t kHintRowHeight = 20;
constexpr int32_t kHintRowGap    = 2;
constexpr int32_t kGroupRowGap   = 2;

// Key 5 switches the greeting between the theme's Title and Display fonts.
// The observer's user data is the ThemeEngine, which outlives every screen.
void greeting_font_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    auto* engine = static_cast<view::ThemeEngine*>(lv_observer_get_user_data(observer));
    if (!label || !engine) {
        return;
    }

    const auto role = lv_subject_get_int(subject) ? view::FontRole::Display : view::FontRole::Title;
    lv_obj_set_style_text_font(label, engine->theme().font(role), 0);
}

void info_visible_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    if (!label) {
        return;
    }

    if (lv_subject_get_int(subject)) {
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

AppleScreen::AppleScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

view::widgets::HelpContent AppleScreen::help_content() {
    return {"HOME",
            {
                {"HOLD ESC / 4", "Exit"},
                {"5", "Text size"},
                {"6", "Next theme"},
                {"7", "Version info"},
                {"8", "Open counter"},
            }};
}

view::widgets::HelpContent AppleScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet AppleScreen::soft_keys() {
    // The view model outlives every screen. Key 4 itself never reaches the
    // bar on this page: app::KeyController takes it first (a short press
    // shows "Hold ESC to exit", holding it quits). Clicking the Exit slot in
    // the simulator still runs its action.
    auto& view_model = this->view_model();
    return {{
        {"Exit", [&view_model] { view_model.request_quit(); }},
        {"Size", [&view_model] { view_model.toggle_bold_text(); }},
        {"Theme", [&view_model] { view_model.request_next_theme(); }, true},
        {"Info", [&view_model] { view_model.toggle_info(); }},
        {"Count", [&view_model] { view_model.open_page(model::AppPage::Butter); }},
    }};
}

void AppleScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    auto* group = lv_obj_create(content);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(group, kGroupRowGap, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(group);

    auto* hello = lv_label_create(group);
    lv_label_bind_text(hello, view_model().greeting_subject(), nullptr);
    lv_obj_set_style_text_color(hello, theme.colors.text, 0);
    // Registering the observer sets the font for the current state.
    lv_subject_add_observer_obj(view_model().bold_text_subject(),
                                greeting_font_observer,
                                hello,
                                &theme_engine());

    auto* info = lv_label_create(group);
    lv_label_set_text_fmt(info, "LVGL v%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    view::style::text(info, theme, view::FontRole::Small);
    lv_subject_add_observer_obj(view_model().info_visible_subject(), info_visible_observer, info, nullptr);

    auto* hints = lv_obj_create(group);
    lv_obj_remove_style_all(hints);
    lv_obj_set_size(hints, 220, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hints, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hints,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hints, kHintRowGap, 0);
    lv_obj_add_flag(hints, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_clear_flag(hints, LV_OBJ_FLAG_SCROLLABLE);

    auto* key_icon_font = assets().load_font("kenney_input_keyboard_and_mouse.ttf", 34);
    auto* extra_key_icon_font = assets().load_font("kenny_keyboard_extra.ttf", 34);
    const lv_font_t* hint_font = theme.font(view::FontRole::Small);
    const auto add_hint = [&](const char* letter_icon,
                              const char* letter_fallback,
                              const char* action_icon,
                              const char* action_fallback,
                              const lv_font_t* action_font,
                              const char* description_text) {
        auto* item = lv_obj_create(hints);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, 196, kHintRowHeight);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 4, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        // Without the key-cap font the key is written as text instead.
        const auto add_key = [&](const char* icon,
                                 const char* fallback,
                                 const lv_font_t* icon_font) {
            auto* key = lv_obj_create(item);
            lv_obj_remove_style_all(key);
            lv_obj_set_size(key, 26, kHintRowHeight);
            lv_obj_add_flag(key, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            lv_obj_clear_flag(key, LV_OBJ_FLAG_SCROLLABLE);

            auto* icon_label = lv_label_create(key);
            lv_label_set_text(icon_label, icon_font ? icon : fallback);
            lv_obj_set_style_text_font(icon_label, icon_font ? icon_font : hint_font, 0);
            lv_obj_set_style_text_color(icon_label, theme.colors.text, 0);
            lv_obj_align(icon_label, LV_ALIGN_CENTER, 0, icon_font ? -3 : 0);
        };

        const auto add_text = [&](const char* text) {
            auto* label = lv_label_create(item);
            lv_label_set_text(label, text);
            lv_obj_set_style_text_font(label, hint_font, 0);
            lv_obj_set_style_text_color(label, theme.colors.text, 0);
        };

        add_key(kIconKbFn, "FN", key_icon_font);
        add_text("+");
        add_key(letter_icon, letter_fallback, key_icon_font);
        add_text("=");
        add_key(action_icon, action_fallback, action_font);
        add_text(description_text);
    };

    add_hint(kIconKbH, "H", kIconKbHelp, "?", extra_key_icon_font, "Help");
    add_hint(kIconKbJ,
             "J",
             kIconKbPrintScreen,
             "PRSC",
             key_icon_font,
             "Screenshot");
}

} // namespace screen

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "remote/remote_switch.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar: the
// list, 304 px wide, with one row per theme, then the "Dim when idle" row
// and the "Remote view" row.
constexpr int32_t kSideMargin    = 8;
constexpr int32_t kWidth         = view::kScreenWidth - 2 * kSideMargin;
constexpr int32_t kListTop       = 3;
constexpr int32_t kRowHeight     = 20;
constexpr int32_t kRowGap        = 1;
constexpr int32_t kDimRow        = view::kThemeCount;
constexpr int32_t kRemoteRow     = view::kThemeCount + 1;
constexpr int32_t kRowCount      = view::kThemeCount + 2;
constexpr int32_t kListHeight    = kRowCount * kRowHeight + (kRowCount - 1) * kRowGap;

static_assert(view::kThemeCount <= 3,
              "the Settings page has room for three theme rows, the dimming row and the Remote "
              "view row; make the list scroll before adding a fourth theme");
static_assert(kListTop + kListHeight <=
                  view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight,
              "the list must end above the soft-key bar");

// Each row shows four squares in that theme's colours. They end
// kSwatchEndInset px before the row's right edge, so they stand in the same
// columns in every row, right of the longest name, and leave the tick a
// column of its own at the right end.
constexpr int32_t kSwatchCount     = 4;
constexpr int32_t kSwatchSize      = 12;
constexpr int32_t kSwatchGap       = 3;
constexpr int32_t kSwatchBoxWidth  = kSwatchCount * kSwatchSize + (kSwatchCount - 1) * kSwatchGap;
constexpr int32_t kSwatchEndInset  = 24;
constexpr int32_t kSwatchMaxRadius = 2;

// Drawn at the right end of the active theme's row. The theme fonts that
// lack it get it from their fallback font (see docs/fonts.md).
constexpr const char* kActiveMark = "\u2713"; // check mark

// The dimming row's name, and its value for on and off.
constexpr const char* kDimRowName = "Dim when idle";

const char* dimming_text(bool on) {
    return on ? "on" : "off";
}

// The Remote view row's name, and its value for each state: "on" while the
// Cardputer shows the code, "viewed" while a browser is paired, each with
// " + hotspot" while hotspot devices may use it too. It is never saved, so
// every start reads "off".
constexpr const char* kRemoteRowName = "Remote view";

const char* remote_text(platform::remote::ViewState state, bool hotspot) {
    switch (state) {
        case platform::remote::ViewState::Waiting:
            return hotspot ? "on + hotspot" : "on";
        case platform::remote::ViewState::Viewed:
            return hotspot ? "viewed + hotspot" : "viewed";
        case platform::remote::ViewState::Off:
            break;
    }
    return "off";
}

const char* remote_text() {
    return remote_text(platform::remote::view_state(), platform::remote::hotspot_allowed());
}

// Index 2 of the soft-key set is key 6, index 3 key 7.
constexpr std::size_t kUseKeyIndex = 2;
constexpr std::size_t kHotspotKeyIndex = 3;

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// A label without styles of its own; it takes font and colour from its
// parent (a list row) unless the caller sets them.
lv_obj_t* plain_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    return label;
}

view::widgets::SoftKey back_key(viewmodel::BaseViewModel& view_model) {
    // The view model outlives every screen.
    return {"Back", [&view_model] { view_model.go_back(); }};
}

// Key 6 switches to the highlighted theme. It is only switched on while the
// highlight is on a row other than the active theme's, and then the view
// model holds that row.
view::widgets::SoftKey use_key(viewmodel::BaseViewModel& view_model, bool enabled) {
    return {"Use",
            [&view_model] {
                const int32_t row = view_model.settings_theme_selection();
                if (row >= 0 && row < view::kThemeCount) {
                    view_model.request_theme(row);
                }
            },
            true,
            enabled};
}

// Key 6 while the highlight is on the dimming row: "Off" switches dimming
// off while it is on, "On" switches it on while it is off.
view::widgets::SoftKey switch_key(viewmodel::BaseViewModel& view_model) {
    return {view_model.screen_dimming() ? "Off" : "On",
            [&view_model] { view_model.set_screen_dimming(!view_model.screen_dimming()); }};
}

// Key 6 while the highlight is on the Remote view row: "On" while it is
// off, "Off" while it is on or viewed. It only asks; app::RemoteController
// switches a moment later.
view::widgets::SoftKey remote_key() {
    return {platform::remote::view_state() == platform::remote::ViewState::Off ? "On" : "Off",
            [] { platform::remote::request_switch(); }};
}

// Key 7 while the highlight is on the Remote view row: "+AP" allows devices
// on LokiCTL's own hotspot too, "-AP" stops that. It only works while
// Remote view is on (dimmed while off) and only asks, like key 6. Other
// rows leave key 7 unused.
view::widgets::SoftKey hotspot_key(int32_t selected) {
    if (selected != kRemoteRow) {
        return {};
    }
    return {platform::remote::hotspot_allowed() ? "-AP" : "+AP",
            [] { platform::remote::request_hotspot(); },
            false,
            platform::remote::view_state() != platform::remote::ViewState::Off};
}

// Key 6 for the highlighted row.
view::widgets::SoftKey row_key(viewmodel::BaseViewModel& view_model, int32_t selected, int32_t active) {
    if (selected == kRemoteRow) {
        return remote_key();
    }
    return selected == kDimRow ? switch_key(view_model) : use_key(view_model, selected != active);
}

// The four squares of one theme: ground, text, accent and second accent.
// Each has an outline in the page's muted colour, so a square never
// disappears into the ground or into the highlight behind it.
void build_swatches(lv_obj_t* row, const view::Theme& theme, const view::ThemeSpec& spec) {
    auto* box = plain_object(row);
    lv_obj_set_size(box, kSwatchBoxWidth, kSwatchSize);
    lv_obj_align(box,
                 LV_ALIGN_RIGHT_MID,
                 -(kSwatchEndInset - theme.metrics.border_width - theme.metrics.space_xs),
                 0);

    const view::ThemeColors swatch = view::to_colors(spec.colors);
    const lv_color_t colors[kSwatchCount] = {
        swatch.background,
        swatch.text,
        swatch.accent,
        swatch.accent2,
    };
    const int32_t radius = std::min<int32_t>(theme.metrics.radius, kSwatchMaxRadius);
    for (int32_t index = 0; index < kSwatchCount; ++index) {
        auto* square = plain_object(box);
        lv_obj_set_size(square, kSwatchSize, kSwatchSize);
        lv_obj_set_pos(square, index * (kSwatchSize + kSwatchGap), 0);
        lv_obj_set_style_bg_color(square, colors[index], 0);
        lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(square, theme.colors.muted, 0);
        lv_obj_set_style_border_opa(square, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(square, 1, 0);
        lv_obj_set_style_border_side(square, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_radius(square, radius, 0);
    }
}

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model,
                               app::AssetManager& assets,
                               view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool SettingsScreen::handles(model::AppPage page) {
    return page == model::AppPage::Settings;
}

int32_t SettingsScreen::active_row() const {
    return static_cast<int32_t>(theme().id);
}

int32_t SettingsScreen::selected_row() const {
    const int32_t selection = view_model().settings_theme_selection();
    if (selection >= 0 && selection < kRowCount) {
        return selection;
    }
    return active_row();
}

view::widgets::SoftKeySet SettingsScreen::soft_keys() {
    auto& view_model = this->view_model();
    return {{
        back_key(view_model),
        {},
        row_key(view_model, selected_row(), active_row()),
        hotspot_key(selected_row()),
        {},
    }};
}

// Up and down move the highlight and stop at the first and the last row; a
// move that changes nothing writes nothing. Enter switches to the
// highlighted theme, unless it is already the active one, switches dimming
// on the dimming row, and asks to switch Remote view on its row.
bool SettingsScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_DOWN: {
            const int32_t current = selected_row();
            const int32_t step = key == LV_KEY_UP ? -1 : 1;
            const int32_t next = std::clamp<int32_t>(current + step, 0, kRowCount - 1);
            if (next != current) {
                view_model().select_settings_theme(next);
            }
            return true;
        }
        case LV_KEY_ENTER: {
            const int32_t row = selected_row();
            if (row == kRemoteRow) {
                platform::remote::request_switch();
            } else if (row == kDimRow) {
                view_model().set_screen_dimming(!view_model().screen_dimming());
            } else if (row != active_row()) {
                view_model().request_theme(row);
            }
            return true;
        }
        default:
            return false;
    }
}

view::widgets::HelpContent SettingsScreen::help() const {
    return {"SETTINGS",
            {
                {"ESC / 4", "Back"},
                {"F / X", "Up / down"},
                {"ENTER / 6", "Use or switch"},
                {"REMOTE", "Screen in a web browser"},
                {"7", "Hotspot devices too"},
            }};
}

void SettingsScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    auto* list = plain_object(content);
    lv_obj_set_pos(list, kSideMargin, kListTop);
    lv_obj_set_size(list, kWidth, kListHeight);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, kRowGap, 0);

    // A fixed row height and fixed places inside the row, so nothing moves
    // when the highlighted row switches to the Strong font. The labels
    // inherit the row's font and colour from view::style::list_row(). The
    // tick is set once: a theme switch builds the page again.
    for (int32_t index = 0; index < view::kThemeCount; ++index) {
        const view::ThemeSpec& spec = view::theme_spec(static_cast<view::ThemeId>(index));

        auto* row = plain_object(list);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);

        auto* name = plain_label(row, spec.display_name);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);

        build_swatches(row, theme, spec);

        auto* mark = plain_label(row, kActiveMark);
        lv_obj_align(mark, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
        if (index != active_row()) {
            lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // The dimming row: its name, and on or off where the themes have their
    // tick. The value follows the setting (it changes without a rebuild).
    auto* dim_row = plain_object(list);
    lv_obj_set_size(dim_row, LV_PCT(100), kRowHeight);
    auto* dim_name = plain_label(dim_row, kDimRowName);
    lv_obj_align(dim_name, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);
    auto* dim_value = plain_label(dim_row, dimming_text(view_model().screen_dimming()));
    lv_obj_align(dim_value, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
    lv_subject_add_observer_obj(view_model().screen_dimming_subject(), dimming_observer, dim_value, this);

    // The Remote view row: its name and its state, which follows the
    // module's state subject. The observer is bound to the value's label.
    auto* remote_row = plain_object(list);
    lv_obj_set_size(remote_row, LV_PCT(100), kRowHeight);
    auto* remote_name = plain_label(remote_row, kRemoteRowName);
    lv_obj_align(remote_name, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);
    auto* remote_value = plain_label(remote_row, remote_text());
    lv_obj_align(remote_value, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
    // The value follows both of the module's subjects (the state and the
    // hotspot choice); keys 6 and 7 follow them while the highlight is on
    // this row. The observers are bound to the value's label.
    const lv_observer_cb_t remote_observer = [](lv_observer_t* observer, lv_subject_t* /*subject*/) {
        auto* value = lv_observer_get_target_obj(observer);
        auto* screen = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
        if (!value || !screen) {
            return;
        }
        lv_label_set_text(value, remote_text());
        auto* bar = screen->soft_key_bar();
        if (bar && screen->selected_row() == kRemoteRow) {
            bar->set_key(kUseKeyIndex, remote_key());
            bar->set_key(kHotspotKeyIndex, hotspot_key(kRemoteRow));
        }
    };
    lv_subject_add_observer_obj(platform::remote::state_subject(), remote_observer, remote_value, this);
    lv_subject_add_observer_obj(platform::remote::hotspot_subject(), remote_observer, remote_value, this);

    // Added last: LVGL calls the observer once right away, which styles the
    // rows and key 6 for the current state.
    lv_subject_add_observer_obj(view_model().settings_theme_selection_subject(),
                                selection_observer,
                                list,
                                this);
}

void SettingsScreen::selection_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* list = lv_observer_get_target_obj(observer);
    auto* screen = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (!list || !screen) {
        return;
    }

    // The screen turns kSettingsThemeFollowsActive into the active row, so
    // the subject's value is not read here.
    const int32_t selected = screen->selected_row();
    const auto rows = static_cast<int32_t>(lv_obj_get_child_count(list));
    for (int32_t index = 0; index < rows; ++index) {
        view::style::list_row(lv_obj_get_child(list, index), screen->theme(), index == selected);
    }

    if (auto* bar = screen->soft_key_bar()) {
        bar->set_key(kUseKeyIndex, row_key(screen->view_model(), selected, screen->active_row()));
        bar->set_key(kHotspotKeyIndex, hotspot_key(selected));
    }
}

void SettingsScreen::dimming_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* value = lv_observer_get_target_obj(observer);
    auto* screen = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (!value || !screen) {
        return;
    }
    lv_label_set_text(value, dimming_text(screen->view_model().screen_dimming()));
    // Key 6 reads On or Off on the dimming row.
    const int32_t selected = screen->selected_row();
    auto* bar = screen->soft_key_bar();
    if (bar && selected == kDimRow) {
        bar->set_key(kUseKeyIndex, row_key(screen->view_model(), selected, screen->active_row()));
    }
}

} // namespace screen

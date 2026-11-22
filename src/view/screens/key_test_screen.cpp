/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "key_test_screen.h"

#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// the list on the left and the box on the right, both 98 px tall with 6 px
// above and below them.
constexpr int32_t kSideMargin  = 8;
constexpr int32_t kTop         = 6;
constexpr int32_t kColumnGap   = 8;
constexpr int32_t kListWidth   = 140;
constexpr int32_t kRowHeight   = 23;
constexpr int32_t kRowGap      = 2;
constexpr int32_t kBoxLeft     = kSideMargin + kListWidth + kColumnGap;
constexpr int32_t kBoxWidth    = view::kScreenWidth - kSideMargin - kBoxLeft;
constexpr int32_t kBoxHeight   = 4 * kRowHeight + 3 * kRowGap;

constexpr const char* kRowNames[] = {"Alpha", "Bravo", "Charlie", "Delta"};
static_assert(std::size(kRowNames) == viewmodel::kKeyTestRowCount,
              "one name per row of the key test page");

// Drawn at the right end of the picked row. The theme fonts that lack it get
// it from their fallback font (see docs/fonts.md).
constexpr const char* kPickedMark = "\u2713"; // check mark

// Each row holds two labels: the name first, then the picked mark.
constexpr int32_t kRowMarkChild = 1;

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

const char* row_name(int32_t index) {
    if (index < 0 || index >= viewmodel::kKeyTestRowCount) {
        return "-";
    }
    return kRowNames[index];
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// Restyles every row of the list (the observer's target), so the row that
// loses the selection keeps nothing of it. The user data is the ThemeEngine,
// which outlives every screen.
void selection_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* list = lv_observer_get_target_obj(observer);
    auto* engine = static_cast<view::ThemeEngine*>(lv_observer_get_user_data(observer));
    if (!list || !engine) {
        return;
    }

    const int32_t selected = lv_subject_get_int(subject);
    const auto rows = static_cast<int32_t>(lv_obj_get_child_count(list));
    for (int32_t index = 0; index < rows; ++index) {
        view::style::list_row(lv_obj_get_child(list, index), engine->theme(), index == selected);
    }
}

// Shows the mark on the picked row of the list (the observer's target) only.
void picked_mark_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* list = lv_observer_get_target_obj(observer);
    if (!list) {
        return;
    }

    const int32_t picked = lv_subject_get_int(subject);
    const auto rows = static_cast<int32_t>(lv_obj_get_child_count(list));
    for (int32_t index = 0; index < rows; ++index) {
        auto* mark = lv_obj_get_child(lv_obj_get_child(list, index), kRowMarkChild);
        if (mark) {
            set_hidden(mark, index != picked);
        }
    }
}

// The picked row's name in the box, or "-" while none has been picked.
void picked_text_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    if (label) {
        lv_label_set_text(label, row_name(lv_subject_get_int(subject)));
    }
}

view::widgets::SoftKey back_key(viewmodel::BaseViewModel& view_model, bool typing) {
    // The view model outlives every screen.
    return {"Back", [&view_model] { view_model.go_back(); }, false, !typing};
}

view::widgets::SoftKey type_key(viewmodel::BaseViewModel& view_model, bool typing) {
    return {"Type", [&view_model] { view_model.key_test_set_typing(true); }, false, !typing};
}

// One line of the box: a caption and a value, standing on one baseline.
// Returns the value label.
lv_obj_t* status_line(lv_obj_t* parent, const view::Theme& theme, const char* caption) {
    auto* line = plain_object(parent);
    lv_obj_set_size(line, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(line, theme.metrics.space_md, 0);

    auto* caption_label = plain_label(line, "");
    view::style::caption(caption_label, theme, caption);
    // Both labels sit on the line's bottom edge; move the caption so the two
    // share one baseline even though the fonts have different descents.
    lv_obj_set_style_translate_y(caption_label,
                                 theme.font(view::FontRole::Small)->base_line -
                                     theme.font(view::FontRole::Body)->base_line,
                                 0);

    auto* value = plain_label(line, "");
    view::style::text(value, theme, view::FontRole::Body);
    return value;
}

} // namespace

KeyTestScreen::KeyTestScreen(viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets,
                             view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool KeyTestScreen::handles(model::AppPage page) {
    return page == model::AppPage::KeyTest;
}

view::widgets::SoftKeySet KeyTestScreen::soft_keys() {
    // Read from the view model, because a theme switch can build the page
    // again while the user is typing.
    auto& view_model = this->view_model();
    const bool typing = view_model.key_test_typing();
    return {{
        back_key(view_model, typing),
        type_key(view_model, typing),
        {},
        {},
        {},
    }};
}

bool KeyTestScreen::on_key(uint32_t key) {
    auto& view_model = this->view_model();
    switch (key) {
        case LV_KEY_UP:
            view_model.key_test_move(-1);
            view_model.key_test_note_key("UP");
            return true;
        case LV_KEY_DOWN:
            view_model.key_test_move(1);
            view_model.key_test_note_key("DOWN");
            return true;
        case LV_KEY_LEFT:
            view_model.key_test_note_key("LEFT");
            return true;
        case LV_KEY_RIGHT:
            view_model.key_test_note_key("RIGHT");
            return true;
        case LV_KEY_ENTER:
            view_model.key_test_pick();
            view_model.key_test_note_key("ENTER");
            return true;
        case LV_KEY_BACKSPACE:
            view_model.key_test_note_key("BACKSPACE");
            return true;
        case ' ':
            view_model.key_test_note_key("SPACE");
            return true;
        default:
            break;
    }

    // Any other printable character is shown as it was typed, so f stays f.
    if (key >= 33 && key <= 126) {
        const char typed[] = {static_cast<char>(key), '\0'};
        view_model.key_test_note_key(typed);
        return true;
    }
    return false;
}

// A short ESC ends typing; otherwise the app goes back one page.
bool KeyTestScreen::on_back() {
    if (!view_model().key_test_typing()) {
        return false;
    }
    view_model().key_test_set_typing(false);
    return true;
}

bool KeyTestScreen::text_entry() const {
    return view_model().key_test_typing();
}

view::widgets::HelpContent KeyTestScreen::help() const {
    return {"KEYS",
            {
                {"ESC / 4", "Back"},
                {"F / X", "Up / down"},
                {"Z / C", "Left / right"},
                {"ENTER", "Pick the row"},
                {"5", "Typing, ESC ends"},
            }};
}

void KeyTestScreen::build_content(lv_obj_t* content) {
    build_list(content);
    build_status(content);
}

void KeyTestScreen::build_list(lv_obj_t* content) {
    const auto& theme = this->theme();

    auto* list = plain_object(content);
    lv_obj_set_pos(list, kSideMargin, kTop);
    lv_obj_set_size(list, kListWidth, kBoxHeight);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, kRowGap, 0);

    // A fixed row height, so the list does not move when the selected row
    // switches to the Strong font. The labels inherit the row's font and
    // colour from view::style::list_row().
    for (const char* name : kRowNames) {
        auto* row = plain_object(list);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);

        auto* name_label = plain_label(row, name);
        lv_obj_align(name_label, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);

        auto* mark = plain_label(row, kPickedMark);
        lv_obj_align(mark, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
    }

    // Added last: LVGL calls each observer once right away, which styles the
    // rows for the current state.
    lv_subject_add_observer_obj(view_model().key_test_selection_subject(),
                                selection_observer,
                                list,
                                &theme_engine());
    lv_subject_add_observer_obj(view_model().key_test_picked_subject(),
                                picked_mark_observer,
                                list,
                                nullptr);
}

void KeyTestScreen::build_status(lv_obj_t* content) {
    const auto& theme = this->theme();

    const auto box =
        view::style::pane(content, theme, "last key", kBoxWidth, kBoxHeight, theme.colors.accent);
    lv_obj_set_pos(box.root, kBoxLeft, kTop);
    // The last key at the top, the two status lines at the bottom.
    lv_obj_set_flex_flow(box.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box.content,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    auto* last_key = plain_label(box.content, "");
    view::style::text(last_key, theme, view::FontRole::Title, theme.colors.accent);
    lv_obj_set_width(last_key, LV_PCT(100));
    lv_label_set_long_mode(last_key, LV_LABEL_LONG_DOT);

    auto* lines = plain_object(box.content);
    lv_obj_set_size(lines, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lines, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(lines, theme.metrics.space_xs, 0);

    auto* picked = status_line(lines, theme, "picked");
    auto* typing = status_line(lines, theme, "typing");

    // Added last; each runs once right away for the current state.
    lv_label_bind_text(last_key, view_model().key_test_last_key_subject(), nullptr);
    lv_subject_add_observer_obj(view_model().key_test_picked_subject(),
                                picked_text_observer,
                                picked,
                                nullptr);
    lv_subject_add_observer_obj(view_model().key_test_typing_subject(), typing_observer, typing, this);
}

void KeyTestScreen::typing_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* label = lv_observer_get_target_obj(observer);
    auto* screen = static_cast<KeyTestScreen*>(lv_observer_get_user_data(observer));
    if (!label || !screen) {
        return;
    }

    const bool typing = lv_subject_get_int(subject) != 0;
    lv_label_set_text(label, typing ? "on" : "off");

    if (auto* bar = screen->soft_key_bar()) {
        auto& view_model = screen->view_model();
        bar->set_key(0, back_key(view_model, typing));
        bar->set_key(1, type_key(view_model, typing));
    }
}

} // namespace screen

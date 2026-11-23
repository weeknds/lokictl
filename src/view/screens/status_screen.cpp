/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "status_screen.h"

#include "home_format.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// kVisibleRows rows of kRowHeight, then the scroll mark at the right edge.
constexpr int32_t kSideMargin  = 8;
constexpr int32_t kListTop     = 1;
constexpr int32_t kRowHeight   = 18;
// Where the text of a row stands on its line, from the row's top.
constexpr int32_t kRowBaseline = 13;
constexpr int32_t kLabelWidth  = 78;
constexpr int32_t kValueLeft   = kSideMargin + kLabelWidth + 4;
constexpr int32_t kValueRight  = view::kScreenWidth - 12;
constexpr int32_t kValueWidth  = kValueRight - kValueLeft;
// A heading's name and its note share the row; the note is right-aligned.
constexpr int32_t kNoteWidth   = 96;
constexpr int32_t kHeadingWidth = kValueRight - kNoteWidth - 4 - kSideMargin;
constexpr int32_t kRuleWidth   = kValueRight - kSideMargin;
// The scroll mark: a thin track with a wider thumb, right of the values.
constexpr int32_t kTrackX      = view::kScreenWidth - 6;
constexpr int32_t kThumbX      = kTrackX - 1;
constexpr int32_t kThumbWidth  = 3;
constexpr int32_t kThumbMinHeight = 8;
constexpr int32_t kListHeight  = StatusScreen::kVisibleRows * kRowHeight;

static_assert(kListTop + kListHeight <=
                  view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight,
              "the rows must end above the soft-key bar");

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, "");
    return label;
}

int32_t row_top(int32_t slot) {
    return kListTop + slot * kRowHeight;
}

// A caption as the theme writes it: in capitals when it asks for that.
std::string caption_text(const view::Theme& theme, const std::string& text) {
    return theme.metrics.uppercase_captions ? home::upper_text(text) : text;
}

} // namespace

StatusScreen::StatusScreen(viewmodel::BaseViewModel& view_model,
                           app::AssetManager& assets,
                           view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool StatusScreen::handles(model::AppPage page) {
    return page == model::AppPage::Status;
}

view::widgets::HelpContent StatusScreen::help_content() {
    return {"STATUS",
            {
                {"ESC / 4", "Back"},
                {"F / X", "Scroll a row"},
                {"Z C / 5 6", "Previous / next part"},
            }};
}

view::widgets::HelpContent StatusScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet StatusScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Up", [this] { jump_section(-1); }},
        {"Down", [this] { jump_section(1); }},
        {},
        {},
    }};
}

bool StatusScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
            scroll_by(-1);
            return true;
        case LV_KEY_DOWN:
            scroll_by(1);
            return true;
        case LV_KEY_LEFT:
            jump_section(-1);
            return true;
        case LV_KEY_RIGHT:
            jump_section(1);
            return true;
        default:
            return false;
    }
}

int32_t StatusScreen::last_top_row() const {
    return std::max<int32_t>(static_cast<int32_t>(rows_.size()) - kVisibleRows, 0);
}

int32_t StatusScreen::top_row() const {
    return std::clamp<int32_t>(view_model().status_scroll(), 0, last_top_row());
}

void StatusScreen::scroll_by(int32_t delta) {
    const int32_t current = top_row();
    const int32_t next = std::clamp<int32_t>(current + delta, 0, last_top_row());
    if (next != current) {
        view_model().set_status_scroll(next);
    }
}

void StatusScreen::jump_section(int32_t direction) {
    const int32_t current = top_row();
    const auto count = static_cast<int32_t>(rows_.size());
    int32_t target = direction > 0 ? last_top_row() : 0;
    if (direction > 0) {
        for (int32_t index = current + 1; index < count; ++index) {
            if (rows_[static_cast<std::size_t>(index)].heading) {
                target = std::min(index, last_top_row());
                break;
            }
        }
    } else {
        for (int32_t index = current - 1; index >= 0; --index) {
            if (rows_[static_cast<std::size_t>(index)].heading) {
                target = index;
                break;
            }
        }
    }
    if (target != current) {
        view_model().set_status_scroll(target);
    }
}

lv_color_t StatusScreen::tone_color(status::NoteTone tone) const {
    switch (tone) {
        case status::NoteTone::Good:
            return theme().colors.success;
        case status::NoteTone::Quiet:
            return theme().colors.muted;
        case status::NoteTone::Warning:
            return theme().colors.warning;
        case status::NoteTone::Plain:
            break;
    }
    return theme().colors.text;
}

void StatusScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();

    for (int32_t index = 0; index < kVisibleRows; ++index) {
        Slot& slot = slots_[static_cast<std::size_t>(index)];
        slot.rule = plain_object(content);
        lv_obj_set_pos(slot.rule, kSideMargin, row_top(index) + kRowHeight - 1);
        lv_obj_set_size(slot.rule, kRuleWidth, 1);
        lv_obj_set_style_bg_color(slot.rule, theme.colors.border_subtle, 0);
        lv_obj_set_style_bg_opa(slot.rule, LV_OPA_COVER, 0);
        lv_obj_add_flag(slot.rule, LV_OBJ_FLAG_HIDDEN);

        slot.label = plain_label(content);
        slot.value = plain_label(content);
        lv_obj_add_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(slot.value, LV_OBJ_FLAG_HIDDEN);
    }

    track_ = plain_object(content);
    lv_obj_set_pos(track_, kTrackX, kListTop);
    lv_obj_set_size(track_, 1, kListHeight);
    lv_obj_set_style_bg_color(track_, theme.colors.border_subtle, 0);
    lv_obj_set_style_bg_opa(track_, LV_OPA_COVER, 0);

    thumb_ = plain_object(content);
    lv_obj_set_style_bg_color(thumb_, theme.colors.muted, 0);
    lv_obj_set_style_bg_opa(thumb_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(thumb_, std::min<int32_t>(theme.metrics.radius, 1), 0);

    // LVGL calls each observer once right away: the first fills the rows,
    // the second shows them again (nothing changes then).
    lv_subject_add_observer_obj(view_model().home_data_subject(), data_observer, content, this);
    lv_subject_add_observer_obj(view_model().status_scroll_subject(), scroll_observer, content, this);
}

void StatusScreen::style_slot(Slot& slot, SlotKind kind, status::NoteTone tone) {
    if (slot.kind == kind && (kind != SlotKind::Heading || slot.tone == tone)) {
        return;
    }
    const auto& theme = this->theme();
    const bool was_hidden = slot.kind == SlotKind::Hidden;
    slot.kind = kind;
    slot.tone = tone;

    if (kind == SlotKind::Hidden) {
        lv_obj_add_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(slot.value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(slot.rule, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (was_hidden) {
        lv_obj_remove_flag(slot.label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(slot.value, LV_OBJ_FLAG_HIDDEN);
    }

    const int32_t slot_index = static_cast<int32_t>(&slot - slots_.data());
    const int32_t top = row_top(slot_index) + kRowBaseline;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    if (kind == SlotKind::Heading) {
        view::style::text(slot.label, theme, view::FontRole::Small, theme.colors.accent);
        lv_obj_set_style_text_letter_space(slot.label, theme.metrics.caption_letter_space, 0);
        fix_width(slot.label, kHeadingWidth, small);
        lv_obj_set_pos(slot.label, kSideMargin, top - baseline_from_top(small));

        view::style::text(slot.value, theme, view::FontRole::Small, tone_color(tone));
        lv_obj_set_style_text_align(slot.value, LV_TEXT_ALIGN_RIGHT, 0);
        fix_width(slot.value, kNoteWidth, small);
        lv_obj_set_pos(slot.value, kValueRight - kNoteWidth, top - baseline_from_top(small));
        lv_obj_remove_flag(slot.rule, LV_OBJ_FLAG_HIDDEN);
    } else {
        const lv_font_t* mono = theme.font(view::FontRole::Mono);
        view::style::text(slot.label, theme, view::FontRole::Small, theme.colors.muted);
        lv_obj_set_style_text_letter_space(slot.label, 0, 0);
        fix_width(slot.label, kLabelWidth, small);
        lv_obj_set_pos(slot.label, kSideMargin, top - baseline_from_top(small));

        view::style::text(slot.value, theme, view::FontRole::Mono);
        lv_obj_set_style_text_align(slot.value, LV_TEXT_ALIGN_LEFT, 0);
        fix_width(slot.value, kValueWidth, mono);
        lv_obj_set_pos(slot.value, kValueLeft, top - baseline_from_top(mono));
        lv_obj_add_flag(slot.rule, LV_OBJ_FLAG_HIDDEN);
    }
    // The texts were set for the other kind of row; set them again below.
    slot.label_text.clear();
    slot.value_text.clear();
    lv_label_set_text(slot.label, "");
    lv_label_set_text(slot.value, "");
}

void StatusScreen::show_rows() {
    const auto& theme = this->theme();
    const int32_t top = top_row();
    for (int32_t index = 0; index < kVisibleRows; ++index) {
        Slot& slot = slots_[static_cast<std::size_t>(index)];
        const auto row_index = static_cast<std::size_t>(top + index);
        if (row_index >= rows_.size()) {
            style_slot(slot, SlotKind::Hidden, status::NoteTone::Plain);
            continue;
        }
        const status::StatusRow& row = rows_[row_index];
        style_slot(slot, row.heading ? SlotKind::Heading : SlotKind::Row, row.tone);
        set_text_if_changed(slot.label, slot.label_text, row.heading ? caption_text(theme, row.label) : row.label);
        set_text_if_changed(slot.value, slot.value_text, row.value);
    }

    // The scroll mark, only when the rows do not all fit.
    const auto count = static_cast<int32_t>(rows_.size());
    if (count <= kVisibleRows) {
        lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(thumb_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(track_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(thumb_, LV_OBJ_FLAG_HIDDEN);
    const int32_t thumb_height = std::max<int32_t>(kListHeight * kVisibleRows / count, kThumbMinHeight);
    const int32_t travel = kListHeight - thumb_height;
    const int32_t last = last_top_row();
    const int32_t offset = last > 0 ? (travel * top + last / 2) / last : 0;
    lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
    lv_obj_set_pos(thumb_, kThumbX, kListTop + offset);
}

void StatusScreen::data_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<StatusScreen*>(lv_observer_get_user_data(observer));
    if (!screen) {
        return;
    }
    screen->rows_ = status::status_rows(screen->view_model().home(), screen->view_model().tools().list_date);
    screen->show_rows();
}

void StatusScreen::scroll_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<StatusScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->show_rows();
    }
}

} // namespace screen

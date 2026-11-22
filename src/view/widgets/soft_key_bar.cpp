/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "soft_key_bar.h"

#include "linux_input.h"
#include "style.h"
#include "theme.h"
#include "theme_engine.h"

#include "logger.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace view::widgets {
namespace {

// Where things sit, in pixels. Rows are counted from the bar's top edge,
// which carries the 1 px rule; the bar is kSoftKeyBarHeight rows tall and
// sits at the bottom of the screen (y 140-169).
//
// Slot i is the clickable area above key 4 + i: kSlotWidth wide, starting at
// kFirstSlotX, so its centre is exactly above the key (x = 49, 105, 161, 217
// and 273).
constexpr int32_t kFirstSlotX = 21;
constexpr int32_t kSlotWidth  = 56;
constexpr int32_t kSlotTop    = 1;  // just below the rule
constexpr int32_t kSlotHeight = 24; // rows 1-24, centred on row 13 (y 153)

// Inside a slot: the number box, a gap, then the word; the three together are
// centred in the slot. Box rows are counted from the slot's top: rows 5-18,
// which is y 146-159 on the screen.
constexpr int32_t kBoxWidth   = 13;
constexpr int32_t kBoxHeight  = 14;
constexpr int32_t kBoxTop     = 5;
constexpr int32_t kBoxWordGap = 4;

// The short line from each slot down to its key: 2 px wide, in the columns
// centre - 1 and centre, filling the bar's last rows (y 165-169).
constexpr int32_t kLineWidth  = 2;
constexpr int32_t kLineHeight = 5;
constexpr int32_t kLineTop    = kSlotTop + kSlotHeight;

constexpr int32_t slot_x(std::size_t index) {
    return kFirstSlotX + kSlotWidth * static_cast<int32_t>(index);
}

constexpr int32_t key_centre_x(std::size_t index) {
    return slot_x(index) + kSlotWidth / 2;
}

static_assert(slot_x(kSoftKeyCount) <= view::kScreenWidth, "the slots must fit the screen width");
static_assert(key_centre_x(0) == 49 && key_centre_x(1) == 105 && key_centre_x(2) == 161 &&
                  key_centre_x(3) == 217 && key_centre_x(4) == 273,
              "each slot must be centred above its key");
static_assert(kLineTop + kLineHeight == view::kSoftKeyBarHeight,
              "the line to the key must reach the bar's last row");
static_assert(kBoxTop + kBoxHeight <= kSlotHeight, "the number box must fit its slot");
static_assert(kSlotTop + kBoxTop + kBoxHeight / 2 == 13, "the number box is centred on row 13 (y 153)");
static_assert(kSoftKeyCount == platform::kSoftKeySlotCount,
              "every soft key needs a slot in the key registry");

constexpr const char* kKeyNumbers[kSoftKeyCount] = {"4", "5", "6", "7", "8"};

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent, const lv_font_t* font) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

void set_visible(lv_obj_t* obj, bool visible) {
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_clickable(lv_obj_t* obj, bool clickable) {
    if (clickable) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
}

int32_t text_width(const char* text, const lv_font_t* font) {
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// The first `max_chars` characters of `text`, counting UTF-8 characters
// rather than bytes.
std::string first_characters(const std::string& text, std::size_t max_chars) {
    std::size_t characters = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        const bool starts_character = (byte & 0xC0U) != 0x80U;
        if (starts_character) {
            if (characters == max_chars) {
                return text.substr(0, index);
            }
            ++characters;
        }
    }
    return text;
}

// The top of the number and word labels, in rows from the slot's top: the
// text line is centred in the slot. Both use the same value, so they stand on
// the same baseline. With the Small fonts of all three themes this puts the
// digits in the middle of the number box (at most 1 px higher than lower).
int32_t text_top(const lv_font_t* font) {
    return (kSlotHeight - lv_font_get_line_height(font)) / 2;
}

} // namespace

SoftKey::SoftKey(std::string text, std::function<void()> on_press, bool is_primary, bool is_enabled)
    : label(std::move(text)), action(std::move(on_press)), primary(is_primary), enabled(is_enabled) {}

SoftKeyBar::SoftKeyBar(lv_obj_t* parent,
                       view::ThemeEngine& theme_engine,
                       SoftKeySet keys,
                       KeyInput key_input)
    : BaseWidgets(parent), theme_engine_(theme_engine), keys_(std::move(keys)), key_input_(key_input) {}

SoftKeyBar::~SoftKeyBar() {
    if (key_input_ != KeyInput::Bound) {
        return;
    }

    // Only clears an entry that still points at this bar's slot: when a page
    // is rebuilt, the new screen registers its keys before this one goes.
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].slot) {
            platform::unregister_soft_key(index, slots_[index].slot);
        }
    }
}

void SoftKeyBar::build() {
    if (core_obj_ || !parent_) {
        return;
    }

    const auto& theme = theme_engine_.theme();

    core_obj_ = plain_object(parent_);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(core_obj_, view::kScreenWidth, view::kSoftKeyBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    view::style::bar(core_obj_, theme, view::style::Edge::Top);
    lv_obj_set_style_pad_all(core_obj_, 0, 0);
    lv_obj_set_style_text_letter_space(core_obj_, 0, 0);
    lv_obj_set_style_text_font(core_obj_, theme.font(view::FontRole::Small), 0);

    for (std::size_t index = 0; index < slots_.size(); ++index) {
        create_slot(index);
        apply_key(index);
    }

    if (key_input_ == KeyInput::Bound) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            platform::register_soft_key(index, slots_[index].slot);
        }
    }
}

void SoftKeyBar::set_key(std::size_t index, SoftKey key) {
    if (index >= keys_.size()) {
        return;
    }

    keys_[index] = std::move(key);
    if (core_obj_) {
        apply_key(index);
    }
}

void SoftKeyBar::create_slot(std::size_t index) {
    const auto& theme = theme_engine_.theme();
    const lv_font_t* font = theme.font(view::FontRole::Small);
    // Children are placed in the bar's content area, which starts below the
    // rule on its top edge.
    const int32_t rule = theme.metrics.border_width;
    auto& slot = slots_[index];

    slot.slot = plain_object(core_obj_);
    lv_obj_set_pos(slot.slot, slot_x(index), kSlotTop - rule);
    lv_obj_set_size(slot.slot, kSlotWidth, kSlotHeight);
    lv_obj_add_event_cb(slot.slot, slot_clicked_cb, LV_EVENT_CLICKED, this);

    slot.box = plain_object(slot.slot);
    lv_obj_clear_flag(slot.box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(slot.box, kBoxWidth, kBoxHeight);

    // Created after the box, so it is drawn on top of it.
    slot.number = plain_label(slot.slot, font);
    lv_label_set_text(slot.number, kKeyNumbers[index]);

    slot.word = plain_label(slot.slot, font);

    slot.line = plain_object(core_obj_);
    lv_obj_clear_flag(slot.line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(slot.line, key_centre_x(index) - 1, kLineTop - rule);
    lv_obj_set_size(slot.line, kLineWidth, kLineHeight);
    lv_obj_set_style_bg_opa(slot.line, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(slot.line, 0, 0);
}

void SoftKeyBar::apply_key(std::size_t index) {
    auto& slot = slots_[index];
    if (!slot.slot) {
        return;
    }

    const auto& key = keys_[index];
    const bool used = !key.label.empty();
    set_visible(slot.box, used);
    set_visible(slot.number, used);
    set_visible(slot.word, used);
    set_visible(slot.line, used);
    // platform::press_soft_key() only clicks a slot that has this flag.
    set_clickable(slot.slot, used && key.enabled);
    if (!used) {
        lv_label_set_text(slot.word, "");
        return;
    }

    const auto& theme = theme_engine_.theme();
    const lv_font_t* font = theme.font(view::FontRole::Small);
    const auto state = !key.enabled ? view::style::SoftKeyState::Disabled
                       : key.primary ? view::style::SoftKeyState::Primary
                                     : view::style::SoftKeyState::Normal;
    const auto look = view::style::soft_key(theme, state);

    std::string shown = first_characters(key.label, kSoftKeyLabelMaxChars);
    if (shown.size() != key.label.size()) {
        LOG_WARN("soft key {}: \"{}\" is longer than {} characters; showing \"{}\"",
                 index + 4,
                 key.label,
                 kSoftKeyLabelMaxChars,
                 shown);
    }
    shown = view::style::soft_key_text(theme, shown);
    lv_label_set_text(slot.word, shown.c_str());

    const int32_t word_width = text_width(shown.c_str(), font);
    const int32_t number_width = text_width(kKeyNumbers[index], font);
    const int32_t group_x = std::max<int32_t>(0, (kSlotWidth - (kBoxWidth + kBoxWordGap + word_width)) / 2);
    const int32_t top = text_top(font);

    lv_obj_set_pos(slot.box, group_x, kBoxTop);
    lv_obj_set_style_radius(slot.box, look.radius, 0);
    if (look.box_filled) {
        lv_obj_set_style_bg_color(slot.box, look.box, 0);
        lv_obj_set_style_bg_opa(slot.box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(slot.box, 0, 0);
        lv_obj_set_style_border_opa(slot.box, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_opa(slot.box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(slot.box, look.box, 0);
        lv_obj_set_style_border_width(slot.box, theme.metrics.border_width, 0);
        lv_obj_set_style_border_side(slot.box, LV_BORDER_SIDE_FULL, 0);
        lv_obj_set_style_border_opa(slot.box, LV_OPA_COVER, 0);
    }

    lv_obj_set_style_text_color(slot.number, look.number, 0);
    lv_obj_set_pos(slot.number, group_x + (kBoxWidth - number_width) / 2, top);

    lv_obj_set_style_text_color(slot.word, look.label, 0);
    lv_obj_set_pos(slot.word, group_x + kBoxWidth + kBoxWordGap, top);

    lv_obj_set_style_bg_color(slot.line, look.box, 0);
}

void SoftKeyBar::slot_clicked_cb(lv_event_t* event) {
    auto* bar = static_cast<SoftKeyBar*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_current_target_obj(event);
    if (!bar || !target) {
        return;
    }

    for (std::size_t index = 0; index < bar->slots_.size(); ++index) {
        if (bar->slots_[index].slot != target) {
            continue;
        }

        const auto& key = bar->keys_[index];
        if (key.label.empty() || !key.enabled) {
            return;
        }
        // A copy: the action may replace this very key with set_key().
        const std::function<void()> action = key.action;
        if (action) {
            action();
        }
        return;
    }
}

} // namespace view::widgets

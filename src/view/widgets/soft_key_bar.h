/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>

namespace view {
class ThemeEngine;
}

namespace view::widgets {

// Keys 4, 5, 6, 7 and 8, left to right, under the bottom edge of the screen.
constexpr std::size_t kSoftKeyCount = 5;
// The longest word a key shows. One character of the 12 px Small font is
// 7 px wide in every theme: number box 13 + gap 4 + 5 x 7 = 52 px, which fits
// the 56 px slot above each key.
constexpr std::size_t kSoftKeyLabelMaxChars = 5;

// What one key under the screen does on a page.
struct SoftKey {
    // An unused key: nothing is drawn and the key does nothing.
    SoftKey() = default;
    SoftKey(std::string label,
            std::function<void()> action = {},
            bool primary = false,
            bool enabled = true);

    std::string label;            // empty = unused; at most kSoftKeyLabelMaxChars characters
    std::function<void()> action; // may be empty: the key is shown and does nothing
    bool primary{false};          // the page's main key, drawn in the accent colour
    bool enabled{true};           // false: drawn dimmed, does nothing
};

// Index 0-4 = keys 4-8.
using SoftKeySet = std::array<SoftKey, kSoftKeyCount>;

// The bar along the bottom of the screen that names what keys 4-8 do. Each
// used key shows its number in a small box and a short word, centred over the
// physical key, with a short line down to it. The look comes from
// view::style::soft_key().
//
// Actions run inside an LVGL click event: they may only ask the view model
// for something (page changes and theme switches are deferred there), never
// delete or rebuild a screen themselves.
//
// The bar reads the active theme when it is built and does not watch for
// theme changes: a theme switch builds the whole screen, and the bar with it,
// again.
class SoftKeyBar : public BaseWidgets {
public:
    // Bound: keys 4-8 are routed to this bar (one bar per screen).
    // None: a picture only, for sample bars on developer pages.
    enum class KeyInput {
        Bound,
        None,
    };

    // The bar is kScreenWidth x kSoftKeyBarHeight and sits at the bottom of
    // `parent`; callers may move root() afterwards.
    SoftKeyBar(lv_obj_t* parent,
               view::ThemeEngine& theme_engine,
               SoftKeySet keys,
               KeyInput key_input = KeyInput::Bound);
    ~SoftKeyBar() override;

    void build() override;

    // Replaces one key (index 0-4 for keys 4-8; other values are ignored).
    // Works before and after build(), also from inside a key's own action.
    void set_key(std::size_t index, SoftKey key);

private:
    struct Slot {
        lv_obj_t* slot{nullptr};   // the clickable area above the key
        lv_obj_t* box{nullptr};    // the box behind the number
        lv_obj_t* number{nullptr}; // "4" to "8"
        lv_obj_t* word{nullptr};   // the key's word
        lv_obj_t* line{nullptr};   // the short line down to the key
    };

    void create_slot(std::size_t index);
    void apply_key(std::size_t index);

    static void slot_clicked_cb(lv_event_t* event);

    view::ThemeEngine& theme_engine_;
    SoftKeySet keys_;
    KeyInput key_input_;
    std::array<Slot, kSoftKeyCount> slots_{};
};

} // namespace view::widgets

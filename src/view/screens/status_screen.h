/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"
#include "status_rows.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The Status page, opened from the home page (key 8): every detail the
// home page has no room for, as one list that scrolls (see
// status::status_rows() for the rows). The title bar stays on top.
//
// Only the rows on screen exist: a fixed set of kVisibleRows row slots,
// each a label, a value and a thin rule. When the values change
// (home_data_subject()) or the list scrolls (status_scroll_subject()), the
// slots are given the rows from the first row shown on, and only texts and
// styles that changed are touched; nothing is built again. A heading row
// shows its name as a caption in the accent colour, its note at the right
// end and a rule under it; a value row shows a muted label and the value in
// the Mono font. A thin mark at the right edge shows where the rows on
// screen lie in the whole list, when the list is longer than the screen.
//
// Up and down (F/X) scroll one row; left and right (Z/C) and keys 5 and 6
// (Up, Down) jump to the previous or next heading. Scrolling stops at both
// ends. Key 4 or a short ESC goes back. The first row shown lives in the
// view model, so it is kept across a theme switch; the page shows it
// clamped to the rows there are.
class StatusScreen : public BaseScreen {
public:
    StatusScreen(viewmodel::BaseViewModel& view_model,
                 app::AssetManager& assets,
                 view::ThemeEngine& theme_engine);

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

    // The page's help.
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

    // How many rows the page shows at once.
    static constexpr int32_t kVisibleRows = 6;

private:
    enum class SlotKind {
        Hidden,
        Heading,
        Row,
        Line,
    };

    struct Slot {
        lv_obj_t* label{nullptr};
        lv_obj_t* value{nullptr};
        lv_obj_t* rule{nullptr};
        SlotKind kind{SlotKind::Hidden};
        status::NoteTone tone{status::NoteTone::Plain};
        std::string label_text;
        std::string value_text;
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The first row that can be shown with the rows there are: the view
    // model's row, at most the last row that still fills the screen.
    int32_t top_row() const;
    int32_t last_top_row() const;
    // Moves the first row shown by `delta` rows, or to the next (`delta`
    // above 0) or previous heading; it stops at both ends.
    void scroll_by(int32_t delta);
    void jump_section(int32_t direction);

    // Gives the slots the rows from top_row() on and moves the scroll mark.
    void show_rows();
    void style_slot(Slot& slot, SlotKind kind, status::NoteTone tone);
    lv_color_t tone_color(status::NoteTone tone) const;

    // The user data is the screen; both observers are bound to the content
    // area, so they go when the screen goes.
    static void data_observer(lv_observer_t* observer, lv_subject_t* subject);
    static void scroll_observer(lv_observer_t* observer, lv_subject_t* subject);

    std::vector<status::StatusRow> rows_;
    std::array<Slot, kVisibleRows> slots_{};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
};

} // namespace screen

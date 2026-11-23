/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tools_screen.h"

#include "home_layout.h"
#include "style.h"
#include "theme.h"
#include "tool_format.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// three rows of two cells, then the status line at the bottom.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin  = 8;
constexpr int32_t kColumnGap   = 8;
constexpr int32_t kCellWidth   = (view::kScreenWidth - 2 * kSideMargin - kColumnGap) / 2;
constexpr int32_t kCellHeight  = 24;
constexpr int32_t kRowGap      = 3;
constexpr int32_t kGridTop     = 4;
constexpr int32_t kRowCount    = 3;
constexpr int32_t kStatusHeight = 18;
constexpr int32_t kStatusTop   = kContentHeight - kStatusHeight - 1;
constexpr int32_t kStatusGap   = 8;
constexpr int32_t kCaptionGap  = 5;

static_assert(kGridTop + kRowCount * kCellHeight + (kRowCount - 1) * kRowGap <= kStatusTop,
              "the grid must end above the status line");

struct ToolInfo {
    const char* name;
    const char* about;  // the status line while the tool is highlighted
};

// In reading order of the columns: the left column, then the right one.
// The first ToolsScreen::kOpenableRows open a page.
constexpr ToolInfo kTools[] = {
    {"Ping", "Is a host there? 10 echoes"},
    {"DNS", "The addresses of a name"},
    {"Trace", "The route, hop by hop"},
    {"Wi-Fi", "Nearby Wi-Fi networks"},
    {"Ports", ""},
    {"Speed", ""},
};

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

} // namespace

ToolsScreen::ToolsScreen(viewmodel::BaseViewModel& view_model,
                         app::AssetManager& assets,
                         view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool ToolsScreen::handles(model::AppPage page) {
    return page == model::AppPage::Tools;
}

view::widgets::HelpContent ToolsScreen::help_content() {
    return {"TOOLS",
            {
                {"ESC / 4", "Back"},
                {"F / X", "Up / down"},
                {"ENTER / 6", "Open the tool"},
                {"Sent", "Packets LokiCTL sent"},
            }};
}

view::widgets::HelpContent ToolsScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet ToolsScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Open", [this] { open_selected(); }, true},
        {},
        {},
    }};
}

int32_t ToolsScreen::selected_row() const {
    return std::clamp<int32_t>(view_model().tools_selection(), 0, kOpenableRows - 1);
}

// Listening and probing, the last tool kinds, belong to the Discover page
// and have no row here.
static_assert(static_cast<int32_t>(model::ToolKind::Trace) == ToolsScreen::kWifiRow - 1 &&
                  static_cast<int32_t>(model::ToolKind::Listen) == ToolsScreen::kWifiRow &&
                  static_cast<int32_t>(model::ToolKind::Probe) == ToolsScreen::kWifiRow + 1 &&
                  model::kToolKindCount == static_cast<std::size_t>(ToolsScreen::kWifiRow) + 2 &&
                  ToolsScreen::kWifiRow == ToolsScreen::kOpenableRows - 1,
              "the openable rows are the tools, in the order of model::ToolKind, then Wi-Fi");

void ToolsScreen::open_selected() {
    // The rows before Wi-Fi follow model::ToolKind: Ping, DNS, Trace.
    const int32_t row = selected_row();
    if (row == kWifiRow) {
        view_model().open_wifi();
        return;
    }
    view_model().open_tool(static_cast<model::ToolKind>(row));
}

bool ToolsScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_DOWN: {
            const int32_t current = selected_row();
            const int32_t next = std::clamp<int32_t>(current + (key == LV_KEY_UP ? -1 : 1), 0, kOpenableRows - 1);
            // Stored even when it stays, so a value out of range comes back.
            if (next != current || view_model().tools_selection() != current) {
                view_model().select_tools_row(next);
            }
            return true;
        }
        case LV_KEY_ENTER:
            open_selected();
            return true;
        default:
            return false;
    }
}

void ToolsScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* small = theme.font(view::FontRole::Small);

    for (int32_t index = 0; index < kCellCount; ++index) {
        Cell& cell = cells_[static_cast<std::size_t>(index)];
        const int32_t column = index / kRowCount;
        const int32_t row = index % kRowCount;
        cell.cell = plain_object(content);
        lv_obj_set_pos(cell.cell, kSideMargin + column * (kCellWidth + kColumnGap),
                       kGridTop + row * (kCellHeight + kRowGap));
        lv_obj_set_size(cell.cell, kCellWidth, kCellHeight);
        view::style::list_row(cell.cell, theme, false);

        // The name inherits the cell's font and colour (view::style::list_row()).
        cell.name = plain_label(cell.cell, kTools[index].name);
        lv_obj_align(cell.name, LV_ALIGN_LEFT_MID, theme.metrics.space_sm, 0);
        cell.tag = plain_label(cell.cell, "");
        lv_obj_set_style_text_font(cell.tag, small, 0);
        lv_obj_align(cell.tag, LV_ALIGN_RIGHT_MID, -theme.metrics.space_sm, 0);
        if (index >= kOpenableRows) {
            lv_obj_set_style_text_color(cell.name, theme.colors.muted, 0);
            lv_obj_set_style_text_color(cell.tag, theme.colors.muted, 0);
            lv_label_set_text(cell.tag, "coming");
        }
    }

    // The status line: what the highlighted tool does, and the Sent count
    // with its caption at the right.
    const int32_t inset = theme.metrics.border_width + theme.metrics.space_xs + theme.metrics.space_sm;
    status_ = plain_label(content, "");
    view::style::text(status_, theme, view::FontRole::Body, theme.colors.muted);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(status_, kSideMargin + inset, kStatusTop);
    sent_ = plain_label(content, "");
    view::style::text(sent_, theme, view::FontRole::Mono, theme.colors.text);
    lv_obj_align(sent_, LV_ALIGN_TOP_RIGHT, -(kSideMargin + inset), kStatusTop);
    sent_caption_ = plain_label(content, "");
    view::style::caption(sent_caption_, theme, "sent");

    // LVGL calls each observer once right away; the first call draws it all.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().tools_selection_subject(), observer_cb, content, this);
}

void ToolsScreen::refresh() {
    const auto& theme = this->theme();
    const model::ToolsData& data = view_model().tools();
    const int32_t selected = selected_row();

    if (selected != shown_selected_) {
        for (int32_t index = 0; index < kOpenableRows; ++index) {
            Cell& cell = cells_[static_cast<std::size_t>(index)];
            view::style::list_row(cell.cell, theme, index == selected);
            // On the highlight the tag takes the row's colour.
            cell.tone_shown = -1;
        }
        shown_selected_ = selected;
    }

    // Wi-Fi has no tag: its list is only read while its page is open.
    const tools::ToneText tags[kOpenableRows] = {tools::ping_tag(data.ping), tools::dns_tag(data.dns),
                                                 tools::trace_tag(data.trace), tools::ToneText{}};
    for (int32_t index = 0; index < kOpenableRows; ++index) {
        Cell& cell = cells_[static_cast<std::size_t>(index)];
        const tools::ToneText& tag = tags[index];
        set_text_if_changed(cell.tag, cell.tag_shown, tag.text);
        const int32_t tone = index == selected ? -2 : tag.tone;
        if (tone != cell.tone_shown) {
            if (tone == -2) {
                lv_obj_remove_local_style_prop(cell.tag, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(cell.tag, tone_color(theme, tone), 0);
            }
            cell.tone_shown = tone;
        }
    }

    const bool sent_changed = set_text_if_changed(sent_, sent_shown_, std::to_string(data.sent));
    if (set_text_if_changed(status_, status_shown_, kTools[selected].about) || sent_changed) {
        const int32_t inset = theme.metrics.border_width + theme.metrics.space_xs + theme.metrics.space_sm;
        // The caption stands left of the count, on its baseline.
        lv_obj_update_layout(sent_);
        lv_obj_align_to(sent_caption_, sent_, LV_ALIGN_OUT_LEFT_BOTTOM, -kCaptionGap,
                        theme.font(view::FontRole::Small)->base_line - theme.font(view::FontRole::Mono)->base_line);
        lv_obj_update_layout(sent_caption_);
        const int32_t width = view::kScreenWidth - 2 * (kSideMargin + inset) - lv_obj_get_width(sent_) -
                              kCaptionGap - lv_obj_get_width(sent_caption_) - kStatusGap;
        lv_obj_set_size(status_, std::max<int32_t>(width, 0), kStatusHeight);
    }
}

void ToolsScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<ToolsScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

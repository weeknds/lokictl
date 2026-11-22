/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "style.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace view::style {
namespace {

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

// Only ASCII letters change; any other byte (including every byte of a
// multi-byte UTF-8 character) is kept as it is.
std::string ascii_upper(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return result;
}

// The lowercase twin of ascii_upper(), with the same rules.
std::string ascii_lower(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

void box_border(lv_obj_t* obj, const Theme& theme) {
    lv_obj_set_style_border_color(obj, theme.colors.border, 0);
    lv_obj_set_style_border_width(obj, theme.metrics.border_width, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(obj, theme.metrics.radius, 0);
}

} // namespace

void screen(lv_obj_t* obj, const Theme& theme) {
    lv_obj_set_style_bg_color(obj, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(obj, theme.colors.text, 0);
    lv_obj_set_style_text_font(obj, theme.font(FontRole::Body), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

void bar(lv_obj_t* obj, const Theme& theme, Edge rule_edge) {
    lv_obj_set_style_bg_color(obj, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, theme.colors.border, 0);
    lv_obj_set_style_border_width(obj, theme.metrics.border_width, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(
        obj, rule_edge == Edge::Top ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_text_color(obj, theme.colors.text, 0);
}

void popup(lv_obj_t* obj, const Theme& theme) {
    lv_obj_set_style_bg_color(obj, theme.colors.surface, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, theme.colors.border_strong, 0);
    lv_obj_set_style_border_width(obj, theme.metrics.border_width, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(obj, theme.metrics.radius, 0);
    lv_obj_set_style_text_color(obj, theme.colors.text, 0);
}

void text(lv_obj_t* label, const Theme& theme, FontRole role) {
    text(label, theme, role, theme.colors.text);
}

void text(lv_obj_t* label, const Theme& theme, FontRole role, lv_color_t color) {
    lv_obj_set_style_text_font(label, theme.font(role), 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void caption(lv_obj_t* label, const Theme& theme, const char* text) {
    lv_obj_set_style_text_font(label, theme.font(FontRole::Small), 0);
    lv_obj_set_style_text_color(label, theme.colors.muted, 0);
    lv_obj_set_style_text_letter_space(label, theme.metrics.caption_letter_space, 0);
    if (theme.metrics.uppercase_captions) {
        lv_label_set_text(label, ascii_upper(text ? text : "").c_str());
    } else {
        lv_label_set_text(label, text ? text : "");
    }
}

void card(lv_obj_t* obj, const Theme& theme) {
    lv_obj_set_style_bg_color(obj, theme.colors.surface, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    box_border(obj, theme);
    lv_obj_set_style_pad_all(obj, theme.metrics.space_sm, 0);
}

void list_row(lv_obj_t* row, const Theme& theme, bool selected) {
    const auto& metrics = theme.metrics;
    lv_obj_set_style_radius(row, metrics.radius, 0);
    lv_obj_set_style_border_width(row, metrics.border_width, 0);
    lv_obj_set_style_pad_hor(row, metrics.space_xs, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);

    if (!selected) {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(row, theme.colors.text, 0);
        lv_obj_set_style_text_font(row, theme.font(FontRole::Body), 0);
        return;
    }

    lv_obj_set_style_text_font(row, theme.font(FontRole::Strong), 0);
    if (metrics.selection_filled) {
        lv_obj_set_style_bg_color(row, theme.colors.selection, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(row, theme.colors.on_selection, 0);
        // Square themes frame the fill with their border colour.
        lv_obj_set_style_border_color(row, theme.colors.border, 0);
        lv_obj_set_style_border_opa(row, metrics.radius == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    } else {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(row, theme.colors.selection, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(row, theme.colors.selection, 0);
    }
}

Pane pane(lv_obj_t* parent,
          const Theme& theme,
          const char* title,
          int32_t width,
          int32_t height,
          lv_color_t title_color,
          PaneDensity density) {
    const auto& metrics = theme.metrics;
    const bool fit_content = height == LV_SIZE_CONTENT;
    const bool compact = density == PaneDensity::Compact;
    Pane result{};

    if (!metrics.pane_title_in_border) {
        // The box itself is the root; a caption sits at its top.
        auto* box = plain_object(parent);
        lv_obj_set_size(box, width, height);
        box_border(box, theme);
        lv_obj_set_style_pad_all(box, compact ? metrics.space_xs : metrics.space_sm, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, metrics.space_xs, 0);

        auto* title_label = plain_label(box);
        caption(title_label, theme, title);

        auto* content = plain_object(box);
        lv_obj_set_width(content, LV_PCT(100));
        if (fit_content) {
            lv_obj_set_height(content, LV_SIZE_CONTENT);
        } else {
            lv_obj_set_flex_grow(content, 1);
        }

        result.root = box;
        result.content = content;
        return result;
    }

    // The title sits on the top border line. The root is a transparent
    // wrapper; the bordered box starts half a title line below its top, and
    // the title label, filled with the ground colour, is drawn over the
    // border line after the box.
    const lv_font_t* title_font = theme.font(FontRole::Small);
    const int32_t title_height = lv_font_get_line_height(title_font);
    const int32_t box_top = title_height / 2;

    auto* root = plain_object(parent);
    lv_obj_set_size(root, width, height);

    auto* box = plain_object(root);
    lv_obj_set_pos(box, 0, box_top);
    lv_obj_set_size(box, width, fit_content ? LV_SIZE_CONTENT : height - box_top);
    box_border(box, theme);
    lv_obj_set_style_pad_hor(box, metrics.space_sm, 0);
    if (compact) {
        // Content starts 2 px into the title line: the title's ground fill
        // covers the first two content rows, which grid pages keep empty.
        lv_obj_set_style_pad_bottom(box, metrics.space_xs, 0);
        lv_obj_set_style_pad_top(
            box,
            std::max<int32_t>(metrics.space_xs,
                              title_height - box_top - metrics.border_width - 2),
            0);
    } else {
        lv_obj_set_style_pad_bottom(box, metrics.space_sm, 0);
        // Content starts below the title label, so a filled first row is not
        // cut by the title's background.
        lv_obj_set_style_pad_top(
            box,
            std::max<int32_t>(metrics.space_sm, title_height - box_top - metrics.border_width),
            0);
    }

    auto* content = plain_object(box);
    lv_obj_set_size(content, LV_PCT(100), fit_content ? LV_SIZE_CONTENT : LV_PCT(100));

    auto* title_label = plain_label(root);
    lv_label_set_text(title_label, title ? title : "");
    lv_obj_set_style_text_font(title_label, title_font, 0);
    lv_obj_set_style_text_color(title_label, title_color, 0);
    lv_obj_set_style_bg_color(title_label, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(title_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(title_label, metrics.space_xs, 0);
    lv_obj_set_pos(title_label, metrics.space_md, 0);

    result.root = root;
    result.content = content;
    return result;
}

lv_obj_t* pane_note(const Pane& pane, const Theme& theme, const char* text, lv_color_t color) {
    const auto& metrics = theme.metrics;
    auto* note = plain_label(pane.root);
    lv_label_set_text(note, text ? text : "");
    lv_obj_set_style_text_font(note, theme.font(FontRole::Small), 0);
    lv_obj_set_style_text_color(note, color, 0);

    if (!metrics.pane_title_in_border) {
        // The root is the box itself; keep the note out of its column of
        // children and put it in the top right corner, inside the padding.
        lv_obj_add_flag(note, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(note, LV_ALIGN_TOP_RIGHT, 0, 0);
        return note;
    }

    // Like the title: on the border line, filled with the ground colour, at
    // the title's distance from the edge. Made after the box, so it is drawn
    // over the line.
    lv_obj_set_style_bg_color(note, theme.colors.background, 0);
    lv_obj_set_style_bg_opa(note, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(note, metrics.space_xs, 0);
    lv_obj_align(note, LV_ALIGN_TOP_RIGHT, -metrics.space_md, 0);
    return note;
}

SoftKeyLook soft_key(const Theme& theme, SoftKeyState state) {
    const auto& colors = theme.colors;
    const auto& metrics = theme.metrics;
    SoftKeyLook look{};
    look.box_filled = metrics.selection_filled;
    look.radius = std::min<int32_t>(metrics.radius, 2);

    switch (state) {
        case SoftKeyState::Primary:
            look.box = colors.accent;
            look.number = look.box_filled ? colors.on_accent : colors.accent;
            look.label = metrics.dark ? colors.accent : colors.text;
            break;
        case SoftKeyState::Disabled:
            look.box = look.box_filled ? colors.border_subtle : colors.border;
            look.number = colors.muted;
            look.label = colors.muted;
            break;
        case SoftKeyState::Normal:
        default:
            look.box = metrics.dark ? colors.muted : colors.text;
            look.number = look.box_filled ? colors.background : look.box;
            look.label = colors.text;
            break;
    }
    return look;
}

std::string soft_key_text(const Theme& theme, std::string_view text) {
    return theme.metrics.uppercase_captions ? ascii_upper(text) : ascii_lower(text);
}

} // namespace view::style

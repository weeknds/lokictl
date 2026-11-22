/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "devices_screen.h"

#include "home_format.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

// Layout of the 320x110 area between the title bar and the soft-key bar:
// kVisibleCards cards of kCardHeight, then the scroll mark at the right
// edge, as on the Status page.
constexpr int32_t kContentHeight = view::kScreenHeight - view::kTitleBarHeight - view::kSoftKeyBarHeight;
constexpr int32_t kSideMargin  = 8;
constexpr int32_t kListTop     = 2;
constexpr int32_t kCardHeight  = 52;
constexpr int32_t kCardGap     = 2;
constexpr int32_t kCardRight   = view::kScreenWidth - 12;
constexpr int32_t kCardWidth   = kCardRight - kSideMargin;
// The three lines of a card, from its top.
constexpr int32_t kFirstLineY  = 1;
constexpr int32_t kSecondLineY = 19;
constexpr int32_t kThirdLineY  = 34;
// Room for the right-hand texts: the signal with "new", the rate and the
// time joined; the left-hand texts get the rest of the line.
constexpr int32_t kSignalWidth = 112;
constexpr int32_t kRateWidth   = 112;
constexpr int32_t kJoinedWidth = 160;
constexpr int32_t kTextGap     = 4;
// The scroll mark: a thin track with a wider thumb, right of the cards.
constexpr int32_t kTrackX      = view::kScreenWidth - 6;
constexpr int32_t kThumbX      = kTrackX - 1;
constexpr int32_t kThumbWidth  = 3;
constexpr int32_t kThumbMinHeight = 8;
constexpr int32_t kListHeight  =
    DevicesScreen::kVisibleCards * kCardHeight + (DevicesScreen::kVisibleCards - 1) * kCardGap;
// The box shown without devices.
constexpr int32_t kEmptyWidth  = 240;
constexpr int32_t kEmptyHeight = 72;

static_assert(kListTop + kListHeight <= kContentHeight, "the cards must end above the soft-key bar");
static_assert(kThirdLineY + 16 <= kCardHeight, "the third line must fit in its card");

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

// A one-line label of a fixed width in the Small font, at `x`, `y` of its
// card, right-aligned in its box when `right` is true.
lv_obj_t* small_label(lv_obj_t* card, const view::Theme& theme, int32_t x, int32_t y, int32_t width, bool right) {
    auto* label = plain_label(card);
    const lv_font_t* small = theme.font(view::FontRole::Small);
    lv_obj_set_style_text_font(label, small, 0);
    fix_width(label, width, small);
    if (right) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_set_pos(label, x, y);
    return label;
}

} // namespace

DevicesScreen::DevicesScreen(viewmodel::BaseViewModel& view_model,
                             app::AssetManager& assets,
                             view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init();
}

bool DevicesScreen::handles(model::AppPage page) {
    return page == model::AppPage::Devices;
}

view::widgets::HelpContent DevicesScreen::help_content() {
    return {"DEVICES",
            {
                {"ESC / 4", "Back"},
                {"F X / 5 6", "Previous / next device"},
                {"7", "Hotspot: join code"},
                {"8", "Block / allow the device"},
            }};
}

view::widgets::HelpContent DevicesScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet DevicesScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {"Up", [this] { move(-1); }},
        {"Down", [this] { move(1); }},
        {"QR", [&view_model] { view_model.open_hotspot(); }},
        block_soft_key(block_key_shown_ = block_key()),
    }};
}

DevicesScreen::BlockKey DevicesScreen::block_key() const {
    const auto& clients = view_model().home().clients;
    if (clients.empty()) {
        return BlockKey::Off;
    }
    const model::HomeClient& client = clients[static_cast<std::size_t>(selected())];
    if (client.hardware_address.empty()) {
        return BlockKey::Off;
    }
    return client.blocked ? BlockKey::Allow : BlockKey::Block;
}

view::widgets::SoftKey DevicesScreen::block_soft_key(BlockKey key) {
    return {key == BlockKey::Allow ? "Allow" : "Block", [this] { toggle_block(); }, false, key != BlockKey::Off};
}

void DevicesScreen::toggle_block() {
    const auto& clients = view_model().home().clients;
    if (clients.empty()) {
        return;
    }
    const model::HomeClient& client = clients[static_cast<std::size_t>(selected())];
    if (client.hardware_address.empty()) {
        return;
    }
    view_model().request_block(client.hardware_address, !client.blocked);
}

bool DevicesScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_LEFT:
            move(-1);
            return true;
        case LV_KEY_DOWN:
        case LV_KEY_RIGHT:
            move(1);
            return true;
        default:
            return false;
    }
}

int32_t DevicesScreen::selected() const {
    const auto count = static_cast<int32_t>(view_model().home().clients.size());
    return std::clamp<int32_t>(view_model().devices_selection(), 0, std::max<int32_t>(count - 1, 0));
}

void DevicesScreen::move(int32_t delta) {
    const auto count = static_cast<int32_t>(view_model().home().clients.size());
    if (count == 0) {
        return;
    }
    const int32_t current = selected();
    const int32_t next = std::clamp<int32_t>(current + delta, 0, count - 1);
    // Stored even when it stays, so a value past the end comes back to the
    // last device shown.
    if (next != current || view_model().devices_selection() != current) {
        view_model().set_devices_selection(next);
    }
}

void DevicesScreen::build_content(lv_obj_t* content) {
    const auto& theme = this->theme();
    const lv_font_t* body = theme.font(view::FontRole::Body);
    const lv_font_t* small = theme.font(view::FontRole::Small);
    const int32_t inner = kCardWidth - 2 * theme.metrics.space_xs;
    // The signal stands on the name's baseline.
    const int32_t signal_y = kFirstLineY + baseline_from_top(body) - baseline_from_top(small);

    for (int32_t index = 0; index < kVisibleCards; ++index) {
        Card& card = cards_[static_cast<std::size_t>(index)];
        card.card = plain_object(content);
        lv_obj_set_pos(card.card, kSideMargin, kListTop + index * (kCardHeight + kCardGap));
        lv_obj_set_size(card.card, kCardWidth, kCardHeight);

        // The name inherits the card's font and colour (view::style::list_row()).
        card.name = plain_label(card.card);
        fix_width(card.name, inner - kSignalWidth - kTextGap, body);
        lv_obj_set_pos(card.name, 0, kFirstLineY);
        card.signal = small_label(card.card, theme, inner - kSignalWidth, signal_y, kSignalWidth, true);
        card.address = small_label(card.card, theme, 0, kSecondLineY, inner - kRateWidth - kTextGap, false);
        card.rate = small_label(card.card, theme, inner - kRateWidth, kSecondLineY, kRateWidth, true);
        card.hardware = small_label(card.card, theme, 0, kThirdLineY, inner - kJoinedWidth - kTextGap, false);
        card.joined = small_label(card.card, theme, inner - kJoinedWidth, kThirdLineY, kJoinedWidth, true);
        lv_obj_add_flag(card.card, LV_OBJ_FLAG_HIDDEN);
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

    empty_box_ = plain_object(content);
    view::style::card(empty_box_, theme);
    lv_obj_set_size(empty_box_, kEmptyWidth, kEmptyHeight);
    lv_obj_align(empty_box_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(empty_box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(empty_box_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(empty_box_, theme.metrics.space_xs, 0);
    empty_title_ = plain_label(empty_box_);
    view::style::text(empty_title_, theme, view::FontRole::Strong);
    empty_note_ = plain_label(empty_box_);
    view::style::text(empty_note_, theme, view::FontRole::Small, theme.colors.muted);
    lv_obj_set_width(empty_note_, LV_PCT(100));
    lv_label_set_long_mode(empty_note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(empty_note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(empty_box_, LV_OBJ_FLAG_HIDDEN);

    // LVGL calls each observer once right away: the first fills the cards,
    // the second shows them again (nothing changes then).
    lv_subject_add_observer_obj(view_model().home_data_subject(), data_observer, content, this);
    lv_subject_add_observer_obj(view_model().devices_selection_subject(), selection_observer, content, this);
}

void DevicesScreen::style_card(Card& card, int32_t style) {
    if (card.style_shown == style) {
        return;
    }
    const auto& theme = this->theme();
    const bool was_hidden = card.style_shown < 0;
    card.style_shown = style;
    if (style < 0) {
        lv_obj_add_flag(card.card, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (was_hidden) {
        lv_obj_remove_flag(card.card, LV_OBJ_FLAG_HIDDEN);
    }

    const bool highlighted = style == 1;
    view::style::list_row(card.card, theme, highlighted);
    // On the highlight every line takes the card's colour; otherwise the
    // address is in the address colour and the hardware address and the
    // time are muted. The right-hand text of the first line is coloured in
    // refresh(), because it depends on the device too.
    if (highlighted) {
        lv_obj_remove_local_style_prop(card.rate, LV_STYLE_TEXT_COLOR, 0);
    } else {
        lv_obj_set_style_text_color(card.rate, theme.colors.text, 0);
    }
    if (highlighted) {
        lv_obj_remove_local_style_prop(card.address, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(card.hardware, LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(card.joined, LV_STYLE_TEXT_COLOR, 0);
    } else {
        lv_obj_set_style_text_color(card.address, theme.colors.accent2, 0);
        lv_obj_set_style_text_color(card.hardware, theme.colors.muted, 0);
        lv_obj_set_style_text_color(card.joined, theme.colors.muted, 0);
    }
}

void DevicesScreen::refresh() {
    const model::HomeData& data = view_model().home();
    const auto& clients = data.clients;
    const auto count = static_cast<int32_t>(clients.size());
    const int32_t chosen = selected();
    // The highlighted card is the top one, unless the list ends first.
    const int32_t top = std::min<int32_t>(chosen, std::max<int32_t>(count - kVisibleCards, 0));

    for (int32_t index = 0; index < kVisibleCards; ++index) {
        Card& card = cards_[static_cast<std::size_t>(index)];
        const int32_t position = top + index;
        if (position >= count) {
            style_card(card, -1);
            continue;
        }
        const model::HomeClient& client = clients[static_cast<std::size_t>(position)];
        set_text_if_changed(card.name, card.name_shown, home::client_name_text(client));
        // A signal that is not known is left out here: some Wi-Fi chips
        // never tell it for the devices on their access point. A blocked
        // device says only that.
        const bool signal_known = client.signal_dbm != model::kSignalUnknown;
        set_text_if_changed(card.signal, card.signal_shown,
                            client.blocked ? std::string("blocked")
                                           : home::join_parts({client.is_new ? "new" : "",
                                                               signal_known ? home::signal_text(client.signal_dbm)
                                                                            : ""}));
        set_text_if_changed(card.address, card.address_shown, home::text_or_unknown(client.address));
        set_text_if_changed(card.rate, card.rate_shown, home::client_rate_text(client.rate_kbps));
        set_text_if_changed(card.hardware, card.hardware_shown, home::text_or_unknown(client.hardware_address));
        set_text_if_changed(card.joined, card.joined_shown, home::joined_text(client.connected_seconds));
        style_card(card, position == chosen ? 1 : 0);
        const int32_t tone = position == chosen ? 0 : (client.blocked ? 2 : 1);
        if (card.tone_shown != tone) {
            card.tone_shown = tone;
            if (tone == 0) {
                lv_obj_remove_local_style_prop(card.signal, LV_STYLE_TEXT_COLOR, 0);
            } else {
                lv_obj_set_style_text_color(card.signal, tone == 2 ? theme().colors.warning : theme().colors.text, 0);
            }
        }
    }

    const BlockKey key = block_key();
    if (key != block_key_shown_ && soft_key_bar()) {
        block_key_shown_ = key;
        soft_key_bar()->set_key(4, block_soft_key(key));
    }

    // The scroll mark, only when there are more devices than cards.
    if (count > kVisibleCards) {
        const int32_t thumb_height = std::max<int32_t>(kThumbMinHeight, kListHeight * kVisibleCards / count);
        const int32_t last_top = count - kVisibleCards;
        const int32_t thumb_y = kListTop + (kListHeight - thumb_height) * top / last_top;
        lv_obj_set_pos(thumb_, kThumbX, thumb_y);
        lv_obj_set_size(thumb_, kThumbWidth, thumb_height);
        lv_obj_remove_flag(track_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(thumb_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(track_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(thumb_, LV_OBJ_FLAG_HIDDEN);
    }

    // The box without devices.
    if (count == 0) {
        std::string title;
        std::string note;
        if (data.ap_active) {
            title = "No devices yet";
            note = "Join " + home::text_or_unknown(data.ap_name) + " to see a device here. Key 7 shows the code.";
        } else {
            title = "Router is off";
            note = "Devices on the hotspot show here while Router runs. Turn it on in Modes.";
        }
        set_text_if_changed(empty_title_, empty_title_shown_, title);
        set_text_if_changed(empty_note_, empty_note_shown_, note);
        lv_obj_remove_flag(empty_box_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(empty_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void DevicesScreen::data_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<DevicesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

void DevicesScreen::selection_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<DevicesScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "home_screen.h"

#include "theme.h"

namespace screen {

HomeScreen::HomeScreen(viewmodel::BaseViewModel& view_model,
                       app::AssetManager& assets,
                       view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init(Layout::NoTitleBar);
}

HomeScreen::~HomeScreen() = default;

bool HomeScreen::handles(model::AppPage page) {
    return page == model::AppPage::Home;
}

view::widgets::HelpContent HomeScreen::help_content() {
    return {"HOME",
            {
                {"HOLD ESC", "Exit"},
                {"F Z / X C", "Previous / next client"},
                {"ENTER", "Devices"},
                {"4", "Modes"},
                {"6 / 8", "Settings / Status"},
            }};
}

view::widgets::HelpContent HomeScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet HomeScreen::soft_keys() {
    // Called from init(), before build_content(): it uses nothing but the
    // view model, which outlives every screen. Scan and Tools open pages
    // that do not exist yet, so they are shown switched off.
    auto& view_model = this->view_model();
    return {{
        {"Modes", [&view_model] { view_model.open_modes(); }},
        {"Scan", {}, false, false},
        {"Setup", [&view_model] { view_model.open_settings(); }, true},
        {"Tools", {}, false, false},
        {"Info", [&view_model] { view_model.open_status(); }},
    }};
}

// Left and up select the previous client, right and down the next one. The
// view model stops the selection at the first and the last client. Enter
// opens the Devices page, on the selected client.
bool HomeScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_LEFT:
        case LV_KEY_UP:
            view_model().home_select(-1);
            return true;
        case LV_KEY_RIGHT:
        case LV_KEY_DOWN:
            view_model().home_select(1);
            return true;
        case LV_KEY_ENTER:
            view_model().open_devices();
            return true;
        default:
            return false;
    }
}

HomeContext HomeScreen::context() {
    return HomeContext{home_content_,
                       theme(),
                       view_model().home(),
                       lv_subject_get_string(view_model().title_subject())};
}

void HomeScreen::build_layout() {
    switch (theme().id) {
        case view::ThemeId::Instrument:
            layout_ = make_instrument_home_layout();
            break;
        case view::ThemeId::Constellation:
            layout_ = make_constellation_home_layout();
            break;
        case view::ThemeId::Terminal:
            layout_ = make_terminal_home_layout();
            break;
        default:
            // A theme without a layout of its own gets the Instrument layout.
            layout_ = make_instrument_home_layout();
            break;
    }

    layout_->build(context());
    shown_ = view_model().home();
}

void HomeScreen::build_content(lv_obj_t* content) {
    home_content_ = content;
    build_layout();

    // LVGL calls each observer once right away. For the values that call
    // changes nothing, because build() has just shown them.
    lv_subject_add_observer_obj(view_model().home_data_subject(), data_observer, content, this);

    // Added last: its first call marks the selected client.
    lv_subject_add_observer_obj(view_model().home_selection_subject(),
                                selection_observer,
                                content,
                                this);
}

void HomeScreen::data_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<HomeScreen*>(lv_observer_get_user_data(observer));
    if (!screen || !screen->layout_) {
        return;
    }

    const model::HomeData& next = screen->view_model().home();
    if (home_values_only(screen->shown_, next)) {
        // Only values the layout shows in place changed.
        screen->layout_->update(screen->context());
        screen->shown_ = next;
        return;
    }

    // A new situation: the content area is emptied and the layout is built
    // afresh with the new values and the current selection. This is safe
    // because both observers of this screen are bound to the content area
    // itself, which stays, and only its children go; the layout's pointers
    // go with the old layout, which is dropped first. The soft-key bar is a
    // child of the screen's root, the help panel and the messages live on
    // the top layer, and layouts add no observers of their own, so nothing
    // else is touched. The content area itself is never deleted, and the
    // screen manager is never asked: it must not be called from an
    // observer.
    screen->layout_.reset();
    lv_obj_clean(screen->home_content_);
    screen->build_layout();
    screen->layout_->select(screen->context(), screen->view_model().home_selection());
}

void HomeScreen::selection_observer(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<HomeScreen*>(lv_observer_get_user_data(observer));
    if (!screen || !screen->layout_) {
        return;
    }

    // When new values arrive, the view model pulls the selection back
    // before it counts the new block. If that block is a new situation, the
    // layout still shows the old one; the data observer, which always
    // follows, builds the page again and marks the selection then.
    if (!home_values_only(screen->shown_, screen->view_model().home())) {
        return;
    }

    screen->layout_->select(screen->context(), screen->view_model().home_selection());
}

} // namespace screen

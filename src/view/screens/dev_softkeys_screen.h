/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"
#include "base_screen.h"
#include "soft_key_bar.h"

#include <memory>

namespace screen {

// Developer page for the soft-key bar. It is a normal page with a title bar
// and its own bar at the bottom (a main key, a switched-off key and an unused
// one). Above it are two sample bars:
// - five 5-letter words, the widest keys a bar can show;
// - a bar built with other keys and then changed key by key with set_key()
//   to the page's own keys, so it must look exactly like the bottom bar.
// The sample bars do not react to keys 4-8. Opened by name only (snapshot
// --page dev-softkeys); not reachable from the normal UI.
class DevSoftKeysScreen : public BaseScreen {
public:
    DevSoftKeysScreen(viewmodel::BaseViewModel& view_model,
                      app::AssetManager& assets,
                      view::ThemeEngine& theme_engine);
    ~DevSoftKeysScreen() override;

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // Destroyed in ~DevSoftKeysScreen(), before the base class deletes the
    // screen and with it the bars' LVGL objects.
    std::unique_ptr<view::widgets::SoftKeyBar> widest_bar_;
    std::unique_ptr<view::widgets::SoftKeyBar> changed_bar_;
};

} // namespace screen

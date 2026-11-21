/*
 * SPDX-FileCopyrightText: 2026 Netdeck contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asset_manager.h"
#include "base_model.h"
#include "base_screen.h"

#include <cstddef>

namespace screen {

// Developer pages that show the bundled text fonts: one overview page with
// every font file, and one page per family with its symbols and a count of
// the symbols the font can draw. They are opened by name only (snapshot
// --page) and are not reachable from the normal UI.
class DevFontsScreen : public BaseScreen {
public:
    DevFontsScreen(viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   model::AppPage page);
    ~DevFontsScreen() override;

    // True for the pages this screen can show.
    static bool handles(model::AppPage page);

private:
    void build_content(lv_obj_t* content) override;
    void build_overview(lv_obj_t* content);
    void build_family(lv_obj_t* content, size_t family_index);

    model::AppPage page_;
    app::FontSet fonts_;
    lv_obj_t* body_{nullptr};
};

} // namespace screen

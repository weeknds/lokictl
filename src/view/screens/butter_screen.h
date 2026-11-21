/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

class ButterScreen : public BaseScreen {
public:
    ButterScreen(viewmodel::BaseViewModel& view_model,
                 app::AssetManager& assets,
                 view::ThemeEngine& theme_engine);

private:
    void build_content(lv_obj_t* content) override;
};

} // namespace screen

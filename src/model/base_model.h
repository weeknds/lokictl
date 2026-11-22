/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace model {

enum class AppPage {
    Apple  = 0,
    Butter = 1,

    // Developer pages. They are opened by name only (snapshot --page) and
    // are never linked from the normal UI.
    DevFonts = 100,
    DevFontsSpaceGrotesk,
    DevFontsIbmPlexMono,
    DevFontsGeistMono,
    DevFontsJetBrainsMono,
    DevFontsInter,
    DevTheme,
    DevHelp,
    DevToast,
};

class BaseModel {
public:
    const char* app_title() const;
    const char* greeting() const;

    AppPage current_page() const;
    void set_current_page(AppPage page);
    void toggle_page();

private:
    AppPage current_page_ = AppPage::Apple;
};

} // namespace model

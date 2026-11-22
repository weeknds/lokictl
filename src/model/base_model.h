/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <array>
#include <cstddef>

namespace model {

enum class AppPage {
    Home     = 0,
    Butter   = 1,
    KeyTest  = 2,
    Settings = 3,
    Status   = 4,
    Modes    = 5,
    Hotspot  = 6,
    Devices  = 7,

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
    DevSoftKeys,
};

// The longest path of pages the app keeps: the first page plus seven opened
// on top of it.
constexpr std::size_t kMaxPageDepth = 8;

class BaseModel {
public:
    const char* app_title() const;

    // The values the home page shows. They start as the sample values of
    // sample_home_data(); the data layer replaces them with set_home()
    // whenever a new block of values arrives.
    const HomeData& home() const;
    void set_home(const HomeData& data);

    // The path of open pages, from the first page to the one on top. Only
    // the top page is shown; the pages below it are rebuilt when the user
    // goes back to them. The path never holds the same page twice.
    AppPage current_page() const;
    std::size_t page_depth() const;
    // The path becomes just `first`.
    void reset_pages(AppPage first);
    // Opens `page` on top of the path. Returns false and changes nothing when
    // `page` is already on top or the path is full. A page that is already
    // further down the path is not added twice: the path is cut back to it
    // instead.
    bool open_page(AppPage page);
    // Closes the top page. Returns false on the first page, which is never
    // closed.
    bool go_back();

private:
    std::array<AppPage, kMaxPageDepth> pages_{AppPage::Home};
    std::size_t depth_ = 1;
    HomeData home_{sample_home_data()};
};

} // namespace model

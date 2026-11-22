/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstddef>

namespace model {

enum class AppPage {
    Apple   = 0,
    Butter  = 1,
    KeyTest = 2,

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
    const char* greeting() const;

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
    std::array<AppPage, kMaxPageDepth> pages_{AppPage::Apple};
    std::size_t depth_ = 1;
};

} // namespace model

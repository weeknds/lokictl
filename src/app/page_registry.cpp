/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "page_registry.h"

namespace app {

const std::vector<PageInfo>& page_registry() {
    // Add one line here for every page added to model::AppPage.
    // The first entry is the default page.
    static const std::vector<PageInfo> pages{
        {"demo-home", model::AppPage::Apple},
        {"demo-counter", model::AppPage::Butter},
        {"demo-keys", model::AppPage::KeyTest},
        // Pages starting with "dev-" are for developers only; nothing in the
        // app links to them.
        {"dev-fonts", model::AppPage::DevFonts},
        {"dev-fonts-space-grotesk", model::AppPage::DevFontsSpaceGrotesk},
        {"dev-fonts-ibm-plex-mono", model::AppPage::DevFontsIbmPlexMono},
        {"dev-fonts-geist-mono", model::AppPage::DevFontsGeistMono},
        {"dev-fonts-jetbrains-mono", model::AppPage::DevFontsJetBrainsMono},
        {"dev-fonts-inter", model::AppPage::DevFontsInter},
        {"dev-theme", model::AppPage::DevTheme},
        {"dev-help", model::AppPage::DevHelp},
        {"dev-toast", model::AppPage::DevToast},
        {"dev-softkeys", model::AppPage::DevSoftKeys},
    };
    return pages;
}

const PageInfo* find_page(std::string_view name) {
    for (const auto& entry : page_registry()) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

const char* page_name(model::AppPage page) {
    for (const auto& entry : page_registry()) {
        if (entry.page == page) {
            return entry.name;
        }
    }
    return "unknown";
}

} // namespace app

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_model.h"

#include <string_view>
#include <vector>

namespace app {

// Pages that can be opened by name (command line, screenshots).
struct PageInfo {
    const char* name;       // lowercase words joined by dashes
    model::AppPage page;
};

// All known pages; the first entry is the default page.
const std::vector<PageInfo>& page_registry();
const PageInfo* find_page(std::string_view name);
// The registry name of `page`, or "unknown" for a page that is not listed.
const char* page_name(model::AppPage page);

} // namespace app

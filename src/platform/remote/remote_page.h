/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string_view>

namespace platform::remote {

// The screen page of Remote view (GET /screen): a form for the code, then
// the screen with a key pad laid out like the device, typing from the
// computer's keyboard, and the saved reports as download links.
// Everything is in the page itself; it loads nothing from anywhere else.
// It is the one page with inline script and style, under its own policy,
// until the dashboard's own "Cardputer screen" page takes its place.
std::string_view page_html();

// The Content-Security-Policy the page is served with: only its own inline
// script and style, pictures from blob: URLs and requests to the server
// itself.
std::string_view page_security_policy();

} // namespace platform::remote

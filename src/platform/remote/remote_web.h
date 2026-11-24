/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string_view>

namespace platform::remote {

// Where a file of the web dashboard comes from.
enum class WebFolder {
    Web,   // the dashboard's own files (assets/web, installed to /usr/share/lokictl/web)
    Fonts, // the app's own fonts (assets/fonts, installed to /usr/share/lokictl/fonts)
};

// One file the server hands out without a session: the dashboard's page,
// style, scripts, icon and fonts. They hold nothing private, and the
// pairing screen needs them before there is a session.
struct WebFile {
    std::string_view path;          // the request path, for example "/app.js"
    WebFolder folder;
    std::string_view name;          // the file's name in that folder
    std::string_view content_type;  // exact, as X-Content-Type-Options: nosniff wants it
    std::string_view cache_control; // "no-store" for the app's own files, a week for fonts
};

// The file for a request path, from a fixed table; nullptr for any other
// path. Paths are never mapped to file names any other way, so nothing
// outside the table can be read.
const WebFile* find_web_file(std::string_view path);

} // namespace platform::remote

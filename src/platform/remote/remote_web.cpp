/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote/remote_web.h"

namespace platform::remote {
namespace {

constexpr std::string_view kNoStore = "no-store";
// The fonts never change between versions; a browser that is not on an
// Apple device loads them once a week at most.
constexpr std::string_view kWeek = "private, max-age=604800";
constexpr std::string_view kScript = "text/javascript; charset=utf-8";

constexpr WebFile kFiles[] = {
    {"/", WebFolder::Web, "index.html", "text/html; charset=utf-8", kNoStore},
    {"/app.css", WebFolder::Web, "app.css", "text/css; charset=utf-8", kNoStore},
    {"/app.js", WebFolder::Web, "app.js", kScript, kNoStore},
    {"/pair.js", WebFolder::Web, "pair.js", kScript, kNoStore},
    {"/shell.js", WebFolder::Web, "shell.js", kScript, kNoStore},
    {"/ui.js", WebFolder::Web, "ui.js", kScript, kNoStore},
    {"/home.js", WebFolder::Web, "home.js", kScript, kNoStore},
    {"/devices.js", WebFolder::Web, "devices.js", kScript, kNoStore},
    {"/security.js", WebFolder::Web, "security.js", kScript, kNoStore},
    {"/preact.module.js", WebFolder::Web, "preact.module.js", kScript, kNoStore},
    {"/icon.svg", WebFolder::Web, "icon.svg", "image/svg+xml", kNoStore},
    {"/fonts/Inter-Regular.ttf", WebFolder::Fonts, "Inter-Regular.ttf", "font/ttf", kWeek},
    {"/fonts/Inter-SemiBold.ttf", WebFolder::Fonts, "Inter-SemiBold.ttf", "font/ttf", kWeek},
};

} // namespace

const WebFile* find_web_file(std::string_view path) {
    for (const WebFile& file : kFiles) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

} // namespace platform::remote

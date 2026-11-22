/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace app {

class AssetManager {
public:
    explicit AssetManager(std::string app_name = default_app_name(),
                          std::filesystem::path cmake_assets_root = default_cmake_assets_root());
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    static std::string default_app_name();
    static std::filesystem::path default_cmake_assets_root();

    const std::vector<std::filesystem::path>& roots() const;
    std::filesystem::path resolve(const std::filesystem::path& relative_path) const;
    std::filesystem::path resolve_font(const std::filesystem::path& file_name) const;

    // A loaded font is identified by its file, its size and its fallback
    // font (the font LVGL asks for characters this one does not have). The
    // same file at the same size with another fallback is a separate font
    // instance, so giving one user a fallback never changes the font other
    // users see.

    // Loads a font from the fonts folder, without a fallback. It stays
    // loaded until the AssetManager is destroyed, so callers never free it.
    lv_font_t* load_font(const std::filesystem::path& file_name, uint32_t size);

    // Counted loading, for fonts that are only needed for a while. Every
    // successful acquire_font() must be matched by one release_font(); the
    // font is freed when the last user releases it, unless load_font() has
    // also loaded it. `fallback` must stay loaded for as long as this font
    // is: acquire it first and release it last. Prefer FontSet over calling
    // these directly.
    lv_font_t* acquire_font(const std::filesystem::path& file_name,
                            uint32_t size,
                            const lv_font_t* fallback = nullptr);
    void release_font(const lv_font_t* font);

    // Number of font instances loaded right now, both the ones kept for
    // good and the counted ones. Used by logs and leak checks.
    std::size_t loaded_font_count() const;

private:
    struct LoadedFont;

    LoadedFont* find_or_load(const std::filesystem::path& file_name,
                             uint32_t size,
                             const lv_font_t* fallback);
    LoadedFont* load_pinned(const std::filesystem::path& file_name,
                            uint32_t size,
                            const lv_font_t* fallback);
    void add_root_if_valid(std::filesystem::path path);

    std::vector<std::filesystem::path> roots_;
    std::vector<std::unique_ptr<LoadedFont>> loaded_fonts_;
};

// Keeps a group of fonts loaded for as long as one screen or theme needs
// them, and gives them back when it is cleared or destroyed.
//
// Rules:
// - Delete the LVGL objects that use these fonts before calling clear() or
//   destroying the FontSet. Labels keep plain pointers to their font.
// - Load a fallback font before the font that uses it. clear() gives the
//   fonts back in the reverse order, so a font always goes before its
//   fallback.
// - A FontSet must be destroyed before the AssetManager it was made with.
class FontSet {
public:
    explicit FontSet(AssetManager& assets);
    ~FontSet();

    FontSet(const FontSet&) = delete;
    FontSet& operator=(const FontSet&) = delete;

    // Returns the font, or nullptr if it could not be loaded. `fallback` is
    // used for characters the font does not have (nullptr: none).
    lv_font_t* load(const std::filesystem::path& file_name,
                    uint32_t size,
                    const lv_font_t* fallback = nullptr);
    // Gives back every font loaded through this set, newest first.
    void clear();

private:
    AssetManager& assets_;
    std::vector<lv_font_t*> fonts_;
};

} // namespace app

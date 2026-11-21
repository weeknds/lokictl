/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "asset_manager.h"

#include "logger.h"

#if LV_USE_FREETYPE
#include "src/libs/freetype/lv_freetype.h"
#endif

#include <algorithm>
#include <utility>

#ifndef APP_NAME
#error "APP_NAME must be defined by the build system (see CMakeLists.txt)"
#endif

#ifndef APP_CMAKE_ASSETS_ROOT
#define APP_CMAKE_ASSETS_ROOT ""
#endif

#ifndef APP_SOURCE_ASSETS_ROOT
#define APP_SOURCE_ASSETS_ROOT "assets"
#endif

namespace app {
namespace {

#if !USE_DESKTOP
constexpr const char* kNotoSansCjkRegular =
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
constexpr const char* kNotoSansCjkBold =
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc";
#endif

bool path_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::filesystem::path weakly_canonical_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal() : canonical;
}

#if !USE_DESKTOP
const lv_font_t* fallback_font(uint32_t size) {
    if (size <= 10) {
        return &lv_font_montserrat_10;
    }
    if (size <= 12) {
        return &lv_font_montserrat_12;
    }
    if (size <= 14) {
        return &lv_font_montserrat_14;
    }
    if (size <= 16) {
        return &lv_font_montserrat_16;
    }
    if (size <= 18) {
        return &lv_font_montserrat_18;
    }
    if (size <= 20) {
        return &lv_font_montserrat_20;
    }
    if (size <= 24) {
        return &lv_font_montserrat_24;
    }
    return &lv_font_montserrat_28;
}
#endif

} // namespace

struct AssetManager::LoadedFont {
    std::filesystem::path path;
    uint32_t size;
    lv_font_t* font;
    // Number of acquire_font() calls not yet matched by release_font().
    uint32_t users{0};
    // Set by load_font(): the font then stays until the AssetManager goes.
    bool pinned{false};
};

AssetManager::AssetManager(std::string app_name, std::filesystem::path cmake_assets_root) {
    add_root_if_valid(std::move(cmake_assets_root));
    add_root_if_valid(default_cmake_assets_root());
    add_root_if_valid(APP_SOURCE_ASSETS_ROOT);
    add_root_if_valid(std::filesystem::path{"/usr/share"} / app_name);
}

AssetManager::~AssetManager() {
#if LV_USE_FREETYPE
    for (auto& loaded_font : loaded_fonts_) {
        if (loaded_font && loaded_font->font) {
            lv_freetype_font_delete(loaded_font->font);
        }
    }

#endif
}

std::string AssetManager::default_app_name() {
    return APP_NAME;
}

std::filesystem::path AssetManager::default_cmake_assets_root() {
    return APP_CMAKE_ASSETS_ROOT;
}

const std::vector<std::filesystem::path>& AssetManager::roots() const {
    return roots_;
}

std::filesystem::path AssetManager::resolve(const std::filesystem::path& relative_path) const {
    if (relative_path.is_absolute() && path_exists(relative_path)) {
        return weakly_canonical_path(relative_path);
    }

    for (const auto& root : roots_) {
        auto candidate = root / relative_path;
        if (path_exists(candidate)) {
            return weakly_canonical_path(candidate);
        }
    }

    return {};
}

std::filesystem::path AssetManager::resolve_font(const std::filesystem::path& file_name) const {
    return resolve(std::filesystem::path{"fonts"} / file_name);
}

lv_font_t* AssetManager::load_font(const std::filesystem::path& file_name, uint32_t size) {
    auto* loaded_font = find_or_load(file_name, size);
    if (!loaded_font) {
        return nullptr;
    }

    loaded_font->pinned = true;
    return loaded_font->font;
}

lv_font_t* AssetManager::acquire_font(const std::filesystem::path& file_name, uint32_t size) {
    auto* loaded_font = find_or_load(file_name, size);
    if (!loaded_font) {
        return nullptr;
    }

    ++loaded_font->users;
    return loaded_font->font;
}

void AssetManager::release_font(const lv_font_t* font) {
#if LV_USE_FREETYPE
    if (!font) {
        return;
    }

    auto it = std::find_if(loaded_fonts_.begin(), loaded_fonts_.end(), [&](const auto& loaded_font) {
        return loaded_font && loaded_font->font == font;
    });
    if (it == loaded_fonts_.end()) {
        LOG_WARN("release_font: unknown font {}", static_cast<const void*>(font));
        return;
    }

    auto& loaded_font = **it;
    if (loaded_font.users == 0) {
        LOG_WARN("release_font: font released more often than acquired: {} ({})",
                 loaded_font.path.string(),
                 loaded_font.size);
        return;
    }

    --loaded_font.users;
    if (loaded_font.users > 0 || loaded_font.pinned) {
        return;
    }

    const auto path = loaded_font.path;
    const auto size = loaded_font.size;
    lv_freetype_font_delete(loaded_font.font);
    loaded_fonts_.erase(it);
    LOG_INFO("released freetype font: {} ({})", path.string(), size);
#else
    LV_UNUSED(font);
#endif
}

AssetManager::LoadedFont* AssetManager::find_or_load(const std::filesystem::path& file_name,
                                                     uint32_t size) {
#if LV_USE_FREETYPE
    auto path = resolve_font(file_name);
    if (path.empty()) {
        LOG_WARN("font asset not found: {}", file_name.string());
        return nullptr;
    }

    auto existing = std::find_if(loaded_fonts_.begin(), loaded_fonts_.end(), [&](const auto& loaded_font) {
        return loaded_font && loaded_font->path == path && loaded_font->size == size;
    });
    if (existing != loaded_fonts_.end()) {
        return existing->get();
    }

    lv_font_t* font = lv_freetype_font_create(path.string().c_str(),
                                              LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                              size,
                                              LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!font) {
        LOG_WARN("failed to load freetype font: {}", path.string());
        return nullptr;
    }

    loaded_fonts_.push_back(std::make_unique<LoadedFont>(LoadedFont{path, size, font, 0, false}));
    LOG_INFO("loaded freetype font: {} ({})", path.string(), size);
    return loaded_fonts_.back().get();
#else
    LV_UNUSED(file_name);
    LV_UNUSED(size);
    LOG_WARN("FreeType font loading requested but LV_USE_FREETYPE is disabled");
    return nullptr;
#endif
}

lv_font_t* AssetManager::load_standard_font(uint32_t size, StandardFontWeight weight) {
#if USE_DESKTOP
    LV_UNUSED(size);
    LV_UNUSED(weight);
    return nullptr;
#else
    const char* path = weight == StandardFontWeight::Regular
                           ? kNotoSansCjkRegular
                           : kNotoSansCjkBold;
    auto* font = load_font(path, size);
    if (font) {
        font->fallback = fallback_font(size);
    }
    return font;
#endif
}

void AssetManager::add_root_if_valid(std::filesystem::path path) {
    if (path.empty()) {
        return;
    }

    path = weakly_canonical_path(path);
    if (!path_exists(path)) {
        return;
    }

    auto it = std::find(roots_.begin(), roots_.end(), path);
    if (it == roots_.end()) {
        roots_.push_back(std::move(path));
    }
}

FontSet::FontSet(AssetManager& assets) : assets_(assets) {}

FontSet::~FontSet() {
    clear();
}

lv_font_t* FontSet::load(const std::filesystem::path& file_name, uint32_t size) {
    auto* font = assets_.acquire_font(file_name, size);
    if (font) {
        fonts_.push_back(font);
    }
    return font;
}

void FontSet::clear() {
    for (auto* font : fonts_) {
        assets_.release_font(font);
    }
    fonts_.clear();
}

} // namespace app

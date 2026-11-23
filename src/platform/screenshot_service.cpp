/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "screenshot_service.h"

#include "lvgl.h"

#include <png.h>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace platform::screenshot {
namespace {

std::filesystem::path screenshot_directory() {
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        return std::filesystem::path(home) / "Pictures" / "Screenshots";
    }
#if !defined(_WIN32)
    if (const passwd* user = getpwuid(getuid()); user && user->pw_dir && user->pw_dir[0] != '\0') {
        return std::filesystem::path(user->pw_dir) / "Pictures" / "Screenshots";
    }
#endif
    return std::filesystem::path("/tmp") / "Screenshots";
}

std::string timestamp_filename() {
    const auto now = std::chrono::system_clock::now();
    const auto timestamp = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  now.time_since_epoch()) %
                              1000;

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &timestamp);
#else
    localtime_r(&timestamp, &local_time);
#endif

    char buffer[48]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "_%04d%02d%02d_%02d%02d%02d_%03d.png",
                  local_time.tm_year + 1900,
                  local_time.tm_mon + 1,
                  local_time.tm_mday,
                  local_time.tm_hour,
                  local_time.tm_min,
                  local_time.tm_sec,
                  static_cast<int>(milliseconds.count()));
    return std::string(APP_NAME) + buffer;
}

bool write_png(const std::filesystem::path& path,
               const lv_draw_buf_t* snapshot,
               std::string& error) {
    if (!snapshot || !snapshot->data || snapshot->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        error = "Invalid LVGL snapshot";
        return false;
    }

    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) {
        error = "Unable to open PNG file";
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info) {
        if (png) {
            png_destroy_write_struct(&png, nullptr);
        }
        std::fclose(file);
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        error = "Unable to initialize PNG encoder";
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        error = "PNG encoding failed";
        return false;
    }

    const auto width = static_cast<png_uint_32>(snapshot->header.w);
    const auto height = static_cast<png_uint_32>(snapshot->header.h);
    png_init_io(png, file);
    png_set_IHDR(png,
                 info,
                 width,
                 height,
                 8,
                 PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    std::vector<uint8_t> row(static_cast<std::size_t>(width) * 4U);
    const auto* source = static_cast<const uint8_t*>(snapshot->data);
    for (png_uint_32 y = 0; y < height; ++y) {
        const auto* source_row = source + static_cast<std::size_t>(y) * snapshot->header.stride;
        for (png_uint_32 x = 0; x < width; ++x) {
            const auto* bgra = source_row + static_cast<std::size_t>(x) * 4U;
            auto* rgba = row.data() + static_cast<std::size_t>(x) * 4U;
            rgba[0] = bgra[2];
            rgba[1] = bgra[1];
            rgba[2] = bgra[0];
            // A screen capture is always opaque. LVGL can leave alpha at
            // 253-254 on anti-aliased image edges, so it is not copied.
            rgba[3] = 0xFF;
        }
        png_write_row(png, row.data());
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return true;
}

} // namespace

CaptureResult capture_active_screen() {
    return capture_active_screen_to(screenshot_directory() / timestamp_filename());
}

CaptureResult capture_active_screen_to(const std::filesystem::path& path) {
    CaptureResult result;
    auto* screen = lv_screen_active();
    if (!screen) {
        result.error = "No active screen";
        return result;
    }

    // The folders made here, deepest first, so a failed capture can take
    // them away again instead of leaving them empty.
    std::vector<std::filesystem::path> made;
    const auto remove_made = [&made] {
        std::error_code remove_error;
        for (const auto& folder : made) {
            std::filesystem::remove(folder, remove_error);  // only an empty folder goes
        }
    };
    if (const auto directory = path.parent_path(); !directory.empty()) {
        std::error_code filesystem_error;
        for (auto folder = directory; !folder.empty() && !std::filesystem::exists(folder, filesystem_error);
             folder = folder.parent_path()) {
            made.push_back(folder);
            if (folder == folder.parent_path()) {
                break;
            }
        }
        std::filesystem::create_directories(directory, filesystem_error);
        if (filesystem_error) {
            result.error = "Unable to create output directory: " + filesystem_error.message();
            remove_made();
            return result;
        }
    }

    lv_draw_buf_t* snapshot = lv_snapshot_take(screen, LV_COLOR_FORMAT_ARGB8888);
    if (!snapshot) {
        result.error = "LVGL snapshot failed";
        remove_made();
        return result;
    }

    result.width = snapshot->header.w;
    result.height = snapshot->header.h;
    result.success = write_png(path, snapshot, result.error);
    lv_draw_buf_destroy(snapshot);
    if (result.success) {
        result.path = path.string();
    } else {
        remove_made();
    }
    return result;
}

} // namespace platform::screenshot

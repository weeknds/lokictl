/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "snapshot.h"

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "logger.h"
#include "screen_manager.h"
#include "screenshot_service.h"
#include "theme_engine.h"

#include "lvgl.h"

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

namespace app {
namespace {

// LVGL time advanced per loop step, and the fewest steps to run so the
// deferred first screen load always happens, even with --settle-ms 0.
constexpr uint32_t kTickStepMs = 5;
constexpr uint32_t kMinSettleSteps = 4;

void advance_virtual_time(uint32_t ms) {
    lv_tick_inc(ms);
}

#if LV_USE_LOG
// Keeps standard output for the result line. While the page renders, LVGL's
// warnings and errors (a missing font or image, for example) go to standard
// error; its info messages are dropped.
void forward_lvgl_warnings(lv_log_level_t level, const char* message) {
    if (level >= LV_LOG_LEVEL_WARN && message) {
        std::fputs(message, stderr);
    }
}

// After the capture only errors are shown, which hides the expected warning
// about deleting the active screen during shutdown.
void forward_lvgl_errors(lv_log_level_t level, const char* message) {
    if (level >= LV_LOG_LEVEL_ERROR && message) {
        std::fputs(message, stderr);
    }
}
#endif

// An LVGL display that renders into memory only: no window, no framebuffer
// device and no input devices.
class HeadlessDisplay {
public:
    HeadlessDisplay() {
        // LVGL time becomes a virtual clock that only this file moves forward.
        // If anything calls LVGL's delay hook, the clock jumps ahead instead of
        // waiting in real time.
        lv_tick_set_cb(nullptr);
        lv_delay_set_cb(advance_virtual_time);

        display_ = lv_display_create(view::kScreenWidth, view::kScreenHeight);
        if (!display_) {
            return;
        }

        framebuffer_.resize(static_cast<size_t>(view::kScreenWidth) * view::kScreenHeight);
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_XRGB8888);
        lv_display_set_buffers(display_,
                               framebuffer_.data(),
                               nullptr,
                               static_cast<uint32_t>(framebuffer_.size() * sizeof(framebuffer_[0])),
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(display_, flush_cb);

        // Debug overlays must never end up in screenshots.
#if LV_USE_SYSMON && LV_USE_PERF_MONITOR
        lv_sysmon_hide_performance(display_);
#endif
#if LV_USE_SYSMON && LV_USE_MEM_MONITOR
        lv_sysmon_hide_memory(display_);
#endif
    }

    ~HeadlessDisplay() {
        if (display_) {
            lv_display_delete(display_);
            display_ = nullptr;
        }
    }

    HeadlessDisplay(const HeadlessDisplay&) = delete;
    HeadlessDisplay& operator=(const HeadlessDisplay&) = delete;

    lv_display_t* display() const {
        return display_;
    }

private:
    static void flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
        LV_UNUSED(area);
        LV_UNUSED(px_map);
        lv_display_flush_ready(display);
    }

    lv_display_t* display_{nullptr};
    std::vector<uint32_t> framebuffer_;
};

} // namespace

int run_snapshot(const SnapshotRequest& request) {
    logger::Logger::init();
    logger::Logger::set_tag(APP_NAME);
    // Only warnings and errors are logged. The logger always writes to
    // standard output, so a problem such as a font that fails to load can
    // appear there next to the result line; it is kept because it explains
    // a broken image.
    logger::Logger::set_level(logger::LogLevel::WARN);

    lv_init();
#if LV_USE_LOG
    lv_log_register_print_cb(forward_lvgl_warnings);
#endif

    // Declaration order matters: objects are destroyed in reverse, so the
    // screens go first, then the display, then the view model subjects, then
    // the theme with its fonts, and the AssetManager last.
    AssetManager assets;

    view::ThemeEngine theme_engine(assets, request.theme);
    if (!theme_engine.fonts_complete()) {
        std::fprintf(stderr,
                     "%s: some fonts of the %s theme could not be loaded; "
                     "check that the font files are installed\n",
                     APP_NAME,
                     theme_engine.theme().name);
        return 1;
    }

    viewmodel::BaseViewModel view_model;
    // Set before any screen or observer exists. The user's settings file is
    // neither read nor written in snapshot mode.
    view_model.show_page(request.page);

    HeadlessDisplay headless;
    auto* display = headless.display();
    if (!display) {
        std::fprintf(stderr, "%s: could not create the in-memory display\n", APP_NAME);
        return 1;
    }
    theme_engine.attach_display(display);

    ScreenManager screen_manager(view_model, assets, theme_engine);
    screen_manager.start();

    // Run LVGL on the virtual clock until the requested time has passed.
    uint32_t elapsed = 0;
    uint32_t steps = 0;
    while (elapsed < request.settle_ms || steps < kMinSettleSteps) {
        lv_tick_inc(kTickStepMs);
        lv_timer_handler();
        elapsed += kTickStepMs;
        ++steps;
    }

    auto* screen = screen_manager.current_screen();
    if (!screen || screen != lv_screen_active()) {
        std::fprintf(stderr, "%s: the page did not load\n", APP_NAME);
        return 1;
    }

    const auto result = platform::screenshot::capture_active_screen_to(request.output_path);
#if LV_USE_LOG
    lv_log_register_print_cb(forward_lvgl_errors);
#endif
    if (!result.success) {
        std::fprintf(stderr,
                     "%s: could not write %s: %s\n",
                     APP_NAME,
                     request.output_path.c_str(),
                     result.error.c_str());
        return 1;
    }

    if (result.width != view::kScreenWidth || result.height != view::kScreenHeight) {
        std::error_code remove_error;
        std::filesystem::remove(request.output_path, remove_error);
        std::fprintf(stderr,
                     "%s: the image came out %dx%d instead of %dx%d\n",
                     APP_NAME,
                     static_cast<int>(result.width),
                     static_cast<int>(result.height),
                     view::kScreenWidth,
                     view::kScreenHeight);
        return 1;
    }

    std::printf("Saved %s (%dx%d)\n",
                result.path.c_str(),
                static_cast<int>(result.width),
                static_cast<int>(result.height));
    return 0;
}

} // namespace app

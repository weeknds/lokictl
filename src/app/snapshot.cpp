/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "snapshot.h"

#include "asset_manager.h"
#include "backlight.h"
#include "base_viewmodel.h"
#include "data_controller.h"
#include "data_source.h"
#include "home_examples.h"
#include "key_controller.h"
#include "linux_input.h"
#include "logger.h"
#include "power_controller.h"
#include "screen_manager.h"
#include "screenshot_service.h"
#include "theme_controller.h"
#include "theme_engine.h"

#include "lvgl.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace app {
namespace {

// LVGL time advanced per loop step, and the fewest steps to run so the
// deferred first screen load always happens, even with --settle-ms 0.
constexpr uint32_t kTickStepMs = 5;
constexpr uint32_t kMinSettleSteps = 4;

// How long a scripted key is held: a short press, and a long one that is well
// past the 700 ms long press (and far below the launcher's 3 s).
constexpr uint32_t kKeyHoldMs = 60;
constexpr uint32_t kLongKeyHoldMs = 1000;
constexpr std::string_view kLongSuffix = ":long";
// The --keys name that is not a key but one further reading.
constexpr std::string_view kDataStepName = "data";

struct NamedKey {
    const char* name;
    uint32_t key;
};

constexpr NamedKey kNamedKeys[] = {
    {"esc", LV_KEY_ESC},
    {"enter", LV_KEY_ENTER},
    {"space", ' '},
    {"backspace", LV_KEY_BACKSPACE},
    {"up", LV_KEY_UP},
    {"down", LV_KEY_DOWN},
    {"left", LV_KEY_LEFT},
    {"right", LV_KEY_RIGHT},
    {"help", platform::kKeyHelp},
};

void advance_virtual_time(uint32_t ms) {
    lv_tick_inc(ms);
}

// Runs LVGL on the virtual clock until `settle_ms` have passed, and for at
// least kMinSettleSteps steps, so deferred work such as a page load or a
// theme switch has happened.
void settle(uint32_t settle_ms) {
    uint32_t elapsed = 0;
    uint32_t steps = 0;
    while (elapsed < settle_ms || steps < kMinSettleSteps) {
        lv_tick_inc(kTickStepMs);
        lv_timer_handler();
        elapsed += kTickStepMs;
        ++steps;
    }
}

bool page_is_shown(const ScreenManager& screen_manager) {
    auto* screen = screen_manager.current_screen();
    return screen && screen == lv_screen_active();
}

// The leak check reads this line: the number must be the same every time the
// same theme comes round again.
void print_font_count(const AssetManager& assets, const view::ThemeEngine& theme_engine) {
    std::fprintf(stderr,
                 "fonts loaded: %zu (%s)\n",
                 assets.loaded_font_count(),
                 theme_engine.theme().name);
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

bool parse_scripted_key(std::string_view text, ScriptedKey& key) {
    std::string name;
    for (const char character : text) {
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    bool long_press = false;
    const std::size_t colon = name.find(':');
    if (colon != std::string::npos) {
        if (std::string_view(name).substr(colon) != kLongSuffix) {
            return false;
        }
        long_press = true;
        name.erase(colon);
    }

    // A reading cannot be held.
    if (name == kDataStepName) {
        if (long_press) {
            return false;
        }
        key = ScriptedKey{0, false, true};
        return true;
    }

    if (name.size() == 1) {
        const char character = name[0];
        if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'z')) {
            key = ScriptedKey{static_cast<uint32_t>(character), long_press, false};
            return true;
        }
        return false;
    }
    for (const auto& entry : kNamedKeys) {
        if (name == entry.name) {
            key = ScriptedKey{entry.key, long_press, false};
            return true;
        }
    }
    return false;
}

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
    // screens go first, then the data controller, then the display, then the
    // view model subjects, then the theme with its fonts, and the
    // AssetManager last.
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
    // neither read nor written in snapshot mode, so the hotspot's name and
    // password are the made-up ones.
    view_model.show_page(request.page);
    view_model.set_default_hotspot_name(model::sample_home_data().ap_name);
    view_model.set_hotspot_name(model::sample_home_data().ap_name);
    view_model.set_hotspot_password(model::kExampleHotspotPassword);

    HeadlessDisplay headless;
    auto* display = headless.display();
    if (!display) {
        std::fprintf(stderr, "%s: could not create the in-memory display\n", APP_NAME);
        return 1;
    }
    theme_engine.attach_display(display);

    // The values: the first reading is taken before the page is built, so
    // the page shows it from the start. Further readings come from step()
    // only; a snapshot never starts the background reader. Its log lines
    // stay out of the output, because the logger only shows warnings here.
    DataController data(view_model, platform::make_data_source(request.data_source, request.data_root));
    // A new password is a fixed made-up one, so the same command always
    // gives the same picture; no saver is set, so nothing is written.
    data.set_password_generator([] { return std::string(model::kExampleNewHotspotPassword); });
    data.read_first();

    ScreenManager screen_manager(view_model, assets, theme_engine);
    screen_manager.start();

    // Battery care with the normal times, counted in LVGL time, so
    // --settle-ms can cross them. The device's backlight is never touched:
    // a stand-in reports each write as "backlight: <n>" on standard error,
    // starting from 100. Declared after the screen manager, so it is
    // destroyed before it.
    PowerController power(view_model,
                          data,
                          platform::make_stand_in_backlight(100, [](int32_t level) {
                              std::fprintf(stderr, "backlight: %d\n", static_cast<int>(level));
                          }));
    power.start();

    settle(request.settle_ms);

    if (!page_is_shown(screen_manager)) {
        std::fprintf(stderr, "%s: the page did not load\n", APP_NAME);
        return 1;
    }

    // --data-steps: one reading after the other, each handed over and shown
    // at once, in place or by building the page's content again, then one
    // settle for all of them.
    for (uint32_t step = 0; step < request.data_steps; ++step) {
        data.step();
    }
    if (request.data_steps > 0) {
        settle(request.settle_ms);
    }

    // Switches the theme for a pick on the Settings page and for
    // --switch-to: each request is applied later, from lv_timer_handler(),
    // with a rebuild of the page, the same path as in a normal run. Declared
    // after the screen manager, so it is destroyed before it. It has no
    // listener, so nothing is saved. A plain snapshot creates none.
    std::optional<ThemeController> theme_controller;
    if (!request.keys.empty() || !request.switch_to.empty()) {
        theme_controller.emplace(view_model, assets, theme_engine, screen_manager);
    }

    if (!request.keys.empty()) {
        {
            // The same key handling as a normal run. It lives only while the
            // keys are pressed, so its help panel and messages are gone
            // before the picture, and it is destroyed before the screen
            // manager it uses.
            KeyController key_controller(view_model, theme_engine, screen_manager);
            key_controller.set_wake_handler([&power] { return power.on_key(); });
            for (const ScriptedKey& key : request.keys) {
                // Not a key: one further reading, whichever page or panel is
                // open, the same as a reading that arrives while the app runs.
                if (key.data_step) {
                    data.step();
                    settle(request.settle_ms);
                    continue;
                }
                platform::inject_key(key.key, true);
                settle(key.long_press ? kLongKeyHoldMs : kKeyHoldMs);
                platform::inject_key(key.key, false);
                settle(request.settle_ms);
            }
        }

        if (!page_is_shown(screen_manager)) {
            std::fprintf(stderr, "%s: the page did not load after the keys\n", APP_NAME);
            return 1;
        }
        if (lv_subject_get_int(view_model.quit_requested_subject())) {
            std::fputs("quit requested\n", stderr);
        }
    }

    if (!request.switch_to.empty()) {
        print_font_count(assets, theme_engine);

        for (const view::ThemeId id : request.switch_to) {
            theme_controller->request(id);
            settle(request.settle_ms);

            const char* name = view::theme_spec(id).name;
            if (theme_engine.theme().id != id) {
                std::fprintf(stderr, "%s: the switch to the %s theme did not happen\n", APP_NAME, name);
                return 1;
            }
            if (!theme_engine.fonts_complete()) {
                std::fprintf(stderr,
                             "%s: some fonts of the %s theme could not be loaded; "
                             "check that the font files are installed\n",
                             APP_NAME,
                             name);
                return 1;
            }
            if (!page_is_shown(screen_manager)) {
                std::fprintf(stderr,
                             "%s: the page did not load again after the switch to the %s theme\n",
                             APP_NAME,
                             name);
                return 1;
            }
            print_font_count(assets, theme_engine);
        }
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

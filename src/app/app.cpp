/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "command_line.h"
#include "help_popup.h"
#include "logger.h"
#include "linux_input.h"
#include "page_registry.h"
#include "screenshot_service.h"
#include "screen_manager.h"
#include "snapshot.h"
#include "base_viewmodel.h"
#include "frame_stats.h"
#include "theme.h"
#include "theme_engine.h"
#include "toast.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#if USE_DESKTOP
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#error "APP_CONFIG_FILE must be defined by the build system (see CMakeLists.txt)"
#endif

namespace app {
namespace {

// Set by SIGTERM or SIGINT. The handler only records the signal number; the
// main loop sees it and shuts the app down normally.
volatile std::sig_atomic_t stop_signal = 0;

void on_stop_signal(int signal_number) {
    stop_signal = signal_number;
}

// Development tools set this variable to a file path to receive frame
// statistics (see platform::FrameStats). Unset or empty means off.
constexpr const char* kFrameStatsFileVariable = "LOKICTL_FRAME_STATS_FILE";

void report_frame_stats_error(const std::string& message) {
    LOG_WARN("frame statistics stopped: {}", message);
}

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    const std::filesystem::path config_file{APP_CONFIG_NAME ".conf"};
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / APP_CONFIG_NAME / config_file).string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / APP_CONFIG_NAME / config_file).string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (dark_mode={})",
                 persistence->config_path,
                 dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

class ShortcutController {
public:
    ShortcutController(viewmodel::BaseViewModel& view_model, view::ThemeEngine& theme_engine)
        : view_model_(view_model),
          toast_(std::make_unique<view::widgets::Toast>(lv_layer_top(), theme_engine)),
          help_popup_(std::make_unique<view::widgets::HelpPopup>(lv_layer_top(), theme_engine)),
          last_dark_mode_(view_model.is_dark_mode()) {
        toast_->build();
        help_popup_->build();
        // LVGL calls the observer once right away; last_dark_mode_ already
        // holds that value, so no toast appears at startup.
        dark_mode_observer_ =
            lv_subject_add_observer(view_model_.dark_mode_subject(), dark_mode_changed_cb, this);
        platform::set_global_key_listener(global_key_listener, this);
        platform::set_key_release_listener(key_release_listener, this);
    }

    ~ShortcutController() {
        platform::clear_key_release_listener(key_release_listener, this);
        platform::clear_global_key_listener(global_key_listener, this);
        if (dark_mode_observer_) {
            lv_observer_remove(dark_mode_observer_);
            dark_mode_observer_ = nullptr;
        }
    }

private:
    bool handle_key(uint32_t key, bool long_pressed) {
        if (key == platform::kKeyPrintScreen) {
            if (!long_pressed) {
                const auto result = platform::screenshot::capture_active_screen();
                if (result.success) {
                    LOG_INFO("screenshot saved: {}", result.path);
                    toast_->show_highlighted("Saved to ",
                                             result.path,
                                             "",
                                             view::widgets::ToastTone::Success,
                                             5200,
                                             true);
                }
                else {
                    LOG_WARN("screenshot failed: {}", result.error);
                    toast_->show("Screenshot failed: " + result.error,
                                 view::widgets::ToastTone::Error,
                                 3200);
                }
            }
            return true;
        }

        if (key == platform::kKeyHelp) {
            if (!long_pressed) {
                toast_->hide();
                if (help_popup_->visible()) {
                    help_popup_->hide();
                }
                else {
                    help_popup_->show(view_model_.current_page());
                }
            }
            return true;
        }

        if (help_popup_->visible()) {
            if (key == LV_KEY_ESC && !long_pressed) {
                help_popup_->hide();
            }
            return true;
        }

        const bool is_escape = key == LV_KEY_ESC;
        const bool is_home_exit_key =
            key == '4' &&
            ((!long_pressed && view_model_.current_page() == model::AppPage::Apple) ||
             (long_pressed && exit_key_pressed_ == key));
        if (!is_escape && !is_home_exit_key) {
            return false;
        }

        if (long_pressed) {
            if (exit_key_pressed_ == key && exit_armed_) {
                toast_->hide();
                view_model_.request_quit();
            }
        }
        else {
            exit_key_pressed_ = key;
            exit_armed_ = view_model_.current_page() == model::AppPage::Apple;
            if (exit_armed_) {
                toast_->show_persistent_highlighted("Hold ",
                                                    "ESC",
                                                    " to exit",
                                                    view::widgets::ToastTone::Warning);
            }
        }
        return true;
    }

    void handle_release(uint32_t key) {
        if (key != exit_key_pressed_) {
            return;
        }

        if (exit_armed_) {
            toast_->hide();
        } else if (key == LV_KEY_ESC &&
                   view_model_.current_page() == model::AppPage::Butter) {
            view_model_.show_apple_page();
        }
        exit_key_pressed_ = 0;
        exit_armed_ = false;
    }

    // Key 6 flips the saved dark_mode setting, which picks the theme of the
    // next start (Terminal for yes, Instrument for no). The running theme
    // stays as it is, so a toast says what will change.
    void handle_dark_mode_change(bool dark_mode) {
        if (dark_mode == last_dark_mode_) {
            return;
        }
        last_dark_mode_ = dark_mode;

        const auto next_theme = dark_mode ? view::ThemeId::Terminal : view::ThemeId::Instrument;
        toast_->show_highlighted("Next start: ",
                                 view::theme_spec(next_theme).display_name,
                                 " theme",
                                 view::widgets::ToastTone::Default,
                                 kThemeToastDurationMs);
    }

    static void dark_mode_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
        auto* controller = static_cast<ShortcutController*>(lv_observer_get_user_data(observer));
        if (controller) {
            controller->handle_dark_mode_change(lv_subject_get_int(subject) != 0);
        }
    }

    static bool global_key_listener(uint32_t key,
                                    const char*,
                                    bool long_pressed,
                                    void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        return controller && controller->handle_key(key, long_pressed);
    }

    static void key_release_listener(uint32_t key, const char*, void* user_data) {
        auto* controller = static_cast<ShortcutController*>(user_data);
        if (controller) {
            controller->handle_release(key);
        }
    }

    static constexpr uint32_t kThemeToastDurationMs = 4500;

    viewmodel::BaseViewModel& view_model_;
    std::unique_ptr<view::widgets::Toast> toast_;
    std::unique_ptr<view::widgets::HelpPopup> help_popup_;
    lv_observer_t* dark_mode_observer_{nullptr};
    bool last_dark_mode_{false};
    uint32_t exit_key_pressed_{0};
    bool exit_armed_{false};
};

#if !USE_DESKTOP
// LVGL's Linux display drivers set a tick source but no delay function, and
// without one lv_delay_ms() busy-waits, keeping a CPU core fully loaded.
void sleep_delay_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

lv_display_t* init_device_display() {
    lv_delay_set_cb(sleep_delay_ms);
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_fbdev_set_file(display, APP_FRAMEBUFFER_DEVICE) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#endif
}
#endif

int report_usage_error(const std::string& message) {
    std::fprintf(stderr, "%s: %s\n\n%s", APP_NAME, message.c_str(), usage_text().c_str());
    return 2;
}

// Turns the --theme value into a theme. An empty name (no --theme) leaves
// `theme` unset. Returns false with a message for any unknown name.
bool resolve_theme_option(const std::string& name,
                          std::optional<view::ThemeId>& theme,
                          std::string& error) {
    theme.reset();
    if (name.empty()) {
        return true;
    }
    if (const view::ThemeSpec* spec = view::find_theme(name)) {
        theme = spec->id;
        return true;
    }
    if (name == "light" || name == "dark") {
        error = "there is no theme '" + name + "' any more: light and dark were replaced by " +
                "the themes " + view::theme_names();
        return false;
    }
    error = "unknown theme '" + name + "' (use one of: " + view::theme_names() + ")";
    return false;
}

const char* yes_no_unset(const std::optional<bool>& value) {
    if (!value) {
        return "unset";
    }
    return *value ? "yes" : "no";
}

} // namespace

int Application::run(int argc, char** argv) {
    // Everything up to run_snapshot() or run_interactive() happens before the
    // logger or LVGL start, so help, page names and errors print cleanly.
    const auto options = parse_command_line(argc, argv);
    switch (options.mode) {
        case LaunchMode::Help:
            std::fputs(usage_text().c_str(), stdout);
            return 0;
        case LaunchMode::Invalid:
            return report_usage_error(options.error);
        case LaunchMode::ListPages:
            for (const auto& entry : page_registry()) {
                std::puts(entry.name);
            }
            return 0;
        case LaunchMode::Snapshot:
        case LaunchMode::Interactive:
            break;
    }

    std::optional<view::ThemeId> theme;
    std::string theme_error;
    if (!resolve_theme_option(options.theme, theme, theme_error)) {
        return report_usage_error(theme_error);
    }

    if (options.mode == LaunchMode::Snapshot) {
        const PageInfo* page =
            options.page.empty() ? &page_registry().front() : find_page(options.page);
        if (!page) {
            return report_usage_error("unknown page '" + options.page + "' (see --list-pages)");
        }

        return run_snapshot(SnapshotRequest{options.snapshot_path,
                                            page->page,
                                            theme.value_or(view::kDefaultThemeId),
                                            options.settle_ms});
    }

    const int exit_code = run_interactive(theme);
    // Logged once run_interactive() has torn everything down, so these are
    // the last lines of a run.
    if (stop_signal != 0) {
        LOG_INFO("stop requested by signal {}", static_cast<int>(stop_signal));
    }
    LOG_INFO("{} stopped", APP_DISPLAY_NAME);
    return exit_code;
}

int Application::run_interactive(std::optional<view::ThemeId> theme_override) {
    logger::Logger::init();
    logger::Logger::set_tag(APP_NAME);

    // Installed before the display (and with it SDL in the simulator) starts:
    // SDL only adds its own handlers where the default one is still in place.
    std::signal(SIGTERM, on_stop_signal);
    std::signal(SIGINT, on_stop_signal);

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        LOG_INFO("loaded config: {} (dark_mode={})",
                 loaded_config_path,
                 yes_no_unset(config.dark_mode));
    } else {
        config = ApplicationConfig{};
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }
    // The view model keeps the saved setting, not a --theme choice, so
    // toggling it saves the right value for the next start.
    view_model.set_dark_mode(
        config.dark_mode.value_or(view::theme_spec(view::kDefaultThemeId).metrics.dark));

    // The theme for this run: --theme wins, then the saved setting, then the
    // default.
    view::ThemeId theme_id = view::kDefaultThemeId;
    const char* theme_source = "the default";
    if (theme_override) {
        theme_id = *theme_override;
        theme_source = "the command line";
    } else if (config.dark_mode) {
        // Interim mapping until the theme is saved by name: the old dark_mode
        // setting picks Terminal (yes) or Instrument (no).
        theme_id = *config.dark_mode ? view::ThemeId::Terminal : view::ThemeId::Instrument;
        theme_source = "the saved setting";
    }

    // Created before the display and destroyed after it, so every LVGL
    // object is gone before the theme's fonts are freed.
    view::ThemeEngine theme_engine(assets, theme_id);
    LOG_INFO("theme: {} (from {})", theme_engine.theme().name, theme_source);
    if (!theme_engine.fonts_complete()) {
        LOG_WARN("some fonts of the {} theme could not be loaded; built-in fonts are used instead",
                 theme_engine.theme().name);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    // Instrument has a light ground and gets the light shell art; the other
    // themes get the dark one.
    simulator_frame.set_dark_shell(theme_engine.theme().metrics.dark);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    // Declared after the display, so it is destroyed before the display is.
    std::unique_ptr<platform::FrameStats> frame_stats;
    if (const char* stats_path = std::getenv(kFrameStatsFileVariable);
        stats_path && stats_path[0] != '\0') {
        // Logged first, so a write error reported while it starts reads in order.
        LOG_INFO("writing frame statistics to {}", stats_path);
        frame_stats = std::make_unique<platform::FrameStats>(
            display, stats_path, report_frame_stats_error);
    }

    theme_engine.attach_display(display);

    ScreenManager screen_manager(view_model, assets, theme_engine);
    screen_manager.start();
    ShortcutController shortcuts(view_model, theme_engine);

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &dark_mode_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("{} {} started at {}x{}", APP_DISPLAY_NAME, APP_VERSION,
             lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running && stop_signal == 0
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }

    return 0;
}

} // namespace app

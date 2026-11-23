/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "backlight.h"
#include "command_line.h"
#include "data_controller.h"
#include "data_readout.h"
#include "data_source.h"
#include "firewall.h"
#include "key_controller.h"
#include "logger.h"
#include "linux_input.h"
#include "mode_switch.h"
#include "page_registry.h"
#include "power_controller.h"
#include "remote_controller.h"
#include "screen_manager.h"
#include "snapshot.h"
#include "base_viewmodel.h"
#include "frame_stats.h"
#include "home_examples.h"
#include "theme.h"
#include "theme_controller.h"
#include "theme_engine.h"
#include "tool_controller.h"
#include "tool_runner.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

// Development and test tools set this variable to a settings file that is
// used instead of the normal ones, for reading and for writing. Unset or
// empty means off. Snapshot mode never reads or writes settings, so it
// ignores the variable.
constexpr const char* kConfigFileVariable = "LOKICTL_CONFIG_FILE";

// Development and test tools set this variable to a folder that stands in
// for the device's root: the live data source then reads <folder>/sys/...
// instead of /sys/..., so every battery state can be tried on any machine.
// Unset or empty means the real root. Only the live source reads files, so
// the other sources ignore it.
constexpr const char* kDataRootVariable = "LOKICTL_DATA_ROOT";

// Test tools set this variable to "<dim>,<dark>" in whole seconds to
// shorten the times after which the screen dims and goes dark (see
// PowerController::parse_times()). Unset or empty means the normal times.
constexpr const char* kIdleSecondsVariable = "LOKICTL_IDLE_SECONDS";

// Test tools set this variable to a plain file that stands in for the
// device's backlight file, so dimming can be followed on any machine.
// Unset or empty means the real backlight on the device and the darkened
// window in the simulator.
constexpr const char* kBacklightFileVariable = "LOKICTL_BACKLIGHT_FILE";

// The data source used when --data is not given: a snapshot always shows
// the fixed sample values, so its pictures never change; a start and
// --print-data read the real values on the device and run the demo in the
// simulator.
platform::DataSourceKind default_data_source(LaunchMode mode) {
    if (mode == LaunchMode::Snapshot) {
        return platform::DataSourceKind::Sample;
    }
#if USE_DESKTOP
    return platform::DataSourceKind::Demo;
#else
    return platform::DataSourceKind::Live;
#endif
}

// The value of LOKICTL_DATA_ROOT, or an empty string when it is not set.
std::string data_root_from_environment() {
    const char* data_root = std::getenv(kDataRootVariable);
    return data_root ? std::string(data_root) : std::string();
}

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

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

// Saves `id` by name as the theme= key of the settings file at `path`.
// Every other line of the file is kept, and an old dark_mode line is
// removed. A failure is logged and reported to the caller.
bool save_theme_choice(const std::string& path, view::ThemeId id) {
    const char* name = view::theme_spec(id).name;
    ApplicationConfig config;
    config.theme = name;
    std::string error;
    if (!save_application_config(path, config, error)) {
        LOG_WARN("could not save the theme to {}: {}", path, error);
        return false;
    }
    LOG_INFO("saved theme={} to {}", name, path);
    return true;
}

// Saves the "Dim when idle" setting as the dim_when_idle= key of the
// settings file at `path`, keeping every other line. A failure is logged
// and reported to the caller.
bool save_dimming_choice(const std::string& path, bool on) {
    ApplicationConfig config;
    config.dim_when_idle = on;
    std::string error;
    if (!save_application_config(path, config, error)) {
        LOG_WARN("could not save dim_when_idle to {}: {}", path, error);
        return false;
    }
    LOG_INFO("saved dim_when_idle={} to {}", on ? "yes" : "no", path);
    return true;
}

// Saves a changed hotspot name or password as the hotspot_name= and
// hotspot_password= keys of the settings file at `path` (which keeps every
// other line, and becomes readable by its owner only when the password is
// written). A value that is not set is left alone. A failure to save is
// logged; the values are still used for this run. Neither value goes to
// the log.
void save_hotspot_settings(const std::string& path,
                           const std::optional<std::string>& name,
                           const std::optional<std::string>& password) {
    ApplicationConfig config;
    config.hotspot_name = name;
    config.hotspot_password = password;
    const char* what = name && password ? "name and password" : name ? "name" : "password";
    std::string error;
    if (save_application_config(path, config, error)) {
        LOG_INFO("saved the hotspot's {} to {}", what, path);
    } else {
        LOG_WARN("could not save the hotspot's {} to {}: {}", what, path, error);
    }
}

// Saves the list of blocked devices as the blocked_devices= key of the
// settings file at `path` (which keeps every other line). A failure to
// save is logged; the list is still used for this run. No address goes to
// the log.
void save_blocked_devices(const std::string& path, const std::vector<std::string>& addresses) {
    ApplicationConfig config;
    config.blocked_devices = platform::blocked_devices_text(addresses);
    std::string error;
    if (save_application_config(path, config, error)) {
        LOG_INFO("saved the blocked devices to {}", path);
    } else {
        LOG_WARN("could not save the blocked devices to {}: {}", path, error);
    }
}

// The idle times of this run: the normal ones, or those of
// LOKICTL_IDLE_SECONDS.
PowerController::Times idle_times_from_environment() {
    PowerController::Times times = PowerController::kDefaultTimes;
    const char* value = std::getenv(kIdleSecondsVariable);
    if (value && value[0] != '\0') {
        if (PowerController::parse_times(value, times)) {
            LOG_INFO("idle times set by {}: dim after {} s, dark after {} s",
                     kIdleSecondsVariable,
                     times.dim_ms / 1000,
                     times.dark_ms / 1000);
        } else {
            LOG_WARN("{} must be <dim>,<dark> in whole seconds (dim at least 1, dark later); "
                     "using the normal times",
                     kIdleSecondsVariable);
        }
    }
    return times;
}

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

} // namespace

int Application::run(int argc, char** argv) {
    // Everything up to run_snapshot() or run_interactive() happens before the
    // logger or LVGL start, so help, page names, the data readout and errors
    // print cleanly.
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
        case LaunchMode::PrintData:
            break;
    }

    // Where the values come from: --data, or else the default of this mode.
    platform::DataSourceKind data_source = default_data_source(options.mode);
    if (!options.data.empty() && !platform::find_data_source(options.data, data_source)) {
        return report_usage_error("unknown data source '" + options.data + "' (use one of: " +
                                  platform::data_source_names() + ")");
    }
    const std::string data_root = data_root_from_environment();

    // --print-data needs nothing else: no theme, no page, no screen.
    if (options.mode == LaunchMode::PrintData) {
        return run_data_readout(data_source, options.data_steps, data_root);
    }

    std::optional<view::ThemeId> theme;
    std::string theme_error;
    if (!resolve_theme_option(options.theme, theme, theme_error)) {
        return report_usage_error(theme_error);
    }

    // The themes of --switch-to follow the same rules as --theme. The parser
    // already refused empty names.
    std::vector<view::ThemeId> switch_to;
    for (const auto& name : options.switch_to) {
        std::optional<view::ThemeId> switch_theme;
        if (!resolve_theme_option(name, switch_theme, theme_error)) {
            return report_usage_error(theme_error);
        }
        if (!switch_theme) {
            return report_usage_error("--switch-to needs theme names separated by commas");
        }
        switch_to.push_back(*switch_theme);
    }

    // The keys of --keys, checked before anything starts. The parser already
    // refused empty names.
    std::vector<ScriptedKey> keys;
    for (const auto& name : options.keys) {
        ScriptedKey key{};
        if (!parse_scripted_key(name, key)) {
            return report_usage_error("unknown key '" + name + "' in --keys (use " +
                                      kScriptedKeyNames + ")");
        }
        keys.push_back(key);
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
                                            options.settle_ms,
                                            switch_to,
                                            keys,
                                            data_source,
                                            options.data_steps,
                                            data_root});
    }

    const int exit_code = run_interactive(theme, data_source, data_root);
    // Logged once run_interactive() has torn everything down, so these are
    // the last lines of a run.
    if (stop_signal != 0) {
        LOG_INFO("stop requested by signal {}", static_cast<int>(stop_signal));
    }
    LOG_INFO("{} stopped", APP_DISPLAY_NAME);
    return exit_code;
}

int Application::run_interactive(std::optional<view::ThemeId> theme_override,
                                 platform::DataSourceKind data_source,
                                 const std::string& data_root) {
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

    // The settings file: the user's own file is written, and read when it
    // exists; otherwise the shipped file is read. LOKICTL_CONFIG_FILE
    // replaces both.
    std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (const char* config_file = std::getenv(kConfigFileVariable);
        config_file && config_file[0] != '\0') {
        user_config_path = config_file;
        loaded_config_path = config_file;
        LOG_INFO("settings file set by {}", kConfigFileVariable);
    } else if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        LOG_INFO("loaded config: {} (theme={})",
                 loaded_config_path,
                 config.theme ? *config.theme : std::string("unset"));
    } else {
        config = ApplicationConfig{};
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }
    // Like an unknown theme name, a dark_mode value that cannot be read
    // writes nothing at this start; the line goes away when the user picks
    // a theme in Settings.
    if (config.dark_mode_invalid) {
        LOG_WARN("invalid dark_mode value in {}; it is ignored", loaded_config_path);
    }
    // Dimming is on unless the user switched it off on the Settings page.
    view_model.set_screen_dimming(config.dim_when_idle.value_or(true));

    // The hotspot's name and password. The simulator shows made-up ones
    // and never writes them; the device uses the saved ones, the default
    // name (from wlan0's hardware address) until the user renames it, and
    // makes and saves a password at the first switch to Router when there
    // is none (see below).
#if USE_DESKTOP
    view_model.set_default_hotspot_name(model::sample_home_data().ap_name);
    view_model.set_hotspot_name(model::sample_home_data().ap_name);
    view_model.set_hotspot_password(model::kExampleHotspotPassword);
#else
    // An empty hotspot_name= line means the default name.
    view_model.set_default_hotspot_name(platform::default_hotspot_name("/"));
    if (config.hotspot_name && valid_hotspot_name(*config.hotspot_name)) {
        view_model.set_hotspot_name(*config.hotspot_name);
    } else {
        if (config.hotspot_name && !config.hotspot_name->empty()) {
            LOG_WARN("the hotspot_name in {} is not 1 to 32 printable characters; the default name is used",
                     loaded_config_path);
        }
        view_model.set_hotspot_name(view_model.default_hotspot_name());
    }
    if (config.hotspot_password && valid_hotspot_password(*config.hotspot_password)) {
        view_model.set_hotspot_password(*config.hotspot_password);
    } else if (config.hotspot_password) {
        LOG_WARN("the hotspot_password in {} is not 8 to 63 printable characters; "
                 "a new one is made at the next switch to Router",
                 loaded_config_path);
    }
#endif

    // The speed test's server; the address itself is never logged.
    if (config.speed_test_server && model::valid_speed_server(*config.speed_test_server)) {
        view_model.set_speed_server(*config.speed_test_server);
    } else if (config.speed_test_server) {
        LOG_WARN("the speed_test_server in {} is not an http:// or https:// address; the default server is used",
                 loaded_config_path);
    }

    // The saved choice: the theme= key, or else the old dark_mode key that
    // came before it (yes: Terminal, no: Instrument). An unknown theme name
    // counts as no choice at all; dark_mode is then ignored too, and the
    // file is left alone until the user picks a theme in Settings.
    std::optional<view::ThemeId> saved_theme;
    const char* saved_theme_source = "the saved setting";
    if (config.theme) {
        if (const view::ThemeSpec* spec = view::find_theme(*config.theme)) {
            saved_theme = spec->id;
        } else if (theme_override) {
            LOG_WARN("unknown theme '{}' in {}; it is ignored", *config.theme, loaded_config_path);
        } else {
            LOG_WARN("unknown theme '{}' in {}; using the default theme",
                     *config.theme,
                     loaded_config_path);
        }
    } else if (config.dark_mode) {
        saved_theme = *config.dark_mode ? view::ThemeId::Terminal : view::ThemeId::Instrument;
        saved_theme_source = "the old dark_mode setting";
    }

    // The theme for this run: --theme wins, then the saved choice, then the
    // default.
    view::ThemeId theme_id = view::kDefaultThemeId;
    const char* theme_source = "the default";
    if (theme_override) {
        theme_id = *theme_override;
        theme_source = "the command line";
    } else if (saved_theme) {
        theme_id = *saved_theme;
        theme_source = saved_theme_source;
    }

    // Created before the display and destroyed after it, so every LVGL
    // object is gone before the theme's fonts are freed.
    view::ThemeEngine theme_engine(assets, theme_id);
    LOG_INFO("theme: {} (from {})", theme_engine.theme().name, theme_source);
    if (!theme_engine.fonts_complete()) {
        LOG_WARN("some fonts of the {} theme could not be loaded; built-in fonts are used instead",
                 theme_engine.theme().name);
    }

    // The old dark_mode key is replaced once by theme=, also on a --theme
    // start. What is written is the saved choice, never the --theme value.
    if (config.dark_mode && saved_theme && save_theme_choice(user_config_path, *saved_theme) &&
        loaded_config_path == user_config_path) {
        LOG_INFO("removed the old dark_mode setting from {}", user_config_path);
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

    // The values: the first reading is taken here, before the first page is
    // built, so the page never shows values that are about to change. The
    // background reader and its timer start right before the main loop and
    // are stopped right after it, before anything else is torn down.
    // Only a normal start may change the network (switching modes).
    DataController data(view_model, platform::make_data_source(data_source, data_root, true));
    data.set_hotspot_saver([&user_config_path](const std::optional<std::string>& name,
                                               const std::optional<std::string>& password) {
        save_hotspot_settings(user_config_path, name, password);
    });
    // The devices blocked on the hotspot: the saved list on the device; the
    // simulator starts with none and never writes one.
#if !USE_DESKTOP
    if (config.blocked_devices) {
        std::size_t dropped = 0;
        data.set_blocked_devices(platform::parse_blocked_devices(*config.blocked_devices, &dropped));
        if (dropped > 0) {
            LOG_WARN("{} entries of blocked_devices in {} are not device addresses, repeats or too many; "
                     "they are ignored",
                     dropped,
                     loaded_config_path);
        }
    }
#endif
    data.set_blocked_saver([&user_config_path](const std::vector<std::string>& addresses) {
        save_blocked_devices(user_config_path, addresses);
    });
    data.read_first();

    // The tools (ping, DNS lookup) run on their own thread; the live runner
    // only with the live data source on the device, the pretend one with
    // made-up results otherwise. Stopped right after the main loop, next to
    // the data controller.
    ToolController tools(view_model, platform::make_tool_runner(data_source == platform::DataSourceKind::Live));

    ScreenManager screen_manager(view_model, assets, theme_engine);
    screen_manager.start();

    // Battery care: dims the screen when no key is pressed, warns when the
    // battery runs low. Only a normal start opens the backlight: snapshots
    // and --print-data never touch it. Declared after the data controller,
    // so it is destroyed before it; stopped right after the main loop.
    std::unique_ptr<platform::Backlight> backlight;
    if (const char* backlight_file = std::getenv(kBacklightFileVariable);
        backlight_file && backlight_file[0] != '\0') {
        LOG_INFO("backlight file set by {}", kBacklightFileVariable);
        backlight = platform::make_file_backlight(backlight_file);
    } else {
#if USE_DESKTOP
        // The simulator darkens its window instead.
        backlight = platform::make_stand_in_backlight(
            100, [&simulator_frame](int32_t level) { simulator_frame.set_screen_level(level); });
#else
        backlight = platform::make_file_backlight(platform::kBacklightFile);
#endif
    }
    PowerController power(view_model, data, std::move(backlight), idle_times_from_environment());

    // Declared after the ScreenManager, so it is destroyed before it.
    KeyController key_controller(view_model, theme_engine, screen_manager);
    key_controller.set_wake_handler([&power] { return power.on_key(); });
    power.set_warning_listener([&key_controller](int32_t percent, bool very_low) {
        key_controller.show_battery_warning(percent, very_low);
    });
    power.set_setting_listener([&key_controller, &user_config_path](bool on) {
        if (!save_dimming_choice(user_config_path, on)) {
            key_controller.show_dimming_not_saved();
        }
    });
    power.start();
    data.set_block_failure_listener([&key_controller](const std::string& message) {
        key_controller.show_block_failed(message);
    });
    // A USB Wi-Fi adapter is optional; plugging one in or out only says so.
    data.set_usb_wifi_listener([&key_controller](const std::string& interface_name, bool connected) {
        key_controller.show_usb_wifi(interface_name, connected);
    });

    // Declared after everything its listener uses, so it is destroyed before
    // them. When the user picks a theme in Settings, the theme switches
    // while the app runs, and every switch is saved.
    ThemeController theme_controller(view_model, assets, theme_engine, screen_manager);
    theme_controller.set_listener([&](const ThemeController::SwitchResult& result) {
#if USE_DESKTOP
        // Instrument has a light ground and gets the light shell art; the
        // other themes get the dark one.
        simulator_frame.set_dark_shell(theme_engine.theme().metrics.dark);
#endif
        const bool saved = save_theme_choice(user_config_path, result.current);
        key_controller.show_theme_switched(view::theme_spec(result.current).display_name, saved);
    });

    // Remote view: off at every start; the Settings row switches it on.
    // Declared after the theme engine and the view model, so it is
    // destroyed before them.
    RemoteController remote(theme_engine, view_model);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("{} {} started at {}x{}", APP_DISPLAY_NAME, APP_VERSION,
             lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    data.start();
    tools.start();
    while (running && stop_signal == 0
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        lv_timer_handler();
        lv_delay_ms(5);
    }
    // First on the way out: the backlight goes back to what it was, and no
    // new values arrive while the rest is torn down.
    power.stop();
    // Then Remote view: the port closes and its thread ends.
    remote.stop();
    tools.stop();
    data.set_block_failure_listener({});
    data.set_usb_wifi_listener({});
    data.stop();

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }

    return 0;
}

} // namespace app

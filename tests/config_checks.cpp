/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of saving the settings file (src/app/application_config.*): a
// save keeps every other line, leaves the file readable by its owner only
// and no temporary file behind, and a save whose writing fails returns
// false and leaves the old file exactly as it was. Built on the Mac only,
// as lokictl_config_tests; exits 1 when a check fails. Only made-up values.

#include "application_config.h"

#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

unsigned mode_of(const std::filesystem::path& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 ? static_cast<unsigned>(info.st_mode & 0777) : 0xFFFFu;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void check_save(const std::filesystem::path& folder) {
    const std::filesystem::path path = folder / "settings.conf";
    const std::filesystem::path temporary = folder / "settings.conf.tmp";
    const std::string seed =
        "# made-up settings\n[application]\ntheme=terminal\nhotspot_password=abcdefgh2345\n"
        "known_devices=02:00:00:00:00:01\n\n[other]\nkey=value\n";
    std::ofstream(path) << seed;
    std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                           std::filesystem::perms::group_read | std::filesystem::perms::others_read);
    std::ofstream(temporary) << "left by a save cut short";

    // A save: the new key goes in, every other line stays.
    app::ApplicationConfig dim;
    dim.dim_when_idle = false;
    std::string error;
    expect(app::save_application_config(path.string(), dim, error) && error.empty(), "a save succeeds");
    const std::string saved = read_file(path);
    expect(contains(saved, "theme=terminal\n") && contains(saved, "hotspot_password=abcdefgh2345\n") &&
               contains(saved, "known_devices=02:00:00:00:00:01\n") && contains(saved, "dim_when_idle=no\n") &&
               contains(saved, "[other]\nkey=value\n") && contains(saved, "# made-up settings\n"),
           "a save keeps every other line");
    expect(mode_of(path) == 0600, "the saved file is its owner's only");
    expect(!std::filesystem::exists(temporary), "no temporary file is left");

    // A save whose writing fails (here: no file may grow) returns false and
    // leaves the file as it was.
    std::signal(SIGXFSZ, SIG_IGN);
    struct rlimit before {};
    ::getrlimit(RLIMIT_FSIZE, &before);
    struct rlimit none = before;
    none.rlim_cur = 0;
    const bool limited = ::setrlimit(RLIMIT_FSIZE, &none) == 0;
    app::ApplicationConfig keep;
    keep.keep_mode = true;
    const bool failed_save = app::save_application_config(path.string(), keep, error);
    ::setrlimit(RLIMIT_FSIZE, &before);
    std::signal(SIGXFSZ, SIG_DFL);
    expect(limited, "the file size limit can be set");
    expect(!failed_save && !error.empty(), "a save that cannot write returns false with a reason");
    expect(read_file(path) == saved, "a failed save leaves the old file as it was");
    expect(mode_of(path) == 0600, "a failed save leaves the file's mode");
    expect(!std::filesystem::exists(temporary), "a failed save leaves no temporary file");

    // A file that does not exist yet is made, its owner's only.
    const std::filesystem::path fresh = folder / "new" / "settings.conf";
    app::ApplicationConfig theme;
    theme.theme = "instrument";
    expect(app::save_application_config(fresh.string(), theme, error) &&
               read_file(fresh) == "[application]\ntheme=instrument\n" && mode_of(fresh) == 0600,
           "a new file is made, its owner's only");
}

} // namespace

int main() {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("lokictl-config-checks-" + std::to_string(::getpid()));
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    check_save(folder);
    std::filesystem::remove_all(folder);
    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("config checks passed\n");
    return 0;
}

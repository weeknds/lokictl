/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "report_files.h"

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>

namespace platform {
namespace {

constexpr std::string_view kPrefix = "lokictl-";
constexpr std::string_view kPageEnd = ".html";
constexpr std::string_view kDevicesEnd = "-devices.csv";
// "20261123-075130"
constexpr std::size_t kStampLength = 15;

bool digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool valid_stamp(std::string_view stamp) {
    return stamp.size() == kStampLength && digits(stamp.substr(0, 8)) && stamp[8] == '-' && digits(stamp.substr(9));
}

// "20261123-075130" plus "1" for the first report of that second, "2" to
// "9" for the others, plus "1" for the page and "0" for the devices: the
// newest report sorts last, and its page before its devices when reversed.
std::string sort_key(std::string_view name) {
    const std::string_view rest = name.substr(kPrefix.size());
    const bool page = rest.size() >= kPageEnd.size() && rest.substr(rest.size() - kPageEnd.size()) == kPageEnd;
    const std::size_t end_size = page ? kPageEnd.size() : kDevicesEnd.size();
    const std::string_view middle = rest.substr(kStampLength, rest.size() - kStampLength - end_size);
    std::string key(rest.substr(0, kStampLength));
    key += middle.empty() ? '1' : middle[1];
    key += page ? '1' : '0';
    return key;
}

bool write_all(int fd, const std::string& data) {
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t written = ::write(fd, data.data() + done, data.size() - done);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(written);
    }
    return true;
}

int create_new(const std::string& path) {
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
}

} // namespace

std::string report_folder() {
    if (const char* folder = std::getenv(kReportFolderVariable); folder && folder[0] == '/') {
        return folder;
    }
#if USE_DESKTOP
    return {};
#else
    std::string home;
    if (const char* value = std::getenv("HOME"); value && value[0] == '/') {
        home = value;
    } else if (const passwd* user = getpwuid(getuid()); user && user->pw_dir && user->pw_dir[0] == '/') {
        home = user->pw_dir;
    }
    if (home.empty()) {
        return {};
    }
    return home + "/Documents/LokiCTL/reports";
#endif
}

bool valid_report_name(std::string_view name) {
    if (name.size() > 64 || name.substr(0, kPrefix.size()) != kPrefix) {
        return false;
    }
    std::string_view rest = name.substr(kPrefix.size());
    if (rest.size() < kStampLength || !valid_stamp(rest.substr(0, kStampLength))) {
        return false;
    }
    rest.remove_prefix(kStampLength);
    if (rest.size() >= 2 && rest[0] == '-' && rest[1] >= '2' && rest[1] <= '9') {
        rest.remove_prefix(2);
    }
    return rest == kPageEnd || rest == kDevicesEnd;
}

bool save_report(const std::string& folder,
                 const std::string& stamp,
                 const std::string& html,
                 const std::string& csv,
                 SavedReport& saved,
                 std::string& reason) {
    if (folder.empty()) {
        reason = "no folder";
        return false;
    }
    if (!valid_stamp(stamp)) {
        reason = "no clock";
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error || ::chmod(folder.c_str(), 0700) != 0) {
        reason = "no folder";
        return false;
    }
    // The LokiCTL folder above the default one is the user's only, too.
    const std::filesystem::path parent = std::filesystem::path(folder).parent_path();
    if (parent.filename() == "LokiCTL") {
        ::chmod(parent.c_str(), 0700);
    }

    for (int copy = 1; copy <= 9; ++copy) {
        const std::string base = std::string(kPrefix) + stamp + (copy > 1 ? "-" + std::to_string(copy) : "");
        const std::string page_name = base + std::string(kPageEnd);
        const std::string devices_name = base + std::string(kDevicesEnd);
        const std::string page_path = folder + "/" + page_name;
        const std::string devices_path = folder + "/" + devices_name;

        const int page_fd = create_new(page_path);
        if (page_fd < 0) {
            if (errno == EEXIST) {
                continue;
            }
            reason = errno == ENOSPC ? "disk full" : "can't make file";
            return false;
        }
        const int devices_fd = create_new(devices_path);
        if (devices_fd < 0) {
            const int create_error = errno;
            ::close(page_fd);
            ::unlink(page_path.c_str());
            if (create_error == EEXIST) {
                continue;
            }
            reason = create_error == ENOSPC ? "disk full" : "can't make file";
            return false;
        }
        bool written = write_all(page_fd, html);
        int write_error = written ? 0 : errno;
        if (written && !write_all(devices_fd, csv)) {
            written = false;
            write_error = errno;
        }
        const bool page_closed = ::close(page_fd) == 0;
        const bool devices_closed = ::close(devices_fd) == 0;
        const bool closed = page_closed && devices_closed;
        if (!written || !closed) {
            ::unlink(page_path.c_str());
            ::unlink(devices_path.c_str());
            reason = write_error == ENOSPC ? "disk full" : "can't write file";
            return false;
        }
        saved.page = page_name;
        saved.devices = devices_name;
        return true;
    }
    reason = "file name taken";
    return false;
}

std::vector<ReportFile> list_reports(const std::string& folder) {
    std::vector<std::pair<std::string, ReportFile>> found;
    if (folder.empty()) {
        return {};
    }
    DIR* dir = ::opendir(folder.c_str());
    if (!dir) {
        return {};
    }
    const int dir_fd = ::dirfd(dir);
    while (const dirent* entry = ::readdir(dir)) {
        const std::string_view name = entry->d_name;
        if (!valid_report_name(name)) {
            continue;
        }
        struct stat info{};
        if (::fstatat(dir_fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(info.st_mode)) {
            continue;
        }
        ReportFile file;
        file.name = std::string(name);
        file.bytes = static_cast<uint64_t>(info.st_size);
        found.emplace_back(sort_key(name), std::move(file));
    }
    ::closedir(dir);
    std::sort(found.begin(), found.end(), [](const auto& left, const auto& right) { return left.first > right.first; });
    std::vector<ReportFile> files;
    for (auto& entry : found) {
        if (files.size() >= kReportListMax) {
            break;
        }
        files.push_back(std::move(entry.second));
    }
    return files;
}

bool read_report(const std::string& folder, std::string_view name, std::string& content) {
    content.clear();
    if (folder.empty() || !valid_report_name(name)) {
        return false;
    }
    const std::string path = folder + "/" + std::string(name);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        static_cast<uint64_t>(info.st_size) > kReportFileMax) {
        ::close(fd);
        return false;
    }
    content.reserve(static_cast<std::size_t>(info.st_size));
    char chunk[16384];
    for (;;) {
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got < 0) {
            ::close(fd);
            content.clear();
            return false;
        }
        if (got == 0) {
            break;
        }
        content.append(chunk, static_cast<std::size_t>(got));
        if (content.size() > kReportFileMax) {
            ::close(fd);
            content.clear();
            return false;
        }
    }
    ::close(fd);
    return true;
}

} // namespace platform

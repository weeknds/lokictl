/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "firewall.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#ifndef APP_HELPER_PATH
#define APP_HELPER_PATH "/usr/libexec/lokictl/lokictl-helper"
#endif

namespace platform {
namespace {

using std::chrono::milliseconds;

constexpr std::size_t kAddressLength = 17; // "02:00:00:00:00:01"
constexpr milliseconds kWaitStep{10};

bool is_lower_hex(char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// What an exit status of the helper means (see src/helper/lokictl_helper.c).
const char* helper_exit_reason(int code) {
    switch (code) {
        case 2:
            return "bad device address";
        case 3:
            return "no permission";
        case 4:
            return "firewall refused";
        case 5:
            return "nft not found";
        default:
            return "helper failed";
    }
}

// Runs the helper with `arguments` and waits for it, at most `limit` (then
// it is killed). It gets an empty environment,
// /dev/null for its standard streams, no blocked signals and the default
// action for SIGTERM, SIGINT and SIGPIPE (the reader thread blocks the
// first two). Returns an empty text when it worked, else a short reason.
std::string run_helper(const std::vector<std::string>& arguments, milliseconds limit) {
    std::vector<std::string> words;
    words.emplace_back("lokictl-helper");
    words.insert(words.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& word : words) {
        argv.push_back(word.data());
    }
    argv.push_back(nullptr);
    char* environment[] = {nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    sigset_t no_signals;
    sigset_t default_signals;
    sigemptyset(&no_signals);
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGTERM);
    sigaddset(&default_signals, SIGINT);
    sigaddset(&default_signals, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    posix_spawnattr_setsigdefault(&attributes, &default_signals);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    pid_t child = -1;
    const int error = posix_spawn(&child, APP_HELPER_PATH, &actions, &attributes, argv.data(), environment);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error == ENOENT) {
        return "helper not installed";
    }
    if (error == EACCES || error == EPERM) {
        return "helper not allowed";
    }
    if (error != 0) {
        return "helper could not start";
    }

    const auto deadline = std::chrono::steady_clock::now() + limit;
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            break;
        }
        if (done < 0 && errno != EINTR) {
            return "helper failed";
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            return "helper timed out";
        }
        std::this_thread::sleep_for(kWaitStep);
    }
    if (!WIFEXITED(status)) {
        return "helper failed";
    }
    return WEXITSTATUS(status) == 0 ? std::string() : std::string(helper_exit_reason(WEXITSTATUS(status)));
}

} // namespace

bool valid_device_address(const std::string& address) {
    if (address.size() != kAddressLength) {
        return false;
    }
    for (std::size_t index = 0; index < kAddressLength; ++index) {
        const bool separator = index % 3 == 2;
        if (separator ? address[index] != ':' : !is_lower_hex(address[index])) {
            return false;
        }
    }
    // The lowest bit of the first byte marks a group address.
    const char second = address[1];
    const int low_digit = second <= '9' ? second - '0' : second - 'a' + 10;
    return (low_digit & 1) == 0;
}

std::vector<std::string> parse_blocked_devices(const std::string& text, std::size_t* dropped) {
    std::vector<std::string> addresses;
    std::size_t left_out = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string entry = text.substr(start, end - start);
        entry.erase(0, entry.find_first_not_of(" \t"));
        entry.erase(entry.find_last_not_of(" \t") + 1);
        std::transform(entry.begin(), entry.end(), entry.begin(), [](char character) {
            return (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a') : character;
        });
        if (!entry.empty()) {
            if (valid_device_address(entry) && !contains(addresses, entry)) {
                addresses.push_back(entry);
            } else {
                ++left_out;
            }
        }
        start = end + 1;
    }
    if (addresses.size() > kMaxBlockedDevices) {
        const std::size_t extra = addresses.size() - kMaxBlockedDevices;
        addresses.erase(addresses.begin(), addresses.begin() + static_cast<std::ptrdiff_t>(extra));
        left_out += extra;
    }
    if (dropped) {
        *dropped = left_out;
    }
    return addresses;
}

std::string blocked_devices_text(const std::vector<std::string>& addresses) {
    std::string text;
    for (const std::string& address : addresses) {
        if (!text.empty()) {
            text += ',';
        }
        text += address;
    }
    return text;
}

DeviceBlocker::DeviceBlocker(bool real)
    : real_(real) {}

void DeviceBlocker::set_wanted(const std::vector<std::string>& addresses) {
    // Settled again even for the same list: a change that failed is tried
    // once more, and when nothing differs no helper runs.
    wanted_ = addresses;
    settled_ = false;
}

void DeviceBlocker::apply(model::HomeData& data, bool hotspot_up, bool may_run_helper) {
    if (real_) {
        if (hotspot_up != hotspot_up_) {
            hotspot_up_ = hotspot_up;
            settled_ = false;
        }
        if (!settled_ && may_run_helper) {
            settled_ = true;
            settle(hotspot_up);
        }
    }
    const std::vector<std::string>& blocked = real_ ? applied_ : wanted_;
    for (model::HomeClient& client : data.clients) {
        client.blocked = !client.hardware_address.empty() && contains(blocked, client.hardware_address);
    }
    data.block_failures = failures_;
    data.block_message = message_;
}

void DeviceBlocker::leave() {
    if (real_ && table_ != Table::Absent) {
        clear(kHelperLeaveTimeLimit);
    }
}

void DeviceBlocker::settle(bool hotspot_up) {
    if (!hotspot_up) {
        if (table_ != Table::Absent) {
            clear();
        }
        return;
    }
    // A table a killed run left behind may hold old addresses.
    if (table_ == Table::Unknown) {
        clear();
    }
    if (table_ != Table::Present) {
        if (!wanted_.empty() && change("block", "Block", wanted_)) {
            table_ = Table::Present;
            applied_ = wanted_;
        }
        return;
    }
    // Only the differences.
    std::vector<std::string> added;
    std::vector<std::string> removed;
    for (const std::string& address : wanted_) {
        if (!contains(applied_, address)) {
            added.push_back(address);
        }
    }
    for (const std::string& address : applied_) {
        if (!contains(wanted_, address)) {
            removed.push_back(address);
        }
    }
    if (!added.empty() && change("block", "Block", added)) {
        applied_.insert(applied_.end(), added.begin(), added.end());
    }
    if (!removed.empty() && change("unblock", "Unblock", removed)) {
        applied_.erase(std::remove_if(applied_.begin(),
                                      applied_.end(),
                                      [&removed](const std::string& address) { return contains(removed, address); }),
                       applied_.end());
    }
}

bool DeviceBlocker::clear(milliseconds limit) {
    if (!run_helper({"clear"}, limit).empty()) {
        return false;
    }
    table_ = Table::Absent;
    applied_.clear();
    return true;
}

bool DeviceBlocker::change(const char* command, const char* what, const std::vector<std::string>& addresses) {
    std::vector<std::string> arguments{command};
    arguments.insert(arguments.end(), addresses.begin(), addresses.end());
    const std::string reason = run_helper(arguments, kHelperTimeLimit);
    if (reason.empty()) {
        return true;
    }
    ++failures_;
    message_ = std::string(what) + " failed: " + reason;
    return false;
}

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "firewall.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <sstream>
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
// The most of the helper's output that is read (its `list` prints a line
// and at most the set's 64 addresses).
constexpr std::size_t kOutputMax = 4096;
// The line `list` prints while the kill switch is armed.
constexpr const char* kKillSwitchLine = "kill-switch";
// The line `list` prints while the trap's chain is there.
constexpr const char* kTrapLine = "trap";

// One run of the helper at a time: the data reader and the trap's thread
// both run it, and trap-off reads the table before it changes it.
std::mutex& helper_mutex() {
    static std::mutex mutex;
    return mutex;
}

// The trap's intent, and the lock that keeps a check of it and the run of
// the helper that follows together (taken before helper_mutex()).
std::mutex& trap_mutex() {
    static std::mutex mutex;
    return mutex;
}
bool trap_wanted = false;

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
        case 6:
            return "Wi-Fi driver refused";
        case 7:
            return "iw not found";
        default:
            return "helper failed";
    }
}

// Reads what is waiting in `fd` (non-blocking) into `output`, up to
// kOutputMax bytes in all.
void drain(int fd, std::string& output) {
    char chunk[512];
    for (;;) {
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            return;
        }
        output.append(chunk, std::min(static_cast<std::size_t>(got), kOutputMax - std::min(kOutputMax, output.size())));
    }
}

// Runs the helper with `arguments` and waits for it, at most `limit` (then
// it is killed). It gets an empty environment,
// /dev/null for its standard streams (its standard output goes to
// `output` instead, when given), no blocked signals and the default
// action for SIGTERM, SIGINT and SIGPIPE (the reader thread blocks the
// first two). Returns an empty text when it worked, else a short reason.
std::string run_helper(const std::vector<std::string>& arguments, milliseconds limit,
                       std::string* output = nullptr) {
    const std::lock_guard<std::mutex> lock(helper_mutex());
    std::vector<std::string> words;
    words.emplace_back("lokictl-helper");
    words.insert(words.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& word : words) {
        argv.push_back(word.data());
    }
    argv.push_back(nullptr);
    char* environment[] = {nullptr};

    // The output pipe: both ends close on exec (the helper gets its own
    // copy on 1), and ours never blocks, so a stray copy of the write end
    // in another child can never hold up a reading.
    int pipe_ends[2] = {-1, -1};
    if (output) {
        output->clear();
        if (pipe(pipe_ends) != 0) {
            return "helper could not start";
        }
        for (const int end : pipe_ends) {
            fcntl(end, F_SETFD, FD_CLOEXEC);
        }
        fcntl(pipe_ends[0], F_SETFL, O_NONBLOCK);
    }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    const auto close_pipe = [&pipe_ends] {
        for (int& end : pipe_ends) {
            if (end >= 0) {
                close(end);
                end = -1;
            }
        }
    };
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    if (output) {
        posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 1);
    } else {
        posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    }
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
    if (pipe_ends[1] >= 0) {
        close(pipe_ends[1]);
        pipe_ends[1] = -1;
    }
    if (error != 0) {
        close_pipe();
    }
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
        if (output) {
            drain(pipe_ends[0], *output);
        }
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            break;
        }
        if (done < 0 && errno != EINTR) {
            close_pipe();
            return "helper failed";
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
            close_pipe();
            return "helper timed out";
        }
        std::this_thread::sleep_for(kWaitStep);
    }
    if (output) {
        drain(pipe_ends[0], *output);
    }
    close_pipe();
    if (!WIFEXITED(status)) {
        return "helper failed";
    }
    return WEXITSTATUS(status) == 0 ? std::string() : std::string(helper_exit_reason(WEXITSTATUS(status)));
}

// After the whole table went: the trap's redirect again, while wanted.
void restore_trap() {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    if (trap_wanted) {
        run_helper({"trap-on"}, kHelperTimeLimit);
    }
}

// A trap a run before this one left (it was killed): removed, unless this
// run wants one.
void remove_stale_trap() {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    if (!trap_wanted) {
        run_helper({"trap-off"}, kHelperTimeLimit);
    }
}

} // namespace

std::string trap_on(std::vector<uint16_t>& ports, milliseconds limit) {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    ports.clear();
    std::string output;
    const std::string reason = run_helper({"trap-on"}, limit, &output);
    if (!reason.empty()) {
        return reason;
    }
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const long port = std::strtol(line.c_str(), nullptr, 10);
        if (port > 0 && port < 1024) {
            ports.push_back(static_cast<uint16_t>(port));
        }
    }
    trap_wanted = !ports.empty();
    return {};
}

std::string trap_off(milliseconds limit) {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    trap_wanted = false;
    return run_helper({"trap-off"}, limit);
}

std::string add_share_interface(milliseconds limit) {
    return run_helper({"ap-add"}, limit);
}

std::string remove_share_interface(milliseconds limit) {
    return run_helper({"ap-del"}, limit);
}

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

std::string read_kill_switch(bool& present, milliseconds limit) {
    present = false;
    std::string output;
    const std::string reason = run_helper({"list"}, limit, &output);
    if (!reason.empty()) {
        return reason;
    }
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line == kKillSwitchLine) {
            present = true;
        }
    }
    return {};
}

std::vector<std::string> parse_blocked_devices(const std::string& text, std::size_t* dropped) {
    return parse_device_list(text, kMaxBlockedDevices, dropped);
}

std::vector<std::string> parse_device_list(const std::string& text, std::size_t max, std::size_t* dropped) {
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
    if (addresses.size() > max) {
        const std::size_t extra = addresses.size() - max;
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
    if (real_ && (table_ != Table::Absent || kill_ != Table::Absent)) {
        clear(kHelperLeaveTimeLimit);
    }
}

void DeviceBlocker::set_kill_wanted(bool wanted) {
    if (wanted != kill_wanted_) {
        kill_wanted_ = wanted;
        settled_ = false;
    }
}

bool DeviceBlocker::kill_wanted() const {
    return kill_wanted_;
}

std::string DeviceBlocker::arm_kill_switch() {
    if (!real_) {
        kill_wanted_ = true;
        return {};
    }
    const std::string reason = run_helper({"kill-on"}, kHelperTimeLimit);
    if (!reason.empty()) {
        return reason;
    }
    kill_wanted_ = true;
    kill_ = Table::Present;
    // A table not read yet stays Unknown, so its blocked set is still read
    // when the hotspot is up.
    if (table_ == Table::Absent) {
        table_ = Table::Present;
    }
    return {};
}

void DeviceBlocker::settle(bool hotspot_up) {
    // What a run before this one left (one that kept its mode on quitting,
    // or was killed) is read rather than removed.
    if (hotspot_up && table_ == Table::Unknown) {
        adopt();
    }
    if (!hotspot_up && !kill_wanted_) {
        if (table_ != Table::Absent || kill_ != Table::Absent) {
            clear();
        }
        return;
    }
    // The kill switch: on while wanted, also with the hotspot down (a
    // restart); off only when the intent went.
    if (kill_wanted_ && kill_ != Table::Present) {
        switch_kill(true);
    } else if (!kill_wanted_ && kill_ != Table::Absent) {
        switch_kill(false);
    }
    if (!hotspot_up) {
        return;  // the blocked set waits for the hotspot
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

void DeviceBlocker::adopt() {
    std::string output;
    if (!run_helper({"list"}, kHelperTimeLimit, &output).empty()) {
        clear();
        return;
    }
    applied_.clear();
    bool kill = false;
    bool trap = false;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line == kKillSwitchLine) {
            kill = true;
        } else if (line == kTrapLine) {
            trap = true;
        } else if (valid_device_address(line) && !contains(applied_, line)) {
            applied_.push_back(line);
        }
    }
    // `list` prints nothing for a table that holds neither, so the table
    // counts as there: removing one that is not costs one spare run.
    table_ = Table::Present;
    kill_ = kill ? Table::Present : Table::Absent;
    if (kill) {
        kill_wanted_ = true;
    }
    // The trap is off at every start: one left behind goes.
    if (trap) {
        remove_stale_trap();
    }
}

bool DeviceBlocker::clear(milliseconds limit) {
    if (!run_helper({"clear"}, limit).empty()) {
        return false;
    }
    table_ = Table::Absent;
    kill_ = Table::Absent;
    applied_.clear();
    // The trap's chain went with the table; a trap that is on gets it back
    // (the table then holds only that, which trap-off removes).
    restore_trap();
    return true;
}

bool DeviceBlocker::switch_kill(bool on) {
    const std::string reason = run_helper({on ? "kill-on" : "kill-off"}, kHelperTimeLimit);
    if (!reason.empty()) {
        ++failures_;
        message_ = "Kill switch failed: " + reason;
        return false;
    }
    kill_ = on ? Table::Present : Table::Absent;
    if (on && table_ == Table::Absent) {
        table_ = Table::Present;
    }
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

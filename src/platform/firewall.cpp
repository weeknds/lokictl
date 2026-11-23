/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "firewall.h"
#include "mode_switch.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
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

// One short run of the helper at a time: the data reader and the trap's
// thread both run it.
std::mutex& helper_mutex() {
    static std::mutex mutex;
    return mutex;
}

// The trap's helper (its trap-on runs while the trap does), and the lock
// that keeps a look at it and what follows together (taken before
// helper_mutex()).
std::mutex& trap_mutex() {
    static std::mutex mutex;
    return mutex;
}
struct TrapHelper {
    pid_t pid{-1};
    int input{-1};       // its standard input, held open while the trap runs
    int output{-1};      // its standard output (non-blocking)
    std::string partial; // output that is not a whole line yet
};
TrapHelper trap_helper;
// Set when a trap-on ran: its chain went at its end, but the table it made
// may be left (empty) for the way out to remove.
bool trap_made_table = false;
// The line trap-on prints, followed by the low ports it sends on.
constexpr const char* kTrapPortsWord = "ports";

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
            return "bad request";
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
        case 8:
            return "stick in use";
        case 9:
            return "hotspot not running";
        case 10:
            return "DNS server user unclear";
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

// Writes `text` to `fd` without ever raising SIGPIPE in the app: a helper
// that has just ended leaves only EPIPE.
void write_quietly(int fd, const char* text) {
    sigset_t pipe_signal;
    sigset_t old_mask;
    sigemptyset(&pipe_signal);
    sigaddset(&pipe_signal, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &pipe_signal, &old_mask) != 0) {
        return;
    }
    const ssize_t written = write(fd, text, std::strlen(text));
    sigset_t pending;
    if (written < 0 && errno == EPIPE && sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
        int number = 0;
        sigwait(&pipe_signal, &number);  // takes the pending SIGPIPE at once
    }
    pthread_sigmask(SIG_SETMASK, &old_mask, nullptr);
}

// A pipe whose ends close on exec. On Linux both are made that way at once,
// so a child another thread starts meanwhile never gets a copy (one with
// the trap's input would keep the trap running).
int close_on_exec_pipe(int ends[2]) {
#ifdef __linux__
    return pipe2(ends, O_CLOEXEC);
#else
    if (pipe(ends) != 0) {
        return -1;
    }
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}

void close_trap_pipes() {
    for (int* end : {&trap_helper.input, &trap_helper.output}) {
        if (*end >= 0) {
            close(*end);
            *end = -1;
        }
    }
    trap_helper.partial.clear();
}

// Starts "lokictl-helper trap-on" with an empty environment, a pipe on its
// standard input (kept open here; closing it ends the trap) and on its
// standard output (read here), /dev/null for its standard error, no blocked
// signals and the default action for SIGTERM, SIGINT, SIGHUP and SIGPIPE.
// Returns an empty text, or a short reason. Under trap_mutex().
std::string start_trap_helper() {
    int in[2] = {-1, -1};
    int out[2] = {-1, -1};
    if (close_on_exec_pipe(in) != 0) {
        return "helper could not start";
    }
    if (close_on_exec_pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        return "helper could not start";
    }
    fcntl(in[1], F_SETFL, O_NONBLOCK);
    fcntl(out[0], F_SETFL, O_NONBLOCK);
    char program[] = "lokictl-helper";
    char command[] = "trap-on";
    char* argv[] = {program, command, nullptr};
    char* environment[] = {nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    sigset_t no_signals;
    sigset_t default_signals;
    sigemptyset(&no_signals);
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGTERM);
    sigaddset(&default_signals, SIGINT);
    sigaddset(&default_signals, SIGHUP);
    sigaddset(&default_signals, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    posix_spawnattr_setsigdefault(&attributes, &default_signals);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    pid_t child = -1;
    const int error = posix_spawn(&child, APP_HELPER_PATH, &actions, &attributes, argv, environment);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(in[0]);
    close(out[1]);
    if (error != 0) {
        close(in[1]);
        close(out[0]);
        if (error == ENOENT) {
            return "helper not installed";
        }
        return error == EACCES || error == EPERM ? "helper not allowed" : "helper could not start";
    }
    trap_helper.pid = child;
    trap_helper.input = in[1];
    trap_helper.output = out[0];
    trap_helper.partial.clear();
    return {};
}

// Reads what the trap's helper printed; the newest whole "ports" line goes
// into `ports`. True when such a line came. Under trap_mutex().
bool read_trap_ports(std::vector<uint16_t>& ports) {
    if (trap_helper.output < 0) {
        return false;
    }
    char chunk[256];
    for (;;) {
        const ssize_t got = read(trap_helper.output, chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            break;
        }
        if (trap_helper.partial.size() < kOutputMax) {
            trap_helper.partial.append(chunk, static_cast<std::size_t>(got));
        }
    }
    bool found = false;
    std::size_t end = 0;
    while ((end = trap_helper.partial.find('\n')) != std::string::npos) {
        const std::string line = trap_helper.partial.substr(0, end);
        trap_helper.partial.erase(0, end + 1);
        std::istringstream words(line);
        std::string word;
        if (!(words >> word) || word != kTrapPortsWord) {
            continue;
        }
        found = true;
        ports.clear();
        long port = 0;
        while (words >> port) {
            if (port > 0 && port < 1024) {
                ports.push_back(static_cast<uint16_t>(port));
            }
        }
    }
    return found;
}

// When the trap's helper has ended: reaps it, closes its pipes and returns
// true, with a short reason in `problem` when it failed (one killed by a
// signal may have left its chain, which trap-off removes). Under
// trap_mutex().
bool trap_helper_ended(std::string& problem, milliseconds limit) {
    if (trap_helper.pid <= 0) {
        return true;
    }
    int status = 0;
    const pid_t done = waitpid(trap_helper.pid, &status, WNOHANG);
    if (done == 0 || (done < 0 && errno == EINTR)) {
        return false;
    }
    const pid_t pid = trap_helper.pid;
    trap_helper.pid = -1;
    close_trap_pipes();
    if (done == pid && WIFEXITED(status)) {
        problem = WEXITSTATUS(status) == 0 ? std::string() : std::string(helper_exit_reason(WEXITSTATUS(status)));
        return true;
    }
    problem = "helper failed";
    run_helper({"trap-off"}, limit);
    return true;
}

// Ends the trap's helper: closes its standard input and sends SIGTERM, so
// it removes its chain; waits at most `limit`, then kills it and runs
// trap-off instead. Returns an empty text when the chain went, else a
// short reason. Under trap_mutex().
std::string stop_trap_helper(milliseconds limit) {
    if (trap_helper.pid <= 0) {
        return {};
    }
    if (trap_helper.input >= 0) {
        close(trap_helper.input);
        trap_helper.input = -1;
    }
    kill(trap_helper.pid, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + limit;
    std::string problem;
    while (!trap_helper_ended(problem, limit)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(trap_helper.pid, SIGKILL);
            while (waitpid(trap_helper.pid, nullptr, 0) < 0 && errno == EINTR) {
            }
            trap_helper.pid = -1;
            close_trap_pipes();
            const std::string reason = run_helper({"trap-off"}, limit);
            return reason.empty() ? std::string("helper timed out") : reason;
        }
        std::this_thread::sleep_for(kWaitStep);
    }
    return problem;
}

// After the whole table went: the running trap sets its chain up again.
void restore_trap() {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    if (trap_helper.pid > 0 && trap_helper.input >= 0) {
        write_quietly(trap_helper.input, "\n");
    }
}

// A trap a run before this one left (it was killed): removed, unless this
// run's trap is on.
void remove_stale_trap() {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    if (trap_helper.pid <= 0) {
        run_helper({"trap-off"}, kHelperTimeLimit);
    }
}

} // namespace

std::string trap_on(std::vector<uint16_t>& ports, milliseconds limit) {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    ports.clear();
    stop_trap_helper(limit);
    const std::string reason = start_trap_helper();
    if (!reason.empty()) {
        return reason;
    }
    trap_made_table = true;
    // Its first "ports" line, within `limit`.
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!read_trap_ports(ports)) {
        std::string problem;
        if (trap_helper_ended(problem, limit)) {
            ports.clear();
            return problem;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            ports.clear();
            kill(trap_helper.pid, SIGKILL);
            while (waitpid(trap_helper.pid, nullptr, 0) < 0 && errno == EINTR) {
            }
            trap_helper.pid = -1;
            close_trap_pipes();
            run_helper({"trap-off"}, limit);
            return "helper timed out";
        }
        std::this_thread::sleep_for(kWaitStep);
    }
    if (ports.empty()) {
        // Nothing to send on: it ends by itself.
        return stop_trap_helper(limit);
    }
    return {};
}

bool trap_changed(std::vector<uint16_t>& ports, std::string& problem) {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    if (trap_helper.pid <= 0) {
        return false;
    }
    const bool fresh = read_trap_ports(ports);
    if (trap_helper_ended(problem, kHelperTimeLimit)) {
        ports.clear();
        return true;
    }
    return fresh;
}

std::string trap_off(milliseconds limit) {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    if (trap_helper.pid > 0) {
        return stop_trap_helper(limit);
    }
    return run_helper({"trap-off"}, limit);
}

bool trap_left_table() {
    const std::lock_guard<std::mutex> lock(trap_mutex());
    return trap_made_table && trap_helper.pid <= 0;
}

std::string add_share_interface(milliseconds limit) {
    return run_helper({"ap-add"}, limit);
}

std::string remove_share_interface(milliseconds limit) {
    return run_helper({"ap-del"}, limit);
}

std::string remove_survey_listener(milliseconds limit) {
    return run_helper({"survey-off"}, limit);
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

void DeviceBlocker::apply(model::HomeData& data, const std::string& hotspot_interface, bool may_run_helper) {
    if (real_) {
        const std::string hotspot = hotspot_interface.empty() ? std::string()
                                    : hotspot_interface == kShareInterface ? std::string("share")
                                                                           : std::string("router");
        if (hotspot != hotspot_) {
            hotspot_ = hotspot;
            settled_ = false;
            // The DNS server's user is read again once a hotspot runs.
            kill_for_hotspot_ = false;
        }
        if (!settled_ && may_run_helper) {
            settled_ = true;
            settle();
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
    if (real_ && (table_ != Table::Absent || kill_ != Table::Absent || trap_left_table())) {
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
    kill_for_hotspot_ = !hotspot_.empty();
    // A table not read yet stays Unknown, so its blocked set is still read
    // when the hotspot is up.
    if (table_ == Table::Absent) {
        table_ = Table::Present;
    }
    return {};
}

void DeviceBlocker::settle() {
    const bool hotspot_up = !hotspot_.empty();
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
    // restart); off only when the intent went. Armed before the hotspot
    // started, it matched dnsmasq's default user: once the hotspot runs it
    // is armed again for the user its DNS server really runs as.
    if (kill_wanted_ && (kill_ != Table::Present || (hotspot_up && !kill_for_hotspot_))) {
        switch_kill(true);
    } else if (!kill_wanted_ && kill_ != Table::Absent) {
        switch_kill(false);
    }
    if (!hotspot_up) {
        return;  // the blocked set waits for the hotspot
    }
    // The two rules match one hotspot's interface: after a change of
    // hotspot (or a table adopted from a run before this one) they are
    // made again for this one.
    if (table_ == Table::Present && rules_for_ != hotspot_ && !applied_.empty()) {
        change("block", "Block", applied_);
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
    // counts as there: removing one that is not costs one spare run. Which
    // hotspot its rules match is not known.
    table_ = Table::Present;
    rules_for_.clear();
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
    rules_for_.clear();
    // The trap's chain went with the table; a trap that is on sets it up
    // again.
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
    kill_for_hotspot_ = on && !hotspot_.empty();
    if (on && table_ == Table::Absent) {
        table_ = Table::Present;
    }
    return true;
}

bool DeviceBlocker::change(const char* command, const char* what, const std::vector<std::string>& addresses) {
    std::vector<std::string> arguments{command, hotspot_};
    arguments.insert(arguments.end(), addresses.begin(), addresses.end());
    const std::string reason = run_helper(arguments, kHelperTimeLimit);
    if (reason.empty()) {
        rules_for_ = hotspot_;
        return true;
    }
    ++failures_;
    message_ = std::string(what) + " failed: " + reason;
    return false;
}

} // namespace platform

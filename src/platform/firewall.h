/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

// The most devices the saved list keeps; blocking one more drops the
// oldest.
constexpr std::size_t kMaxBlockedDevices = 16;

// How long a run of the helper may take before it is killed; on the way
// out (DeviceBlocker::leave()) less, so the switch back to Client keeps
// most of its time.
constexpr std::chrono::milliseconds kHelperTimeLimit{3000};
constexpr std::chrono::milliseconds kHelperLeaveTimeLimit{1000};

// Share's interface (kShareInterface), through the helper: adds it on the
// built-in Wi-Fi as an access-point interface, or removes it. Each is done
// when the interface is already there (or already gone). Waits for the
// helper, at most `limit` (then it is killed), so it runs on the data
// reader's thread only, never on the UI thread. Returns an empty text when
// it worked, else a short reason ("helper not installed").
std::string add_share_interface(std::chrono::milliseconds limit = kHelperTimeLimit);
std::string remove_share_interface(std::chrono::milliseconds limit = kHelperTimeLimit);

// True for a device's hardware address as LokiCTL writes it: six pairs of
// lowercase hex digits joined by ':' ("02:00:00:00:00:01"), not a group
// address (lowest bit of the first byte), which never sends.
bool valid_device_address(const std::string& address);

// The saved list (the blocked_devices= key): addresses separated by commas,
// spaces allowed around them. Capitals become lowercase; anything else
// that is not a valid_device_address() and repeats are dropped, and of the
// rest the last kMaxBlockedDevices are kept. `dropped`, when given, gets
// how many entries were left out.
std::vector<std::string> parse_blocked_devices(const std::string& text, std::size_t* dropped = nullptr);

// The list as the blocked_devices= key holds it: "a,b,c" (empty for none).
std::string blocked_devices_text(const std::vector<std::string>& addresses);

// Blocks devices on the hotspot by their hardware address (F4.4). A blocked
// device stays joined to the hotspot, but LokiCTL's firewall table drops
// everything it sends in: no address, no internet, no answer from the
// Cardputer.
//
// It works in one of two ways:
// - Real (the live source on the device, when it may change the network):
//   it runs the helper program (lokictl-helper, the only part of LokiCTL
//   with the right to change the firewall) and makes LokiCTL's table match
//   the wanted list while the hotspot runs: when the hotspot comes up,
//   when the list changes, and the table goes when the hotspot stops,
//   once after the start (a table a killed run left behind) and on the way
//   out (leave()). A device counts as blocked only once the helper said
//   so. A change that fails keeps the old state and counts as a failure
//   with a message ("Block failed: helper not installed"); a failure to
//   remove the table is not counted, because a helper that cannot run
//   never made one. Each step runs once; after a failure the next try
//   comes with the next set_wanted() (a key press asks again) or change of
//   the hotspot.
// - Pretend (the made-up sources, test folders, the Mac, snapshots,
//   --print-data): every wanted device counts as blocked at once and
//   nothing runs.
//
// Only one thread at a time may use it (the data reader's).
class DeviceBlocker {
public:
    explicit DeviceBlocker(bool real);

    // The devices that should be blocked (valid_device_address() each).
    // Real: the table is checked against it at the next reading even when
    // the list did not change, so a change that failed is tried again.
    void set_wanted(const std::vector<std::string>& addresses);

    // Called once per reading, after `data.clients` is filled in: marks
    // each blocked client (HomeClient::blocked) and copies the failures
    // into `data`. Real: first makes the table match, when something
    // changed and `may_run_helper` is true (it is false in the first
    // reading, which runs on the UI thread, and while a switch between
    // modes runs; the table is then made to match at a later reading).
    // `hotspot_up`: LokiCTL's hotspot is active.
    void apply(model::HomeData& data, bool hotspot_up, bool may_run_helper);

    // On the way out (real only): removes the table unless it is surely
    // gone. Waits for the helper, at most kHelperLeaveTimeLimit.
    void leave();

private:
    enum class Table {
        Unknown, // not known since the start: a killed run may have left one
        Absent,
        Present,
    };

    // Brings the table in line with the wanted list and the hotspot (real).
    void settle(bool hotspot_up);
    // Runs the helper's `clear`, waiting at most `limit`; true when it
    // worked.
    bool clear(std::chrono::milliseconds limit = kHelperTimeLimit);
    // Runs `command` (block or unblock) for `addresses`; true when it
    // worked, otherwise counts a failure with `what` ("Block", "Unblock").
    bool change(const char* command, const char* what, const std::vector<std::string>& addresses);

    const bool real_;
    std::vector<std::string> wanted_;
    std::vector<std::string> applied_; // what the table holds, as far as known
    Table table_{Table::Unknown};
    bool settled_{false};   // nothing to do until the list or the hotspot changes
    bool hotspot_up_{false};
    int32_t failures_{0};
    std::string message_;
};

} // namespace platform

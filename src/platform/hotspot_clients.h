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
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace platform {

// One device the access point lists (a "station"), as the Wi-Fi driver
// tells it. A value that is not known keeps the "not known" value of
// model::HomeClient.
struct Station {
    std::string hardware_address;                        // "02:5e:11:7a:c3:04", lowercase
    int32_t signal_dbm{model::kSignalUnknown};           // received signal, dBm
    int32_t connected_seconds{model::kConnectedUnknown}; // time on the access point
    bool has_bytes{false};                               // `bytes` was told
    uint64_t bytes{0};                                   // received and sent, together
};

// The most devices HotspotClients lists.
constexpr std::size_t kMaxClients = 16;

// Reads the devices on the access point of `interface_name` from the
// kernel's Wi-Fi interface (nl80211: a station dump over generic netlink),
// which every user may read. It only reads. A device that is listed but
// not yet let in (not "authorized") is left out. Linux only: elsewhere it
// returns false. Returns false, with `stations` empty, when the kernel could
// not be asked or answered with an error; it waits at most about a second
// for an answer. Never throws.
bool read_system_stations(const std::string& interface_name, std::vector<Station>& stations);

// Reads a file that stands in for the station dump in a test folder. Lines
// of "<interface> <hardware address> <signal|-> <connected seconds|->
// <bytes|->"; "-" means not known, and blank lines and lines starting with
// '#' are skipped. Only the lines of `interface_name` count. The seconds
// and the bytes may be lists, "100,2600,5100": reading `reading` (counting
// from 0) takes the value at that place, or the last one past the end, so a
// test can give a rate or a time that moves. A line that cannot be read is
// skipped; a missing file lists no devices. Never throws.
std::vector<Station> read_stations_file(const std::filesystem::path& path,
                                        const std::string& interface_name,
                                        std::size_t reading);

// The neighbour table of `interface_name`: hardware address (lowercase) ->
// IPv4 address, from "<root>/proc/net/arp", which every user may read.
// Only complete entries count (flag 0x2); the first address of a hardware
// address wins. Never throws.
std::map<std::string, std::string> read_neighbours(const std::filesystem::path& root,
                                                   const std::string& interface_name);

// Reads a file that stands in for the DNS server's names in a test folder:
// lines of "<IPv4> <name>"; the same cleaning as ask_host_names() applies.
// Never throws.
std::map<std::string, std::string> read_host_names_file(const std::filesystem::path& path);

// A host name as the pages may show it: the first label only (up to the
// first '.'), at most 32 characters, and every byte that is not printable
// ASCII (a control character, a byte of another script) becomes '?'. Host
// names come from the devices themselves, so nothing is taken on trust.
std::string clean_host_name(const std::string& name);

// Asks the DNS server at `server` (an IPv4 address, port 53) for the names
// of `addresses` (reverse lookups, "PTR"). All questions go out at once on
// one socket, and it waits at most `limit` in all for the answers. Returns
// address -> name (see clean_host_name()) for every address that got an
// answer: an empty name when the server knows none. An address without an
// answer is not in the result. Only answers from the server to the
// questions asked count. Never throws.
std::map<std::string, std::string> ask_host_names(const std::string& server,
                                                  const std::vector<std::string>& addresses,
                                                  std::chrono::milliseconds limit);

// The devices on LokiCTL's hotspot, for the live source (see
// LiveDataSource): each reading lists the stations of the hotspot's
// interface, adds their address from the neighbour table and their name
// from NetworkManager's own DNS server on the hotspot (dnsmasq, which knows
// the names the devices sent when they got their address), and works out
// their traffic since the reading before.
//
// - Order: a device keeps its place from the reading before; newcomers go
//   after the devices already listed, the longest joined first (a tie, or a
//   time that is not known, by hardware address). At most kMaxClients.
// - A station dump that fails keeps the last list as it is.
// - New: joined less than model::kClientNewSeconds ago.
// - Rate: received and sent bytes since the last reading, per device, over
//   the time between the two readings. Not known at a device's first
//   reading, and when its counter is not told or went backwards.
// - Names: asked of the DNS server at the hotspot interface's own IPv4
//   address, once per device address, and again after kNameRetry while the
//   server knew none or did not answer; at most kNameWait per reading in
//   all. A name stays while its address is listed.
//
// In a test folder the stations come from "<root>/stations" (see
// read_stations_file()), the neighbour table from "<root>/proc/net/arp" and
// the names from "<root>/hostnames" (see read_host_names_file()); nothing
// is asked, and each reading counts as exactly kTestInterval after the one
// before, so a run with the same files always gives the same values.
class HotspotClients {
public:
    // `root` stands in for the root of the file system, and with
    // `test_folder` the files above stand in for the system. `addresses_file`
    // as for read_network(): empty on the real system.
    HotspotClients(std::filesystem::path root, std::filesystem::path addresses_file, bool test_folder);

    // Reads the devices on `interface_name` now. `may_ask_names` false
    // asks the DNS server nothing in this reading (while a switch between
    // modes runs, so the reader is never held up then).
    void read(const std::string& interface_name, bool may_ask_names);

    // Forgets everything: no devices, no counters, no names.
    void clear();

    // The devices of the last reading, in order.
    const std::vector<model::HomeClient>& clients() const;

    // True after a read() since the last clear().
    bool has_read() const;

    // The interface of the last reading.
    const std::string& interface_name() const;

    static constexpr std::chrono::milliseconds kNameWait{250};
    static constexpr std::chrono::seconds kNameRetry{60};
    static constexpr std::chrono::milliseconds kTestInterval{5000};

private:
    // What is kept of a device between readings, by hardware address.
    struct Counter {
        bool has_bytes{false};
        uint64_t bytes{0};
    };
    // What the DNS server said about an address.
    struct NameEntry {
        std::string name;
        std::chrono::steady_clock::time_point asked_at{};
    };

    // Asks the names of the addresses that need asking (see the class
    // comment) and forgets the names of addresses no longer listed.
    void update_names(const std::string& interface_name,
                      const std::vector<std::string>& addresses,
                      std::chrono::steady_clock::time_point now,
                      bool may_ask);

    const std::filesystem::path root_;
    const std::filesystem::path addresses_file_;
    const bool test_folder_;
    std::vector<model::HomeClient> clients_;
    bool has_read_{false};
    std::string interface_name_;
    std::chrono::steady_clock::time_point read_at_{};
    std::size_t reading_{0}; // test folder: the reading the next read() takes
    std::map<std::string, Counter> counters_;
    std::map<std::string, NameEntry> names_; // by IPv4 address
};

} // namespace platform

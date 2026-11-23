/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "listen.h"
#include "watch_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Watch (model::WatchRun): passive alerts about the network LokiCTL is on.
// The capture helper's fixed `watch` command (src/helper/lokictl_capture.c)
// passes ARP, DHCP, TCP connection requests (SYN without ACK) addressed to
// this machine and IPv6 router announcements; everything here reads them
// without privileges and never sends.
namespace platform {

using HardwareAddress = std::array<uint8_t, 6>;
using Ipv6Address = std::array<uint8_t, 16>;

// What a WatchDetector starts from.
struct WatchSetup {
    std::vector<OwnPort> own_ports;        // the Cardputer's own ports: never reported
    uint32_t own_address{0};               // the uplink's IPv4 address; 0: not known (any counts)
    uint32_t gateway{0};                   // the router's IPv4 address; 0: none
    std::string gateway_hardware;          // the router's hardware address; empty: the first one heard
    std::vector<Ipv6Address> ipv6_routers; // IPv6 routers the system already uses
    std::vector<std::string> known;        // known devices' hardware addresses
    int64_t learn_until_ms{0};             // frames before this time learn devices instead of alerting
    // The text of an alert's time ("07:46") from a frame's time.
    std::function<std::string(int64_t at_ms)> clock_text;
};

// Finds the alerts in what the helper passes on. One frame at a time, with
// its time in milliseconds (the PCAP record's time; any origin, as long as
// it does not go back). Every table it keeps has a fixed size, so a flood
// of made-up addresses cannot make it grow. The Cardputer's own hardware
// addresses never raise an alert and never count. Each alert is raised
// once for its subject in a run; a scan or a flood may raise one again
// after its source has been quiet for model::kWatchQuietS. Every length is
// checked against the bytes captured.
//
// - New device: a hardware address heard as an ARP sender or a DHCP client
//   that is not on the known list. While learning (learn_until_ms), it is
//   added to the known list instead (learned()).
// - Fake router: an ARP sender claims the router's address with another
//   hardware address than the router's.
// - Same address: an ARP sender claims an address that another hardware
//   address claimed within model::kWatchSameAddressWindowS, or the
//   Cardputer's own address.
// - Second DHCP: a DHCP offer, acknowledgement or refusal from another
//   server than the first one heard.
// - IPv6 router: a router announcement (ICMPv6 type 134, hop limit 255)
//   from a router the system does not use, whose hardware address is not
//   the IPv4 router's.
// - Scan: one source asks for connections to model::kWatchScanPorts
//   different ports of the Cardputer's address within
//   model::kWatchScanWindowS; the alert's count then follows how many
//   different ports it tried.
// - ARP flood: more than model::kWatchArpFloodPerSecond ARP frames within
//   one second.
class WatchDetector {
public:
    explicit WatchDetector(WatchSetup setup);

    void note_frame(const uint8_t* frame, std::size_t size, int64_t at_ms);

    // The newest first, at most model::kWatchMaxAlerts.
    const std::vector<model::WatchAlert>& alerts() const { return alerts_; }
    int32_t raised() const { return raised_; }
    int32_t frames() const { return frames_; }
    const std::vector<std::string>& learned() const { return learned_; }
    // True once after anything above changed.
    bool take_changed();

private:
    struct SameEntry {
        uint32_t address{0};
        HardwareAddress hardware{};
        int64_t at_ms{0};
    };
    struct ScanSource {
        uint32_t address{0};
        int64_t last_ms{0};
        std::vector<std::pair<uint16_t, int64_t>> recent;  // port, time: within the window
        std::vector<uint16_t> tried;                       // every port of this episode, sorted
        bool alerted{false};
        uint32_t alert_id{0};
    };

    void note_arp(const uint8_t* frame, std::size_t size, int64_t at_ms);
    void note_ipv4(const uint8_t* frame, std::size_t size, int64_t at_ms);
    void note_dhcp(const uint8_t* bootp, std::size_t size, const uint8_t* sender, uint32_t source, int64_t at_ms);
    void note_syn(const uint8_t* sender, uint32_t source, uint32_t destination, uint16_t port, int64_t at_ms);
    void note_ipv6(const uint8_t* frame, std::size_t size, int64_t at_ms);
    void note_device(const uint8_t* hardware, uint32_t address, int64_t at_ms);
    bool own(const uint8_t* hardware) const;
    bool known(const std::string& hardware) const;
    uint32_t raise(model::WatchAlert alert, int64_t at_ms);
    model::WatchAlert* find(uint32_t id);

    WatchSetup setup_;
    std::vector<HardwareAddress> own_hardware_;
    std::vector<std::string> known_;
    std::vector<std::string> learned_;
    std::vector<std::string> new_seen_;      // devices a New device alert was raised for
    std::string gateway_hardware_;           // the router's hardware address; empty until known
    std::vector<std::string> fake_routers_;  // impostors already reported
    std::vector<SameEntry> same_;
    std::vector<uint32_t> same_reported_;
    uint32_t dhcp_server_{0};
    std::vector<uint32_t> dhcp_reported_;
    std::vector<ScanSource> scans_;
    int64_t flood_second_{-1};
    int32_t flood_count_{0};
    int64_t flood_last_ms_{0};
    bool flood_alerted_{false};
    uint32_t flood_alert_id_{0};
    std::vector<Ipv6Address> ipv6_routers_;
    std::vector<model::WatchAlert> alerts_;
    std::vector<uint32_t> alert_ids_;  // alongside alerts_
    uint32_t next_id_{1};
    int32_t raised_{0};
    int32_t frames_{0};
    bool changed_{false};
};

// The hardware address the system's neighbour table (`path`, normally
// /proc/net/arp) has for `address` on `interface_name`; empty when none is
// complete.
std::string read_neighbour_hardware(const std::string& interface_name, uint32_t address,
                                    const std::string& path = "/proc/net/arp");
// The IPv6 routers the system's default routes on `interface_name` lead to
// (`path`, normally /proc/net/ipv6_route); at most 8.
std::vector<Ipv6Address> read_ipv6_routers(const std::string& interface_name,
                                           const std::string& path = "/proc/net/ipv6_route");

// An IPv4 address in dotted form ("192.0.2.1") as a number; 0 when the text
// is not one.
uint32_t parse_ipv4(const std::string& text);

// Made-up frames, for the pretend runner and the checks (tests/).
struct WatchFrame {
    uint32_t at_ms{0};
    std::vector<uint8_t> bytes;
};
std::vector<uint8_t> watch_arp_frame(const HardwareAddress& sender, uint32_t sender_address,
                                     uint32_t target_address, bool reply = false);
std::vector<uint8_t> watch_syn_frame(const HardwareAddress& sender, const HardwareAddress& to, uint32_t source,
                                     uint32_t destination, uint16_t port, bool ack = false);
std::vector<uint8_t> watch_dhcp_reply_frame(const HardwareAddress& sender, uint32_t server, uint8_t message);
std::vector<uint8_t> watch_dhcp_request_frame(const HardwareAddress& client, uint32_t requested);
std::vector<uint8_t> watch_router_advert_frame(const HardwareAddress& sender, const Ipv6Address& source,
                                               uint8_t hop_limit = 255);

// The pretend runner's made-up network (documentation-style 10.0.0.x
// addresses, locally administered hardware addresses): its frames in time
// order, one of each alert kind within the first 20 s, the setup it starts
// from, and the known devices the simulator starts with.
const std::vector<WatchFrame>& pretend_watch_frames();
WatchSetup pretend_watch_setup();
std::vector<std::string> pretend_known_devices();

// What a Watch run should listen on, and what it starts from.
struct WatchJob {
    std::string interface_name;   // checked with model::valid_interface_name()
    std::string own_address;      // the uplink's IPv4 address as text; may be empty
    std::string gateway_address;  // the router's IPv4 address as text; may be empty
    std::vector<std::string> known;
};

// Runs Watch away from the UI thread and keeps its latest state for the
// screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first run and kept until shutdown(). It reads the router's
// hardware address and the IPv6 routers from /proc, then runs the capture
// helper's `watch` command on the interface for model::kWatchHelperSeconds
// at a time, and again after about a second each time it ends normally (its
// time or size limit); the detector keeps what it learned. Any other end
// fails the run. Every wait is at most 100 ms, so a stop ends the helper
// within about 0.2 s. It sends nothing and never changes a network setting.
//
// The pretend runner (the Mac simulator and snapshots) has no thread and
// opens nothing: each take() feeds the made-up frames up to the LVGL time
// since start() through the same detector, so the same keys and settle time
// always give the same picture.
//
// All functions are called on the UI thread.
class WatchRunner {
public:
    virtual ~WatchRunner() = default;

    // Starts a run, which then reads Running at once. Returns false and
    // changes nothing while one runs or when the interface is not valid.
    virtual bool start(const WatchJob& job) = 0;
    // Asks the run to end; it then reads Stopped. Never waits.
    virtual void stop() = 0;
    // Copies the latest state into `run` and returns true, but only when it
    // changed since the last take().
    virtual bool take(model::WatchRun& run) = 0;
    // Stops the run and ends the thread, waiting for it. Safe to call more
    // than once; start() refuses every run afterwards.
    virtual void shutdown() = 0;
};

std::unique_ptr<WatchRunner> make_watch_runner(bool live);

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of Watch's alerts (src/platform/watch.h): made-up frames for each
// alert kind are replayed as a PCAP stream through the same reader the app
// uses, and the detector must raise exactly the alerts expected, and none
// for ordinary traffic. Built on the Mac only, as lokictl_watch_tests;
// exits 1 when a check fails. Only made-up addresses: no real capture.

#include "listen.h"
#include "watch.h"
#include "watch_data.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using platform::HardwareAddress;
using platform::WatchDetector;
using platform::WatchSetup;
using Kind = model::WatchAlertKind;

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

constexpr uint32_t ipv4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

constexpr HardwareAddress kOwn{0x02, 0x00, 0x5e, 0x00, 0x00, 0x01};
constexpr HardwareAddress kRouter{0x02, 0x00, 0x5e, 0x00, 0x00, 0x0a};
constexpr HardwareAddress kPhone{0x02, 0x00, 0x5e, 0x00, 0x00, 0x14};
constexpr HardwareAddress kStranger{0x02, 0x00, 0x5e, 0x00, 0x00, 0x63};
constexpr uint32_t kOwnAddress = ipv4(10, 0, 0, 12);
constexpr uint32_t kGateway = ipv4(10, 0, 0, 1);
constexpr uint32_t kPhoneAddress = ipv4(10, 0, 0, 20);
constexpr uint32_t kStrangerAddress = ipv4(10, 0, 0, 99);

std::string text(const HardwareAddress& hardware) {
    char out[18];
    std::snprintf(out, sizeof(out), "%02x:%02x:%02x:%02x:%02x:%02x", hardware[0], hardware[1], hardware[2],
                  hardware[3], hardware[4], hardware[5]);
    return out;
}

WatchSetup setup() {
    WatchSetup result;
    result.own_ports = {{"eth0", text(kOwn)}};
    result.own_address = kOwnAddress;
    result.gateway = kGateway;
    result.gateway_hardware = text(kRouter);
    result.known = {text(kRouter), text(kPhone)};
    result.clock_text = [](int64_t) { return std::string("07:46"); };
    return result;
}

// The frames as the helper writes them: a PCAP stream, read back in pieces
// of 7 bytes through platform::CaptureReader into the detector.
void replay(WatchDetector& detector, const std::vector<platform::WatchFrame>& frames) {
    std::vector<uint8_t> stream;
    const auto put = [&stream](uint32_t value) {
        uint8_t bytes[4];
        std::memcpy(bytes, &value, sizeof(bytes));
        stream.insert(stream.end(), bytes, bytes + 4);
    };
    put(0xa1b2c3d4u);
    const uint16_t version[2] = {2, 4};
    uint8_t version_bytes[4];
    std::memcpy(version_bytes, version, sizeof(version_bytes));
    stream.insert(stream.end(), version_bytes, version_bytes + 4);
    put(0);
    put(0);
    put(platform::kCaptureSnapLength);
    put(1);
    for (const platform::WatchFrame& frame : frames) {
        put(frame.at_ms / 1000);
        put((frame.at_ms % 1000) * 1000);
        put(static_cast<uint32_t>(frame.bytes.size()));
        put(static_cast<uint32_t>(frame.bytes.size()));
        stream.insert(stream.end(), frame.bytes.begin(), frame.bytes.end());
    }
    platform::CaptureReader reader;
    const auto on_frame = [&detector](const uint8_t* frame, std::size_t size, uint32_t seconds) {
        detector.note_frame(frame, size, static_cast<int64_t>(seconds) * 1000);
    };
    for (std::size_t at = 0; at < stream.size(); at += 7) {
        const std::size_t size = std::min<std::size_t>(7, stream.size() - at);
        expect(reader.feed(stream.data() + at, size, on_frame), "the PCAP stream reads");
    }
}

int count(const WatchDetector& detector, Kind kind) {
    int found = 0;
    for (const model::WatchAlert& alert : detector.alerts()) {
        found += alert.kind == kind ? 1 : 0;
    }
    return found;
}

std::vector<platform::WatchFrame> syn_sweep(uint32_t first_ms, uint32_t step_ms, int ports, uint16_t first_port,
                                            bool same_port = false) {
    std::vector<platform::WatchFrame> frames;
    for (int index = 0; index < ports; ++index) {
        const auto port = static_cast<uint16_t>(same_port ? first_port : first_port + index);
        frames.push_back({first_ms + static_cast<uint32_t>(index) * step_ms,
                          platform::watch_syn_frame(kStranger, kOwn, kStrangerAddress, kOwnAddress, port)});
    }
    return frames;
}

void check_pretend_network() {
    WatchDetector detector(platform::pretend_watch_setup());
    replay(detector, platform::pretend_watch_frames());
    const std::vector<Kind> expected{Kind::ArpFlood,    Kind::Ipv6Router, Kind::SecondDhcp, Kind::SameAddress,
                                     Kind::FakeRouter,  Kind::Scan,       Kind::NewDevice,  Kind::NewDevice};
    const auto& alerts = detector.alerts();
    expect(alerts.size() == expected.size(),
           "the made-up network raises 8 alerts (got " + std::to_string(alerts.size()) + ")");
    for (std::size_t index = 0; index < alerts.size() && index < expected.size(); ++index) {
        expect(alerts[index].kind == expected[index], "alert " + std::to_string(index) + " has the expected kind");
    }
    for (const model::WatchAlert& alert : alerts) {
        if (alert.kind == Kind::Scan) {
            expect(alert.count == 20, "the scan counts 20 ports (got " + std::to_string(alert.count) + ")");
            expect(alert.address == "10.0.0.66", "the scan names its source");
        }
        if (alert.kind == Kind::ArpFlood) {
            expect(alert.count == 60, "the flood counts 60 frames in its second");
        }
        if (alert.kind == Kind::FakeRouter) {
            expect(alert.address == "10.0.0.1" && alert.before == "02:1a:2b:3c:00:01",
                   "the fake router alert names the router and its real hardware address");
        }
        expect(alert.time == "07:46", "the made-up clock reads 07:46");
    }
    expect(detector.raised() == 8, "raised counts 8");
}

void check_scans() {
    {
        WatchDetector detector(setup());
        replay(detector, syn_sweep(1000, 100, 20, 20));
        expect(count(detector, Kind::Scan) == 1, "20 ports in 2 s: exactly one scan alert");
        expect(!detector.alerts().empty() && detector.alerts().front().count == 20, "it counts 20 ports");
        // More of the same scan 5 s later: still the same alert.
        replay(detector, syn_sweep(8000, 100, 20, 100));
        expect(count(detector, Kind::Scan) == 1, "a scan that goes on raises no second alert");
        expect(detector.alerts().front().count == 40, "the alert counts on to 40");
        // Quiet for over a minute, then again: a new alert.
        replay(detector, syn_sweep(80000, 100, 10, 300));
        expect(count(detector, Kind::Scan) == 2, "a scan after a quiet minute raises a new alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, syn_sweep(1000, 100, 9, 20));
        expect(count(detector, Kind::Scan) == 0, "9 ports: no scan alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, syn_sweep(1000, 3000, 10, 20));
        expect(count(detector, Kind::Scan) == 0, "10 ports over 30 s: no scan alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, syn_sweep(1000, 100, 20, 443, true));
        expect(count(detector, Kind::Scan) == 0, "the same port 20 times: no scan alert");
    }
    {
        WatchDetector detector(setup());
        std::vector<platform::WatchFrame> frames;
        for (int index = 0; index < 20; ++index) {
            const auto port = static_cast<uint16_t>(20 + index);
            const uint32_t at = 1000 + static_cast<uint32_t>(index) * 100;
            // Answers (SYN with ACK) and requests to another address.
            frames.push_back(
                {at, platform::watch_syn_frame(kStranger, kOwn, kStrangerAddress, kOwnAddress, port, true)});
            frames.push_back(
                {at, platform::watch_syn_frame(kStranger, kOwn, kStrangerAddress, kPhoneAddress, port)});
            // This device's own requests.
            frames.push_back({at, platform::watch_syn_frame(kOwn, kStranger, kOwnAddress, kOwnAddress, port)});
        }
        replay(detector, frames);
        expect(count(detector, Kind::Scan) == 0, "answers, other addresses and own requests: no scan alert");
    }
}

void check_addresses() {
    {
        WatchDetector detector(setup());
        replay(detector, {{100, platform::watch_arp_frame(kRouter, kGateway, kOwnAddress)},
                          {200, platform::watch_arp_frame(kPhone, kPhoneAddress, kGateway)},
                          {300, platform::watch_arp_frame(kOwn, kOwnAddress, kGateway)},
                          {400, platform::watch_dhcp_reply_frame(kRouter, kGateway, 5)},
                          {500, platform::watch_dhcp_request_frame(kPhone, kPhoneAddress)}});
        expect(detector.alerts().empty(), "ordinary traffic of known devices: no alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, {{100, platform::watch_arp_frame(kStranger, kGateway, kOwnAddress, true)},
                          {200, platform::watch_arp_frame(kStranger, kGateway, kOwnAddress, true)}});
        expect(count(detector, Kind::FakeRouter) == 1, "another device claims the router's address: one alert");
        expect(count(detector, Kind::NewDevice) == 1, "and it is a new device");
    }
    {
        WatchSetup first = setup();
        first.gateway_hardware.clear();
        WatchDetector detector(first);
        replay(detector, {{100, platform::watch_arp_frame(kRouter, kGateway, kOwnAddress)},
                          {200, platform::watch_arp_frame(kStranger, kGateway, kOwnAddress, true)}});
        expect(count(detector, Kind::FakeRouter) == 1, "the first router heard counts, a second one is reported");
    }
    {
        WatchDetector detector(setup());
        replay(detector, {{100, platform::watch_arp_frame(kPhone, kPhoneAddress, kGateway)},
                          {5000, platform::watch_arp_frame(kStranger, kPhoneAddress, kGateway, true)}});
        expect(count(detector, Kind::SameAddress) == 1, "two devices on one address within 2 min: one alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, {{100, platform::watch_arp_frame(kPhone, kPhoneAddress, kGateway)},
                          {200000, platform::watch_arp_frame(kStranger, kPhoneAddress, kGateway, true)}});
        expect(count(detector, Kind::SameAddress) == 0, "an address taken over after 3 min: no alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, {{100, platform::watch_arp_frame(kStranger, kOwnAddress, kGateway, true)}});
        expect(count(detector, Kind::SameAddress) == 1, "another device claims this device's address: one alert");
    }
    {
        WatchDetector detector(setup());
        replay(detector, {{100, platform::watch_dhcp_reply_frame(kRouter, kGateway, 2)},
                          {200, platform::watch_dhcp_reply_frame(kStranger, kStrangerAddress, 2)},
                          {300, platform::watch_dhcp_reply_frame(kStranger, kStrangerAddress, 5)}});
        expect(count(detector, Kind::SecondDhcp) == 1, "a second DHCP server: one alert");
    }
}

void check_routers_and_floods() {
    const platform::Ipv6Address router_ll{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
    const platform::Ipv6Address stranger_ll{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x63};
    {
        WatchSetup with_router = setup();
        with_router.ipv6_routers = {router_ll};
        WatchDetector detector(with_router);
        replay(detector, {{100, platform::watch_router_advert_frame(kPhone, router_ll)},
                          {200, platform::watch_router_advert_frame(kRouter, stranger_ll)},
                          {300, platform::watch_router_advert_frame(kStranger, stranger_ll, 64)}});
        expect(count(detector, Kind::Ipv6Router) == 0,
               "a router in use, the IPv4 router, a wrong hop limit: no IPv6 router alert");
        replay(detector, {{400, platform::watch_router_advert_frame(kStranger,
                                                                    platform::Ipv6Address{0xfe, 0x80, 0, 0, 0, 0, 0,
                                                                                          0, 0, 0, 0, 0, 0, 0, 0,
                                                                                          0x64})}});
        expect(count(detector, Kind::Ipv6Router) == 1, "an unknown IPv6 router: one alert");
    }
    for (const int frames : {50, 51}) {
        WatchDetector detector(setup());
        std::vector<platform::WatchFrame> list;
        for (int index = 0; index < frames; ++index) {
            list.push_back({2000 + static_cast<uint32_t>(index) * 10,
                            platform::watch_arp_frame(kStranger, kStrangerAddress, ipv4(10, 0, 0, 100))});
        }
        replay(detector, list);
        expect(count(detector, Kind::ArpFlood) == (frames > 50 ? 1 : 0),
               std::to_string(frames) + " ARP frames in one second: " + (frames > 50 ? "one alert" : "no alert"));
    }
}

void check_learning_and_junk() {
    {
        WatchSetup learning = setup();
        learning.known.clear();
        learning.learn_until_ms = 60000;
        WatchDetector detector(learning);
        replay(detector, {{100, platform::watch_arp_frame(kPhone, kPhoneAddress, kGateway)},
                          {200, platform::watch_arp_frame(kRouter, kGateway, kOwnAddress)},
                          {70000, platform::watch_arp_frame(kStranger, kStrangerAddress, kGateway)},
                          {70100, platform::watch_arp_frame(kPhone, kPhoneAddress, kGateway)}});
        expect(detector.learned().size() == 2, "while learning, devices heard are learned");
        expect(count(detector, Kind::NewDevice) == 1, "after learning, only the newcomer raises New device");
    }
    {
        WatchDetector detector(setup());
        std::vector<platform::WatchFrame> junk;
        std::vector<uint8_t> cut = platform::watch_arp_frame(kStranger, kGateway, kOwnAddress, true);
        cut.resize(20);
        junk.push_back({100, cut});
        std::vector<uint8_t> dhcp = platform::watch_dhcp_reply_frame(kStranger, kStrangerAddress, 2);
        dhcp.resize(dhcp.size() - 10);  // options cut short
        junk.push_back({200, dhcp});
        std::vector<uint8_t> advert = platform::watch_router_advert_frame(
            kStranger, platform::Ipv6Address{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x65});
        advert.resize(60);
        junk.push_back({300, advert});
        std::vector<uint8_t> noise(64);
        for (std::size_t index = 0; index < noise.size(); ++index) {
            noise[index] = static_cast<uint8_t>(index * 37 + 11);
        }
        junk.push_back({400, noise});
        junk.push_back({500, std::vector<uint8_t>(10, 0xff)});
        replay(detector, junk);
        expect(count(detector, Kind::FakeRouter) == 0 && count(detector, Kind::Ipv6Router) == 0,
               "frames cut short raise nothing");
        expect(detector.frames() == 5, "every frame is counted");
    }
}

void check_proc_files() {
    const std::string folder = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
    const std::string arp_path = folder + "/lokictl-watch-arp.txt";
    const std::string route_path = folder + "/lokictl-watch-route.txt";
    {
        std::ofstream arp(arp_path);
        arp << "IP address       HW type     Flags       HW address            Mask     Device\n"
               "10.0.0.1         0x1         0x2         02:00:5E:00:00:0A     *        eth0\n"
               "10.0.0.20        0x1         0x0         00:00:00:00:00:00     *        eth0\n";
        std::ofstream route(route_path);
        route << "00000000000000000000000000000000 00 00000000000000000000000000000000 00 "
                 "fe800000000000000000000000000001 00000400 00000001 00000000 00000003 eth0\n"
                 "fe800000000000000000000000000000 40 00000000000000000000000000000000 00 "
                 "00000000000000000000000000000000 00000100 00000001 00000000 00000001 eth0\n";
    }
    expect(platform::read_neighbour_hardware("eth0", kGateway, arp_path) == "02:00:5e:00:00:0a",
           "the router's hardware address is read from the neighbour table");
    expect(platform::read_neighbour_hardware("eth0", kPhoneAddress, arp_path).empty(),
           "an incomplete entry gives none");
    expect(platform::read_neighbour_hardware("wlan0", kGateway, arp_path).empty(), "another interface gives none");
    const auto routers = platform::read_ipv6_routers("eth0", route_path);
    expect(routers.size() == 1 && routers[0][0] == 0xfe && routers[0][15] == 0x01,
           "the default IPv6 router is read from the routes");
    std::remove(arp_path.c_str());
    std::remove(route_path.c_str());
}

} // namespace

int main() {
    check_pretend_network();
    check_scans();
    check_addresses();
    check_routers_and_floods();
    check_learning_and_junk();
    check_proc_files();
    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all Watch checks passed\n");
    return 0;
}

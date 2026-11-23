/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "leak_test.h"

#include "leak_check.h"
#include "wireguard.h"

#include <string>
#include <vector>

#if !USE_DESKTOP
#include "firewall.h"
#include "speed_test.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>
#endif

namespace platform {
namespace {

// The made-up test: the system's checks after this long, the address's
// answer after this long.
constexpr uint32_t kPretendChecksMs = 300;
constexpr uint32_t kPretendAnswerMs = 900;
// Documentation addresses: the VPN's exit and the network's own.
constexpr const char* kPretendVpnAddress = "198.51.100.7";
constexpr const char* kPretendOwnAddress = "203.0.113.5";
constexpr const char* kPretendTunnelDns = "10.64.0.1";
constexpr const char* kPretendRouterDns = "10.0.0.1";
constexpr const char* kPretendUplink = "wlan0";

} // namespace

model::LeakRun pretend_leak_test(uint32_t elapsed_ms, const model::LeakLocal& local, const std::string& without_vpn,
                                 uint32_t& sent, bool& done) {
    model::LeakRun run;
    run.state = model::ToolState::Running;
    run.vpn_on = local.vpn_on;
    run.gateway = local.gateway;
    run.file = local.file;
    run.address_without_vpn = without_vpn;
    if (elapsed_ms < kPretendChecksMs) {
        return run;
    }
    const bool leaky = local.file == kPretendLeakyFile;
    if (local.vpn_on) {
        model::LeakRoute route;
        route.server = leaky ? kPretendRouterDns : kPretendTunnelDns;
        route.reachable = true;
        route.interface_name = leaky ? kPretendUplink : kVpnInterface;
        model::judge_dns(run, {route}, kVpnInterface);
        model::judge_ipv6(run, true, true, leaky ? kPretendUplink : kVpnInterface, kVpnInterface);
    } else {
        model::judge_vpn_off(run);
    }
    model::judge_kill(run, {}, !leaky);
    sent = 1;  // the address's request
    if (elapsed_ms < kPretendAnswerMs) {
        return run;
    }
    model::judge_address(run, local.vpn_on ? kPretendVpnAddress : kPretendOwnAddress, {});
    if (!local.vpn_on) {
        run.address_without_vpn = run.public_address;
    }
    run.state = model::ToolState::Done;
    done = true;
    return run;
}

#if !USE_DESKTOP
namespace {

constexpr const char* kResolvConf = "/etc/resolv.conf";
// The system's resolver uses the first three nameservers (MAXNS).
constexpr std::size_t kResolverServers = 3;
// A public IPv6 address (Cloudflare's resolver) the IPv6 check finds the
// way to; nothing is ever sent to it.
constexpr const char* kIpv6Probe = "2606:4700:4700::1111";
constexpr uint16_t kDnsPort = 53;
constexpr uint16_t kHttpsPort = 443;

struct Destination {
    sockaddr_storage address{};
    socklen_t length{0};
    bool loopback{false};
};

// An IPv4 or IPv6 address as resolv.conf writes it ("fe80::1%wlan0" with
// its interface), at `port`. False when it is not one.
bool parse_destination(const std::string& text, uint16_t port, Destination& out) {
    std::string host = text;
    std::string scope;
    const std::size_t percent = text.find('%');
    if (percent != std::string::npos) {
        host = text.substr(0, percent);
        scope = text.substr(percent + 1);
    }
    out = Destination{};
    sockaddr_in ipv4{};
    if (scope.empty() && inet_pton(AF_INET, host.c_str(), &ipv4.sin_addr) == 1) {
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(port);
        std::memcpy(&out.address, &ipv4, sizeof(ipv4));
        out.length = sizeof(ipv4);
        out.loopback = (ntohl(ipv4.sin_addr.s_addr) >> 24) == 127;
        return true;
    }
    sockaddr_in6 ipv6{};
    if (inet_pton(AF_INET6, host.c_str(), &ipv6.sin6_addr) != 1) {
        return false;
    }
    ipv6.sin6_family = AF_INET6;
    ipv6.sin6_port = htons(port);
    if (!scope.empty()) {
        ipv6.sin6_scope_id = if_nametoindex(scope.c_str());
        if (ipv6.sin6_scope_id == 0) {
            return false;
        }
    }
    std::memcpy(&out.address, &ipv6, sizeof(ipv6));
    out.length = sizeof(ipv6);
    out.loopback = IN6_IS_ADDR_LOOPBACK(&ipv6.sin6_addr);
    return true;
}

// The nameservers the system's resolver uses, in order.
std::vector<std::string> resolver_servers() {
    std::vector<std::string> servers;
    std::ifstream file(kResolvConf);
    std::string line;
    while (servers.size() < kResolverServers && std::getline(file, line)) {
        std::istringstream words(line);
        std::string keyword;
        std::string value;
        Destination unused;
        if ((words >> keyword >> value) && keyword == "nameserver" && parse_destination(value, kDnsPort, unused)) {
            servers.push_back(value);
        }
    }
    return servers;
}

// The interface that holds the address `local`; empty when none does.
std::string interface_of(const sockaddr_storage& local, const ifaddrs* list) {
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != local.ss_family) {
            continue;
        }
        bool same = false;
        if (local.ss_family == AF_INET) {
            const auto* mine = reinterpret_cast<const sockaddr_in*>(&local);
            const auto* theirs = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            same = mine->sin_addr.s_addr == theirs->sin_addr.s_addr;
        } else if (local.ss_family == AF_INET6) {
            const auto* mine = reinterpret_cast<const sockaddr_in6*>(&local);
            const auto* theirs = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
            same = std::memcmp(&mine->sin6_addr, &theirs->sin6_addr, sizeof(in6_addr)) == 0;
        }
        if (same) {
            return entry->ifa_name != nullptr ? std::string(entry->ifa_name) : std::string();
        }
    }
    return {};
}

// The way to `destination`: a UDP socket connected to it (which sends
// nothing) says whether the system has a route, and the source address it
// picks says which interface the route leaves by.
model::LeakRoute way_to(const Destination& destination, const ifaddrs* list) {
    model::LeakRoute route;
    route.loopback = destination.loopback;
    const int fd = ::socket(destination.address.ss_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return route;
    }
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&destination.address), destination.length) == 0) {
        route.reachable = true;
        sockaddr_storage local{};
        socklen_t length = sizeof(local);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
            route.interface_name = interface_of(local, list);
        }
    }
    ::close(fd);
    return route;
}

// Whether an interface other than `tunnel` and the loopback has a global
// IPv6 address (2000::/3).
bool network_has_ipv6(const ifaddrs* list, const std::string& tunnel) {
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET6 || entry->ifa_name == nullptr ||
            (entry->ifa_flags & IFF_LOOPBACK) != 0 || tunnel == entry->ifa_name) {
            continue;
        }
        const auto* address = reinterpret_cast<const sockaddr_in6*>(entry->ifa_addr);
        if ((address->sin6_addr.s6_addr[0] & 0xe0) == 0x20) {
            return true;
        }
    }
    return false;
}

} // namespace

void run_leak_test(const model::LeakLocal& local,
                   std::string& without_vpn,
                   const std::function<bool()>& stopping,
                   const std::function<void(const model::LeakRun&)>& publish) {
    model::LeakRun run;
    run.state = model::ToolState::Running;
    run.vpn_on = local.vpn_on;
    run.gateway = local.gateway;
    run.file = local.file;
    run.address_without_vpn = without_vpn;
    publish(run);

    if (local.vpn_on) {
        ifaddrs* list = nullptr;
        if (::getifaddrs(&list) != 0) {
            list = nullptr;
        }
        std::vector<model::LeakRoute> routes;
        for (const std::string& server : resolver_servers()) {
            Destination destination;
            if (parse_destination(server, kDnsPort, destination)) {
                model::LeakRoute route = way_to(destination, list);
                route.server = server;
                routes.push_back(route);
            }
        }
        model::judge_dns(run, routes, kVpnInterface);
        const bool has_ipv6 = network_has_ipv6(list, kVpnInterface);
        model::LeakRoute ipv6;
        Destination probe;
        if (has_ipv6 && parse_destination(kIpv6Probe, kHttpsPort, probe)) {
            ipv6 = way_to(probe, list);
        }
        model::judge_ipv6(run, has_ipv6, ipv6.reachable, ipv6.interface_name, kVpnInterface);
        if (list != nullptr) {
            ::freeifaddrs(list);
        }
    } else {
        model::judge_vpn_off(run);
    }
    if (local.gateway) {
        bool present = false;
        const std::string reason = read_kill_switch(present);
        model::judge_kill(run, reason, present);
    } else {
        model::judge_kill(run, {}, false);
    }
    publish(run);
    if (stopping()) {
        run.state = model::ToolState::Stopped;
        publish(run);
        return;
    }

    std::string address;
    const std::string reason = fetch_public_address(stopping, address);
    if (address.empty() && stopping()) {
        run.state = model::ToolState::Stopped;
        publish(run);
        return;
    }
    model::judge_address(run, address, reason);
    if (!local.vpn_on && !address.empty()) {
        without_vpn = address;
        run.address_without_vpn = address;
    }
    run.state = model::ToolState::Done;
    publish(run);
}
#endif

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "port_scan.h"

#include "service_banner.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstddef>
#include <fstream>

namespace platform {
namespace {

// "a.b.c.d" as a number (host byte order); false when it is not one.
bool parse_address(const std::string& text, uint32_t& out) {
    if (!model::is_ipv4_text(text)) {
        return false;
    }
    uint32_t number = 0;
    uint32_t part = 0;
    for (const char character : text) {
        if (character == '.') {
            number = (number << 8) | part;
            part = 0;
        } else {
            part = part * 10 + static_cast<uint32_t>(character - '0');
        }
    }
    out = (number << 8) | part;
    return true;
}

constexpr uint32_t address_of(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

constexpr uint32_t kPretendNetmask = 0xFFFFFF00U;

// The Cardputer's made-up addresses: the sample's uplink, its hotspot, and
// the Wi-Fi example's uplink.
const std::vector<OwnNetwork>& pretend_networks() {
    static const std::vector<OwnNetwork> networks{
        {address_of(10, 0, 0, 12), kPretendNetmask},
        {address_of(10, 42, 0, 1), kPretendNetmask},
        {address_of(10, 20, 30, 44), kPretendNetmask},
    };
    return networks;
}

// The made-up open ports of each kind of device, and the ports that never
// answer.
const std::vector<uint16_t> kRouterOpen{53, 80, 443, 1900};
const std::vector<uint16_t> kRouterSilent{23, 7547};
const std::vector<uint16_t> kOwnOpen{22};
const std::vector<uint16_t> kPrinterOpen{80, 443, 515, 631, 9100};
const std::vector<uint16_t> kMediaOpen{7000, 8008, 8009, 8443};
const std::vector<uint16_t> kStorageOpen{22, 139, 445, 5000, 32400};
const std::vector<uint16_t> kDeviceSilent{3389};

// What the made-up open ports of each kind of device say about themselves;
// an open port not listed says nothing. TLS ports say "TLS" by themselves
// (model::banner_text()). Some versions are old on purpose, so the check
// against the list of known holes has something to show: the router's
// name server and UPnP have holes, the Cardputer's own SSH is a Debian
// build ("check"), the printer's web server has a hole attackers use, the
// storage box has holes and outdated versions, the media player an
// outdated web server.
const std::vector<model::PortBanner> kRouterBanners{
    {53, "dnsmasq-2.85"},
    {80, "lighttpd/1.4.76"},
    {1900, "Linux/6.1 UPnP/1.0 MiniUPnPd/2.3.7"},
};
const std::vector<model::PortBanner> kOwnBanners{
    {22, "OpenSSH_10.0p2 Debian-7"},
};
const std::vector<model::PortBanner> kPrinterBanners{
    {80, "GoAhead-http/3.6.2"},
    {631, "CUPS/2.4 IPP/2.1"},
};
const std::vector<model::PortBanner> kMediaBanners{
    {7000, "MediaRenderer/3.2"},
    {8008, "nginx/1.28.0"},
};
const std::vector<model::PortBanner> kStorageBanners{
    {22, "dropbear_2019.78"},
    {5000, "nginx/1.18.0; PHP/7.4.33"},
};
// A made-up greeting comes this long after its port took the connection.
constexpr uint32_t kPretendBannerMs = 200;

bool contains(const std::vector<uint16_t>& list, uint16_t port) {
    return std::find(list.begin(), list.end(), port) != list.end();
}

// What the made-up `port` says, out of `banners`, and how long after its
// connection it is done reading (model::kBannerReadMs when it says
// nothing).
std::string pretend_banner(const std::vector<model::PortBanner>& banners, uint16_t port, uint32_t& reading_ms) {
    const model::BannerProbe probe = model::banner_probe(port);
    if (probe == model::BannerProbe::Tls) {
        reading_ms = 0;
        return model::banner_text(probe, nullptr, 0);
    }
    for (const model::PortBanner& banner : banners) {
        if (banner.port == port) {
            reading_ms = kPretendBannerMs;
            return banner.text;
        }
    }
    reading_ms = static_cast<uint32_t>(model::kBannerReadMs);
    return {};
}

} // namespace

std::vector<OwnNetwork> read_own_networks() {
    std::vector<OwnNetwork> networks;
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return networks;
    }
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET || entry->ifa_netmask == nullptr ||
            entry->ifa_netmask->sa_family != AF_INET || (entry->ifa_flags & IFF_LOOPBACK) != 0 ||
            (entry->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        OwnNetwork network;
        network.address = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
        network.netmask = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask)->sin_addr.s_addr);
        networks.push_back(network);
    }
    ::freeifaddrs(list);
    return networks;
}

bool port_scan_allowed(uint32_t target, const std::vector<OwnNetwork>& networks, std::string& reason) {
    for (const OwnNetwork& network : networks) {
        if (target == network.address) {
            return true;  // the Cardputer itself
        }
    }
    for (const OwnNetwork& network : networks) {
        if (network.netmask == 0 || (target & network.netmask) != (network.address & network.netmask)) {
            continue;
        }
        const uint32_t host = target & ~network.netmask;
        // A /31 or /32 has no network or broadcast address.
        if (~network.netmask > 1 && (host == 0 || host == ~network.netmask)) {
            reason = "a network or broadcast address";
            return false;
        }
        return true;
    }
    reason = "not on the Cardputer's networks";
    return false;
}

model::PortRun pretend_port_scan(uint32_t elapsed_ms, const std::string& target, uint32_t& sent, bool& done) {
    model::PortRun run;
    run.state = model::ToolState::Running;
    run.target = target;
    const std::vector<model::CommonPort>& ports = model::common_ports();
    run.total = static_cast<int32_t>(ports.size());
    uint32_t address = 0;
    if (!parse_address(target, address) || !port_scan_allowed(address, pretend_networks(), run.reason)) {
        run.state = model::ToolState::Failed;
        run.error = model::ToolError::Failed;
        if (run.reason.empty()) {
            run.reason = "not a valid address";
        }
        done = true;
        return run;
    }

    const std::vector<uint16_t>* open = &kRouterOpen;
    const std::vector<uint16_t>* silent = &kDeviceSilent;
    const std::vector<model::PortBanner>* banners = &kRouterBanners;
    const bool own = std::any_of(pretend_networks().begin(), pretend_networks().end(),
                                 [address](const OwnNetwork& network) { return network.address == address; });
    if (own) {
        open = &kOwnOpen;
        banners = &kOwnBanners;
    } else if ((address & 0xFFU) == 1) {
        silent = &kRouterSilent;
    } else {
        switch ((address & 0xFFU) % 3) {
            case 0:
                open = &kPrinterOpen;
                banners = &kPrinterBanners;
                break;
            case 1:
                open = &kMediaOpen;
                banners = &kMediaBanners;
                break;
            default:
                open = &kStorageOpen;
                banners = &kStorageBanners;
                break;
        }
    }

    uint32_t end = 0;
    for (std::size_t index = 0; index < ports.size(); ++index) {
        const uint16_t port = ports[index].port;
        const uint32_t sent_at = static_cast<uint32_t>(index) * static_cast<uint32_t>(model::kPortIntervalMs);
        const bool quiet = contains(*silent, port);
        const bool is_open = !quiet && contains(*open, port);
        const uint32_t settled_at = sent_at + (quiet ? static_cast<uint32_t>(model::kPortTimeoutMs) : 2u);
        uint32_t reading_ms = 0;
        const std::string banner = is_open ? pretend_banner(*banners, port, reading_ms) : std::string();
        end = std::max(end, settled_at + reading_ms);
        if (elapsed_ms < sent_at) {
            continue;
        }
        ++sent;
        ++run.asked;
        if (elapsed_ms < settled_at) {
            continue;
        }
        if (quiet) {
            ++run.silent;
        } else if (is_open) {
            run.open.push_back(port);
            const model::BannerProbe probe = model::banner_probe(port);
            if (probe == model::BannerProbe::Http || probe == model::BannerProbe::Dns) {
                ++sent;  // the request for its headers, or the version question
            }
            if (!banner.empty() && elapsed_ms >= settled_at + reading_ms) {
                run.banners.push_back({port, banner});
            }
        } else {
            ++run.closed;
        }
    }
    if (elapsed_ms >= end) {
        run.state = model::ToolState::Done;
        done = true;
    }
    return run;
}

void check_port_services(model::PortRun& run, const std::string& list_path) {
    run.list_date.clear();
    run.verdicts.clear();
    run.findings.clear();
    if (list_path.empty()) {
        return;
    }
    std::ifstream list(list_path);
    if (!list) {
        return;
    }
    model::VulnCheck check = model::check_services(list, run);
    run.list_date = std::move(check.date);
    run.verdicts = std::move(check.verdicts);
    run.findings = std::move(check.findings);
}

std::string read_list_date(const std::string& list_path) {
    if (list_path.empty()) {
        return {};
    }
    std::ifstream list(list_path);
    return list ? model::vuln_list_date(list) : std::string();
}

} // namespace platform

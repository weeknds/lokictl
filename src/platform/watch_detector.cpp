/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "watch.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

namespace platform {
namespace {

constexpr std::size_t kEthernetHeader = 14;
constexpr uint16_t kTypeArp = 0x0806;
constexpr uint16_t kTypeIpv4 = 0x0800;
constexpr uint16_t kTypeIpv6 = 0x86dd;
constexpr std::size_t kArpSize = 28;  // Ethernet over IPv4
constexpr uint8_t kProtocolTcp = 6;
constexpr uint8_t kProtocolUdp = 17;
constexpr uint8_t kProtocolIcmpv6 = 58;
constexpr uint8_t kTcpSyn = 0x02;
constexpr uint8_t kTcpAck = 0x10;
constexpr std::size_t kIpv6Header = 40;
constexpr uint8_t kRouterAdvert = 134;
constexpr std::size_t kRouterAdvertSize = 16;
constexpr uint8_t kRouterHopLimit = 255;  // hosts drop announcements with any other
constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDhcpClientPort = 68;
constexpr std::size_t kBootpFixed = 236;
constexpr uint32_t kDhcpCookie = 0x63825363u;
constexpr uint8_t kBootRequest = 1;
constexpr uint8_t kBootReply = 2;
constexpr uint8_t kOptionPad = 0;
constexpr uint8_t kOptionRequestedAddress = 50;
constexpr uint8_t kOptionMessageType = 53;
constexpr uint8_t kOptionServer = 54;
constexpr uint8_t kOptionEnd = 255;
constexpr uint8_t kDhcpOffer = 2;
constexpr uint8_t kDhcpRequest = 3;
constexpr uint8_t kDhcpAck = 5;
constexpr uint8_t kDhcpNak = 6;

// Every table has a fixed size; the oldest entry makes room.
constexpr std::size_t kMaxSeen = 256;         // devices a New device alert was raised for
constexpr std::size_t kMaxAddresses = 256;    // addresses and who claimed them last
constexpr std::size_t kMaxReported = 64;      // each alert's "already said" list
constexpr std::size_t kMaxScanSources = 16;
constexpr std::size_t kMaxRecentPorts = 64;   // a source's ports within the scan window
constexpr std::size_t kMaxTriedPorts = 1024;  // a source's ports in one episode
constexpr std::size_t kMaxIpv6Routers = 16;

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
}

uint32_t read32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

// Not a group address and not all zeros.
bool unicast_hardware(const uint8_t* hardware) {
    if ((hardware[0] & 1U) != 0) {
        return false;
    }
    for (int index = 0; index < 6; ++index) {
        if (hardware[index] != 0) {
            return true;
        }
    }
    return false;
}

// An IPv4 address one device can have.
bool device_address(uint32_t address) {
    const uint32_t first = address >> 24;
    return address != 0 && first < 224 && first != 127;
}

std::string hardware_text(const uint8_t* hardware) {
    char text[18];
    std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x", hardware[0], hardware[1], hardware[2],
                  hardware[3], hardware[4], hardware[5]);
    return text;
}

std::string address_text(uint32_t address) {
    return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xffU) + "." +
           std::to_string((address >> 8) & 0xffU) + "." + std::to_string(address & 0xffU);
}

std::string ipv6_text(const uint8_t* address) {
    char text[INET6_ADDRSTRLEN] = {};
    if (!inet_ntop(AF_INET6, address, text, sizeof(text))) {
        return {};
    }
    return text;
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

// "02:4c:4b:00:00:01" (either case) into six bytes.
bool parse_hardware(const std::string& text, HardwareAddress& out) {
    if (text.size() != 17) {
        return false;
    }
    for (std::size_t index = 0; index < 6; ++index) {
        const int high = hex_value(text[index * 3]);
        const int low = hex_value(text[index * 3 + 1]);
        if (high < 0 || low < 0 || (index < 5 && text[index * 3 + 2] != ':')) {
            return false;
        }
        out[index] = static_cast<uint8_t>(high * 16 + low);
    }
    return true;
}

std::string lowercase(std::string text) {
    for (char& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

template <typename T>
bool contains(const std::vector<T>& list, const T& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// Adds `value`, dropping the oldest entry when the list is full.
template <typename T>
void remember(std::vector<T>& list, const T& value, std::size_t max) {
    if (list.size() >= max) {
        list.erase(list.begin());
    }
    list.push_back(value);
}

} // namespace

// ---------------------------------------------------------------------------
// WatchDetector

WatchDetector::WatchDetector(WatchSetup setup) : setup_(std::move(setup)) {
    for (const OwnPort& port : setup_.own_ports) {
        HardwareAddress hardware{};
        if (parse_hardware(port.hardware_address, hardware)) {
            own_hardware_.push_back(hardware);
        }
    }
    for (const std::string& text : setup_.known) {
        const std::string address = lowercase(text);
        if (known_.size() < model::kWatchKnownMax && !contains(known_, address)) {
            known_.push_back(address);
        }
    }
    HardwareAddress gateway{};
    if (parse_hardware(setup_.gateway_hardware, gateway) && unicast_hardware(gateway.data())) {
        gateway_hardware_ = hardware_text(gateway.data());
    }
    for (const Ipv6Address& router : setup_.ipv6_routers) {
        if (ipv6_routers_.size() < kMaxIpv6Routers) {
            ipv6_routers_.push_back(router);
        }
    }
}

bool WatchDetector::take_changed() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

bool WatchDetector::own(const uint8_t* hardware) const {
    for (const HardwareAddress& own : own_hardware_) {
        if (std::memcmp(own.data(), hardware, own.size()) == 0) {
            return true;
        }
    }
    return false;
}

bool WatchDetector::known(const std::string& hardware) const {
    return contains(known_, hardware);
}

uint32_t WatchDetector::raise(model::WatchAlert alert, int64_t at_ms) {
    const uint32_t id = next_id_++;
    if (setup_.clock_text) {
        alert.time = setup_.clock_text(at_ms);
    }
    alerts_.insert(alerts_.begin(), std::move(alert));
    alert_ids_.insert(alert_ids_.begin(), id);
    if (alerts_.size() > model::kWatchMaxAlerts) {
        alerts_.pop_back();
        alert_ids_.pop_back();
    }
    ++raised_;
    changed_ = true;
    return id;
}

model::WatchAlert* WatchDetector::find(uint32_t id) {
    for (std::size_t index = 0; index < alert_ids_.size(); ++index) {
        if (alert_ids_[index] == id) {
            return &alerts_[index];
        }
    }
    return nullptr;
}

void WatchDetector::note_frame(const uint8_t* frame, std::size_t size, int64_t at_ms) {
    ++frames_;
    changed_ = true;
    if (size < kEthernetHeader) {
        return;
    }
    switch (read16(frame + 12)) {
        case kTypeArp:
            note_arp(frame, size, at_ms);
            break;
        case kTypeIpv4:
            note_ipv4(frame, size, at_ms);
            break;
        case kTypeIpv6:
            note_ipv6(frame, size, at_ms);
            break;
        default:
            break;
    }
}

void WatchDetector::note_device(const uint8_t* hardware, uint32_t address, int64_t at_ms) {
    if (!unicast_hardware(hardware) || own(hardware)) {
        return;
    }
    const std::string text = hardware_text(hardware);
    if (known(text)) {
        return;
    }
    if (contains(new_seen_, text)) {
        // Already reported: its alert gets the address when it had none.
        if (device_address(address)) {
            for (model::WatchAlert& alert : alerts_) {
                if (alert.kind == model::WatchAlertKind::NewDevice && alert.hardware == text) {
                    if (alert.address.empty()) {
                        alert.address = address_text(address);
                        changed_ = true;
                    }
                    break;
                }
            }
        }
        return;
    }
    remember(new_seen_, text, kMaxSeen);
    if (setup_.learn_until_ms > 0 && at_ms < setup_.learn_until_ms) {
        // Learning the network: known from now on, and saved.
        if (known_.size() < model::kWatchKnownMax) {
            known_.push_back(text);
            learned_.push_back(text);
            changed_ = true;
        }
        return;
    }
    model::WatchAlert alert;
    alert.kind = model::WatchAlertKind::NewDevice;
    alert.hardware = text;
    if (device_address(address)) {
        alert.address = address_text(address);
    }
    raise(std::move(alert), at_ms);
}

void WatchDetector::note_arp(const uint8_t* frame, std::size_t size, int64_t at_ms) {
    const uint8_t* arp = frame + kEthernetHeader;
    // Hardware type Ethernet, protocol IPv4, 6- and 4-byte addresses, a
    // request or a reply.
    if (size - kEthernetHeader < kArpSize || read16(arp) != 1 || read16(arp + 2) != kTypeIpv4 || arp[4] != 6 ||
        arp[5] != 4) {
        return;
    }
    const uint16_t operation = read16(arp + 6);
    if (operation != 1 && operation != 2) {
        return;
    }
    const uint8_t* sender = arp + 8;
    const uint32_t sender_address = read32(arp + 14);
    if (own(sender)) {
        return;
    }

    // The flood: ARP frames within one second, from anyone but this device.
    const int64_t second = at_ms / 1000;
    if (second != flood_second_) {
        flood_second_ = second;
        flood_count_ = 0;
    }
    ++flood_count_;
    if (flood_alerted_ && at_ms - flood_last_ms_ > static_cast<int64_t>(model::kWatchQuietS) * 1000) {
        flood_alerted_ = false;
    }
    if (flood_count_ > model::kWatchArpFloodPerSecond) {
        flood_last_ms_ = at_ms;
        if (!flood_alerted_) {
            flood_alerted_ = true;
            model::WatchAlert alert;
            alert.kind = model::WatchAlertKind::ArpFlood;
            if (unicast_hardware(sender)) {
                alert.hardware = hardware_text(sender);
            }
            if (device_address(sender_address)) {
                alert.address = address_text(sender_address);
            }
            alert.count = flood_count_;
            flood_alert_id_ = raise(std::move(alert), at_ms);
        } else if (model::WatchAlert* alert = find(flood_alert_id_); alert && flood_count_ > alert->count) {
            alert->count = flood_count_;
            changed_ = true;
        }
    }

    if (!unicast_hardware(sender)) {
        return;
    }
    note_device(sender, sender_address, at_ms);
    if (!device_address(sender_address)) {
        return;  // a probe: it claims no address
    }
    const std::string text = hardware_text(sender);

    if (setup_.gateway != 0 && sender_address == setup_.gateway) {
        if (gateway_hardware_.empty()) {
            gateway_hardware_ = text;  // the first one heard is the router
            return;
        }
        if (text == gateway_hardware_ || contains(fake_routers_, text)) {
            return;
        }
        remember(fake_routers_, text, kMaxReported);
        model::WatchAlert alert;
        alert.kind = model::WatchAlertKind::FakeRouter;
        alert.address = address_text(sender_address);
        alert.hardware = text;
        alert.before = gateway_hardware_;
        raise(std::move(alert), at_ms);
        return;
    }

    if (setup_.own_address != 0 && sender_address == setup_.own_address) {
        // Someone else claims this device's own address.
        if (!contains(same_reported_, sender_address)) {
            remember(same_reported_, sender_address, kMaxReported);
            model::WatchAlert alert;
            alert.kind = model::WatchAlertKind::SameAddress;
            alert.address = address_text(sender_address);
            alert.hardware = text;
            alert.before = "this device";
            raise(std::move(alert), at_ms);
        }
        return;
    }

    for (SameEntry& entry : same_) {
        if (entry.address != sender_address) {
            continue;
        }
        if (std::memcmp(entry.hardware.data(), sender, entry.hardware.size()) != 0) {
            const bool recent =
                at_ms - entry.at_ms <= static_cast<int64_t>(model::kWatchSameAddressWindowS) * 1000;
            if (recent && !contains(same_reported_, sender_address)) {
                remember(same_reported_, sender_address, kMaxReported);
                model::WatchAlert alert;
                alert.kind = model::WatchAlertKind::SameAddress;
                alert.address = address_text(sender_address);
                alert.hardware = text;
                alert.before = hardware_text(entry.hardware.data());
                raise(std::move(alert), at_ms);
            }
            std::memcpy(entry.hardware.data(), sender, entry.hardware.size());
        }
        entry.at_ms = at_ms;
        return;
    }
    if (same_.size() >= kMaxAddresses) {
        same_.erase(std::min_element(same_.begin(), same_.end(), [](const SameEntry& left, const SameEntry& right) {
            return left.at_ms < right.at_ms;
        }));
    }
    SameEntry entry;
    entry.address = sender_address;
    std::memcpy(entry.hardware.data(), sender, entry.hardware.size());
    entry.at_ms = at_ms;
    same_.push_back(entry);
}

void WatchDetector::note_ipv4(const uint8_t* frame, std::size_t size, int64_t at_ms) {
    const uint8_t* packet = frame + kEthernetHeader;
    std::size_t length = size - kEthernetHeader;
    if (length < 20 || (packet[0] >> 4) != 4) {
        return;
    }
    const std::size_t header = static_cast<std::size_t>(packet[0] & 0x0fU) * 4;
    const std::size_t total = read16(packet + 2);
    if (header < 20 || header > length || total < header) {
        return;
    }
    length = std::min(length, total);  // a short frame is padded
    if ((read16(packet + 6) & 0x3fffU) != 0) {
        return;  // a fragment
    }
    const uint32_t source = read32(packet + 12);
    const uint32_t destination = read32(packet + 16);
    const uint8_t* body = packet + header;
    const std::size_t room = length - header;
    if (packet[9] == kProtocolUdp) {
        if (room < 8) {
            return;
        }
        const uint16_t source_port = read16(body);
        const uint16_t destination_port = read16(body + 2);
        const std::size_t udp_length = read16(body + 4);
        if (udp_length < 8) {
            return;
        }
        const auto dhcp_port = [](uint16_t port) { return port == kDhcpServerPort || port == kDhcpClientPort; };
        if (dhcp_port(source_port) && dhcp_port(destination_port)) {
            note_dhcp(body + 8, std::min(udp_length, room) - 8, frame + 6, source, at_ms);
        }
    } else if (packet[9] == kProtocolTcp) {
        if (room < 14) {
            return;
        }
        const uint8_t flags = body[13];
        if ((flags & kTcpSyn) != 0 && (flags & kTcpAck) == 0) {
            note_syn(frame + 6, source, destination, read16(body + 2), at_ms);
        }
    }
}

void WatchDetector::note_dhcp(const uint8_t* bootp,
                              std::size_t size,
                              const uint8_t* sender,
                              uint32_t source,
                              int64_t at_ms) {
    if (size < kBootpFixed + 4 || read32(bootp + kBootpFixed) != kDhcpCookie) {
        return;
    }
    uint8_t message = 0;
    uint32_t server = 0;
    uint32_t requested = 0;
    std::size_t at = kBootpFixed + 4;
    while (at < size) {
        const uint8_t code = bootp[at];
        if (code == kOptionPad) {
            ++at;
            continue;
        }
        if (code == kOptionEnd || at + 1 >= size) {
            break;
        }
        const std::size_t length = bootp[at + 1];
        if (at + 2 + length > size) {
            break;  // runs past the packet: read no further
        }
        const uint8_t* value = bootp + at + 2;
        if (code == kOptionMessageType && length == 1) {
            message = value[0];
        } else if (code == kOptionServer && length == 4) {
            server = read32(value);
        } else if (code == kOptionRequestedAddress && length == 4) {
            requested = read32(value);
        }
        at += 2 + length;
    }

    if (bootp[0] == kBootRequest) {
        // A client: Ethernet, 6-byte address.
        if (bootp[1] == 1 && bootp[2] == 6) {
            const uint32_t client_address = read32(bootp + 12);
            note_device(bootp + 28, device_address(client_address) ? client_address : requested, at_ms);
        }
        return;
    }
    if (bootp[0] != kBootReply || (message != kDhcpOffer && message != kDhcpAck && message != kDhcpNak) ||
        own(sender)) {
        return;
    }
    if (server == 0) {
        server = source;
    }
    if (!device_address(server)) {
        return;
    }
    if (dhcp_server_ == 0) {
        dhcp_server_ = server;  // the first one heard
        return;
    }
    if (server == dhcp_server_ || contains(dhcp_reported_, server)) {
        return;
    }
    remember(dhcp_reported_, server, kMaxReported);
    model::WatchAlert alert;
    alert.kind = model::WatchAlertKind::SecondDhcp;
    alert.address = address_text(server);
    if (unicast_hardware(sender)) {
        alert.hardware = hardware_text(sender);
    }
    alert.before = address_text(dhcp_server_);
    raise(std::move(alert), at_ms);
}

void WatchDetector::note_syn(const uint8_t* sender,
                             uint32_t source,
                             uint32_t destination,
                             uint16_t port,
                             int64_t at_ms) {
    if (own(sender) || !device_address(source)) {
        return;
    }
    if (setup_.own_address != 0 && destination != setup_.own_address) {
        return;
    }
    const int64_t quiet_ms = static_cast<int64_t>(model::kWatchQuietS) * 1000;
    const int64_t window_ms = static_cast<int64_t>(model::kWatchScanWindowS) * 1000;

    auto found = std::find_if(scans_.begin(), scans_.end(),
                              [source](const ScanSource& entry) { return entry.address == source; });
    if (found == scans_.end()) {
        if (scans_.size() >= kMaxScanSources) {
            scans_.erase(std::min_element(scans_.begin(), scans_.end(),
                                          [](const ScanSource& left, const ScanSource& right) {
                                              return left.last_ms < right.last_ms;
                                          }));
        }
        ScanSource entry;
        entry.address = source;
        entry.last_ms = at_ms;
        scans_.push_back(entry);
        found = scans_.end() - 1;
    }
    ScanSource& scan = *found;
    if (at_ms - scan.last_ms > quiet_ms) {
        // Quiet long enough: a new episode, which may raise a new alert.
        scan.alerted = false;
        scan.tried.clear();
        scan.recent.clear();
    }
    scan.last_ms = at_ms;

    scan.recent.erase(std::remove_if(scan.recent.begin(), scan.recent.end(),
                                     [at_ms, window_ms](const std::pair<uint16_t, int64_t>& entry) {
                                         return at_ms - entry.second > window_ms;
                                     }),
                      scan.recent.end());
    auto recent = std::find_if(scan.recent.begin(), scan.recent.end(),
                               [port](const std::pair<uint16_t, int64_t>& entry) { return entry.first == port; });
    if (recent != scan.recent.end()) {
        recent->second = at_ms;
    } else {
        remember(scan.recent, std::make_pair(port, at_ms), kMaxRecentPorts);
    }
    const auto tried = std::lower_bound(scan.tried.begin(), scan.tried.end(), port);
    if ((tried == scan.tried.end() || *tried != port) && scan.tried.size() < kMaxTriedPorts) {
        scan.tried.insert(tried, port);
    }

    const auto count = static_cast<int32_t>(scan.tried.size());
    if (!scan.alerted && scan.recent.size() >= static_cast<std::size_t>(model::kWatchScanPorts)) {
        scan.alerted = true;
        model::WatchAlert alert;
        alert.kind = model::WatchAlertKind::Scan;
        alert.address = address_text(source);
        if (unicast_hardware(sender)) {
            alert.hardware = hardware_text(sender);
        }
        alert.count = count;
        scan.alert_id = raise(std::move(alert), at_ms);
    } else if (scan.alerted) {
        if (model::WatchAlert* alert = find(scan.alert_id); alert && alert->count != count) {
            alert->count = count;
            changed_ = true;
        }
    }
}

void WatchDetector::note_ipv6(const uint8_t* frame, std::size_t size, int64_t at_ms) {
    if (size < kEthernetHeader + kIpv6Header + kRouterAdvertSize) {
        return;
    }
    const uint8_t* packet = frame + kEthernetHeader;
    if ((packet[0] >> 4) != 6 || packet[6] != kProtocolIcmpv6 || packet[7] != kRouterHopLimit ||
        read16(packet + 4) < kRouterAdvertSize) {
        return;
    }
    const uint8_t* icmp = packet + kIpv6Header;
    if (icmp[0] != kRouterAdvert || icmp[1] != 0) {
        return;
    }
    const uint8_t* sender = frame + 6;
    if (own(sender)) {
        return;
    }
    Ipv6Address source{};
    std::memcpy(source.data(), packet + 8, source.size());
    if (contains(ipv6_routers_, source)) {
        return;
    }
    remember(ipv6_routers_, source, kMaxIpv6Routers);
    const std::string text = unicast_hardware(sender) ? hardware_text(sender) : std::string();
    if (!text.empty() && text == gateway_hardware_) {
        return;  // the IPv4 router itself
    }
    model::WatchAlert alert;
    alert.kind = model::WatchAlertKind::Ipv6Router;
    alert.address = ipv6_text(source.data());
    alert.hardware = text;
    raise(std::move(alert), at_ms);
}

// ---------------------------------------------------------------------------
// Reading /proc

std::string read_neighbour_hardware(const std::string& interface_name, uint32_t address, const std::string& path) {
    std::ifstream file(path);
    if (!file || address == 0) {
        return {};
    }
    const std::string wanted = address_text(address);
    std::string line;
    std::getline(file, line);  // the heading
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string ip;
        std::string type;
        std::string flags;
        std::string hardware;
        std::string mask;
        std::string device;
        if (!(fields >> ip >> type >> flags >> hardware >> mask >> device)) {
            continue;
        }
        HardwareAddress bytes{};
        const unsigned long flag_bits = std::strtoul(flags.c_str(), nullptr, 16);
        if (ip == wanted && device == interface_name && (flag_bits & 0x2UL) != 0 &&
            parse_hardware(hardware, bytes) && unicast_hardware(bytes.data())) {
            return hardware_text(bytes.data());
        }
    }
    return {};
}

std::vector<Ipv6Address> read_ipv6_routers(const std::string& interface_name, const std::string& path) {
    std::vector<Ipv6Address> routers;
    std::ifstream file(path);
    std::string line;
    while (file && std::getline(file, line) && routers.size() < 8) {
        std::istringstream fields(line);
        std::string destination;
        std::string destination_length;
        std::string source;
        std::string source_length;
        std::string next_hop;
        std::string metric;
        std::string references;
        std::string use;
        std::string flags;
        std::string device;
        if (!(fields >> destination >> destination_length >> source >> source_length >> next_hop >> metric >>
              references >> use >> flags >> device)) {
            continue;
        }
        if (device != interface_name || destination != std::string(32, '0') || destination_length != "00" ||
            next_hop.size() != 32 || next_hop == std::string(32, '0')) {
            continue;
        }
        Ipv6Address router{};
        bool ok = true;
        for (std::size_t index = 0; index < router.size() && ok; ++index) {
            const int high = hex_value(next_hop[index * 2]);
            const int low = hex_value(next_hop[index * 2 + 1]);
            ok = high >= 0 && low >= 0;
            router[index] = static_cast<uint8_t>(ok ? high * 16 + low : 0);
        }
        if (ok && !contains(routers, router)) {
            routers.push_back(router);
        }
    }
    return routers;
}

uint32_t parse_ipv4(const std::string& text) {
    in_addr address{};
    if (text.empty() || inet_pton(AF_INET, text.c_str(), &address) != 1) {
        return 0;
    }
    return ntohl(address.s_addr);
}

// ---------------------------------------------------------------------------
// Made-up frames

namespace {

constexpr HardwareAddress kBroadcast{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr HardwareAddress kAllNodes{0x33, 0x33, 0x00, 0x00, 0x00, 0x01};

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xffU));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value >> 16));
    put16(out, static_cast<uint16_t>(value & 0xffffU));
}

void put_hardware(std::vector<uint8_t>& out, const HardwareAddress& hardware) {
    out.insert(out.end(), hardware.begin(), hardware.end());
}

std::vector<uint8_t> ethernet(const HardwareAddress& to, const HardwareAddress& from, uint16_t type) {
    std::vector<uint8_t> frame;
    put_hardware(frame, to);
    put_hardware(frame, from);
    put16(frame, type);
    return frame;
}

void put_ipv4_header(std::vector<uint8_t>& frame,
                     uint8_t protocol,
                     uint16_t payload,
                     uint32_t source,
                     uint32_t destination) {
    frame.push_back(0x45);
    frame.push_back(0);
    put16(frame, static_cast<uint16_t>(20 + payload));
    put16(frame, 0);       // id
    put16(frame, 0x4000);  // don't fragment
    frame.push_back(64);
    frame.push_back(protocol);
    put16(frame, 0);  // checksum (not checked)
    put32(frame, source);
    put32(frame, destination);
}

std::vector<uint8_t> dhcp_frame(const HardwareAddress& sender,
                                bool reply,
                                const HardwareAddress& client,
                                uint32_t source,
                                const std::vector<uint8_t>& options) {
    std::vector<uint8_t> bootp;
    bootp.push_back(reply ? kBootReply : kBootRequest);
    bootp.push_back(1);
    bootp.push_back(6);
    bootp.push_back(0);
    put32(bootp, 0x5eed0000u | client[5]);  // xid
    put16(bootp, 0);
    put16(bootp, 0x8000);  // broadcast
    bootp.resize(28, 0);   // ciaddr, yiaddr, siaddr, giaddr
    put_hardware(bootp, client);
    bootp.resize(kBootpFixed, 0);
    put32(bootp, kDhcpCookie);
    bootp.insert(bootp.end(), options.begin(), options.end());
    bootp.push_back(kOptionEnd);

    std::vector<uint8_t> frame = ethernet(kBroadcast, sender, kTypeIpv4);
    const auto udp_length = static_cast<uint16_t>(8 + bootp.size());
    put_ipv4_header(frame, kProtocolUdp, udp_length, source, 0xffffffffu);
    put16(frame, reply ? kDhcpServerPort : kDhcpClientPort);
    put16(frame, reply ? kDhcpClientPort : kDhcpServerPort);
    put16(frame, udp_length);
    put16(frame, 0);
    frame.insert(frame.end(), bootp.begin(), bootp.end());
    return frame;
}

} // namespace

std::vector<uint8_t> watch_arp_frame(const HardwareAddress& sender,
                                     uint32_t sender_address,
                                     uint32_t target_address,
                                     bool reply) {
    std::vector<uint8_t> frame = ethernet(kBroadcast, sender, kTypeArp);
    put16(frame, 1);
    put16(frame, kTypeIpv4);
    frame.push_back(6);
    frame.push_back(4);
    put16(frame, reply ? 2 : 1);
    put_hardware(frame, sender);
    put32(frame, sender_address);
    put_hardware(frame, HardwareAddress{});
    put32(frame, target_address);
    frame.resize(60, 0);  // padded to Ethernet's shortest frame
    return frame;
}

std::vector<uint8_t> watch_syn_frame(const HardwareAddress& sender,
                                     const HardwareAddress& to,
                                     uint32_t source,
                                     uint32_t destination,
                                     uint16_t port,
                                     bool ack) {
    std::vector<uint8_t> frame = ethernet(to, sender, kTypeIpv4);
    put_ipv4_header(frame, kProtocolTcp, 20, source, destination);
    put16(frame, 51000);                     // source port
    put16(frame, port);                      // destination port
    put32(frame, 0x10000000u + port);        // sequence
    put32(frame, ack ? 0x20000000u : 0u);    // acknowledgement
    frame.push_back(0x50);                   // header length 20
    frame.push_back(ack ? kTcpSyn | kTcpAck : kTcpSyn);
    put16(frame, 64240);                     // window
    put16(frame, 0);                         // checksum (not checked)
    put16(frame, 0);                         // urgent pointer
    frame.resize(60, 0);
    return frame;
}

std::vector<uint8_t> watch_dhcp_reply_frame(const HardwareAddress& sender, uint32_t server, uint8_t message) {
    const std::vector<uint8_t> options{kOptionMessageType,
                                       1,
                                       message,
                                       kOptionServer,
                                       4,
                                       static_cast<uint8_t>(server >> 24),
                                       static_cast<uint8_t>(server >> 16),
                                       static_cast<uint8_t>(server >> 8),
                                       static_cast<uint8_t>(server)};
    const HardwareAddress client{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x99};
    return dhcp_frame(sender, true, client, server, options);
}

std::vector<uint8_t> watch_dhcp_request_frame(const HardwareAddress& client, uint32_t requested) {
    const std::vector<uint8_t> options{kOptionMessageType,
                                       1,
                                       kDhcpRequest,
                                       kOptionRequestedAddress,
                                       4,
                                       static_cast<uint8_t>(requested >> 24),
                                       static_cast<uint8_t>(requested >> 16),
                                       static_cast<uint8_t>(requested >> 8),
                                       static_cast<uint8_t>(requested)};
    return dhcp_frame(client, false, client, 0, options);
}

std::vector<uint8_t> watch_router_advert_frame(const HardwareAddress& sender,
                                               const Ipv6Address& source,
                                               uint8_t hop_limit) {
    std::vector<uint8_t> frame = ethernet(kAllNodes, sender, kTypeIpv6);
    put32(frame, 0x60000000u);
    put16(frame, static_cast<uint16_t>(kRouterAdvertSize));
    frame.push_back(kProtocolIcmpv6);
    frame.push_back(hop_limit);
    frame.insert(frame.end(), source.begin(), source.end());
    const Ipv6Address all_nodes{0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
    frame.insert(frame.end(), all_nodes.begin(), all_nodes.end());
    frame.push_back(kRouterAdvert);
    frame.push_back(0);  // code
    put16(frame, 0);     // checksum (not checked)
    frame.push_back(64); // hop limit it advises
    frame.push_back(0);  // flags
    put16(frame, 1800);  // router lifetime
    put32(frame, 0);     // reachable time
    put32(frame, 0);     // retransmit timer
    return frame;
}

// ---------------------------------------------------------------------------
// The pretend network

namespace {

constexpr uint32_t ipv4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

constexpr HardwareAddress kPretendCable{0x02, 0x4c, 0x4b, 0x00, 0x00, 0x01};  // listen.cpp's own ports
constexpr HardwareAddress kPretendRouter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x01};
constexpr HardwareAddress kPretendPrinter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x24};
constexpr HardwareAddress kPretendPhone{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x42};
constexpr HardwareAddress kPretendSpeaker{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x57};
constexpr HardwareAddress kPretendVisitor{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x77};
constexpr HardwareAddress kPretendIntruder{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x66};
constexpr uint32_t kPretendOwnAddress = ipv4(10, 0, 0, 12);
constexpr uint32_t kPretendGateway = ipv4(10, 0, 0, 1);
constexpr uint32_t kPretendIntruderAddress = ipv4(10, 0, 0, 66);
// The made-up clock starts at 07:46, like the sample data's.
constexpr int64_t kPretendClockStartMinutes = 7 * 60 + 46;
// The router repeats its ARP question this often.
constexpr uint32_t kPretendRouterPeriodMs = 10000;

Ipv6Address link_local(const HardwareAddress& hardware) {
    // fe80:: with the hardware address inside (EUI-64).
    return Ipv6Address{0xfe, 0x80, 0, 0, 0, 0, 0, 0, static_cast<uint8_t>(hardware[0] ^ 0x02), hardware[1],
                       hardware[2], 0xff, 0xfe, hardware[3], hardware[4], hardware[5]};
}

std::string pretend_clock_text(int64_t at_ms) {
    const int64_t minutes = (kPretendClockStartMinutes + at_ms / 60000) % (24 * 60);
    char text[8];
    std::snprintf(text, sizeof(text), "%02d:%02d", static_cast<int>(minutes / 60), static_cast<int>(minutes % 60));
    return text;
}

} // namespace

const std::vector<WatchFrame>& pretend_watch_frames() {
    static const std::vector<WatchFrame> frames = [] {
        std::vector<WatchFrame> list;
        // Known devices go about their business.
        list.push_back({300, watch_arp_frame(kPretendRouter, kPretendGateway, kPretendOwnAddress)});
        list.push_back({600, watch_arp_frame(kPretendPhone, ipv4(10, 0, 0, 42), kPretendGateway)});
        list.push_back({900, watch_arp_frame(kPretendPrinter, ipv4(10, 0, 0, 24), kPretendGateway)});
        list.push_back({1500, watch_arp_frame(kPretendSpeaker, ipv4(10, 0, 0, 57), kPretendGateway)});
        list.push_back({2000, watch_dhcp_reply_frame(kPretendRouter, kPretendGateway, kDhcpAck)});
        // This device asks for the router: never an alert.
        list.push_back({2200, watch_arp_frame(kPretendCable, kPretendOwnAddress, kPretendGateway)});
        // A visitor's phone asks for an address: new device.
        list.push_back({3000, watch_dhcp_request_frame(kPretendVisitor, ipv4(10, 0, 0, 77))});
        // An intruder joins (new device), then tries 20 ports on this
        // device, 10 a second: one scan alert, which counts to 20.
        list.push_back({4000, watch_arp_frame(kPretendIntruder, kPretendIntruderAddress, kPretendOwnAddress)});
        constexpr uint16_t kPorts[] = {21,  22,  23,  25,  53,   80,   110,  135,  139,  143,
                                       443, 445, 993, 995, 1723, 3306, 3389, 5900, 8080, 8443};
        uint32_t at = 5000;
        for (const uint16_t port : kPorts) {
            list.push_back({at, watch_syn_frame(kPretendIntruder, kPretendCable, kPretendIntruderAddress,
                                                kPretendOwnAddress, port)});
            at += 100;
        }
        // It answers for the router's address: someone may be pretending.
        list.push_back({8000, watch_arp_frame(kPretendIntruder, kPretendGateway, kPretendOwnAddress, true)});
        // The speaker takes the printer's address (heard 9 s before).
        list.push_back({10000, watch_arp_frame(kPretendSpeaker, ipv4(10, 0, 0, 24), kPretendGateway, true)});
        // A second server offers addresses.
        list.push_back({12000, watch_dhcp_reply_frame(kPretendIntruder, kPretendIntruderAddress, kDhcpOffer)});
        // The router announces IPv6 (fine: it is the router); then the
        // intruder does.
        list.push_back({13500, watch_router_advert_frame(kPretendRouter, link_local(kPretendRouter))});
        list.push_back({14000, watch_router_advert_frame(kPretendIntruder, link_local(kPretendIntruder))});
        // 60 ARP questions within one second.
        for (uint32_t index = 0; index < 60; ++index) {
            list.push_back({16000 + index * 10, watch_arp_frame(kPretendIntruder, kPretendIntruderAddress,
                                                                ipv4(10, 0, 0, 100 + index % 50))});
        }
        // The router keeps asking now and then.
        for (uint32_t repeat = kPretendRouterPeriodMs + 300; repeat <= 60000; repeat += kPretendRouterPeriodMs) {
            list.push_back({repeat, watch_arp_frame(kPretendRouter, kPretendGateway, kPretendOwnAddress)});
        }
        std::stable_sort(list.begin(), list.end(),
                         [](const WatchFrame& left, const WatchFrame& right) { return left.at_ms < right.at_ms; });
        return list;
    }();
    return frames;
}

std::vector<std::string> pretend_known_devices() {
    return {hardware_text(kPretendRouter.data()), hardware_text(kPretendPrinter.data()),
            hardware_text(kPretendPhone.data()), hardware_text(kPretendSpeaker.data())};
}

WatchSetup pretend_watch_setup() {
    WatchSetup setup;
    setup.own_ports = pretend_own_ports();
    setup.own_address = kPretendOwnAddress;
    setup.gateway = kPretendGateway;
    setup.gateway_hardware = hardware_text(kPretendRouter.data());
    setup.known = pretend_known_devices();
    setup.clock_text = pretend_clock_text;
    return setup;
}

} // namespace platform

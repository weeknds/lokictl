/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "network.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <vector>

namespace platform {
namespace {

constexpr uint32_t kInterfaceUp = 0x0001;  // IFF_UP in sys/class/net/<name>/flags
constexpr uint32_t kRouteUp = 0x0001;      // RTF_UP: the route is in use
constexpr uint32_t kRouteGateway = 0x0002; // RTF_GATEWAY: the route goes through a router
constexpr int64_t kEthernetType = 1;       // ARPHRD_ETHER in sys/class/net/<name>/type
constexpr int64_t kLowestSignalDbm = -255;
constexpr int64_t kHighestSignalDbm = -1;
// The longest interface name the kernel allows, without the final zero.
constexpr std::size_t kMaxInterfaceName = 15;
// A number in these files has at most this many digits.
constexpr std::size_t kMaxDigits = 10;
// A byte counter has at most this many digits (the largest 64-bit number).
constexpr std::size_t kMaxCounterDigits = 20;
// The scope of a link-local IPv6 address in proc/net/if_inet6, and of the
// loopback address.
constexpr uint32_t kIpv6ScopeLink = 0x20;
constexpr uint32_t kIpv6ScopeHost = 0x10;
// IFA_F_TENTATIVE and IFA_F_DADFAILED: the address cannot be used (yet).
constexpr uint32_t kIpv6Unusable = 0x40 | 0x08;
// The longest hardware address text read_ports() takes (20 bytes as hex).
constexpr std::size_t kMaxHardwareAddress = 59;

struct DefaultRoute {
    std::string interface_name;
    // The router's address as the kernel prints it: the address in network
    // order, read as a number of this machine, so it goes into s_addr as it is.
    uint32_t gateway{0};
    bool has_gateway{false};
    int64_t metric{0};
};

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return std::isspace(ch) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

// The first line of a small text file, without the white space around it.
// Returns false if the file cannot be opened or read (sys/class/net answers
// some files with a read error, for example "speed" without a cable).
bool read_first_line(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return false;
    }
    value = trim(line);
    return true;
}

// A whole number: an optional minus sign and 1 to kMaxDigits digits, nothing else.
bool parse_whole_number(const std::string& text, int64_t& value) {
    const bool negative = !text.empty() && text[0] == '-';
    const std::size_t first = negative ? 1 : 0;
    const std::size_t digits = text.size() - first;
    if (digits == 0 || digits > kMaxDigits) {
        return false;
    }
    int64_t result = 0;
    for (std::size_t index = first; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        result = result * 10 + (text[index] - '0');
    }
    value = negative ? -result : result;
    return true;
}

// 1 to 8 hexadecimal digits, nothing else, as proc/net/route writes them.
bool parse_hex32(const std::string& text, uint32_t& value) {
    if (text.empty() || text.size() > 8) {
        return false;
    }
    uint32_t result = 0;
    for (const char character : text) {
        uint32_t digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<uint32_t>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<uint32_t>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<uint32_t>(character - 'A' + 10);
        } else {
            return false;
        }
        result = (result << 4) | digit;
    }
    value = result;
    return true;
}

// Only names the kernel could give an interface, so a name read from a file
// can never lead out of sys/class/net.
bool valid_interface_name(const std::string& name) {
    if (name.empty() || name.size() > kMaxInterfaceName || name == "." || name == "..") {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](unsigned char ch) {
        return ch == '/' || std::isgraph(ch) == 0;
    });
}

std::string ipv4_text(uint32_t raw_address) {
    in_addr address{};
    address.s_addr = raw_address;
    char text[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address, text, sizeof(text)) == nullptr) {
        return {};
    }
    return text;
}

// The default routes of proc/net/route that are up, lowest metric first; on
// a tie the route listed first comes first. The first line is the header.
std::vector<DefaultRoute> read_default_routes(const std::filesystem::path& route_file) {
    std::vector<DefaultRoute> routes;
    std::ifstream file(route_file);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return routes;
    }
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string name, destination, gateway, flags, references, use, metric, mask;
        if (!(fields >> name >> destination >> gateway >> flags >> references >> use >> metric >> mask)) {
            continue;
        }
        uint32_t destination_value = 0;
        uint32_t gateway_value = 0;
        uint32_t flags_value = 0;
        uint32_t mask_value = 0;
        int64_t metric_value = 0;
        if (!parse_hex32(destination, destination_value) || !parse_hex32(gateway, gateway_value) ||
            !parse_hex32(flags, flags_value) || !parse_hex32(mask, mask_value) ||
            !parse_whole_number(metric, metric_value) || !valid_interface_name(name)) {
            continue;
        }
        if (destination_value != 0 || mask_value != 0 || (flags_value & kRouteUp) == 0) {
            continue;
        }
        DefaultRoute route;
        route.interface_name = name;
        route.gateway = gateway_value;
        route.has_gateway = (flags_value & kRouteGateway) != 0 && gateway_value != 0;
        route.metric = metric_value;
        routes.push_back(route);
    }
    std::stable_sort(routes.begin(), routes.end(), [](const DefaultRoute& left, const DefaultRoute& right) {
        return left.metric < right.metric;
    });
    return routes;
}

bool carrier_up(const std::filesystem::path& interface_dir) {
    std::string text;
    return read_first_line(interface_dir / "carrier", text) && text == "1";
}

model::LinkKind link_kind(const std::filesystem::path& interface_dir) {
    std::error_code ec;
    if (std::filesystem::is_directory(interface_dir / "wireless", ec)) {
        return model::LinkKind::Wifi;
    }
    std::string text;
    int64_t type = 0;
    if (read_first_line(interface_dir / "type", text) && parse_whole_number(text, type) &&
        type == kEthernetType) {
        return model::LinkKind::Cable;
    }
    return model::LinkKind::None;
}

int32_t read_speed(const std::filesystem::path& interface_dir) {
    std::string text;
    int64_t speed = 0;
    if (!read_first_line(interface_dir / "speed", text) || !parse_whole_number(text, speed) || speed <= 0 ||
        speed > std::numeric_limits<int32_t>::max()) {
        return model::kSpeedUnknown;
    }
    return static_cast<int32_t>(speed);
}

// A line of proc/net/wireless reads "wlan0: 0000   53.  -57.  -256 ...":
// the status, the link quality, the level and the noise. The two header
// lines have no colon.
int32_t read_signal(const std::filesystem::path& wireless_file, const std::string& name) {
    std::ifstream file(wireless_file);
    std::string line;
    while (std::getline(file, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos || trim(line.substr(0, colon)) != name) {
            continue;
        }
        std::istringstream fields(line.substr(colon + 1));
        std::string status, quality, level;
        if (!(fields >> status >> quality >> level)) {
            return model::kSignalUnknown;
        }
        if (!level.empty() && level.back() == '.') {
            level.pop_back();
        }
        int64_t dbm = 0;
        if (!parse_whole_number(level, dbm) || dbm < kLowestSignalDbm || dbm > kHighestSignalDbm) {
            return model::kSignalUnknown;
        }
        return static_cast<int32_t>(dbm);
    }
    return model::kSignalUnknown;
}

std::string system_address(const std::string& name) {
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return {};
    }
    std::string address;
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET ||
            entry->ifa_name == nullptr || name != entry->ifa_name) {
            continue;
        }
        sockaddr_in socket_address{};
        std::memcpy(&socket_address, entry->ifa_addr, sizeof(socket_address));
        address = ipv4_text(socket_address.sin_addr.s_addr);
        if (!address.empty()) {
            break;
        }
    }
    freeifaddrs(list);
    return address;
}

// "<IPv4>" or "<IPv4>/<prefix>" (prefix 0 to 32) as an address and a
// prefix; the prefix is -1 when the text has none. Returns false for
// anything else.
bool parse_address_with_prefix(const std::string& text, uint32_t& raw_address, int& prefix) {
    const auto slash = text.find('/');
    prefix = -1;
    if (slash != std::string::npos) {
        int64_t value = 0;
        const std::string digits = text.substr(slash + 1);
        if (digits.empty() || digits[0] == '-' || !parse_whole_number(digits, value) || value > 32) {
            return false;
        }
        prefix = static_cast<int>(value);
    }
    in_addr address{};
    if (inet_pton(AF_INET, text.substr(0, slash).c_str(), &address) != 1) {
        return false;
    }
    raw_address = address.s_addr;
    return true;
}

// The first address of `name` in the file that stands in for the system's
// list, with its prefix when the line has one (`prefix` is -1 otherwise).
bool file_address_line(const std::filesystem::path& addresses_file,
                       const std::string& name,
                       uint32_t& raw_address,
                       int& prefix) {
    std::ifstream file(addresses_file);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string interface_name, text;
        if (fields >> interface_name >> text && interface_name == name &&
            parse_address_with_prefix(text, raw_address, prefix)) {
            return true;
        }
    }
    return false;
}

std::string file_address(const std::filesystem::path& addresses_file, const std::string& name) {
    uint32_t raw_address = 0;
    int prefix = -1;
    if (!file_address_line(addresses_file, name, raw_address, prefix)) {
        return {};
    }
    return ipv4_text(raw_address);
}

// An address with its prefix, "10.0.0.12/24", or the address alone when the
// prefix is -1.
std::string address_with_prefix(uint32_t raw_address, int prefix) {
    std::string text = ipv4_text(raw_address);
    if (!text.empty() && prefix >= 0) {
        text += "/" + std::to_string(prefix);
    }
    return text;
}

// The first IPv4 address of `name` with its prefix, from the system's list.
std::string system_address_with_prefix(const std::string& name) {
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return {};
    }
    std::string text;
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET ||
            entry->ifa_name == nullptr || name != entry->ifa_name) {
            continue;
        }
        sockaddr_in socket_address{};
        std::memcpy(&socket_address, entry->ifa_addr, sizeof(socket_address));
        int prefix = -1;
        if (entry->ifa_netmask != nullptr && entry->ifa_netmask->sa_family == AF_INET) {
            sockaddr_in mask{};
            std::memcpy(&mask, entry->ifa_netmask, sizeof(mask));
            uint32_t bits = ntohl(mask.sin_addr.s_addr);
            prefix = 0;
            while (bits & 0x80000000u) {
                ++prefix;
                bits <<= 1;
            }
        }
        text = address_with_prefix(socket_address.sin_addr.s_addr, prefix);
        if (!text.empty()) {
            break;
        }
    }
    freeifaddrs(list);
    return text;
}

// A line of proc/net/if_inet6 reads "<32 hex digits> <index> <prefix>
// <scope> <flags> <name>", all numbers in hexadecimal. Fills in the port's
// first address that is not link-local and its first link-local one.
void read_ipv6(const std::filesystem::path& if_inet6_file, model::StatusPort& port) {
    std::ifstream file(if_inet6_file);
    std::string line;
    while (std::getline(file, line) && (port.ipv6.empty() || port.ipv6_link.empty())) {
        std::istringstream fields(line);
        std::string hex, index, prefix, scope, flags, name;
        uint32_t scope_value = 0;
        uint32_t flags_value = 0;
        if (!(fields >> hex >> index >> prefix >> scope >> flags >> name) || name != port.name ||
            hex.size() != 32 || !parse_hex32(scope, scope_value) || !parse_hex32(flags, flags_value) ||
            (flags_value & kIpv6Unusable) != 0 || scope_value == kIpv6ScopeHost) {
            continue;
        }
        in6_addr address{};
        bool valid = true;
        for (std::size_t byte = 0; byte < 16 && valid; ++byte) {
            uint32_t value = 0;
            valid = parse_hex32(hex.substr(byte * 2, 2), value);
            address.s6_addr[byte] = static_cast<uint8_t>(value);
        }
        char text[INET6_ADDRSTRLEN] = {};
        if (!valid || inet_ntop(AF_INET6, &address, text, sizeof(text)) == nullptr) {
            continue;
        }
        std::string& slot = scope_value == kIpv6ScopeLink ? port.ipv6_link : port.ipv6;
        if (slot.empty()) {
            slot = text;
        }
    }
}

// "aa:bb:cc:dd:ee:ff": only hexadecimal digits and colons, as the kernel
// writes it; empty for anything else.
std::string read_hardware_address(const std::filesystem::path& interface_dir) {
    std::string text;
    if (!read_first_line(interface_dir / "address", text) || text.empty() ||
        text.size() > kMaxHardwareAddress ||
        !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return ch == ':' || std::isxdigit(ch) != 0;
        })) {
        return {};
    }
    return text;
}

// The interface's flags ("0x1003"); false when they cannot be read.
bool read_flags(const std::filesystem::path& interface_dir, uint32_t& flags) {
    std::string text;
    if (!read_first_line(interface_dir / "flags", text)) {
        return false;
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
    }
    return parse_hex32(text, flags);
}

// A byte counter: 1 to 20 digits, nothing else, at most the largest 64-bit
// number.
bool parse_counter(const std::string& text, uint64_t& value) {
    if (text.empty() || text.size() > kMaxCounterDigits) {
        return false;
    }
    uint64_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const auto digit = static_cast<uint64_t>(character - '0');
        if (result > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

// Line `line` of a counter file (counting from 0), or its last line when it
// has fewer, read as a counter.
bool read_counter(const std::filesystem::path& path, std::size_t line, uint64_t& value) {
    std::ifstream file(path);
    std::string text;
    std::string current;
    bool found = false;
    for (std::size_t index = 0; index <= line && std::getline(file, current); ++index) {
        text = trim(current);
        found = true;
    }
    return found && parse_counter(text, value);
}

} // namespace

NetworkReading read_network(const std::filesystem::path& root,
                            const std::filesystem::path& addresses_file) {
    NetworkReading reading;
    const std::filesystem::path net_dir = root / "sys/class/net";

    for (const DefaultRoute& route : read_default_routes(root / "proc/net/route")) {
        const std::filesystem::path interface_dir = net_dir / route.interface_name;
        if (!carrier_up(interface_dir)) {
            continue;
        }
        const model::LinkKind kind = link_kind(interface_dir);
        if (kind == model::LinkKind::None) {
            continue;
        }

        reading.kind = kind;
        reading.interface_name = route.interface_name;
        if (route.has_gateway) {
            reading.gateway = ipv4_text(route.gateway);
        }
        if (kind == model::LinkKind::Wifi) {
            reading.signal_dbm = read_signal(root / "proc/net/wireless", route.interface_name);
        } else {
            reading.speed_mbps = read_speed(interface_dir);
        }
        reading.address = addresses_file.empty() ? system_address(route.interface_name)
                                                 : file_address(addresses_file, route.interface_name);
        break;
    }
    return reading;
}

std::vector<model::StatusPort> read_ports(const std::filesystem::path& root,
                                          const std::filesystem::path& addresses_file,
                                          const std::string& uplink_name) {
    std::vector<model::StatusPort> ports;
    const std::filesystem::path net_dir = root / "sys/class/net";

    // The names first, so they can be taken in order. The iterator is
    // stepped with increment() and an error code, because a range-for would
    // throw.
    std::error_code error;
    std::filesystem::directory_iterator entry(net_dir, error);
    if (error) {
        return ports;
    }
    std::vector<std::string> names;
    const std::filesystem::directory_iterator end;
    while (entry != end) {
        const std::string name = entry->path().filename().string();
        if (valid_interface_name(name)) {
            names.push_back(name);
        }
        entry.increment(error);
        if (error) {
            return ports;
        }
    }
    std::sort(names.begin(), names.end());

    const std::vector<DefaultRoute> routes = read_default_routes(root / "proc/net/route");
    for (const std::string& name : names) {
        if (ports.size() >= kMaxPorts) {
            break;
        }
        const std::filesystem::path interface_dir = net_dir / name;
        std::string text;
        int64_t type = 0;
        if (!read_first_line(interface_dir / "type", text) || !parse_whole_number(text, type) ||
            type != kEthernetType || !std::filesystem::exists(interface_dir / "device", error)) {
            continue;
        }

        model::StatusPort port;
        port.name = name;
        port.kind = std::filesystem::is_directory(interface_dir / "wireless", error) ? model::LinkKind::Wifi
                                                                                     : model::LinkKind::Cable;
        port.uplink = name == uplink_name;
        port.hardware_address = read_hardware_address(interface_dir);

        uint32_t flags = 0;
        if (!read_flags(interface_dir, flags)) {
            port.state = model::PortState::Unknown;
        } else if ((flags & kInterfaceUp) == 0) {
            port.state = model::PortState::Off;
        } else if (!carrier_up(interface_dir)) {
            port.state = model::PortState::NoLink;
        } else {
            port.state = model::PortState::Connected;
        }

        if (port.state == model::PortState::Connected) {
            if (port.kind == model::LinkKind::Wifi) {
                const int32_t dbm = read_signal(root / "proc/net/wireless", name);
                port.signal_dbm = dbm == model::kSignalUnknown ? model::kDetailUnknown : dbm;
            } else {
                const int32_t mbps = read_speed(interface_dir);
                port.speed_mbps = mbps == model::kSpeedUnknown ? model::kDetailUnknown : mbps;
            }
            for (const DefaultRoute& route : routes) {
                if (route.interface_name == name && route.has_gateway) {
                    port.router = ipv4_text(route.gateway);
                    break;
                }
            }
        }

        if (addresses_file.empty()) {
            port.ipv4 = system_address_with_prefix(name);
        } else {
            uint32_t raw_address = 0;
            int prefix = -1;
            if (file_address_line(addresses_file, name, raw_address, prefix)) {
                port.ipv4 = address_with_prefix(raw_address, prefix);
            }
        }
        read_ipv6(root / "proc/net/if_inet6", port);
        ports.push_back(std::move(port));
    }
    return ports;
}

std::vector<std::string> read_dns_servers(const std::filesystem::path& root) {
    std::vector<std::string> servers;
    std::ifstream file(root / "etc/resolv.conf");
    std::string line;
    while (servers.size() < kMaxDnsServers && std::getline(file, line)) {
        std::istringstream fields(line);
        std::string keyword, text;
        if (!(fields >> keyword >> text) || keyword != "nameserver") {
            continue;
        }
        in_addr ipv4{};
        in6_addr ipv6{};
        if (inet_pton(AF_INET, text.c_str(), &ipv4) == 1 || inet_pton(AF_INET6, text.c_str(), &ipv6) == 1) {
            servers.push_back(text);
        }
    }
    return servers;
}

bool read_traffic_counters(const std::filesystem::path& root,
                           const std::string& interface_name,
                           std::size_t line,
                           TrafficCounters& counters) {
    if (!valid_interface_name(interface_name)) {
        return false;
    }
    const std::filesystem::path statistics = root / "sys/class/net" / interface_name / "statistics";
    TrafficCounters reading;
    if (!read_counter(statistics / "rx_bytes", line, reading.received) ||
        !read_counter(statistics / "tx_bytes", line, reading.sent)) {
        return false;
    }
    counters = reading;
    return true;
}

} // namespace platform

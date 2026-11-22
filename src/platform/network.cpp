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

std::string file_address(const std::filesystem::path& addresses_file, const std::string& name) {
    std::ifstream file(addresses_file);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string interface_name, text;
        in_addr address{};
        if (fields >> interface_name >> text && interface_name == name &&
            inet_pton(AF_INET, text.c_str(), &address) == 1) {
            return ipv4_text(address.s_addr);
        }
    }
    return {};
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

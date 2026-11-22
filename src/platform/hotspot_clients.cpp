/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "hotspot_clients.h"

#include "network.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <sys/time.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

namespace platform {
namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

// The longest host name kept, as long as the longest network name.
constexpr std::size_t kMaxHostName = 32;
// ATF_COM in proc/net/arp: the entry is complete.
constexpr uint32_t kArpComplete = 0x2;

// "02:5E:11:7A:C3:04" as "02:5e:11:7a:c3:04"; empty for anything that is
// not six pairs of hex digits joined by ':'.
std::string normal_hardware_address(const std::string& text) {
    if (text.size() != 17) {
        return {};
    }
    std::string result = text;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto byte = static_cast<unsigned char>(result[index]);
        if (index % 3 == 2) {
            if (byte != ':') {
                return {};
            }
        } else if (!std::isxdigit(byte)) {
            return {};
        } else {
            result[index] = static_cast<char>(std::tolower(byte));
        }
    }
    return result;
}

std::string hardware_address_text(const uint8_t* bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    for (int index = 0; index < 6; ++index) {
        if (index > 0) {
            text += ':';
        }
        text += kHex[bytes[index] >> 4];
        text += kHex[bytes[index] & 0x0F];
    }
    return text;
}

// A whole number of at most 19 digits, without a sign.
bool parse_unsigned(const std::string& text, uint64_t& value) {
    if (text.empty() || text.size() > 19) {
        return false;
    }
    uint64_t result = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        result = result * 10 + static_cast<uint64_t>(character - '0');
    }
    value = result;
    return true;
}

// A test file's number for reading `reading`: `text` is one number or a
// list "100,2600,5100", and the reading takes the value at its place, or the
// last one past the end. False for "-" and for anything that is not such a
// list.
bool pick_reading(const std::string& text, std::size_t reading, uint64_t& value) {
    if (text == "-") {
        return false;
    }
    std::vector<uint64_t> values;
    std::istringstream list(text);
    std::string item;
    while (std::getline(list, item, ',')) {
        uint64_t number = 0;
        if (!parse_unsigned(item, number)) {
            return false;
        }
        values.push_back(number);
    }
    if (values.empty()) {
        return false;
    }
    value = values[std::min(reading, values.size() - 1)];
    return true;
}

bool skip_line(const std::string& line) {
    const auto first = line.find_first_not_of(" \t\r");
    return first == std::string::npos || line[first] == '#';
}

// ---- Reverse DNS -------------------------------------------------------

constexpr std::size_t kDnsHeader = 12;
constexpr std::size_t kDnsMaxMessage = 512;
constexpr uint16_t kDnsTypePtr = 12;
constexpr uint16_t kDnsClassIn = 1;
constexpr int kDnsRcodeNameError = 3;

uint16_t read_u16(const uint8_t* bytes) {
    return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
}

void put_u16(std::vector<uint8_t>& message, uint16_t value) {
    message.push_back(static_cast<uint8_t>(value >> 8));
    message.push_back(static_cast<uint8_t>(value & 0xFF));
}

// The question part of a reverse lookup for `address`:
// "<d>.<c>.<b>.<a>.in-addr.arpa", type PTR, class IN. Empty when `address`
// is not an IPv4 address.
std::vector<uint8_t> reverse_question(const std::string& address) {
    in_addr parsed{};
    if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
        return {};
    }
    uint8_t parts[4];
    std::memcpy(parts, &parsed.s_addr, sizeof(parts)); // network order: a, b, c, d
    std::vector<uint8_t> question;
    auto put_label = [&question](const std::string& label) {
        question.push_back(static_cast<uint8_t>(label.size()));
        question.insert(question.end(), label.begin(), label.end());
    };
    for (int index = 3; index >= 0; --index) {
        put_label(std::to_string(parts[index]));
    }
    put_label("in-addr");
    put_label("arpa");
    question.push_back(0);
    put_u16(question, kDnsTypePtr);
    put_u16(question, kDnsClassIn);
    return question;
}

// Reads the name at `offset` in `message`, following compressed parts, into
// `name` (labels joined by '.'). `offset` moves past the name as it stands
// at `offset`. Returns false for a name that runs past the end, loops or
// grows too long.
bool read_dns_name(const uint8_t* message, std::size_t size, std::size_t& offset, std::string* name) {
    std::size_t position = offset;
    bool jumped = false;
    int jumps = 0;
    std::size_t length = 0;
    while (true) {
        if (position >= size) {
            return false;
        }
        const uint8_t label = message[position];
        if ((label & 0xC0) == 0xC0) {
            if (position + 1 >= size || ++jumps > 16) {
                return false;
            }
            const std::size_t target = static_cast<std::size_t>(((label & 0x3F) << 8) | message[position + 1]);
            if (!jumped) {
                offset = position + 2;
                jumped = true;
            }
            position = target;
            continue;
        }
        if ((label & 0xC0) != 0) {
            return false;
        }
        if (label == 0) {
            if (!jumped) {
                offset = position + 1;
            }
            return true;
        }
        if (position + 1 + label > size) {
            return false;
        }
        length += label + 1U;
        if (length > 255) {
            return false;
        }
        if (name) {
            if (!name->empty()) {
                *name += '.';
            }
            name->append(reinterpret_cast<const char*>(message + position + 1), label);
        }
        position += 1U + label;
    }
}

// Reads one answer to `question` with id `id`. Returns false when the
// message is no such answer. Otherwise `name` is the first PTR name in it,
// or empty when the server knows none.
bool read_ptr_answer(const uint8_t* message,
                     std::size_t size,
                     uint16_t id,
                     const std::vector<uint8_t>& question,
                     std::string& name) {
    if (size < kDnsHeader + question.size() || read_u16(message) != id) {
        return false;
    }
    const uint16_t flags = read_u16(message + 2);
    const bool is_answer = (flags & 0x8000) != 0;
    const int opcode = (flags >> 11) & 0x0F;
    const int rcode = flags & 0x0F;
    if (!is_answer || opcode != 0 || read_u16(message + 4) != 1 ||
        std::memcmp(message + kDnsHeader, question.data(), question.size()) != 0) {
        return false;
    }
    name.clear();
    if (rcode == kDnsRcodeNameError) {
        return true;
    }
    if (rcode != 0) {
        return false;
    }
    const uint16_t answers = read_u16(message + 6);
    std::size_t offset = kDnsHeader + question.size();
    for (uint16_t index = 0; index < answers; ++index) {
        if (!read_dns_name(message, size, offset, nullptr) || offset + 10 > size) {
            return true;
        }
        const uint16_t type = read_u16(message + offset);
        const uint16_t record_class = read_u16(message + offset + 2);
        const uint16_t data_length = read_u16(message + offset + 8);
        offset += 10;
        if (offset + data_length > size) {
            return true;
        }
        if (type == kDnsTypePtr && record_class == kDnsClassIn) {
            std::size_t name_offset = offset;
            std::string found;
            if (read_dns_name(message, size, name_offset, &found)) {
                name = clean_host_name(found);
            }
            return true;
        }
        offset += data_length;
    }
    return true;
}

// ---- Station dump (Linux) -----------------------------------------------

#if defined(__linux__)

constexpr std::size_t align4(std::size_t size) {
    return (size + 3U) & ~static_cast<std::size_t>(3U);
}

constexpr std::size_t kMessageHeader = align4(sizeof(nlmsghdr));
constexpr std::size_t kFamilyHeader = align4(sizeof(genlmsghdr));
constexpr std::size_t kAttributeHeader = align4(sizeof(nlattr));
constexpr uint16_t kAttributeTypeMask = 0x3FFF; // without NLA_F_NESTED and NLA_F_NET_BYTEORDER
constexpr std::size_t kReceiveBuffer = 32768;
constexpr int kReceiveTimeoutMs = 500;
// The most messages one answer may take; a station dump has one per device.
constexpr int kMaxMessages = 256;

class NetlinkSocket {
public:
    NetlinkSocket() {
        fd_ = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
        if (fd_ < 0) {
            return;
        }
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = kReceiveTimeoutMs * 1000;
        sockaddr_nl local{};
        local.nl_family = AF_NETLINK;
        if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
            bind(fd_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            close(fd_);
            fd_ = -1;
        }
    }
    ~NetlinkSocket() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    NetlinkSocket(const NetlinkSocket&) = delete;
    NetlinkSocket& operator=(const NetlinkSocket&) = delete;

    bool ok() const { return fd_ >= 0; }

    // Sends a generic netlink request: `type` (the family), `flags`
    // besides NLM_F_REQUEST, `command`, and one attribute.
    bool send_request(uint16_t type, uint16_t flags, uint32_t sequence, uint8_t command, uint8_t version,
                      uint16_t attribute, const void* data, std::size_t size) {
        const std::size_t attribute_length = kAttributeHeader + size;
        std::vector<uint8_t> message(kMessageHeader + kFamilyHeader + align4(attribute_length), 0);
        nlmsghdr header{};
        header.nlmsg_len = static_cast<uint32_t>(message.size());
        header.nlmsg_type = type;
        header.nlmsg_flags = static_cast<uint16_t>(NLM_F_REQUEST | flags);
        header.nlmsg_seq = sequence;
        std::memcpy(message.data(), &header, sizeof(header));
        genlmsghdr family{};
        family.cmd = command;
        family.version = version;
        std::memcpy(message.data() + kMessageHeader, &family, sizeof(family));
        nlattr attribute_header{};
        attribute_header.nla_len = static_cast<uint16_t>(attribute_length);
        attribute_header.nla_type = attribute;
        std::memcpy(message.data() + kMessageHeader + kFamilyHeader, &attribute_header, sizeof(attribute_header));
        std::memcpy(message.data() + kMessageHeader + kFamilyHeader + kAttributeHeader, data, size);

        sockaddr_nl kernel{};
        kernel.nl_family = AF_NETLINK;
        const ssize_t sent = sendto(fd_, message.data(), message.size(), 0,
                                    reinterpret_cast<const sockaddr*>(&kernel), sizeof(kernel));
        return sent == static_cast<ssize_t>(message.size());
    }

    // Receives the answer to request `sequence` and hands each message of
    // type `type` to `handle` (its payload after the generic netlink
    // header). Returns false on an error answer, a timeout or a broken
    // message. A dump ends with NLMSG_DONE; a plain request after one
    // message of `type` or an acknowledgement.
    template <typename Handle>
    bool receive(uint32_t sequence, uint16_t type, bool dump, Handle&& handle) {
        std::vector<uint8_t> buffer(kReceiveBuffer);
        for (int round = 0; round < kMaxMessages; ++round) {
            const ssize_t received = recv(fd_, buffer.data(), buffer.size(), 0);
            if (received <= 0) {
                return false;
            }
            std::size_t offset = 0;
            const auto size = static_cast<std::size_t>(received);
            while (offset + sizeof(nlmsghdr) <= size) {
                nlmsghdr header{};
                std::memcpy(&header, buffer.data() + offset, sizeof(header));
                if (header.nlmsg_len < sizeof(nlmsghdr) || offset + header.nlmsg_len > size) {
                    return false;
                }
                const uint8_t* payload = buffer.data() + offset + kMessageHeader;
                const std::size_t payload_size =
                    header.nlmsg_len > kMessageHeader ? header.nlmsg_len - kMessageHeader : 0;
                offset += align4(header.nlmsg_len);
                if (header.nlmsg_seq != sequence) {
                    continue;
                }
                if (header.nlmsg_type == NLMSG_DONE) {
                    return true;
                }
                if (header.nlmsg_type == NLMSG_ERROR) {
                    nlmsgerr error{};
                    if (payload_size < sizeof(error)) {
                        return false;
                    }
                    std::memcpy(&error, payload, sizeof(error));
                    return error.error == 0 && !dump;
                }
                if (header.nlmsg_type == type && payload_size >= kFamilyHeader) {
                    handle(payload + kFamilyHeader, payload_size - kFamilyHeader);
                    if (!dump) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

private:
    int fd_{-1};
};

// Calls `visit(type, data, size)` for each attribute in `data`.
template <typename Visit>
void for_each_attribute(const uint8_t* data, std::size_t size, Visit&& visit) {
    std::size_t offset = 0;
    while (offset + kAttributeHeader <= size) {
        nlattr header{};
        std::memcpy(&header, data + offset, sizeof(header));
        if (header.nla_len < kAttributeHeader || offset + header.nla_len > size) {
            return;
        }
        visit(static_cast<uint16_t>(header.nla_type & kAttributeTypeMask), data + offset + kAttributeHeader,
              header.nla_len - kAttributeHeader);
        offset += align4(header.nla_len);
    }
}

template <typename T>
bool read_value(const uint8_t* data, std::size_t size, T& value) {
    if (size < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, data, sizeof(T));
    return true;
}

// The station's values from its NL80211_ATTR_STA_INFO attribute. Returns
// false when the station is listed but not let in yet.
bool read_station_info(const uint8_t* data, std::size_t size, Station& station) {
    bool authorized = true;
    bool has_rx = false;
    bool has_tx = false;
    uint64_t rx = 0;
    uint64_t tx = 0;
    bool has_rx64 = false;
    bool has_tx64 = false;
    for_each_attribute(data, size, [&](uint16_t type, const uint8_t* value, std::size_t value_size) {
        uint32_t u32 = 0;
        uint64_t u64 = 0;
        uint8_t u8 = 0;
        switch (type) {
            case NL80211_STA_INFO_SIGNAL:
                if (read_value(value, value_size, u8)) {
                    const auto dbm = static_cast<int8_t>(u8);
                    if (dbm < 0) {
                        station.signal_dbm = dbm;
                    }
                }
                break;
            case NL80211_STA_INFO_CONNECTED_TIME:
                if (read_value(value, value_size, u32)) {
                    station.connected_seconds = static_cast<int32_t>(std::min<uint32_t>(u32, INT32_MAX));
                }
                break;
            case NL80211_STA_INFO_RX_BYTES64:
                if (read_value(value, value_size, u64)) {
                    rx = u64;
                    has_rx = has_rx64 = true;
                }
                break;
            case NL80211_STA_INFO_TX_BYTES64:
                if (read_value(value, value_size, u64)) {
                    tx = u64;
                    has_tx = has_tx64 = true;
                }
                break;
            case NL80211_STA_INFO_RX_BYTES:
                if (!has_rx64 && read_value(value, value_size, u32)) {
                    rx = u32;
                    has_rx = true;
                }
                break;
            case NL80211_STA_INFO_TX_BYTES:
                if (!has_tx64 && read_value(value, value_size, u32)) {
                    tx = u32;
                    has_tx = true;
                }
                break;
            case NL80211_STA_INFO_STA_FLAGS: {
                nl80211_sta_flag_update flags{};
                const uint32_t bit = 1U << NL80211_STA_FLAG_AUTHORIZED;
                if (read_value(value, value_size, flags) && (flags.mask & bit) != 0 && (flags.set & bit) == 0) {
                    authorized = false;
                }
                break;
            }
            default:
                break;
        }
    });
    if (has_rx && has_tx) {
        station.has_bytes = true;
        station.bytes = rx + tx;
    }
    return authorized;
}

#endif // __linux__

} // namespace

#if defined(__linux__)

bool read_system_stations(const std::string& interface_name, std::vector<Station>& stations) {
    stations.clear();
    const unsigned int index = if_nametoindex(interface_name.c_str());
    if (index == 0) {
        return false;
    }
    NetlinkSocket socket_handle;
    if (!socket_handle.ok()) {
        return false;
    }

    // The number the kernel gave the nl80211 family.
    static const char kFamilyName[] = NL80211_GENL_NAME;
    uint16_t family = 0;
    if (!socket_handle.send_request(GENL_ID_CTRL, 0, 1, CTRL_CMD_GETFAMILY, 1, CTRL_ATTR_FAMILY_NAME, kFamilyName,
                                    sizeof(kFamilyName)) ||
        !socket_handle.receive(1, GENL_ID_CTRL, false, [&family](const uint8_t* data, std::size_t size) {
            for_each_attribute(data, size, [&family](uint16_t type, const uint8_t* value, std::size_t value_size) {
                if (type == CTRL_ATTR_FAMILY_ID) {
                    read_value(value, value_size, family);
                }
            });
        }) ||
        family == 0) {
        return false;
    }

    const uint32_t interface_index = index;
    std::vector<Station> found;
    const bool ok = socket_handle.send_request(family, NLM_F_DUMP, 2, NL80211_CMD_GET_STATION, 0, NL80211_ATTR_IFINDEX,
                                               &interface_index, sizeof(interface_index)) &&
                    socket_handle.receive(2, family, true, [&found](const uint8_t* data, std::size_t size) {
                        Station station;
                        bool authorized = true;
                        for_each_attribute(data, size, [&](uint16_t type, const uint8_t* value, std::size_t value_size) {
                            if (type == NL80211_ATTR_MAC && value_size >= 6) {
                                station.hardware_address = hardware_address_text(value);
                            } else if (type == NL80211_ATTR_STA_INFO) {
                                authorized = read_station_info(value, value_size, station);
                            }
                        });
                        if (authorized && !station.hardware_address.empty() && found.size() < 4 * kMaxClients) {
                            found.push_back(std::move(station));
                        }
                    });
    if (!ok) {
        return false;
    }
    stations = std::move(found);
    return true;
}

#else

bool read_system_stations(const std::string& /*interface_name*/, std::vector<Station>& stations) {
    stations.clear();
    return false;
}

#endif

std::vector<Station> read_stations_file(const std::filesystem::path& path,
                                        const std::string& interface_name,
                                        std::size_t reading) {
    std::vector<Station> stations;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (skip_line(line)) {
            continue;
        }
        std::istringstream fields(line);
        std::string name, address, signal, connected, bytes;
        if (!(fields >> name >> address >> signal >> connected >> bytes) || name != interface_name) {
            continue;
        }
        Station station;
        station.hardware_address = normal_hardware_address(address);
        if (station.hardware_address.empty()) {
            continue;
        }
        uint64_t value = 0;
        if (signal.size() > 1 && signal[0] == '-' && parse_unsigned(signal.substr(1), value) && value >= 1 &&
            value <= 255) {
            station.signal_dbm = -static_cast<int32_t>(value);
        }
        if (pick_reading(connected, reading, value) && value <= INT32_MAX) {
            station.connected_seconds = static_cast<int32_t>(value);
        }
        if (pick_reading(bytes, reading, value)) {
            station.has_bytes = true;
            station.bytes = value;
        }
        stations.push_back(std::move(station));
    }
    return stations;
}

std::map<std::string, std::string> read_neighbours(const std::filesystem::path& root,
                                                   const std::string& interface_name) {
    std::map<std::string, std::string> neighbours;
    std::ifstream file(root / "proc/net/arp");
    std::string line;
    std::getline(file, line); // the heading
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string address, hardware_type, flags_text, hardware_address, mask, device;
        if (!(fields >> address >> hardware_type >> flags_text >> hardware_address >> mask >> device) ||
            device != interface_name) {
            continue;
        }
        uint32_t flags = 0;
        try {
            flags = static_cast<uint32_t>(std::stoul(flags_text, nullptr, 16));
        } catch (...) {
            continue;
        }
        in_addr parsed{};
        const std::string normal = normal_hardware_address(hardware_address);
        if ((flags & kArpComplete) == 0 || normal.empty() || normal == "00:00:00:00:00:00" ||
            inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
            continue;
        }
        neighbours.emplace(normal, address);
    }
    return neighbours;
}

std::map<std::string, std::string> read_host_names_file(const std::filesystem::path& path) {
    std::map<std::string, std::string> names;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (skip_line(line)) {
            continue;
        }
        std::istringstream fields(line);
        std::string address, name;
        in_addr parsed{};
        if (fields >> address >> name && inet_pton(AF_INET, address.c_str(), &parsed) == 1) {
            names[address] = clean_host_name(name);
        }
    }
    return names;
}

std::string clean_host_name(const std::string& name) {
    std::string result = name.substr(0, name.find('.'));
    if (result.size() > kMaxHostName) {
        result.resize(kMaxHostName);
    }
    // Host names are plain ASCII; anything else (a control character, a
    // byte of another script) becomes '?', so nothing odd reaches the page.
    for (char& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7E) {
            character = '?';
        }
    }
    return result;
}

std::map<std::string, std::string> ask_host_names(const std::string& server,
                                                  const std::vector<std::string>& addresses,
                                                  milliseconds limit) {
    std::map<std::string, std::string> found;
    in_addr server_address{};
    if (addresses.empty() || inet_pton(AF_INET, server.c_str(), &server_address) != 1) {
        return found;
    }
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return found;
    }
    // A connected socket only gets datagrams from the server's port 53.
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    to.sin_addr = server_address;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 || fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        connect(fd, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != 0) {
        close(fd);
        return found;
    }

    // One question per address; the ids start at a number that changes
    // with the clock, so an old answer is not taken for a new one.
    struct Question {
        std::string address;
        uint16_t id{0};
        std::vector<uint8_t> question;
    };
    std::vector<Question> questions;
    const auto start = steady_clock::now();
    auto id = static_cast<uint16_t>(start.time_since_epoch().count() & 0xFFFF);
    for (const std::string& address : addresses) {
        Question entry{address, ++id, reverse_question(address)};
        if (entry.question.empty()) {
            continue;
        }
        std::vector<uint8_t> message;
        put_u16(message, entry.id);
        put_u16(message, 0x0100); // a standard query, recursion desired
        put_u16(message, 1);      // one question
        put_u16(message, 0);
        put_u16(message, 0);
        put_u16(message, 0);
        message.insert(message.end(), entry.question.begin(), entry.question.end());
        if (send(fd, message.data(), message.size(), 0) == static_cast<ssize_t>(message.size())) {
            questions.push_back(std::move(entry));
        }
    }

    const auto deadline = start + limit;
    std::array<uint8_t, kDnsMaxMessage> buffer{};
    while (found.size() < questions.size()) {
        const auto left = std::chrono::duration_cast<milliseconds>(deadline - steady_clock::now()).count();
        if (left <= 0) {
            break;
        }
        pollfd waiting{fd, POLLIN, 0};
        if (poll(&waiting, 1, static_cast<int>(left)) <= 0) {
            break;
        }
        const ssize_t received = recv(fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            continue;
        }
        const auto size = static_cast<std::size_t>(received);
        for (const Question& entry : questions) {
            std::string name;
            if (read_ptr_answer(buffer.data(), size, entry.id, entry.question, name)) {
                found[entry.address] = name;
                break;
            }
        }
    }
    close(fd);
    return found;
}

HotspotClients::HotspotClients(std::filesystem::path root, std::filesystem::path addresses_file, bool test_folder)
    : root_(std::move(root)), addresses_file_(std::move(addresses_file)), test_folder_(test_folder) {}

const std::vector<model::HomeClient>& HotspotClients::clients() const {
    return clients_;
}

bool HotspotClients::has_read() const {
    return has_read_;
}

const std::string& HotspotClients::interface_name() const {
    return interface_name_;
}

void HotspotClients::clear() {
    clients_.clear();
    has_read_ = false;
    interface_name_.clear();
    reading_ = 0;
    counters_.clear();
    names_.clear();
}

void HotspotClients::read(const std::string& interface_name, bool may_ask_names) {
    if (has_read_ && interface_name != interface_name_) {
        clear();
    }
    const auto now = steady_clock::now();
    const int64_t elapsed_ms =
        test_folder_ ? kTestInterval.count()
                     : std::chrono::duration_cast<milliseconds>(now - read_at_).count();
    const bool had_reading = has_read_;

    std::vector<Station> stations;
    if (test_folder_) {
        stations = read_stations_file(root_ / "stations", interface_name, reading_++);
    } else if (!read_system_stations(interface_name, stations)) {
        // The kernel could not be asked, or did not answer in time: the
        // last list stays, with its counters, so one slow answer never
        // empties the page. It counts as a reading, so it is not asked
        // again before the next one is due.
        has_read_ = true;
        interface_name_ = interface_name;
        return;
    }

    // Devices already listed keep their places, so the order never changes
    // under the highlight (two devices that joined in the same second tie on
    // one reading and not on the next, since the time is in whole seconds).
    // Newcomers go after them: the longest joined first, then by hardware
    // address.
    std::vector<Station> ordered;
    for (const model::HomeClient& listed : clients_) {
        const auto found = std::find_if(stations.begin(), stations.end(), [&listed](const Station& station) {
            return station.hardware_address == listed.hardware_address;
        });
        if (found != stations.end()) {
            ordered.push_back(std::move(*found));
            stations.erase(found);
        }
    }
    std::sort(stations.begin(), stations.end(), [](const Station& left, const Station& right) {
        if (left.connected_seconds != right.connected_seconds) {
            return left.connected_seconds > right.connected_seconds;
        }
        return left.hardware_address < right.hardware_address;
    });
    ordered.insert(ordered.end(), std::make_move_iterator(stations.begin()), std::make_move_iterator(stations.end()));
    if (ordered.size() > kMaxClients) {
        ordered.resize(kMaxClients);
    }
    stations = std::move(ordered);

    const auto neighbours = read_neighbours(root_, interface_name);
    std::map<std::string, Counter> counters;
    std::vector<model::HomeClient> clients;
    std::vector<std::string> addresses;
    for (const Station& station : stations) {
        model::HomeClient client;
        client.hardware_address = station.hardware_address;
        client.signal_dbm = station.signal_dbm;
        client.connected_seconds = station.connected_seconds;
        client.is_new = station.connected_seconds >= 0 && station.connected_seconds < model::kClientNewSeconds;
        const auto neighbour = neighbours.find(station.hardware_address);
        if (neighbour != neighbours.end()) {
            client.address = neighbour->second;
            addresses.push_back(client.address);
        }

        // The rate needs a reading before this one with a counter that did
        // not go backwards.
        const auto previous = counters_.find(station.hardware_address);
        if (had_reading && station.has_bytes && previous != counters_.end() && previous->second.has_bytes &&
            station.bytes >= previous->second.bytes && elapsed_ms > 0) {
            client.rate_kbps = rate_kbps(station.bytes - previous->second.bytes, elapsed_ms);
        }
        counters[station.hardware_address] = Counter{station.has_bytes, station.bytes};
        clients.push_back(std::move(client));
    }
    counters_ = std::move(counters);

    update_names(interface_name, addresses, now, may_ask_names);
    for (model::HomeClient& client : clients) {
        const auto name = names_.find(client.address);
        if (!client.address.empty() && name != names_.end()) {
            client.name = name->second.name;
        }
    }

    clients_ = std::move(clients);
    has_read_ = true;
    interface_name_ = interface_name;
    read_at_ = now;
}

void HotspotClients::update_names(const std::string& interface_name,
                                  const std::vector<std::string>& addresses,
                                  steady_clock::time_point now,
                                  bool may_ask) {
    if (test_folder_) {
        const auto names = read_host_names_file(root_ / "hostnames");
        names_.clear();
        for (const std::string& address : addresses) {
            const auto name = names.find(address);
            names_[address] = NameEntry{name != names.end() ? name->second : std::string(), now};
        }
        return;
    }

    // Forget the names of addresses no longer listed.
    for (auto entry = names_.begin(); entry != names_.end();) {
        if (std::find(addresses.begin(), addresses.end(), entry->first) == addresses.end()) {
            entry = names_.erase(entry);
        } else {
            ++entry;
        }
    }
    if (!may_ask) {
        return;
    }

    std::vector<std::string> wanted;
    for (const std::string& address : addresses) {
        const auto entry = names_.find(address);
        if (entry == names_.end() || (entry->second.name.empty() && now - entry->second.asked_at >= kNameRetry)) {
            wanted.push_back(address);
        }
    }
    if (wanted.empty()) {
        return;
    }
    const std::string server = interface_address(addresses_file_, interface_name);
    const auto answers = ask_host_names(server, wanted, kNameWait);
    for (const std::string& address : wanted) {
        const auto answer = answers.find(address);
        names_[address] = NameEntry{answer != answers.end() ? answer->second : std::string(), now};
    }
}

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "probe.h"

#include "dns_message.h"
#include "listen.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstdio>

namespace platform {
namespace {

std::string address_text(uint32_t address) {
    return std::to_string(address >> 24) + "." + std::to_string((address >> 16) & 0xffU) + "." +
           std::to_string((address >> 8) & 0xffU) + "." + std::to_string(address & 0xffU);
}

// "a.b.c.d" as a number (host byte order); false when it is not one.
bool parse_address(std::string_view text, uint32_t& out) {
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

int hex_digit(char character) {
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

// "aa:bb:cc:dd:ee:ff" into six bytes.
bool parse_hardware(std::string_view text, std::array<uint8_t, 6>& out) {
    if (text.size() != 17) {
        return false;
    }
    for (std::size_t index = 0; index < 6; ++index) {
        const int high = hex_digit(text[index * 3]);
        const int low = hex_digit(text[index * 3 + 1]);
        if (high < 0 || low < 0 || (index < 5 && text[index * 3 + 2] != ':')) {
            return false;
        }
        out[index] = static_cast<uint8_t>(high * 16 + low);
    }
    return true;
}

// A hex number such as "0x2"; -1 when it is not one.
long hex_number(std::string_view text) {
    if (text.size() < 3 || text.size() > 10 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) {
        return -1;
    }
    long value = 0;
    for (std::size_t index = 2; index < text.size(); ++index) {
        const int digit = hex_digit(text[index]);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

// The next word of `line` from `at`, words split by spaces and tabs.
std::string_view next_word(std::string_view line, std::size_t& at) {
    while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
        ++at;
    }
    const std::size_t start = at;
    while (at < line.size() && line[at] != ' ' && line[at] != '\t') {
        ++at;
    }
    return line.substr(start, at - start);
}

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xffU));
}

// Writes `name` as DNS labels; false for an empty label, one longer than
// 63, a character outside printable ASCII, or a name longer than
// dns::kMaxNameText.
bool put_name(std::vector<uint8_t>& out, std::string_view name) {
    if (name.empty() || name.size() > dns::kMaxNameText) {
        return false;
    }
    std::size_t at = 0;
    while (at <= name.size()) {
        const std::size_t end = std::min(name.find('.', at), name.size());
        const std::size_t length = end - at;
        if (length == 0 || length > 63) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(length));
        for (std::size_t index = at; index < end; ++index) {
            const auto character = static_cast<unsigned char>(name[index]);
            if (character <= 0x20 || character >= 0x7f) {
                return false;
            }
            out.push_back(character);
        }
        at = end + 1;
    }
    out.push_back(0);
    return true;
}

// ---------------------------------------------------------------------------
// The made-up probe. Its devices are those of the pretend capture (the same
// made-up, locally administered hardware addresses), plus a file server and
// a camera that never speak up while listening.

constexpr uint32_t kPretendNetwork = 0x0a000000u;  // 10.0.0.0/24
constexpr uint32_t kPretendOwnCable = kPretendNetwork | 12;
constexpr uint32_t kPretendOwnWifi = kPretendNetwork | 13;
// A device answers ARP this long after its echo went out, and its echo
// reply comes this long after.
constexpr uint32_t kPretendArpMs = 10;
constexpr uint32_t kPretendReplyMs = 4;
// The made-up answers to the questions come this long after the first one.
constexpr uint32_t kPretendAnswerMs = 400;

struct PretendDevice {
    uint8_t host;   // its address: 10.0.0.<host>
    uint8_t last;   // its hardware address: 02:1a:2b:3c:00:<last>
    bool replies;   // answers the echo (one with a firewall only answers ARP)
};

constexpr PretendDevice kPretendDevices[] = {
    {1, 0x01, true},    // the router
    {24, 0x24, true},   // the printer
    {31, 0x31, true},   // the TV
    {42, 0x42, false},  // the phone
    {50, 0x50, true},   // a file server, quiet while listening
    {57, 0x57, true},   // the speaker
    {77, 0x77, false},  // a camera, quiet while listening, that answers nothing else
};

std::array<uint8_t, 6> pretend_hardware(uint8_t last) {
    return {0x02, 0x1a, 0x2b, 0x3c, 0x00, last};
}

// The made-up neighbour table at `at_ms`: every device whose echo went out
// (index `index` at index * kProbeIntervalMs) long enough before, plus one
// address that never answered (flags 0x0) and one on the Cardputer's other
// network, both of which parse_neighbours() leaves out.
std::string pretend_table(const ProbeRange& range, uint32_t at_ms) {
    std::string text = "IP address       HW type     Flags       HW address            Mask     Device\n";
    for (const PretendDevice& device : kPretendDevices) {
        const uint32_t address = kPretendNetwork | device.host;
        const auto found = std::lower_bound(range.addresses.begin(), range.addresses.end(), address);
        if (found == range.addresses.end() || *found != address) {
            continue;
        }
        const auto index = static_cast<uint32_t>(found - range.addresses.begin());
        if (index * static_cast<uint32_t>(model::kProbeIntervalMs) + kPretendArpMs > at_ms) {
            continue;
        }
        char hardware[18];
        std::snprintf(hardware, sizeof(hardware), "02:1a:2b:3c:00:%02x", device.last);
        text += address_text(address) + "         0x1         0x2         " + hardware + "     *        eth0\n";
    }
    text += "10.0.0.66        0x1         0x0         00:00:00:00:00:00     *        eth0\n";
    text += "192.0.2.7        0x1         0x2         02:1a:2b:3c:00:07     *        wlan0\n";
    return text;
}

// A made-up mDNS response with one PTR record, `owner` PTR `target`,
// written as a responder answers a legacy unicast question.
std::vector<uint8_t> pretend_pointer_reply(std::string_view owner, std::string_view target) {
    std::vector<uint8_t> message;
    put16(message, 0x1234);  // the question's ID, repeated
    put16(message, 0x8400);  // a response, authoritative
    put16(message, 0);
    put16(message, 1);
    put16(message, 0);
    put16(message, 0);
    put_name(message, owner);
    put16(message, dns::kTypePtr);
    put16(message, dns::kClassIn);
    put16(message, 0);
    put16(message, 10);  // a time to live of 10 s, as legacy unicast answers have
    std::vector<uint8_t> data;
    put_name(data, target);
    put16(message, static_cast<uint16_t>(data.size()));
    message.insert(message.end(), data.begin(), data.end());
    return message;
}

} // namespace

bool private_network_address(uint32_t address) {
    return (address >> 24) == 10 || (address >> 20) == ((172u << 4) | 1u) || (address >> 16) == ((192u << 8) | 168u);
}

bool make_probe_range(uint32_t address,
                      uint32_t netmask,
                      const std::vector<uint32_t>& own,
                      ProbeRange& out,
                      std::string& reason) {
    out = ProbeRange{};
    if (!private_network_address(address)) {
        reason = "not a private network";
        return false;
    }
    int32_t prefix = 0;
    while (prefix < 32 && (netmask & (0x80000000u >> prefix)) != 0) {
        ++prefix;
    }
    if (prefix > 30) {
        reason = "network too small";
        return false;
    }
    prefix = std::max<int32_t>(prefix, 24);
    const uint32_t mask = 0xffffffffu << (32 - prefix);
    out.network = address & mask;
    out.prefix = prefix;
    const uint32_t broadcast = out.network | ~mask;
    for (uint32_t host = out.network + 1; host < broadcast; ++host) {
        if (std::find(own.begin(), own.end(), host) == own.end()) {
            out.addresses.push_back(host);
        }
    }
    out.text = address_text(out.network) + "/" + std::to_string(prefix);
    if (out.addresses.empty()) {
        reason = "nothing to ask";
        return false;
    }
    return true;
}

bool read_probe_range(const std::string& interface_name, ProbeRange& out, std::string& reason) {
    out = ProbeRange{};
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        reason = "no addresses";
        return false;
    }
    bool found = false;
    uint32_t address = 0;
    uint32_t netmask = 0;
    std::vector<uint32_t> own;
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        const uint32_t value = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
        own.push_back(value);
        if (!found && entry->ifa_name != nullptr && interface_name == entry->ifa_name && entry->ifa_netmask != nullptr &&
            entry->ifa_netmask->sa_family == AF_INET) {
            address = value;
            netmask = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask)->sin_addr.s_addr);
            found = true;
        }
    }
    ::freeifaddrs(list);
    if (!found) {
        reason = "no IPv4 address on " + interface_name;
        return false;
    }
    return make_probe_range(address, netmask, own, out, reason);
}

std::vector<Neighbour> parse_neighbours(std::string_view text, const ProbeRange& range) {
    constexpr long kComplete = 0x2;  // ATF_COM
    std::vector<Neighbour> found;
    std::size_t start = text.find('\n');  // the header line is skipped
    while (start != std::string_view::npos && start + 1 < text.size() &&
           found.size() < static_cast<std::size_t>(model::kProbeMaxAddresses)) {
        ++start;
        std::size_t end = text.find('\n', start);
        const std::string_view line = text.substr(start, (end == std::string_view::npos ? text.size() : end) - start);
        start = end;
        std::size_t at = 0;
        const std::string_view address_word = next_word(line, at);
        const std::string_view type_word = next_word(line, at);
        const std::string_view flags_word = next_word(line, at);
        const std::string_view hardware_word = next_word(line, at);
        Neighbour neighbour;
        const long flags = hex_number(flags_word);
        if (!parse_address(address_word, neighbour.address) || hex_number(type_word) != 1 || flags < 0 ||
            (flags & kComplete) == 0 || !parse_hardware(hardware_word, neighbour.hardware)) {
            continue;
        }
        const bool unicast = (neighbour.hardware[0] & 1U) == 0 &&
                             std::any_of(neighbour.hardware.begin(), neighbour.hardware.end(),
                                         [](uint8_t byte) { return byte != 0; });
        if (unicast && std::binary_search(range.addresses.begin(), range.addresses.end(), neighbour.address)) {
            found.push_back(neighbour);
        }
    }
    return found;
}

std::vector<uint8_t> mdns_query(std::string_view name, uint16_t type, uint16_t id) {
    std::vector<uint8_t> message;
    put16(message, id);
    put16(message, 0);  // a query, no flags (mDNS asks for no recursion)
    put16(message, 1);
    put16(message, 0);
    put16(message, 0);
    put16(message, 0);
    if (!put_name(message, name)) {
        return {};
    }
    put16(message, type);
    put16(message, dns::kClassIn);
    return message;
}

std::string ssdp_search() {
    return "M-SEARCH * HTTP/1.1\r\n"
           "HOST: 239.255.255.250:1900\r\n"
           "MAN: \"ssdp:discover\"\r\n"
           "MX: 2\r\n"
           "ST: ssdp:all\r\n"
           "\r\n";
}

model::ProbeRun pretend_probe(uint32_t elapsed_ms, const std::string& interface_name, uint32_t& sent, bool& done) {
    model::ProbeRun run;
    run.state = model::ToolState::Running;
    run.interface_name = interface_name;
    sent = 0;
    done = false;
    ProbeRange range;
    std::string reason;
    make_probe_range(kPretendOwnCable, 0xffffff00u, {kPretendOwnCable, kPretendOwnWifi}, range, reason);
    run.network = range.text;
    run.total = static_cast<int32_t>(range.addresses.size());

    // The sweep: one echo every kProbeIntervalMs, then the settle time.
    const auto interval = static_cast<uint32_t>(model::kProbeIntervalMs);
    const auto total = static_cast<uint32_t>(range.addresses.size());
    const uint32_t last_echo_ms = (total - 1) * interval;
    const uint32_t names_ms = last_echo_ms + kProbeSettleMs;
    run.asked = static_cast<int32_t>(std::min(total, elapsed_ms / interval + 1));
    sent = static_cast<uint32_t>(run.asked);
    for (const PretendDevice& device : kPretendDevices) {
        const auto found = std::lower_bound(range.addresses.begin(), range.addresses.end(), kPretendNetwork | device.host);
        const auto index = static_cast<uint32_t>(found - range.addresses.begin());
        if (device.replies && index * interval + kPretendArpMs + kPretendReplyMs <= elapsed_ms) {
            ++run.replied;
        }
    }

    // The neighbour table, read every kProbeTableReadMs during the sweep
    // and once more when it settled.
    HeardDevices heard(pretend_own_ports());
    const auto read_table = [&](uint32_t at_ms) {
        for (const Neighbour& neighbour : parse_neighbours(pretend_table(range, at_ms), range)) {
            heard.note_neighbour(neighbour.hardware.data(), neighbour.address, static_cast<int32_t>(at_ms / 1000));
        }
    };
    for (uint32_t at_ms = kProbeTableReadMs; at_ms < names_ms && at_ms <= elapsed_ms; at_ms += kProbeTableReadMs) {
        read_table(at_ms);
    }
    if (elapsed_ms < names_ms) {
        run.elapsed_s = static_cast<int32_t>(elapsed_ms / 1000);
        run.devices = heard.devices();
        return run;
    }
    read_table(names_ms);

    // The questions: the service enumeration, the SSDP search, then one
    // reverse question per device found without a name, each
    // kProbeIntervalMs after the one before.
    run.step = model::ProbeStep::Names;
    std::size_t unnamed = 0;
    for (const model::HeardDevice& device : heard.devices()) {
        if (device.name.empty() && device.own_port.empty() && !device.address.empty()) {
            ++unnamed;
        }
    }
    const auto questions = static_cast<uint32_t>(2 + unnamed);
    sent += std::min(questions, (elapsed_ms - names_ms) / interval + 1);
    const uint32_t end_ms = names_ms + (questions - 1) * interval + kProbeAnswerMs;
    if (elapsed_ms >= names_ms + kPretendAnswerMs) {
        // The file server offers file sharing and names itself; the router
        // answers the search as an internet gateway, under its product.
        const auto at_s = static_cast<int32_t>((names_ms + kPretendAnswerMs) / 1000);
        const uint32_t server = kPretendNetwork | 50;
        const std::array<uint8_t, 6> server_hardware = pretend_hardware(0x50);
        const std::vector<uint8_t> services = pretend_pointer_reply(kServiceEnumeration, "_smb._tcp.local");
        heard.note_mdns_reply(server_hardware.data(), server, services.data(), services.size(), at_s);
        const std::vector<uint8_t> name = pretend_pointer_reply(dns::reverse_name(address_text(server)), "nas.local");
        heard.note_mdns_reply(server_hardware.data(), server, name.data(), name.size(), at_s);
        const std::string reply = "HTTP/1.1 200 OK\r\n"
                                  "CACHE-CONTROL: max-age=1800\r\n"
                                  "EXT:\r\n"
                                  "LOCATION: http://10.0.0.1:5000/rootDesc.xml\r\n"
                                  "SERVER: Linux/5.4 UPnP/1.1 Router/2.3\r\n"
                                  "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
                                  "USN: uuid:00000000-0000-0000-0000-000000000001\r\n\r\n";
        const std::array<uint8_t, 6> router_hardware = pretend_hardware(0x01);
        heard.note_ssdp_reply(router_hardware.data(), kPretendNetwork | 1, reinterpret_cast<const uint8_t*>(reply.data()),
                              reply.size(), at_s);
    }
    run.devices = heard.devices();
    run.elapsed_s = static_cast<int32_t>(std::min(elapsed_ms, end_ms) / 1000);
    if (elapsed_ms >= end_ms) {
        run.state = model::ToolState::Done;
        done = true;
    }
    return run;
}

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "listen.h"

#include "dns_message.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

#if !USE_DESKTOP
#include <chrono>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#endif

#ifndef APP_CAPTURE_PATH
#define APP_CAPTURE_PATH "/usr/libexec/lokictl/lokictl-capture"
#endif

namespace platform {
namespace {

// The PCAP file header and record header (microsecond times, this
// machine's byte order) and the one link type read: Ethernet.
constexpr uint32_t kPcapMagic = 0xa1b2c3d4u;
constexpr std::size_t kPcapFileHeader = 24;
constexpr std::size_t kPcapRecordHeader = 16;
constexpr uint32_t kLinkEthernet = 1;

constexpr std::size_t kEthernetHeader = 14;
constexpr uint16_t kTypeArp = 0x0806;
constexpr uint16_t kTypeIpv4 = 0x0800;
constexpr std::size_t kArpSize = 28;      // Ethernet over IPv4
constexpr uint8_t kProtocolUdp = 17;
constexpr uint16_t kDhcpServerPort = 67;
constexpr uint16_t kDhcpClientPort = 68;
// BOOTP's fixed part, then the DHCP magic cookie and the options.
constexpr std::size_t kBootpFixed = 236;
constexpr uint32_t kDhcpCookie = 0x63825363u;
constexpr uint8_t kBootRequest = 1;
constexpr uint8_t kBootReply = 2;
constexpr uint8_t kOptionPad = 0;
constexpr uint8_t kOptionHostName = 12;
constexpr uint8_t kOptionRequestedAddress = 50;
constexpr uint8_t kOptionMessageType = 53;
constexpr uint8_t kOptionEnd = 255;
constexpr uint8_t kDhcpRequest = 3;
constexpr uint8_t kDhcpAck = 5;
constexpr std::size_t kMaxOwnPorts = 16;
constexpr uint16_t kMdnsPort = 5353;
constexpr uint16_t kSsdpPort = 1900;
// mDNS sets the top bit of a record's class to say "flush your cache".
constexpr uint16_t kClassMask = 0x7fff;
// An SSDP message is read for at most this many header lines.
constexpr int kSsdpMaxLines = 32;

uint16_t read16(const uint8_t* bytes) {
    return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
}

uint32_t read32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

uint32_t native32(const uint8_t* bytes) {
    uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

// A hardware address that can belong to one device: not a group address
// (lowest bit of the first byte) and not all zeros.
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

// An IPv4 address one device can have: not 0.0.0.0, not a multicast or
// reserved address (224.0.0.0 and up), not 127/8.
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

// A DHCP host name as shown: the first label, at most kHostNameMax
// characters, printable ASCII only (anything else becomes '?').
std::string host_name(const uint8_t* bytes, std::size_t size) {
    std::string name;
    for (std::size_t index = 0; index < size && name.size() < model::kHostNameMax; ++index) {
        const uint8_t character = bytes[index];
        if (character == '.' || character == 0) {
            break;
        }
        name.push_back(character >= 0x20 && character < 0x7f ? static_cast<char>(character) : '?');
    }
    return name;
}

std::string host_name(const std::string& text) {
    return host_name(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

// A kind as shown: at most kDeviceKindMax characters, printable ASCII only
// (anything else becomes '?').
std::string kind_text(std::string_view text) {
    std::string kind;
    for (const char value : text) {
        if (kind.size() >= model::kDeviceKindMax) {
            break;
        }
        const auto character = static_cast<unsigned char>(value);
        kind.push_back(character >= 0x20 && character < 0x7f ? value : '?');
    }
    return kind;
}

// The kinds known from mDNS service types, the surest first: a device
// that offers several is shown as the first one it offers.
struct ServiceKind {
    const char* service;
    const char* kind;
};

constexpr ServiceKind kServiceKinds[] = {
    {"_ipp._tcp", "printer"},
    {"_ipps._tcp", "printer"},
    {"_printer._tcp", "printer"},
    {"_pdl-datastream._tcp", "printer"},
    {"_uscan._tcp", "scanner"},
    {"_scanner._tcp", "scanner"},
    {"_googlecast._tcp", "Google Cast"},
    {"_airplay._tcp", "AirPlay"},
    {"_raop._tcp", "AirPlay"},
    {"_sonos._tcp", "Sonos"},
    {"_spotify-connect._tcp", "speaker"},
    {"_hap._tcp", "HomeKit"},
    {"_hap._udp", "HomeKit"},
    {"_matter._tcp", "Matter"},
    {"_matterc._udp", "Matter"},
    {"_esphomelib._tcp", "ESPHome"},
    {"_companion-link._tcp", "Apple device"},
    {"_workstation._tcp", "computer"},
    {"_smb._tcp", "file share"},
    {"_afpovertcp._tcp", "file share"},
    {"_nfs._tcp", "file share"},
    {"_ssh._tcp", "SSH"},
    {"_sftp-ssh._tcp", "SSH"},
    {"_mqtt._tcp", "MQTT"},
    {"_http._tcp", "web page"},
};

// The kinds known from SSDP NT values (UPnP device types, and DIAL, which
// TVs and streaming sticks offer), the surest first.
constexpr ServiceKind kSsdpKinds[] = {
    {":device:InternetGatewayDevice:", "router"},
    {":device:WLANAccessPointDevice:", "access point"},
    {":device:Printer:", "printer"},
    {":device:ZonePlayer:", "Sonos"},
    {":device:MediaRenderer:", "media player"},
    {":device:MediaServer:", "media server"},
    {"urn:dial-multiscreen-org:", "TV"},
};

// A kind's rank: the source's rank, then its place in its table (earlier
// is surer), so a kind is only ever replaced by a surer one.
int32_t kind_rank(int32_t source_rank, std::size_t index, std::size_t count) {
    return source_rank * 1000 + static_cast<int32_t>(count - index);
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// The DNS-SD service type a name ends in ("_ipp._tcp" for
// "office._ipp._tcp.local" and for "_ipp._tcp.local"), or empty.
std::string_view service_type(std::string_view name) {
    std::string_view protocol;
    if (ends_with(name, "._tcp.local")) {
        protocol = "._tcp";
    } else if (ends_with(name, "._udp.local")) {
        protocol = "._udp";
    } else {
        return {};
    }
    name.remove_suffix(std::string_view("._tcp.local").size());
    const std::size_t dot = name.rfind('.');
    const std::string_view service = dot == std::string_view::npos ? name : name.substr(dot + 1);
    if (service.size() < 2 || service[0] != '_') {
        return {};
    }
    // The service and its protocol, as they stand in `name`.
    const std::size_t start = static_cast<std::size_t>(service.data() - name.data());
    return std::string_view(name.data() + start, service.size() + protocol.size());
}

char lower(char character) {
    return character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character;
}

// Whether `text` starts with `prefix`, ignoring case.
bool starts_with_nocase(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (lower(text[index]) != lower(prefix[index])) {
            return false;
        }
    }
    return true;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// The product an SSDP SERVER header names after its "UPnP/1.x" part
// ("Speaker" in "Linux/5.10 UPnP/1.0 Speaker/2.1"), or empty.
std::string_view server_product(std::string_view server) {
    std::size_t at = 0;
    while (at < server.size()) {
        const std::size_t end = std::min(server.find(' ', at), server.size());
        const std::string_view token = server.substr(at, end - at);
        at = end + 1;
        if (!starts_with_nocase(token, "UPnP/")) {
            continue;
        }
        // The next token, up to its version.
        while (at < server.size() && server[at] == ' ') {
            ++at;
        }
        if (at >= server.size()) {
            return {};
        }
        std::string_view product = server.substr(at);
        product = product.substr(0, std::min({product.find('/'), product.find(' '), product.size()}));
        return product;
    }
    return {};
}

} // namespace

std::vector<OwnPort> read_own_ports(const std::string& root) {
    std::vector<OwnPort> ports;
    DIR* directory = opendir(root.c_str());
    if (!directory) {
        return ports;
    }
    while (const dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || name == "lo" || ports.size() >= kMaxOwnPorts) {
            continue;
        }
        std::ifstream file(root + "/" + name + "/address");
        std::string address;
        if (!(file >> address) || address.size() != 17 || address == "00:00:00:00:00:00") {
            continue;
        }
        std::transform(address.begin(), address.end(), address.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        ports.push_back({name, address});
    }
    closedir(directory);
    std::sort(ports.begin(), ports.end(),
              [](const OwnPort& left, const OwnPort& right) { return left.name < right.name; });
    return ports;
}

// ---------------------------------------------------------------------------
// CaptureReader

bool CaptureReader::feed(const uint8_t* data, std::size_t size, const FrameFn& on_frame) {
    if (broken_) {
        return false;
    }
    pending_.insert(pending_.end(), data, data + size);
    std::size_t used = 0;
    if (!header_read_) {
        if (pending_.size() < kPcapFileHeader) {
            return true;
        }
        const uint8_t* header = pending_.data();
        const bool magic_ok = native32(header) == kPcapMagic;
        uint16_t major = 0;
        uint16_t minor = 0;
        std::memcpy(&major, header + 4, sizeof(major));
        std::memcpy(&minor, header + 6, sizeof(minor));
        const uint32_t snap = native32(header + 16);
        const uint32_t link = native32(header + 20);
        if (!magic_ok || major != 2 || minor != 4 || snap == 0 || snap > kCaptureSnapLength ||
            link != kLinkEthernet) {
            broken_ = true;
            pending_.clear();
            return false;
        }
        header_read_ = true;
        used = kPcapFileHeader;
        if (keep_max_ >= kPcapFileHeader) {
            kept_.insert(kept_.end(), header, header + kPcapFileHeader);
        } else if (keep_max_ > 0) {
            kept_full_ = true;
        }
    }
    while (pending_.size() - used >= kPcapRecordHeader) {
        const uint8_t* record = pending_.data() + used;
        const uint32_t seconds = native32(record);
        const uint32_t captured = native32(record + 8);
        if (captured > kCaptureSnapLength) {
            broken_ = true;
            pending_.clear();
            return false;
        }
        if (pending_.size() - used - kPcapRecordHeader < captured) {
            break;  // the rest of the frame comes later
        }
        if (keep_max_ > 0 && !kept_full_) {
            // Whole records only, so what was kept is always a valid file.
            const std::size_t record_size = kPcapRecordHeader + captured;
            if (kept_.size() + record_size <= keep_max_) {
                kept_.insert(kept_.end(), record, record + record_size);
            } else {
                kept_full_ = true;
            }
        }
        on_frame(record + kPcapRecordHeader, captured, seconds);
        used += kPcapRecordHeader + captured;
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(used));
    return true;
}

// ---------------------------------------------------------------------------
// HeardDevices

HeardDevices::HeardDevices(std::vector<OwnPort> own_ports) : own_ports_(std::move(own_ports)) {}

bool HeardDevices::note_frame(const uint8_t* frame, std::size_t size, int32_t at_s) {
    ++frames_;
    if (size < kEthernetHeader) {
        return false;
    }
    const uint16_t type = read16(frame + 12);
    if (type == kTypeArp) {
        return note_arp(frame + kEthernetHeader, size - kEthernetHeader, at_s);
    }
    if (type == kTypeIpv4) {
        return note_ipv4(frame, size, at_s);
    }
    return false;
}

bool HeardDevices::note_arp(const uint8_t* arp, std::size_t size, int32_t at_s) {
    // Hardware type Ethernet (1), protocol IPv4, 6-byte and 4-byte
    // addresses, a request (1) or a reply (2).
    if (size < kArpSize || read16(arp) != 1 || read16(arp + 2) != kTypeIpv4 || arp[4] != 6 || arp[5] != 4) {
        return false;
    }
    const uint16_t operation = read16(arp + 6);
    if (operation != 1 && operation != 2) {
        return false;
    }
    const uint8_t* sender = arp + 8;
    Heard heard;
    heard.address = read32(arp + 14);
    return note(sender, heard, at_s);
}

bool HeardDevices::note_ipv4(const uint8_t* frame, std::size_t size, int32_t at_s) {
    const uint8_t* packet = frame + kEthernetHeader;
    std::size_t length = size - kEthernetHeader;
    if (length < 20 || (packet[0] >> 4) != 4) {
        return false;
    }
    const std::size_t header = static_cast<std::size_t>(packet[0] & 0x0fU) * 4;
    const std::size_t total = read16(packet + 2);
    if (header < 20 || header > length || total < header) {
        return false;
    }
    // Only the bytes of this packet count (a short frame is padded).
    length = std::min(length, total);
    // UDP only, and no fragments (no "more fragments" flag, offset 0).
    if (packet[9] != kProtocolUdp || (read16(packet + 6) & 0x3fffU) != 0) {
        return false;
    }
    const uint32_t source = read32(packet + 12);
    const uint8_t* udp = packet + header;
    const std::size_t udp_room = length - header;
    if (udp_room < 8) {
        return false;
    }
    const uint16_t source_port = read16(udp);
    const uint16_t destination_port = read16(udp + 2);
    const std::size_t udp_length = read16(udp + 4);
    if (udp_length < 8) {
        return false;
    }
    const std::size_t payload = std::min(udp_length, udp_room) - 8;
    const auto dhcp_port = [](uint16_t port) { return port == kDhcpServerPort || port == kDhcpClientPort; };
    if (dhcp_port(source_port) && dhcp_port(destination_port)) {
        return note_dhcp(udp + 8, payload, frame + 6, source, at_s);
    }
    if (destination_port == kMdnsPort) {
        // Only an answer sent from port 5353 is an mDNS response; anything
        // else to the port is a question.
        return note_mdns(udp + 8, payload, source_port == kMdnsPort, frame + 6, source, at_s);
    }
    if (destination_port == kSsdpPort) {
        return note_ssdp(udp + 8, payload, frame + 6, source, at_s);
    }
    return false;
}

bool HeardDevices::note_dhcp(const uint8_t* bootp,
                             std::size_t size,
                             const uint8_t* sender,
                             uint32_t source,
                             int32_t at_s) {
    // Hardware type Ethernet, a 6-byte address, and the magic cookie.
    if (size < kBootpFixed + 4 || bootp[1] != 1 || bootp[2] != 6 || read32(bootp + kBootpFixed) != kDhcpCookie) {
        return false;
    }
    const uint8_t operation = bootp[0];
    const uint32_t client_address = read32(bootp + 12);  // ciaddr
    const uint32_t your_address = read32(bootp + 16);    // yiaddr
    const uint8_t* client = bootp + 28;                  // chaddr

    uint8_t message = 0;
    uint32_t requested = 0;
    std::string name;
    // The options, read only while each is whole; one running past the end
    // ends the reading and keeps what came before it.
    std::size_t index = kBootpFixed + 4;
    while (index < size) {
        const uint8_t code = bootp[index];
        if (code == kOptionPad) {
            ++index;
            continue;
        }
        if (code == kOptionEnd || index + 1 >= size) {
            break;
        }
        const std::size_t length = bootp[index + 1];
        if (index + 2 + length > size) {
            break;
        }
        const uint8_t* value = bootp + index + 2;
        if (code == kOptionMessageType && length >= 1) {
            message = value[0];
        } else if (code == kOptionHostName && length >= 1) {
            name = host_name(value, length);
        } else if (code == kOptionRequestedAddress && length == 4) {
            requested = read32(value);
        }
        index += 2 + length;
    }

    bool changed = false;
    if (operation == kBootRequest) {
        Heard heard;
        heard.address = client_address;
        if (heard.address == 0 && message == kDhcpRequest) {
            heard.address = requested;
        }
        if (!name.empty()) {
            heard.name = name;
            heard.name_rank = kRankDhcp;
        }
        changed = note(client, heard, at_s);
    } else if (operation == kBootReply) {
        if (message == kDhcpAck && your_address != 0) {
            Heard heard;
            heard.address = your_address;
            changed = note(client, heard, at_s);
        }
        // The server (or the relay that passed the answer on).
        Heard server;
        server.address = source;
        if (note(sender, server, at_s)) {
            changed = true;
        }
    }
    return changed;
}

bool HeardDevices::note_mdns(const uint8_t* message,
                             std::size_t size,
                             bool response,
                             const uint8_t* sender,
                             uint32_t source,
                             int32_t at_s) {
    Heard heard;
    heard.address = source;
    dns::Message parsed;
    // A question only says that its sender is there. A response speaks for
    // its sender only: a name only from its own address record, which keeps
    // a sleep proxy answering for others from naming itself after them.
    if (response && dns::parse_all_records(message, size, parsed) && parsed.response) {
        constexpr std::size_t kServiceCount = sizeof(kServiceKinds) / sizeof(kServiceKinds[0]);
        std::size_t best = kServiceCount;
        const auto consider = [&best](std::string_view name) {
            const std::string_view service = service_type(name);
            for (std::size_t index = 0; index < best && !service.empty(); ++index) {
                if (service == kServiceKinds[index].service) {
                    best = index;
                    break;
                }
            }
        };
        for (const dns::Record& record : parsed.answers) {
            if ((record.klass & kClassMask) != dns::kClassIn) {
                continue;
            }
            if (record.type == dns::kTypeA && record.data_length == 4 && heard.name.empty() &&
                read32(message + record.data_offset) == source) {
                heard.name = host_name(record.name);
            }
            consider(record.name);
            if (record.type == dns::kTypePtr) {
                std::size_t offset = record.data_offset;
                std::string target;
                if (dns::read_name(message, size, offset, target) &&
                    offset <= record.data_offset + record.data_length) {
                    consider(target);
                }
            }
        }
        if (!heard.name.empty()) {
            heard.name_rank = kRankMdns;
        }
        if (best < kServiceCount) {
            heard.kind = kServiceKinds[best].kind;
            heard.kind_rank = kind_rank(kRankMdns, best, kServiceCount);
        }
    }
    return note(sender, heard, at_s);
}

bool HeardDevices::note_ssdp(const uint8_t* text,
                             std::size_t size,
                             const uint8_t* sender,
                             uint32_t source,
                             int32_t at_s) {
    Heard heard;
    heard.address = source;
    const std::string_view message(reinterpret_cast<const char*>(text), size);
    // Only an announcement says what the sender is; a search (M-SEARCH)
    // only says that it is there.
    if (starts_with_nocase(message, "NOTIFY ")) {
        constexpr std::size_t kTypeCount = sizeof(kSsdpKinds) / sizeof(kSsdpKinds[0]);
        std::string_view server;
        std::size_t at = message.find('\n');
        for (int lines = 0; lines < kSsdpMaxLines && at != std::string_view::npos && at + 1 < message.size();
             ++lines) {
            const std::size_t start = at + 1;
            at = message.find('\n', start);
            const std::size_t stop = at == std::string_view::npos ? message.size() : at;
            const std::string_view line = message.substr(start, stop - start);
            const std::size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                continue;
            }
            const std::string_view name = trimmed(line.substr(0, colon));
            const std::string_view value = trimmed(line.substr(colon + 1));
            if (name.size() == 2 && starts_with_nocase(name, "NT")) {
                for (std::size_t index = 0; index < kTypeCount; ++index) {
                    if (value.find(kSsdpKinds[index].service) != std::string_view::npos) {
                        heard.kind = kSsdpKinds[index].kind;
                        heard.kind_rank = kind_rank(kRankSsdpType, index, kTypeCount);
                        break;
                    }
                }
            } else if (name.size() == 6 && starts_with_nocase(name, "SERVER")) {
                server = value;
            }
        }
        if (heard.kind.empty()) {
            const std::string kind = kind_text(server_product(server));
            if (!kind.empty()) {
                heard.kind = kind;
                heard.kind_rank = kind_rank(kRankSsdpServer, 0, 1);
            }
        }
    }
    return note(sender, heard, at_s);
}

bool HeardDevices::note(const uint8_t* hardware, const Heard& heard, int32_t at_s) {
    if (!unicast_hardware(hardware)) {
        return false;
    }
    auto found = std::find_if(entries_.begin(), entries_.end(),
                              [hardware](const Entry& entry) { return std::memcmp(entry.hardware, hardware, 6) == 0; });
    if (found == entries_.end()) {
        if (entries_.size() >= model::kListenMaxDevices) {
            return false;
        }
        Entry entry;
        std::memcpy(entry.hardware, hardware, 6);
        const std::string text = hardware_text(hardware);
        for (const OwnPort& port : own_ports_) {
            if (port.hardware_address == text) {
                entry.own_port = port.name;
                break;
            }
        }
        entries_.push_back(entry);
        found = entries_.end() - 1;
    }
    bool changed = false;
    if (device_address(heard.address) && found->address != heard.address) {
        found->address = heard.address;
        changed = true;
    }
    // A name from as sure a source replaces it (a device may rename
    // itself); a kind only from a surer one.
    if (!heard.name.empty() && heard.name_rank >= found->name_rank && found->name != heard.name) {
        found->name = heard.name;
        found->name_rank = heard.name_rank;
        changed = true;
    }
    if (!heard.kind.empty() && heard.kind_rank > found->kind_rank) {
        found->kind = heard.kind;
        found->kind_rank = heard.kind_rank;
        changed = true;
    }
    if (found->heard_s != at_s || changed || devices_.size() != entries_.size()) {
        found->heard_s = at_s;
        rebuild();
        return true;
    }
    return false;
}

void HeardDevices::rebuild() {
    std::vector<const Entry*> order;
    order.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        order.push_back(&entry);
    }
    std::sort(order.begin(), order.end(), [](const Entry* left, const Entry* right) {
        if ((left->address == 0) != (right->address == 0)) {
            return left->address != 0;
        }
        if (left->address != right->address) {
            return left->address < right->address;
        }
        return std::memcmp(left->hardware, right->hardware, 6) < 0;
    });
    devices_.clear();
    for (const Entry* entry : order) {
        model::HeardDevice device;
        device.hardware_address = hardware_text(entry->hardware);
        device.address = entry->address != 0 ? address_text(entry->address) : std::string();
        device.name = entry->name;
        device.kind = entry->kind;
        device.own_port = entry->own_port;
        device.heard_s = entry->heard_s;
        devices_.push_back(std::move(device));
    }
}

// ---------------------------------------------------------------------------
// The pretend capture.

namespace {

using Hardware = std::array<uint8_t, 6>;

constexpr Hardware kPretendCable{0x02, 0x4c, 0x4b, 0x00, 0x00, 0x01};  // home_examples' cable port
constexpr Hardware kPretendWifi{0x02, 0x4c, 0x4b, 0x00, 0x00, 0x02};   // and its Wi-Fi port
constexpr Hardware kPretendRouter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x01};
constexpr Hardware kPretendTv{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x31};
constexpr Hardware kPretendPrinter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x24};
constexpr Hardware kPretendPhone{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x42};
constexpr Hardware kPretendSpeaker{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x57};
constexpr Hardware kBroadcast{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr Hardware kMdnsGroup{0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb};
constexpr Hardware kSsdpGroup{0x01, 0x00, 0x5e, 0x7f, 0xff, 0xfa};

constexpr uint32_t ipv4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// The router repeats its ARP question this often.
constexpr uint32_t kPretendRouterPeriodMs = 10000;

void put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xffU));
}

void put32(std::vector<uint8_t>& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value >> 16));
    put16(out, static_cast<uint16_t>(value & 0xffffU));
}

void put_native32(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t bytes[4];
    std::memcpy(bytes, &value, sizeof(bytes));
    out.insert(out.end(), bytes, bytes + 4);
}

void put_hardware(std::vector<uint8_t>& out, const Hardware& hardware) {
    out.insert(out.end(), hardware.begin(), hardware.end());
}

std::vector<uint8_t> ethernet(const Hardware& to, const Hardware& from, uint16_t type) {
    std::vector<uint8_t> frame;
    put_hardware(frame, to);
    put_hardware(frame, from);
    put16(frame, type);
    return frame;
}

std::vector<uint8_t> arp_frame(const Hardware& sender, uint32_t sender_address, uint32_t target_address) {
    std::vector<uint8_t> frame = ethernet(kBroadcast, sender, kTypeArp);
    put16(frame, 1);
    put16(frame, kTypeIpv4);
    frame.push_back(6);
    frame.push_back(4);
    put16(frame, 1);
    put_hardware(frame, sender);
    put32(frame, sender_address);
    put_hardware(frame, Hardware{});
    put32(frame, target_address);
    frame.resize(60, 0);  // padded to Ethernet's shortest frame
    return frame;
}

// An Ethernet frame with an IPv4 UDP packet carrying `payload`.
std::vector<uint8_t> udp_frame(const Hardware& to,
                               const Hardware& from,
                               uint32_t source,
                               uint32_t destination,
                               uint16_t source_port,
                               uint16_t destination_port,
                               const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame = ethernet(to, from, kTypeIpv4);
    const auto udp_length = static_cast<uint16_t>(8 + payload.size());
    frame.push_back(0x45);
    frame.push_back(0);
    put16(frame, static_cast<uint16_t>(20 + udp_length));
    put32(frame, 0);  // id, flags, fragment offset
    frame.push_back(64);
    frame.push_back(kProtocolUdp);
    put16(frame, 0);  // checksum (not checked)
    put32(frame, source);
    put32(frame, destination);
    put16(frame, source_port);
    put16(frame, destination_port);
    put16(frame, udp_length);
    put16(frame, 0);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

struct PretendDhcp {
    uint8_t operation{kBootRequest};
    uint8_t message{kDhcpRequest};
    Hardware client{};
    Hardware sender{};
    uint32_t source{0};
    uint32_t your_address{0};
    uint32_t requested{0};
    std::string name;
    bool bad_option{false};  // ends with an option that runs past the packet
};

std::vector<uint8_t> dhcp_frame(const PretendDhcp& dhcp) {
    std::vector<uint8_t> bootp;
    bootp.push_back(dhcp.operation);
    bootp.push_back(1);
    bootp.push_back(6);
    bootp.push_back(0);
    put32(bootp, 0x5eed0000u | dhcp.client[5]);  // xid
    put16(bootp, 0);
    put16(bootp, 0x8000);  // broadcast
    put32(bootp, 0);       // ciaddr
    put32(bootp, dhcp.your_address);
    put32(bootp, 0);
    put32(bootp, 0);
    put_hardware(bootp, dhcp.client);
    bootp.resize(kBootpFixed, 0);
    put32(bootp, kDhcpCookie);
    bootp.push_back(kOptionMessageType);
    bootp.push_back(1);
    bootp.push_back(dhcp.message);
    if (dhcp.requested != 0) {
        bootp.push_back(kOptionRequestedAddress);
        bootp.push_back(4);
        put32(bootp, dhcp.requested);
    }
    if (!dhcp.name.empty()) {
        bootp.push_back(kOptionHostName);
        bootp.push_back(static_cast<uint8_t>(dhcp.name.size()));
        bootp.insert(bootp.end(), dhcp.name.begin(), dhcp.name.end());
    }
    if (dhcp.bad_option) {
        bootp.push_back(kOptionHostName);
        bootp.push_back(200);  // far more than is left
        bootp.push_back('x');
    } else {
        bootp.push_back(kOptionEnd);
    }

    const bool from_server = dhcp.operation == kBootReply;
    return udp_frame(kBroadcast, dhcp.sender, dhcp.source, 0xffffffffu, from_server ? kDhcpServerPort : kDhcpClientPort,
                     from_server ? kDhcpClientPort : kDhcpServerPort, bootp);
}

// A made-up mDNS response from `sender` at `source`, as a real responder
// writes one: names compressed, address records with the cache-flush bit.
//   answer:     <service>.local PTR <instance>.<service>.local (shared)
//   additional: <host>.local A <source> (cache flush),
//               <instance>.<service>.local TXT "v=1" (cache flush)
// `service` is "_x._tcp", `host` and `instance` one label each.
std::vector<uint8_t> mdns_answer(const Hardware& sender,
                                 uint32_t source,
                                 const std::string& service,
                                 const std::string& instance,
                                 const std::string& host) {
    const auto label = [](std::vector<uint8_t>& out, const std::string& text) {
        out.push_back(static_cast<uint8_t>(text.size()));
        out.insert(out.end(), text.begin(), text.end());
    };
    std::vector<uint8_t> dns;
    put16(dns, 0);       // id
    put16(dns, 0x8400);  // a response, authoritative
    put16(dns, 0);       // questions
    put16(dns, 1);       // answers
    put16(dns, 0);       // authority
    put16(dns, 2);       // additional
    // <service>.local: the service's two labels, then "local".
    const std::size_t service_at = dns.size();
    const std::size_t dot = service.find('.');
    label(dns, service.substr(0, dot));
    label(dns, service.substr(dot + 1));
    const std::size_t local_at = dns.size();
    label(dns, "local");
    dns.push_back(0);
    put16(dns, 12);  // PTR
    put16(dns, 1);   // IN, shared
    put32(dns, 4500);
    put16(dns, static_cast<uint16_t>(1 + instance.size() + 2));
    const std::size_t instance_at = dns.size();
    label(dns, instance);
    put16(dns, static_cast<uint16_t>(0xc000 | service_at));
    // <host>.local A <source>
    label(dns, host);
    put16(dns, static_cast<uint16_t>(0xc000 | local_at));
    put16(dns, 1);       // A
    put16(dns, 0x8001);  // IN, cache flush
    put32(dns, 120);
    put16(dns, 4);
    put32(dns, source);
    // <instance>.<service>.local TXT "v=1"
    put16(dns, static_cast<uint16_t>(0xc000 | instance_at));
    put16(dns, 16);  // TXT
    put16(dns, 0x8001);
    put32(dns, 4500);
    put16(dns, 4);
    dns.push_back(3);
    dns.push_back('v');
    dns.push_back('=');
    dns.push_back('1');
    return udp_frame(kMdnsGroup, sender, source, ipv4(224, 0, 0, 251), kMdnsPort, kMdnsPort, dns);
}

// A made-up mDNS question for `service` (from port 5353): it names a
// service, but says nothing about its sender.
std::vector<uint8_t> mdns_question(const Hardware& sender, uint32_t source, const std::string& service) {
    std::vector<uint8_t> dns;
    put16(dns, 0);
    put16(dns, 0);  // a query
    put16(dns, 1);
    put16(dns, 0);
    put16(dns, 0);
    put16(dns, 0);
    std::size_t at = 0;
    const std::string name = service + ".local";
    while (at <= name.size()) {
        const std::size_t end = std::min(name.find('.', at), name.size());
        dns.push_back(static_cast<uint8_t>(end - at));
        dns.insert(dns.end(), name.begin() + static_cast<std::ptrdiff_t>(at),
                   name.begin() + static_cast<std::ptrdiff_t>(end));
        at = end + 1;
    }
    dns.push_back(0);
    put16(dns, 12);  // PTR
    put16(dns, 1);
    return udp_frame(kMdnsGroup, sender, source, ipv4(224, 0, 0, 251), kMdnsPort, kMdnsPort, dns);
}

// A made-up SSDP announcement.
std::vector<uint8_t> ssdp_notify(const Hardware& sender,
                                 uint32_t source,
                                 const std::string& nt,
                                 const std::string& server) {
    const std::string text = "NOTIFY * HTTP/1.1\r\n"
                             "HOST: 239.255.255.250:1900\r\n"
                             "CACHE-CONTROL: max-age=1800\r\n"
                             "NT: " + nt + "\r\n"
                             "NTS: ssdp:alive\r\n"
                             "Server: " + server + "\r\n"
                             "USN: uuid:00000000-0000-0000-0000-000000000000::" + nt + "\r\n\r\n";
    const std::vector<uint8_t> payload(text.begin(), text.end());
    return udp_frame(kSsdpGroup, sender, source, ipv4(239, 255, 255, 250), 50000, kSsdpPort, payload);
}

struct PretendFrame {
    uint32_t at_ms;
    std::vector<uint8_t> bytes;
};

const std::vector<PretendFrame>& pretend_frames() {
    static const std::vector<PretendFrame> frames = [] {
        const uint32_t router = ipv4(10, 0, 0, 1);
        std::vector<PretendFrame> list;
        // This device (the cable port) asks for the router; the router asks
        // back.
        list.push_back({150, arp_frame(kPretendCable, ipv4(10, 0, 0, 12), router)});
        list.push_back({300, arp_frame(kPretendRouter, router, ipv4(10, 0, 0, 12))});
        list.push_back({900, arp_frame(kPretendTv, ipv4(10, 0, 0, 31), router)});
        // A printer renews its address, and the router agrees.
        PretendDhcp printer;
        printer.client = kPretendPrinter;
        printer.sender = kPretendPrinter;
        printer.requested = ipv4(10, 0, 0, 24);
        printer.name = "printer";
        list.push_back({1400, dhcp_frame(printer)});
        PretendDhcp ack;
        ack.operation = kBootReply;
        ack.message = kDhcpAck;
        ack.client = kPretendPrinter;
        ack.sender = kPretendRouter;
        ack.source = router;
        ack.your_address = ipv4(10, 0, 0, 24);
        list.push_back({1500, dhcp_frame(ack)});
        // The printer announces its web page, then printing: the printer
        // wins, and its DHCP name stays.
        const uint32_t printer_address = ipv4(10, 0, 0, 24);
        list.push_back({1700, mdns_answer(kPretendPrinter, printer_address, "_http._tcp", "office", "office-printer")});
        list.push_back({1800, mdns_answer(kPretendPrinter, printer_address, "_ipp._tcp", "office", "office-printer")});
        // A frame cut short in the middle of its ARP part: ignored.
        std::vector<uint8_t> cut = arp_frame(kPretendSpeaker, ipv4(10, 0, 0, 99), router);
        cut.resize(kEthernetHeader + 10);
        list.push_back({2100, cut});
        // The TV answers for Google Cast; the copy that comes cut short in
        // its last record still gives the name and the kind.
        std::vector<uint8_t> tv =
            mdns_answer(kPretendTv, ipv4(10, 0, 0, 31), "_googlecast._tcp", "tv", "living-room-tv");
        tv.resize(tv.size() - 6);
        list.push_back({2300, tv});
        // This device's Wi-Fi port, on the same network.
        list.push_back({2600, arp_frame(kPretendWifi, ipv4(10, 0, 0, 13), router)});
        // A phone joins: it looks for a server (its name has a domain and
        // a broken last option), then asks for its address.
        PretendDhcp discover;
        discover.message = 1;
        discover.client = kPretendPhone;
        discover.sender = kPretendPhone;
        discover.name = "pixel-7.lan";
        discover.bad_option = true;
        list.push_back({3200, dhcp_frame(discover)});
        PretendDhcp request;
        request.client = kPretendPhone;
        request.sender = kPretendPhone;
        request.requested = ipv4(10, 0, 0, 42);
        request.name = "pixel-7";
        list.push_back({3300, dhcp_frame(request)});
        // The phone looks for something to cast to: a question, no kind.
        list.push_back({3400, mdns_question(kPretendPhone, ipv4(10, 0, 0, 42), "_googlecast._tcp")});

        // A speaker checks that an address is free (a probe, no address),
        // then takes it.
        list.push_back({4000, arp_frame(kPretendSpeaker, 0, ipv4(10, 0, 0, 57))});
        // The router announces itself over SSDP.
        list.push_back({5000, ssdp_notify(kPretendRouter, router, "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                          "Linux/5.4 UPnP/1.1 Router/2.3")});
        list.push_back({7000, arp_frame(kPretendSpeaker, ipv4(10, 0, 0, 57), ipv4(10, 0, 0, 57))});
        // The speaker's root device names only its product; its media
        // renderer then says what it is.
        list.push_back({7200, ssdp_notify(kPretendSpeaker, ipv4(10, 0, 0, 57), "upnp:rootdevice",
                                          "Linux/5.10 UPnP/1.0 Speaker/2.1")});
        list.push_back({7300, ssdp_notify(kPretendSpeaker, ipv4(10, 0, 0, 57),
                                          "urn:schemas-upnp-org:device:MediaRenderer:1",
                                          "Linux/5.10 UPnP/1.0 Speaker/2.1")});
        return list;
    }();
    return frames;
}

void put_record(std::vector<uint8_t>& out, uint32_t at_ms, const std::vector<uint8_t>& frame) {
    put_native32(out, at_ms / 1000);
    put_native32(out, (at_ms % 1000) * 1000);
    put_native32(out, static_cast<uint32_t>(frame.size()));
    put_native32(out, static_cast<uint32_t>(frame.size()));
    out.insert(out.end(), frame.begin(), frame.end());
}

} // namespace

std::vector<uint8_t> pretend_capture(uint32_t elapsed_ms) {
    std::vector<uint8_t> out;
    put_native32(out, kPcapMagic);
    const uint16_t version[2] = {2, 4};
    uint8_t version_bytes[4];
    std::memcpy(version_bytes, version, sizeof(version_bytes));
    out.insert(out.end(), version_bytes, version_bytes + 4);
    put_native32(out, 0);
    put_native32(out, 0);
    put_native32(out, kCaptureSnapLength);
    put_native32(out, kLinkEthernet);

    // The fixed frames and the router's repeated question, in time order.
    const std::vector<PretendFrame>& frames = pretend_frames();
    std::size_t next = 0;
    uint32_t router_at = kPretendRouterPeriodMs + 300;
    const std::vector<uint8_t> router_question = arp_frame(kPretendRouter, ipv4(10, 0, 0, 1), ipv4(10, 0, 0, 31));
    while (true) {
        const bool fixed_left = next < frames.size() && frames[next].at_ms <= elapsed_ms;
        const bool router_left = router_at <= elapsed_ms;
        if (!fixed_left && !router_left) {
            break;
        }
        if (fixed_left && (!router_left || frames[next].at_ms <= router_at)) {
            put_record(out, frames[next].at_ms, frames[next].bytes);
            ++next;
        } else {
            put_record(out, router_at, router_question);
            router_at += kPretendRouterPeriodMs;
        }
    }
    return out;
}

std::vector<OwnPort> pretend_own_ports() {
    return {{"eth0", hardware_text(kPretendCable.data())}, {"wlan0", hardware_text(kPretendWifi.data())}};
}

// ---------------------------------------------------------------------------
// Saved captures.

std::string capture_folder() {
    std::string base;
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && data[0] == '/') {
        base = data;
    } else {
        std::string home;
        if (const char* value = std::getenv("HOME"); value && value[0] == '/') {
            home = value;
        } else if (const passwd* user = getpwuid(getuid()); user && user->pw_dir && user->pw_dir[0] == '/') {
            home = user->pw_dir;
        }
        if (home.empty()) {
            return {};
        }
        base = home + "/.local/share";
    }
    return base + "/lokictl/captures";
}

CaptureFile::~CaptureFile() {
    close();
}

bool CaptureFile::create(const std::string& folder, std::string& reason) {
    close();
    if (folder.empty()) {
        reason = "no home folder";
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error || ::chmod(folder.c_str(), 0700) != 0) {
        reason = "no folder for captures";
        return false;
    }
    char stamp[32] = {};
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (!localtime_r(&now, &local) || std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local) == 0) {
        reason = "no clock";
        return false;
    }
    for (int copy = 1; copy <= 9; ++copy) {
        const std::string name = std::string(stamp) + (copy > 1 ? "-" + std::to_string(copy) : std::string()) + ".pcap";
        const std::string path = folder + "/" + name;
        // A new file only: never an existing one, never through a link.
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            fd_ = fd;
            name_ = name;
            bytes_ = 0;
            return true;
        }
        if (errno != EEXIST) {
            reason = errno == ENOSPC ? "disk full" : "could not create the file";
            return false;
        }
    }
    reason = "file name taken";
    return false;
}

bool CaptureFile::append(const uint8_t* data, std::size_t size, std::string& reason) {
    if (fd_ < 0) {
        reason = "file not open";
        return false;
    }
    while (size > 0) {
        const ssize_t written = ::write(fd_, data, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            reason = written < 0 && errno == ENOSPC ? "disk full" : "could not write the file";
            close();
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
        bytes_ += static_cast<uint64_t>(written);
    }
    return true;
}

void CaptureFile::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ---------------------------------------------------------------------------
// The capture helper (device builds).

#if !USE_DESKTOP

namespace {

constexpr auto kReapStep = std::chrono::milliseconds(10);

} // namespace

CaptureProcess::~CaptureProcess() {
    stop();
}

bool CaptureProcess::start(const std::string& interface_name,
                           int32_t seconds,
                           model::ToolError& error,
                           std::string& reason) {
    if (pid_ > 0 || !model::valid_interface_name(interface_name)) {
        error = model::ToolError::Failed;
        reason = "interface not usable";
        return false;
    }
    int ends[2] = {-1, -1};
    if (pipe2(ends, O_CLOEXEC) != 0) {
        error = model::ToolError::Failed;
        reason = "no pipe";
        return false;
    }
    // Only this end stays here; it never blocks the runner's loop.
    fcntl(ends[0], F_SETFL, fcntl(ends[0], F_GETFL) | O_NONBLOCK);

    std::string words[] = {"lokictl-capture", "discover", interface_name, std::to_string(seconds)};
    char* argv[] = {words[0].data(), words[1].data(), words[2].data(), words[3].data(), nullptr};
    char* environment[] = {nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attributes);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    // dup2 clears close-on-exec on the copy, so only fd 1 reaches the
    // helper; both ends of the pipe themselves are closed on exec.
    posix_spawn_file_actions_adddup2(&actions, ends[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    sigset_t no_signals;
    sigset_t default_signals;
    sigemptyset(&no_signals);
    sigemptyset(&default_signals);
    sigaddset(&default_signals, SIGTERM);
    sigaddset(&default_signals, SIGINT);
    sigaddset(&default_signals, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    posix_spawnattr_setsigdefault(&attributes, &default_signals);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    pid_t child = -1;
    const int spawned = posix_spawn(&child, APP_CAPTURE_PATH, &actions, &attributes, argv, environment);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(ends[1]);
    if (spawned != 0) {
        close(ends[0]);
        if (spawned == EACCES || spawned == EPERM) {
            error = model::ToolError::NotAllowed;
            reason.clear();
        } else {
            error = model::ToolError::Failed;
            reason = spawned == ENOENT ? "capture helper not installed" : "capture helper could not start";
        }
        return false;
    }
    fd_ = ends[0];
    pid_ = child;
    return true;
}

void CaptureProcess::close_pipe() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

int CaptureProcess::reap(int timeout_ms) {
    if (pid_ <= 0) {
        return -1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(pid_, &status, WNOHANG);
        if (done == pid_) {
            break;
        }
        if (done < 0 && errno != EINTR) {
            pid_ = -1;
            return -1;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid_, SIGKILL);
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
            }
            pid_ = -1;
            return -1;
        }
        std::this_thread::sleep_for(kReapStep);
    }
    pid_ = -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void CaptureProcess::stop() {
    close_pipe();
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
        reap(100);
    }
}

int CaptureProcess::finish() {
    close_pipe();
    return reap(500);
}

bool capture_exit_ok(int status, model::ToolError& error, std::string& reason) {
    switch (status) {
        case 0:
            return true;
        case 2:
            error = model::ToolError::Failed;
            reason = "interface not usable";
            return false;
        case 3:
            error = model::ToolError::NotAllowed;
            reason.clear();
            return false;
        case 4:
            error = model::ToolError::Failed;
            reason = "capture could not start";
            return false;
        default:
            error = model::ToolError::Failed;
            reason = "capture helper ended";
            return false;
    }
}

#endif

} // namespace platform

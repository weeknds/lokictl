/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "wireguard.h"

#include "home_data.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <system_error>

namespace platform {
namespace {

constexpr std::size_t kKeyLength = 44; // base64 of 32 bytes, with one '=' at the end
constexpr std::size_t kMaxNameBytes = 64;
constexpr std::size_t kMaxHostBytes = 253;
constexpr std::string_view kSuffix = ".conf";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string lowercase(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

// The comma-separated items of `value`, trimmed; empty items are dropped.
std::vector<std::string_view> split_list(std::string_view value) {
    std::vector<std::string_view> items;
    while (!value.empty()) {
        const auto comma = value.find(',');
        const std::string_view item = trim(value.substr(0, comma));
        if (!item.empty()) {
            items.push_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1);
    }
    return items;
}

// A whole number from `low` to `high`, in decimal (or in hex after "0x"
// when `hex` is true).
bool parse_number(std::string_view text, uint64_t low, uint64_t high, uint64_t& value, bool hex = false) {
    int base = 10;
    if (hex && text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 10) {
        return false;
    }
    uint64_t result = 0;
    for (const char character : text) {
        int digit = -1;
        if (character >= '0' && character <= '9') {
            digit = character - '0';
        } else if (base == 16 && std::isxdigit(static_cast<unsigned char>(character)) != 0) {
            digit = std::tolower(static_cast<unsigned char>(character)) - 'a' + 10;
        }
        if (digit < 0) {
            return false;
        }
        result = result * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    if (result < low || result > high) {
        return false;
    }
    value = result;
    return true;
}

bool is_ipv4(const std::string& text) {
    in_addr address{};
    return inet_pton(AF_INET, text.c_str(), &address) == 1;
}

bool is_ipv6(const std::string& text) {
    in6_addr address{};
    return inet_pton(AF_INET6, text.c_str(), &address) == 1;
}

// "address" or "address/prefix", IPv4 or IPv6.
bool parse_address(std::string_view text, WireGuardAddress& result) {
    const auto slash = text.find('/');
    WireGuardAddress address;
    address.address = std::string(trim(text.substr(0, slash)));
    if (is_ipv4(address.address)) {
        address.prefix = 32;
    } else if (is_ipv6(address.address)) {
        address.ipv6 = true;
        address.prefix = 128;
    } else {
        return false;
    }
    if (slash != std::string_view::npos) {
        uint64_t prefix = 0;
        if (!parse_number(trim(text.substr(slash + 1)), 0, address.ipv6 ? 128 : 32, prefix)) {
            return false;
        }
        address.prefix = static_cast<int32_t>(prefix);
    }
    result = std::move(address);
    return true;
}

bool parse_address_list(std::string_view value, std::vector<WireGuardAddress>& list) {
    for (const std::string_view item : split_list(value)) {
        WireGuardAddress address;
        if (!parse_address(item, address)) {
            return false;
        }
        list.push_back(std::move(address));
    }
    return true;
}

bool valid_host_name(std::string_view host) {
    if (host.empty() || host.size() > kMaxHostBytes) {
        return false;
    }
    return std::all_of(host.begin(), host.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '.' || character == '-';
    });
}

// "host:port" or "[v6]:port"; the host is an IPv4 address, a name or (in
// brackets) an IPv6 address.
bool parse_endpoint(std::string_view value, WireGuardPeer& peer) {
    const auto colon = value.rfind(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    uint64_t port = 0;
    if (!parse_number(value.substr(colon + 1), 1, 65535, port)) {
        return false;
    }
    std::string_view host = value.substr(0, colon);
    std::string text;
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
        if (!is_ipv6(std::string(host))) {
            return false;
        }
        text = "[" + std::string(host) + "]";
    } else {
        if (!valid_host_name(host)) {
            return false;
        }
        text = std::string(host);
    }
    peer.endpoint = text + ":" + std::to_string(port);
    peer.endpoint_host = std::string(host);
    return true;
}

void wipe(std::string& text) {
    std::fill(text.begin(), text.end(), '\0');
    text.clear();
}

int base64_value(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

} // namespace

bool valid_wireguard_key(std::string_view key) {
    if (key.size() != kKeyLength || key.back() != '=') {
        return false;
    }
    for (std::size_t index = 0; index + 1 < kKeyLength; ++index) {
        if (base64_value(key[index]) < 0) {
            return false;
        }
    }
    // 43 characters carry 258 bits; the last two must be zero for 32 bytes.
    return (base64_value(key[kKeyLength - 2]) & 0x3) == 0;
}

bool routes_everything(const WireGuardConfig& config) {
    for (const WireGuardPeer& peer : config.peers) {
        for (const WireGuardAddress& allowed : peer.allowed_ips) {
            if (allowed.prefix == 0) {
                return true;
            }
        }
    }
    return false;
}

void clear_wireguard_config(WireGuardConfig& config) {
    wipe(config.private_key);
    for (WireGuardPeer& peer : config.peers) {
        wipe(peer.public_key);
        wipe(peer.preshared_key);
    }
    config = WireGuardConfig{};
}

std::string parse_wireguard_config(std::string_view text, WireGuardConfig& config) {
    enum class Section { None, Interface, Peer };
    WireGuardConfig result;
    Section section = Section::None;
    bool has_interface = false;
    std::string reason;
    int line_number = 0;

    while (!text.empty() && reason.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++line_number;

        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            const std::string name = lowercase(line);
            if (name == "[interface]") {
                if (has_interface) {
                    reason = "two [Interface]";
                }
                has_interface = true;
                section = Section::Interface;
            } else if (name == "[peer]") {
                result.peers.emplace_back();
                section = Section::Peer;
            } else {
                reason = "unknown section at line " + std::to_string(line_number);
            }
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || section == Section::None) {
            reason = "bad line " + std::to_string(line_number);
            continue;
        }
        const std::string key = lowercase(trim(line.substr(0, equals)));
        const std::string_view value = trim(line.substr(equals + 1));
        uint64_t number = 0;

        if (section == Section::Interface) {
            if (key == "privatekey") {
                if (!valid_wireguard_key(value)) {
                    reason = "bad PrivateKey";
                } else {
                    result.private_key = std::string(value);
                }
            } else if (key == "address") {
                if (!parse_address_list(value, result.addresses)) {
                    reason = "bad Address";
                }
            } else if (key == "dns") {
                // Servers only; names (search domains) are not used.
                for (const std::string_view item : split_list(value)) {
                    const std::string server(item);
                    if (is_ipv4(server) || is_ipv6(server)) {
                        result.dns.push_back(server);
                    }
                }
            } else if (key == "listenport") {
                if (!parse_number(value, 0, 65535, number)) {
                    reason = "bad ListenPort";
                }
                result.listen_port = static_cast<int32_t>(number);
            } else if (key == "mtu") {
                if (!parse_number(value, 0, 65535, number)) {
                    reason = "bad MTU";
                }
                result.mtu = static_cast<int32_t>(number);
            } else if (key == "fwmark") {
                if (lowercase(value) != "off" && !parse_number(value, 0, 0xFFFFFFFFULL, number, true)) {
                    reason = "bad FwMark";
                }
                result.fwmark = static_cast<uint32_t>(number);
            }
            // Every other key (PreUp, PostUp, PreDown, PostDown, Table,
            // SaveConfig, ...) is skipped and never acted on.
        } else {
            WireGuardPeer& peer = result.peers.back();
            if (key == "publickey") {
                if (!valid_wireguard_key(value)) {
                    reason = "bad PublicKey";
                } else {
                    peer.public_key = std::string(value);
                }
            } else if (key == "presharedkey") {
                if (!valid_wireguard_key(value)) {
                    reason = "bad PresharedKey";
                } else {
                    peer.preshared_key = std::string(value);
                }
            } else if (key == "allowedips") {
                if (!parse_address_list(value, peer.allowed_ips)) {
                    reason = "bad AllowedIPs";
                }
            } else if (key == "endpoint") {
                if (!parse_endpoint(value, peer)) {
                    reason = "bad Endpoint";
                }
            } else if (key == "persistentkeepalive") {
                if (lowercase(value) != "off" && !parse_number(value, 0, 65535, number)) {
                    reason = "bad PersistentKeepalive";
                }
                peer.keepalive = static_cast<int32_t>(number);
            }
        }
    }

    if (reason.empty()) {
        if (!has_interface) {
            reason = "no [Interface]";
        } else if (result.private_key.empty()) {
            reason = "no PrivateKey";
        } else if (result.addresses.empty()) {
            reason = "no Address";
        } else if (result.peers.empty()) {
            reason = "no [Peer]";
        } else if (std::any_of(result.peers.begin(), result.peers.end(),
                               [](const WireGuardPeer& peer) { return peer.public_key.empty(); })) {
            reason = "no PublicKey";
        }
    }
    if (!reason.empty()) {
        clear_wireguard_config(result);
        return reason;
    }
    clear_wireguard_config(config);
    config = std::move(result);
    return {};
}

std::filesystem::path wireguard_folder(const std::filesystem::path& root, bool test_folder) {
    if (test_folder) {
        return root / "wireguard";
    }
    const std::filesystem::path name = std::filesystem::path(APP_CONFIG_NAME) / "wireguard";
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path config(xdg_config_home);
        if (!config.empty() && config.is_absolute()) {
            return config / name;
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path path(home);
        if (!path.empty() && path.is_absolute()) {
            return path / ".config" / name;
        }
    }
    return {};
}

bool prepare_wireguard_folder(const std::filesystem::path& folder) {
    if (folder.empty()) {
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(folder, error)) {
        std::filesystem::create_directories(folder, error);
    }
    struct stat info {};
    if (lstat(folder.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false;
    }
    if ((info.st_mode & 0077) != 0) {
        chmod(folder.c_str(), 0700);
    }
    // The settings folder above it (~/.config/lokictl), which
    // create_directories() above may just have made, is the user's only,
    // too.
    const std::filesystem::path parent = folder.parent_path();
    struct stat parent_info {};
    if (parent.filename() == APP_CONFIG_NAME && lstat(parent.c_str(), &parent_info) == 0 &&
        S_ISDIR(parent_info.st_mode) && (parent_info.st_mode & 0077) != 0) {
        chmod(parent.c_str(), 0700);
    }
    for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end;
         entry.increment(error)) {
        const std::string name = entry->path().filename().string();
        if (name.size() <= kSuffix.size() || name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }
        struct stat file {};
        if (lstat(entry->path().c_str(), &file) == 0 && S_ISREG(file.st_mode) && (file.st_mode & 0077) != 0) {
            chmod(entry->path().c_str(), 0600);
        }
    }
    return true;
}

bool valid_wireguard_name(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameBytes || name.front() == '.') {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20 || byte == 0x7F || character == '/';
    });
}

std::vector<std::string> list_wireguard_files(const std::filesystem::path& folder) {
    std::vector<std::string> names;
    if (folder.empty()) {
        return names;
    }
    std::error_code error;
    for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end;
         entry.increment(error)) {
        const std::string file_name = entry->path().filename().string();
        if (file_name.size() <= kSuffix.size() ||
            file_name.compare(file_name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }
        const std::string name = file_name.substr(0, file_name.size() - kSuffix.size());
        std::error_code status_error;
        if (!valid_wireguard_name(name) ||
            !std::filesystem::is_regular_file(entry->symlink_status(status_error)) || status_error) {
            continue;
        }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    if (names.size() > model::kVpnMaxFiles) {
        names.resize(model::kVpnMaxFiles);
    }
    return names;
}

std::string read_wireguard_file(const std::filesystem::path& folder, const std::string& name,
                                WireGuardConfig& config) {
    if (folder.empty() || !valid_wireguard_name(name)) {
        return "file not found";
    }
    const std::filesystem::path path = folder / (name + std::string(kSuffix));
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        return "file not found";
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return "file unreadable";
    }
    if (size > kWireGuardMaxFileBytes) {
        return "file too large";
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "file unreadable";
    }
    std::string text;
    text.reserve(static_cast<std::size_t>(size));
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (text.size() > kWireGuardMaxFileBytes) {
        wipe(text);
        return "file too large";
    }
    std::string reason = parse_wireguard_config(text, config);
    wipe(text);
    return reason;
}

} // namespace platform

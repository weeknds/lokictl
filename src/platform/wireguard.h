/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// WireGuard client files for the VPN mode: where they live, which of them
// can be used, and a small reader for their contents.
//
// The keys a file holds are secrets. They are read here only at the moment
// a tunnel is asked for (see LiveDataSource::request_mode()), handed to the
// system's network service and wiped (clear_wireguard_config()). They never
// reach model::HomeData, the log or the screen; neither does any other line
// of a file. Pages show only a file's name and the server's host.
namespace platform {

// The interface LokiCTL's tunnel runs on.
constexpr const char* kVpnInterface = "lokiwg0";

// The largest file read.
constexpr std::uintmax_t kWireGuardMaxFileBytes = 16 * 1024;

// One address with its prefix length, as an [Interface] Address or a
// [Peer] AllowedIPs entry gives it.
struct WireGuardAddress {
    std::string address; // as written in the file, checked with inet_pton()
    int32_t prefix{0};   // 0-32 for IPv4, 0-128 for IPv6; the full length when the file gives none
    bool ipv6{false};
};

struct WireGuardPeer {
    std::string public_key;    // base64 of 32 bytes
    std::string preshared_key; // base64 of 32 bytes; empty when the file has none
    std::string endpoint;      // "host:port" or "[v6]:port"; empty when the file has none
    std::string endpoint_host; // the host alone, without brackets, as pages show it
    std::vector<WireGuardAddress> allowed_ips;
    int32_t keepalive{0};      // seconds; 0 is off
};

// What a WireGuard client file says, as far as LokiCTL uses it. Lines it
// does not use (PreUp, PostUp, PreDown, PostDown, Table, SaveConfig and any
// other key) are skipped: no command of a file is ever run.
struct WireGuardConfig {
    std::string private_key;                // base64 of 32 bytes
    std::vector<WireGuardAddress> addresses; // at least one
    std::vector<std::string> dns;           // DNS servers (IPv4 or IPv6 addresses only)
    int32_t listen_port{0};                 // 0 lets the system pick
    int32_t mtu{0};                         // 0 lets the system pick
    uint32_t fwmark{0};                     // 0 is off
    std::vector<WireGuardPeer> peers;       // at least one
};

// True when one of the peers sends all IPv4 or all IPv6 traffic (an
// AllowedIPs entry of prefix 0): the tunnel then becomes the default route.
bool routes_everything(const WireGuardConfig& config);

// Overwrites the keys in `config` and empties it.
void clear_wireguard_config(WireGuardConfig& config);

// Reads a WireGuard client file's text into `config`. Returns an empty
// string when it can be used, otherwise a short, fixed reason that quotes
// nothing of the file: "bad PrivateKey", "no [Peer]", "bad line 7", ...
// Keys and section names are read without regard to case; '#' starts a
// comment; each key's value is the text after its first '='. Never throws.
std::string parse_wireguard_config(std::string_view text, WireGuardConfig& config);

// True when `key` is base64 of exactly 32 bytes, as WireGuard writes keys.
bool valid_wireguard_key(std::string_view key);

// LokiCTL's WireGuard folder: "<root>/wireguard" in a test folder;
// otherwise "lokictl/wireguard" in $XDG_CONFIG_HOME (when it is an absolute
// path), else in "$HOME/.config". Empty when neither variable helps.
std::filesystem::path wireguard_folder(const std::filesystem::path& root, bool test_folder);

// Creates `folder` when it is missing (mode 0700) and takes away every
// right of group and others from it and from each file in it whose name
// ends in ".conf" (0700 and 0600). Only a normal start on the device calls
// it. Returns false when the folder is not there afterwards. Never throws.
bool prepare_wireguard_folder(const std::filesystem::path& folder);

// True for a name the VPN row may offer: 1 to 64 bytes, no '/', no control
// character, not starting with '.'.
bool valid_wireguard_name(const std::string& name);

// The usable files in `folder`: regular files (no links) whose name ends in
// ".conf", by name without ".conf" (see valid_wireguard_name()), sorted, at
// most model::kVpnMaxFiles. A missing folder has none. Never throws.
std::vector<std::string> list_wireguard_files(const std::filesystem::path& folder);

// The longest name a file added from the web dashboard may have.
constexpr std::size_t kWireGuardUploadNameMax = 32;

// True for the name of a file added from the web dashboard: 1 to
// kWireGuardUploadNameMax letters, digits, '.', '_' or '-', not starting
// with '.'.
bool valid_upload_name(std::string_view name);

// What save_wireguard_file() did.
enum class WireGuardSave {
    Saved,
    NoFolder,     // there is no WireGuard folder to save into
    BadName,      // not a name valid_upload_name() takes
    TooLarge,     // more than kWireGuardMaxFileBytes
    NotWireGuard, // parse_wireguard_config() refused it; `reason` says why
    NameTaken,    // "<name>.conf" is there already
    Full,         // model::kVpnMaxFiles files are there already
    Failed,       // the folder or the file could not be written
};

// Checks `text` (a WireGuard client file a browser sent; a UTF-8 byte
// order mark at its start is dropped) with parse_wireguard_config() and
// saves it as "<folder>/<name>.conf": a new file (never over another one or
// through a link) of mode 0600, in a folder prepared by
// prepare_wireguard_folder(). On Saved, `server` is the first peer's host
// (WireGuardPeer::endpoint_host). The keys it read are wiped; nothing of
// `text` is logged or kept. Never throws.
WireGuardSave save_wireguard_file(const std::filesystem::path& folder, std::string_view name,
                                  std::string_view text, std::string& server, std::string& reason);

// Reads "<folder>/<name>.conf" (a regular file of at most
// kWireGuardMaxFileBytes) into `config`. Returns an empty string, or a
// short fixed reason ("file not found", "file too large", or one of
// parse_wireguard_config()'s). The text read is wiped before it returns.
// Never throws.
std::string read_wireguard_file(const std::filesystem::path& folder, const std::string& name,
                                WireGuardConfig& config);

} // namespace platform

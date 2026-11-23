/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace model {

// The tools that can run (the Ping, DNS, Trace, Ports and Speed pages, and
// listening and probing on the Discover page). Only one runs at a time.
enum class ToolKind : int32_t {
    Ping   = 0,
    Dns    = 1,
    Trace  = 2,
    Listen = 3,
    Probe  = 4,
    Ports  = 5,
    Speed  = 6,
};

// How many kinds there are, for tables indexed by ToolKind.
constexpr std::size_t kToolKindCount = 7;

const char* tool_name(ToolKind kind); // "ping", "dns", "trace", "listen", "probe", "ports", "speed"

// Where a tool's latest run stands.
enum class ToolState : int32_t {
    Idle = 0,  // never run
    Running,
    Done,      // ended by itself; the result says how it went
    Stopped,   // ended by the user
    Failed,    // could not run; ToolError says why
};

// Why a run failed or found nothing.
enum class ToolError : int32_t {
    None = 0,
    NotAllowed,  // ping, trace and probe: the system refused the ICMP socket (ping_group_range);
                 // listen: the capture helper may not be run, or lacks its capability
    NoServer,    // no IPv4 nameserver in /etc/resolv.conf
    NoSuchName,  // the DNS server said the name does not exist (NXDOMAIN)
    NoAnswer,    // the DNS server did not answer in time
    NoAddress,   // the name exists but has no address of the kinds asked for
    BadTarget,   // the target is not a valid name or IPv4 address
    Failed,      // anything else; ToolRun::reason says what
};

// Every run sends one echo a second, this many in all.
constexpr int32_t kPingCount = 10;
// An echo with no reply after this long is lost.
constexpr int32_t kPingTimeoutMs = 2000;
// The Ping page shows the last this many replies.
constexpr std::size_t kPingShownReplies = 5;
// A DNS lookup keeps at most this many addresses.
constexpr std::size_t kDnsMaxAddresses = 6;
// A typed target is at most this long.
constexpr std::size_t kToolTargetMax = 63;
// The preset targets the tool pages offer (Ping and Trace after the router)
// and the command bar completes.
constexpr const char* kPingPresets[] = {"9.9.9.9", "1.1.1.1"};
constexpr const char* kDnsPresets[] = {"example.com", "wikipedia.org"};
// A time not known.
constexpr int32_t kTimeUnknown = -1;
// A trace probes at most this many hops, and waits this long for each.
constexpr int32_t kTraceMaxHops = 20;
constexpr int32_t kTraceHopTimeoutMs = 1000;

// PingReply::time_us (and TraceHop::time_us) of an echo that is still
// waiting for its reply, and of one given up on.
constexpr int32_t kPingWaiting = -2;
constexpr int32_t kPingLost    = -1;

// One echo of a ping run.
struct PingReply {
    int32_t sequence{0};             // 1 to kPingCount
    int32_t time_us{kPingWaiting};   // round trip in microseconds, kPingWaiting or kPingLost
};

bool operator==(const PingReply& left, const PingReply& right);
bool operator!=(const PingReply& left, const PingReply& right);

// The latest ping run.
struct PingRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;              // with ToolError::Failed, or the last send error
    std::string target;              // as asked: an IPv4 address or a name
    std::string address;             // the IPv4 address pinged; empty while a name is looked up
    int32_t sent{0};                 // echoes handed to the system
    int32_t received{0};             // replies received
    int32_t lost{0};                 // echoes given up on (no reply in kPingTimeoutMs, or the send failed)
    int32_t min_us{kTimeUnknown};
    int32_t avg_us{kTimeUnknown};
    int32_t max_us{kTimeUnknown};
    std::vector<PingReply> replies;  // the last kPingShownReplies echoes, by sequence, oldest first
};

bool operator==(const PingRun& left, const PingRun& right);
bool operator!=(const PingRun& left, const PingRun& right);

// The latest DNS lookup.
struct DnsRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;              // with ToolError::Failed
    std::string name;                // the name looked up
    std::string server;              // the DNS server that answered, or the last one asked (IPv4)
    std::vector<std::string> addresses; // IPv4 first, then IPv6; at most kDnsMaxAddresses
    int32_t time_ms{kTimeUnknown};   // from the first question to the last answer
};

bool operator==(const DnsRun& left, const DnsRun& right);
bool operator!=(const DnsRun& left, const DnsRun& right);

// One hop of a trace: the probe sent with this time to live, who answered
// it and how fast.
struct TraceHop {
    int32_t ttl{0};                  // 1 to kTraceMaxHops
    std::string address;             // the IPv4 address that answered; empty while waiting or with no answer
    std::string name;                // its reverse name (PTR); empty while not known, or when it has none
    int32_t time_us{kPingWaiting};   // round trip in microseconds, kPingWaiting or kPingLost (no answer)
};

bool operator==(const TraceHop& left, const TraceHop& right);
bool operator!=(const TraceHop& left, const TraceHop& right);

// Why a trace stopped sending probes. The run still reads Running after
// that while the hops' names are looked up.
enum class TraceEnd : int32_t {
    None = 0,     // still probing, or stopped or failed before the end
    Reached,      // the target answered
    Unreachable,  // a hop said the target cannot be reached (the last hop)
    MaxHops,      // kTraceMaxHops probed without reaching the target
};

// The latest trace.
struct TraceRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;              // with ToolError::Failed, or why a probe could not be sent
    std::string target;              // as asked: an IPv4 address or a name
    std::string address;             // the IPv4 address traced; empty while a name is looked up
    std::vector<TraceHop> hops;      // by time to live, the first hop first
    TraceEnd end{TraceEnd::None};
};

bool operator==(const TraceRun& left, const TraceRun& right);
bool operator!=(const TraceRun& left, const TraceRun& right);

// Discover, step 1: listening. A run listens this long, keeps at most this
// many devices, keeps a host name at most this long and a kind ("printer")
// at most this long.
constexpr int32_t kListenSeconds = 5 * 60;
constexpr std::size_t kListenMaxDevices = 64;
constexpr std::size_t kHostNameMax = 32;
constexpr std::size_t kDeviceKindMax = 16;
// A saved capture holds at most this many bytes of one run (whole frames).
constexpr std::size_t kCaptureSaveMax = 4u * 1024u * 1024u;
// An interface name is at most this long (the system's limit, IFNAMSIZ - 1).
constexpr std::size_t kInterfaceNameMax = 15;

// One device heard while listening. A text that is not known is empty.
struct HeardDevice {
    std::string hardware_address;  // "02:00:00:00:00:01"
    std::string address;           // its IPv4 address as text; empty while none was heard
    std::string name;              // its host name: from DHCP, else from mDNS; empty when none
    std::string kind;              // what it is, from mDNS services or SSDP ("printer"); empty when not known
    std::string own_port;          // one of the Cardputer's own ports: its interface ("wlan0"); else empty
    int32_t heard_s{0};            // seconds into the run when it was last heard
};

bool operator==(const HeardDevice& left, const HeardDevice& right);
bool operator!=(const HeardDevice& left, const HeardDevice& right);

// Where saving a listen run as a PCAP file stands.
enum class CaptureSave : int32_t {
    None = 0,  // not asked
    Saving,    // the file is being written (and grows while the run lasts)
    Saved,     // the file is complete
    Failed,    // ListenRun::save_reason says why
};

// The latest listen run of the Discover page. It only receives: nothing is
// sent, so the Sent count does not move.
struct ListenRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;               // with ToolError::Failed
    std::string interface_name;       // the interface listened on, for example "eth0"
    int32_t elapsed_s{0};             // seconds since the run started; stops when it ends
    int32_t frames{0};                // ARP, DHCP, mDNS and SSDP frames heard
    std::vector<HeardDevice> devices; // by IPv4 address, those without one last; at most kListenMaxDevices
    CaptureSave save{CaptureSave::None};
    std::string save_name;            // the file's name, without its folder ("20261123-075130.pcap")
    std::string save_reason;          // with CaptureSave::Failed ("disk full")
    uint64_t save_bytes{0};           // bytes in the file so far
    bool save_full{false};            // the run heard more than kCaptureSaveMax; the file has its start
};

bool operator==(const ListenRun& left, const ListenRun& right);
bool operator!=(const ListenRun& left, const ListenRun& right);

// Discover, step 3: probing. A probe asks at most this many addresses
// (one /24), one ICMP echo every kProbeIntervalMs.
constexpr int32_t kProbeMaxAddresses = 254;
constexpr int32_t kProbeIntervalMs = 50;

// What a probe run is doing.
enum class ProbeStep : int32_t {
    Sweep = 0,  // sending one echo to each address, then waiting for the last answers
    Names,      // asking for kinds and names (mDNS and SSDP), then waiting for the answers
};

// The latest probe run of the Discover page. Every echo and question it
// sends counts in the Sent count.
struct ProbeRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;               // with ToolError::Failed ("not a private network")
    std::string interface_name;       // the interface probed from, for example "eth0"
    std::string network;              // what is asked, "10.0.0.0/24"; empty until known
    ProbeStep step{ProbeStep::Sweep};
    int32_t total{0};                 // addresses to ask (the range without the Cardputer's own)
    int32_t asked{0};                 // addresses asked so far
    int32_t replied{0};               // addresses that answered their echo
    int32_t elapsed_s{0};             // seconds since the run started; stops when it ends
    std::vector<HeardDevice> devices; // found, like a listen run's list; at most kListenMaxDevices
};

bool operator==(const ProbeRun& left, const ProbeRun& right);
bool operator!=(const ProbeRun& left, const ProbeRun& right);

// The port scan: one device's TCP ports from a fixed list of common ones
// (common_ports()), one new connection attempt every kPortIntervalMs, at
// most kPortsWaiting waiting at once, each given kPortTimeoutMs.
constexpr int32_t kPortIntervalMs = 100;
constexpr int32_t kPortsWaiting = 8;
constexpr int32_t kPortTimeoutMs = 1000;

// One port of the scan's list and the service usually found on it.
struct CommonPort {
    uint16_t port;
    const char* service;  // short, for example "ssh"
};

// The ports a scan asks, ascending (50 of them).
const std::vector<CommonPort>& common_ports();
// The service of `port` in common_ports(); "" for a port not in it.
const char* port_service(uint16_t port);

// The latest port scan of the Ports page. Every connection attempt the
// system takes counts in the Sent count.
struct PortRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;          // with ToolError::Failed ("not on the Cardputer's networks")
    std::string target;          // the IPv4 address scanned
    int32_t total{0};            // ports to ask
    int32_t asked{0};            // attempts made so far
    int32_t closed{0};           // ports that refused
    int32_t silent{0};           // ports that did not answer in kPortTimeoutMs
    std::vector<uint16_t> open;  // ports that took the connection, ascending
};

bool operator==(const PortRun& left, const PortRun& right);
bool operator!=(const PortRun& left, const PortRun& right);

// The speed test: a 10 s download, then a 10 s upload, against a speed
// server (Cloudflare's by default, kSpeedServerDefault). Each phase shows
// one sample every kSpeedSampleMs.
constexpr int32_t kSpeedPhaseMs = 10000;
constexpr int32_t kSpeedSampleMs = 500;
constexpr std::size_t kSpeedSamples = static_cast<std::size_t>(kSpeedPhaseMs / kSpeedSampleMs);
// The server used when the settings name none: Cloudflare's anycast speed
// endpoints (GET <server>/__down?bytes=N and POST <server>/__up), answered
// by the nearest of its sites anywhere.
constexpr const char* kSpeedServerDefault = "https://speed.cloudflare.com";
// A speed server's address is at most this long.
constexpr std::size_t kSpeedServerMax = 200;
// A speed that is not known (yet).
constexpr int32_t kSpeedRateUnknown = -1;

// What a speed test is doing.
enum class SpeedPhase : int32_t {
    Connect = 0,  // the first small request: name, connection and TLS
    Download,
    Upload,
};

// The latest speed test of the Speed page. Every request it makes counts
// once in the Sent count.
struct SpeedRun {
    ToolState state{ToolState::Idle};
    ToolError error{ToolError::None};
    std::string reason;                   // with ToolError::Failed ("server said 403")
    std::string server;                   // the server, as in the settings
    SpeedPhase phase{SpeedPhase::Connect};
    int32_t phase_ms{0};                  // time into the current phase (0 until its first byte)
    uint64_t down_bytes{0};               // body bytes received while downloading
    uint64_t up_bytes{0};                 // bytes the server acknowledged while uploading
    int32_t down_kbps{kSpeedRateUnknown};     // down_bytes over the download's time so far
    int32_t up_kbps{kSpeedRateUnknown};       // up_bytes over the upload's time so far
    std::vector<int32_t> down_samples;    // kbit/s of each kSpeedSampleMs, oldest first; at most kSpeedSamples
    std::vector<int32_t> up_samples;      // the same while uploading
};

bool operator==(const SpeedRun& left, const SpeedRun& right);
bool operator!=(const SpeedRun& left, const SpeedRun& right);

// Whether `text` may be used as the speed server: "https://" or "http://"
// and a host (with an optional port and path), at most kSpeedServerMax
// printable ASCII characters, with no space, '@', '?' or '#'.
bool valid_speed_server(std::string_view text);
// The host of a valid speed server, for the page: "speed.cloudflare.com".
std::string speed_server_host(std::string_view text);

// What the tools show: the latest run of each, which of the Discover
// page's two kinds started last, and the app-wide count of packets LokiCTL
// has sent in this run of the app.
struct ToolsData {
    PingRun ping;
    DnsRun dns;
    TraceRun trace;
    ListenRun listen;
    ProbeRun probe;
    PortRun ports;
    SpeedRun speed;
    ToolKind latest{ToolKind::Listen};  // Listen or Probe: the Discover run started last
    uint64_t sent{0};
};

bool operator==(const ToolsData& left, const ToolsData& right);
bool operator!=(const ToolsData& left, const ToolsData& right);

// True while any tool runs.
bool tool_running(const ToolsData& data);
// The state of `kind`'s latest run.
ToolState tool_state(const ToolsData& data, ToolKind kind);

// One entry of the Discover page's list: what the latest listen run and
// the latest probe run found about one hardware address, put together.
struct DiscoverDevice {
    HeardDevice device;  // heard_s is not used
    int32_t age_s{0};    // seconds since it was last heard, as of the end (or now) of the run that heard it
};

// The Discover page's list: every device of `listen` and of `probe`, one
// entry per hardware address. A device in both keeps the listen run's name
// and kind (the probe's when the listen run has none), the probe's IPv4
// address (the listen run's when the probe has none) and the smaller age.
// Sorted by IPv4 address, those without one last, then by hardware address.
std::vector<DiscoverDevice> discover_devices(const ListenRun& listen, const ProbeRun& probe);

// Whether `text` may be used as a typed target: 1 to kToolTargetMax
// characters, only a-z, 0-9 and '.', no empty label (no dot at either end,
// no two dots in a row). A text of digits and dots only must be an IPv4
// address (four numbers of 0-255). For DNS a name is needed, so an IPv4
// address is refused there; Ping and Trace take both. The port scan takes
// only an IPv4 address that port_scan_address() allows. The listen and
// probe tools' target is an interface, checked with valid_interface_name()
// instead, and the speed test's is its server, checked with
// valid_speed_server().
bool valid_tool_target(ToolKind kind, std::string_view text);

// Whether `text` is an IPv4 address in dotted form ("192.0.2.1").
bool is_ipv4_text(std::string_view text);

// Whether the port scan may ask `text`: an IPv4 address in dotted form on a
// private network (10/8, 172.16/12, 192.168/16) or a link-local one
// (169.254/16). The runner also needs it on one of the Cardputer's own
// networks.
bool port_scan_address(std::string_view text);

// Whether `text` may be handed to the capture helper as an interface name:
// 1 to kInterfaceNameMax characters of a-z, A-Z, 0-9, '.', '_' and '-', and
// not "." or "..". The listen and probe tools' target is checked with it.
bool valid_interface_name(std::string_view text);

} // namespace model

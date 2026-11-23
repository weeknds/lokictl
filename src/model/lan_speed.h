/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The LAN speed test (the LAN page): the speed between the Cardputer and
// another machine on the same network, measured with the iperf3 program.
// LokiCTL runs iperf3 as a child process with --json-stream (one JSON object
// per line, as each event happens) and reads its output here. Nothing in
// this file opens a socket or starts a program: platform/lan_speed.* does.
namespace model {

// iperf3's usual port; both ends use it.
constexpr uint16_t kLanPort = 5201;
// A test from the Cardputer: 10 s download, then 10 s upload.
constexpr int32_t kLanPhaseSeconds = 10;
// Each phase reports twice a second, like the Speed page.
constexpr int32_t kLanSampleMs = 500;
constexpr std::size_t kLanSamples = static_cast<std::size_t>(kLanPhaseSeconds * 1000 / kLanSampleMs);
// The server mode's list keeps this many results, newest first.
constexpr std::size_t kLanServedMax = 3;
// A test a phone or PC runs against the Cardputer is ended after this long.
constexpr int32_t kLanServeTestMaxSeconds = 120;
// A line of iperf3 output longer than this is dropped unread.
constexpr std::size_t kIperfLineMax = 128 * 1024;
// JSON nested deeper than this is not read.
constexpr int32_t kIperfDepthMax = 12;

enum class LanMode : int32_t {
    Client = 0,  // the Cardputer tests against another machine's iperf3 server
    Server = 1,  // the Cardputer is the server; a phone or PC tests against it
};

enum class LanPhase : int32_t {
    Connect = 0,  // client: starting the download
    Download,     // client: the other machine sends (iperf3 -R)
    Upload,       // client: the Cardputer sends
    Waiting,      // server: listening, no test yet
    Testing,      // server: a test runs
};

// One test a phone or PC ran against the Cardputer's server.
struct LanServed {
    std::string peer;           // its IPv4 address; empty when iperf3 gave none
    bool to_peer{false};        // the Cardputer sent (the app's "reverse"); else it received
    bool both{false};           // both ways at once: kbps came in, back_kbps went out
    int32_t kbps{kSpeedRateUnknown};
    int32_t back_kbps{kSpeedRateUnknown};
    uint64_t bytes{0};          // bytes moved in the main way
    bool done{false};           // false while it runs
};

bool operator==(const LanServed& left, const LanServed& right);
bool operator!=(const LanServed& left, const LanServed& right);

// One of the Cardputer's own addresses the server may listen on.
struct LanPlace {
    std::string address;  // "10.0.0.12"
    std::string label;    // "network" or "hotspot"
};

bool operator==(const LanPlace& left, const LanPlace& right);
bool operator!=(const LanPlace& left, const LanPlace& right);

// The latest LAN run. Only one runs at a time, in one mode.
struct LanRun {
    ToolState state{ToolState::Idle};
    LanMode mode{LanMode::Client};
    std::string reason;          // with ToolState::Failed: short plain words ("Nothing answers there")
    std::string target;          // client: the other machine; server: the address listened on
    LanPhase phase{LanPhase::Connect};
    int32_t phase_ms{0};         // time into the current phase (client) or test (server)
    // Client: as on the Speed page. Down is from the other machine to here.
    uint64_t down_bytes{0};
    uint64_t up_bytes{0};
    int32_t down_kbps{kSpeedRateUnknown};
    int32_t up_kbps{kSpeedRateUnknown};
    std::vector<int32_t> down_samples;  // kbit/s of each half second, at most kLanSamples
    std::vector<int32_t> up_samples;
    // Server: the latest tests, newest first; at most kLanServedMax.
    std::vector<LanServed> served;
    int32_t tests{0};            // tests answered since the server was switched on
    // Where the server can listen (filled by app::LanController, not the runner).
    std::vector<LanPlace> places;
};

bool operator==(const LanRun& left, const LanRun& right);
bool operator!=(const LanRun& left, const LanRun& right);

// Whether the client may test against `text`: an IPv4 address on a private
// or link-local network (port_scan_address()).
bool valid_lan_target(std::string_view text);

// ---------------------------------------------------------------------------
// Reading iperf3's --json-stream output.

enum class IperfEventKind : int32_t {
    None = 0,  // not an event LokiCTL uses
    Start,
    Interval,
    End,
    Error,
};

// What one line of output says, with only the values LokiCTL uses. Every
// number is checked: never negative, never larger than kIperfBytesMax or
// kIperfBpsMax, never NaN.
struct IperfEvent {
    IperfEventKind kind{IperfEventKind::None};
    // Start:
    std::string peer;    // the other end's IPv4 address; empty unless it is one
    bool reverse{false}; // the server sends
    bool bidir{false};   // both send at once
    // Interval: the sum over all streams ("sum"), and for a two-way test
    // the other way's sum ("sum_bidir_reverse").
    double seconds{0};
    uint64_t bytes{0};
    double bps{0};
    double back_bps{0};
    // End: "sum_sent", "sum_received" and, two-way, "sum_sent_bidir_reverse".
    bool has_sent{false};
    bool has_received{false};
    uint64_t sent_bytes{0};
    double sent_bps{0};
    uint64_t received_bytes{0};
    double received_bps{0};
    double back_sent_bps{0};
    // Error: iperf3's own text. Never shown or logged as it is: see
    // lan_error_words().
    std::string error;
};

constexpr uint64_t kIperfBytesMax = 1000000000000000ULL;  // 1 PB
constexpr double kIperfBpsMax = 1e13;                      // 10 Tbit/s

// Reads one line (without its newline). False for anything that is not a
// whole JSON object with a string "event" and a "data" member, also for a
// line cut off in the middle; `out` is then reset.
bool read_iperf_event(std::string_view line, IperfEvent& out);

// Splits the bytes read from iperf3's output into lines and reads each
// whole one. A line longer than kIperfLineMax is dropped (up to its end);
// what follows the last newline waits for more bytes.
class IperfLines {
public:
    void add(std::string_view bytes, std::vector<IperfEvent>& events);
    // Forgets a line not finished yet.
    void clear();

private:
    std::string partial_;
    bool skipping_{false};  // inside a line that got too long
};

// Short plain words for iperf3's error text, at most about 30 characters:
// "Nothing answers there", "Port 5201 is busy". In client mode the words
// are about the other machine, in server mode about the Cardputer.
std::string lan_error_words(std::string_view error, LanMode mode);
// True when a server's error means it could not listen at all (the run
// fails), not that one test went wrong (the server starts again).
bool lan_server_cannot_listen(std::string_view error);

// bits per second as kbit/s, rounded, at most the largest int32_t.
int32_t lan_kbps(double bps);

// What a client phase's events do to `run` (whose phase is Download or
// Upload). Interval: a sample (a last piece shorter than 0.2 s is left out),
// the bytes so far and the speed so far. End: the receiver's bytes and
// speed. Error: the run fails with lan_error_words().
void lan_client_event(LanRun& run, const IperfEvent& event);
// What the server's events do to `run`. Start: a new test on top of
// `served`. Interval: its speed so far. End: its speed, done, tests + 1.
// Error: nothing here (the runner decides with lan_server_cannot_listen()).
void lan_server_event(LanRun& run, const IperfEvent& event);
// The server program ended (it answers one test, then starts again): a
// test it left running is finished as it stood, or dropped when it never
// measured anything; the server waits again.
void lan_server_exit(LanRun& run);

} // namespace model

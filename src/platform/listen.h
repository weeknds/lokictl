/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Listening for the Discover page (its step 1): reading what the capture
// helper (lokictl-capture, src/helper/lokictl_capture.c) hears, and the
// device list made from it. Everything here runs without privileges; only
// the helper may open the packet socket, and it only receives.
namespace platform {

// The longest frame the helper passes on (its PCAP snap length). A record
// longer than this means the stream is broken.
constexpr uint32_t kCaptureSnapLength = 2048;

// One of the Cardputer's own ports.
struct OwnPort {
    std::string name;              // interface, for example "wlan0"
    std::string hardware_address;  // "02:00:00:00:00:01"
};

// Every interface under `root` (the system's /sys/class/net) except the
// loopback, with its hardware address; at most 16, interfaces without an
// address left out. Empty when there are none (on the Mac).
std::vector<OwnPort> read_own_ports(const std::string& root = "/sys/class/net");

// Reads the helper's output: a PCAP stream (the 24-byte file header, then
// records of a 16-byte header and the frame), in this machine's byte order,
// with Ethernet frames of at most kCaptureSnapLength bytes. The stream may
// arrive in pieces of any size.
class CaptureReader {
public:
    // Called for each whole frame: its bytes, its length and the record's
    // time in whole seconds.
    using FrameFn = std::function<void(const uint8_t* frame, std::size_t size, uint32_t seconds)>;

    // Adds `size` bytes and calls `on_frame` for every record they finish.
    // Returns false once the stream is broken (a wrong file header or a
    // record longer than the snap length); then it stays broken and reads
    // nothing more.
    bool feed(const uint8_t* data, std::size_t size, const FrameFn& on_frame);

    bool broken() const { return broken_; }

private:
    std::vector<uint8_t> pending_;
    bool header_read_{false};
    bool broken_{false};
};

// The device list of one listen run, from the frames heard: one entry per
// hardware address (group addresses and all zeros are ignored), at most
// model::kListenMaxDevices. It learns:
// - from ARP (Ethernet over IPv4): the sender's hardware address and IPv4
//   address (a sender address of 0.0.0.0, a probe, gives no address);
// - from DHCP: the client's hardware address (chaddr, never the frame's
//   sender, which may be the server), its address (ciaddr, the requested
//   address of a request, or the address an acknowledgement gives) and the
//   host name of option 12 (the first label, at most model::kHostNameMax
//   printable ASCII characters, anything else '?'); and the server itself
//   (the frame's sender and the packet's source address).
// Every length is checked against the captured bytes; a frame cut short or
// an option running past the end is read only as far as it is whole.
class HeardDevices {
public:
    explicit HeardDevices(std::vector<OwnPort> own_ports);

    // Notes what one Ethernet frame says, heard `at_s` seconds into the
    // run. Returns true when the list changed (also when a known device was
    // only heard again later).
    bool note_frame(const uint8_t* frame, std::size_t size, int32_t at_s);

    // The list, sorted by IPv4 address, those without one last (then by
    // hardware address).
    const std::vector<model::HeardDevice>& devices() const { return devices_; }
    // Frames handed to note_frame().
    int32_t frames() const { return frames_; }

private:
    struct Entry {
        uint8_t hardware[6]{};
        uint32_t address{0};  // 0: none heard
        std::string name;
        std::string own_port;
        int32_t heard_s{0};
    };

    bool note_arp(const uint8_t* arp, std::size_t size, int32_t at_s);
    bool note_ipv4(const uint8_t* frame, std::size_t size, int32_t at_s);
    bool note_dhcp(const uint8_t* bootp, std::size_t size, const uint8_t* sender, uint32_t source, int32_t at_s);
    bool note(const uint8_t* hardware, uint32_t address, const std::string& name, int32_t at_s);
    void rebuild();

    std::vector<OwnPort> own_ports_;
    std::vector<Entry> entries_;
    std::vector<model::HeardDevice> devices_;
    int32_t frames_{0};
};

// The pretend runner's made-up capture: the PCAP stream of what a listen
// run hears in its first `elapsed_ms` milliseconds (record times count from
// 0). Made-up, locally administered hardware addresses on the sample's
// 10.0.0.x network, including the two pretend_own_ports(), one frame cut
// short and one DHCP option running past its packet's end.
std::vector<uint8_t> pretend_capture(uint32_t elapsed_ms);
std::vector<OwnPort> pretend_own_ports();

#if !USE_DESKTOP
// The capture helper, run for one listen (device builds only). All of it is
// called on the tool runner's thread, never on the UI thread.
class CaptureProcess {
public:
    CaptureProcess() = default;
    // Calls stop().
    ~CaptureProcess();

    CaptureProcess(const CaptureProcess&) = delete;
    CaptureProcess& operator=(const CaptureProcess&) = delete;

    // Runs "lokictl-capture discover <interface_name> <seconds>" with an
    // empty environment, /dev/null for its standard input and error, no
    // blocked signals, and its standard output on a pipe whose reading end
    // (fd(), non-blocking, closed on exec) stays here. Returns true, or
    // false with `error` and `reason` set (ToolError::NotAllowed, or
    // ToolError::Failed with a short reason such as "capture helper not
    // installed").
    bool start(const std::string& interface_name, int32_t seconds, model::ToolError& error, std::string& reason);
    // The reading end of the pipe; -1 before start() and after stop().
    int fd() const { return fd_; }
    // Ends the helper: SIGTERM, then SIGKILL when it is still there after
    // 100 ms; waits for it and closes the pipe. Safe to call more than once.
    void stop();
    // After the helper closed its output: waits for it (at most 500 ms,
    // then it is killed) and closes the pipe. Returns its exit status, or
    // -1 when it was ended by a signal.
    int finish();

private:
    void close_pipe();
    int reap(int timeout_ms);

    int fd_{-1};
    int pid_{-1};
};

// What the helper's exit status means: true for a normal end (its time was
// up, its size limit reached, or the reader went away); otherwise false
// with `error` and `reason` set.
bool capture_exit_ok(int status, model::ToolError& error, std::string& reason);
#endif

} // namespace platform

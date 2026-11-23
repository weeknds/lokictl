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

    // From now on also keeps the stream as read, for saving it as a PCAP
    // file: the file header and then each whole record, as long as all of
    // it stays within `max_bytes`. Once a record does not fit, nothing more
    // is kept and kept_full() is true. Call it before the first feed().
    void keep(std::size_t max_bytes) { keep_max_ = max_bytes; }
    const std::vector<uint8_t>& kept() const { return kept_; }
    bool kept_full() const { return kept_full_; }
    // Hands the kept bytes over, leaving none here.
    std::vector<uint8_t> take_kept() { return std::move(kept_); }

private:
    std::vector<uint8_t> pending_;
    std::vector<uint8_t> kept_;
    std::size_t keep_max_{0};
    bool kept_full_{false};
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
//   (the frame's sender and the packet's source address);
// - from mDNS (UDP to port 5353) and SSDP (UDP to port 1900): the frame's
//   sender and the packet's source address. An mDNS response (from port
//   5353) also gives a host name, from an A record whose address is the
//   packet's source (its first label, cleaned like a DHCP name), and a kind
//   from the service types it names (owner names and PTR targets, such as
//   "_ipp._tcp" for a printer). An SSDP NOTIFY gives a kind from its NT
//   device type, else from the product its SERVER header names.
// For probing (probe.h) it also learns from what came to LokiCTL's own
// sockets: a complete entry of the system's neighbour table (hardware and
// IPv4 address), and an mDNS or SSDP reply sent to a question of its own,
// read like the multicast ones above. An SSDP reply ("HTTP/1.1 200") gives
// a kind from its ST header as a NOTIFY does from NT. In any mDNS response
// a reverse (PTR) record for the packet's source address gives a host name
// too (the first label of the name it points to).
// An SSDP announcement or reply also marks the device as speaking UPnP and
// keeps its SERVER line (at most model::kSsdpServerMax printable ASCII
// characters; the last one heard), where a version may stand.
// A DHCP host name always wins over an mDNS one; a kind, once known, is
// replaced only by one from a surer source (an mDNS service, then an SSDP
// device type, then an SSDP server product), so nothing flips back and
// forth. Kinds are at most model::kDeviceKindMax printable ASCII
// characters. Every length is checked against the captured bytes; a frame
// cut short or an option or record running past the end is read only as
// far as it is whole.
class HeardDevices {
public:
    explicit HeardDevices(std::vector<OwnPort> own_ports);

    // Notes what one Ethernet frame says, heard `at_s` seconds into the
    // run. Returns true when the list changed (also when a known device was
    // only heard again later).
    bool note_frame(const uint8_t* frame, std::size_t size, int32_t at_s);

    // Notes what probing found about the device with the 6-byte hardware
    // address `hardware`: a neighbour table entry for `address`, or an mDNS
    // or SSDP reply from `source` to one of LokiCTL's questions. Each
    // returns true when the list changed.
    bool note_neighbour(const uint8_t* hardware, uint32_t address, int32_t at_s);
    bool note_mdns_reply(const uint8_t* hardware, uint32_t source, const uint8_t* message, std::size_t size,
                         int32_t at_s);
    bool note_ssdp_reply(const uint8_t* hardware, uint32_t source, const uint8_t* text, std::size_t size,
                         int32_t at_s);

    // The list, sorted by IPv4 address, those without one last (then by
    // hardware address).
    const std::vector<model::HeardDevice>& devices() const { return devices_; }
    // Frames handed to note_frame().
    int32_t frames() const { return frames_; }

private:
    // How sure a name or a kind is: a surer one replaces a less sure one,
    // never the other way round.
    enum Rank : int32_t {
        kRankNone = 0,
        kRankSsdpServer = 1,  // kind: the product an SSDP SERVER header names
        kRankSsdpType = 2,    // kind: an SSDP NT device type
        kRankMdns = 3,        // name: an mDNS A record; kind: an mDNS service type
        kRankDhcp = 4,        // name: DHCP option 12
    };

    // What one frame says about one device.
    struct Heard {
        uint32_t address{0};
        std::string name;
        int32_t name_rank{kRankNone};
        std::string kind;
        int32_t kind_rank{kRankNone};
        bool upnp{false};    // an SSDP announcement or reply
        std::string server;  // its SERVER line
    };

    struct Entry {
        uint8_t hardware[6]{};
        uint32_t address{0};  // 0: none heard
        std::string name;
        int32_t name_rank{kRankNone};
        std::string kind;
        int32_t kind_rank{kRankNone};
        std::string own_port;
        int32_t heard_s{0};
        bool upnp{false};
        std::string server;
    };

    bool note_arp(const uint8_t* arp, std::size_t size, int32_t at_s);
    bool note_ipv4(const uint8_t* frame, std::size_t size, int32_t at_s);
    bool note_dhcp(const uint8_t* bootp, std::size_t size, const uint8_t* sender, uint32_t source, int32_t at_s);
    bool note_mdns(const uint8_t* message, std::size_t size, bool response, const uint8_t* sender, uint32_t source,
                   int32_t at_s);
    bool note_ssdp(const uint8_t* text, std::size_t size, const uint8_t* sender, uint32_t source, int32_t at_s);
    bool note(const uint8_t* hardware, const Heard& heard, int32_t at_s);
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
// short, one DHCP option running past its packet's end, mDNS answers shaped
// like real ones (the cache-flush bit, compressed names, the address in the
// additional section, one cut short) and SSDP NOTIFY announcements.
std::vector<uint8_t> pretend_capture(uint32_t elapsed_ms);
std::vector<OwnPort> pretend_own_ports();
// The made-up file name the pretend runner shows for a saved capture (it
// writes no file).
constexpr const char* kPretendCaptureName = "20260614-074600.pcap";

// The folder saved captures go to: $XDG_DATA_HOME/lokictl/captures when
// XDG_DATA_HOME is an absolute path, otherwise
// ~/.local/share/lokictl/captures ($HOME, else the account's home folder).
// Empty when no home folder is known.
std::string capture_folder();

// One saved capture: a new PCAP file written from the bytes a CaptureReader
// kept. Only ever used on the tool runner's thread.
class CaptureFile {
public:
    CaptureFile() = default;
    // Calls close().
    ~CaptureFile();

    CaptureFile(const CaptureFile&) = delete;
    CaptureFile& operator=(const CaptureFile&) = delete;

    // Makes `folder` (and the folders above it) when missing, sets it to
    // 0700, and creates a new file in it named after the local time,
    // "YYYYMMDD-HHMMSS.pcap" ("-2" to "-9" added when the name is taken),
    // mode 0600, never through a link and never over an existing file.
    // Returns false with a short `reason` ("no home folder", "disk full").
    bool create(const std::string& folder, std::string& reason);
    // Adds `size` bytes at the end. Returns false with `reason` when they
    // could not all be written; the file is closed then.
    bool append(const uint8_t* data, std::size_t size, std::string& reason);
    void close();

    bool is_open() const { return fd_ >= 0; }
    const std::string& name() const { return name_; }
    uint64_t bytes() const { return bytes_; }

private:
    int fd_{-1};
    std::string name_;
    uint64_t bytes_{0};
};

#if !USE_DESKTOP
// The helper's four fixed commands: `discover` (ARP, DHCP, mDNS and SSDP,
// for the Discover page), `watch` (ARP, DHCP, TCP connection requests to
// this machine and IPv6 router announcements, for Watch, watch.h),
// `topology` (LLDP and CDP, what switches say about themselves, for the
// Map, topology.h) and `names` (DNS questions to the interface's own
// address, the names the hotspot's devices look up, talk.h).
enum class CaptureCommand {
    Discover,
    Watch,
    Topology,
    Names,
};

// The capture helper, run for one listen or one Watch run (device builds
// only). All of it is called on a runner's thread, never on the UI thread.
class CaptureProcess {
public:
    CaptureProcess() = default;
    // Calls stop().
    ~CaptureProcess();

    CaptureProcess(const CaptureProcess&) = delete;
    CaptureProcess& operator=(const CaptureProcess&) = delete;

    // Runs "lokictl-capture discover <interface_name> <seconds>" (or
    // "watch" for CaptureCommand::Watch, "topology" for
    // CaptureCommand::Topology, "names" for CaptureCommand::Names) with an empty environment, /dev/null for its standard input and error, no
    // blocked signals, and its standard output on a pipe whose reading end
    // (fd(), non-blocking, closed on exec) stays here. Returns true, or
    // false with `error` and `reason` set (ToolError::NotAllowed, or
    // ToolError::Failed with a short reason such as "capture helper not
    // installed").
    bool start(const std::string& interface_name,
               int32_t seconds,
               model::ToolError& error,
               std::string& reason,
               CaptureCommand command = CaptureCommand::Discover);
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

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <memory>
#include <string>

namespace platform {

// One job for a ToolRunner: a ping run, a DNS lookup, a trace or a port
// scan of `target`, listening on or probing from the interface `target`,
// or a speed test against the server `target`.
struct ToolJob {
    model::ToolKind kind{model::ToolKind::Ping};
    std::string target;  // checked with model::valid_tool_target()
};

// Runs the tools (ping, DNS lookup, trace, listening, probing, port scan, speed test) away from the UI
// thread, one job at a time, and keeps the latest run of each for the screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first job and kept until shutdown(); it never touches the view
// model, LVGL or the log. Every wait in it is at most 100 ms, so a stop is
// seen quickly: a job ends within about 0.1 s of stop(), and shutdown()
// returns within about 0.2 s (a listen takes up to 0.1 s more, while the
// helper ends). It sends only what the tools need: echoes on
// an unprivileged ICMP datagram socket (a trace sets each echo's time to
// live and reads the hops' answers from the socket's error queue) and
// questions to the IPv4 nameservers of /etc/resolv.conf (the first one;
// the retry goes to the next one), each counted with count_sent(). A listen
// sends nothing: it runs the capture helper (platform::CaptureProcess,
// which alone may open the packet socket and only receives) and reads its
// stream into the device list (listen.h). A probe (probe.h) sends one
// echo to each address of the uplink's own private network (at most its
// /24), 20 a second, reads the system's neighbour table, then sends one
// mDNS and one SSDP question and one mDNS reverse question per device found
// without a name, each counted with count_sent(). A port scan
// (port_scan.h) makes one unprivileged TCP connection attempt to each port
// of model::common_ports() on one private address of the Cardputer's own
// networks, 10 a second, at most 8 sockets at once, each counted with
// count_sent(); a connection made is asked what it is (service_banner.h:
// what the service says first, or one counted HEAD request on a web port,
// read for at most 1.5 s), then closed. A speed test
// (speed_test.h) downloads, then uploads, each for 10 s or 50 MB over
// HTTPS through libcurl (loaded for the run), each request counted once
// with count_sent(). It never changes a network setting. A listen run keeps what it heard (whole PCAP records, at most
// model::kCaptureSaveMax) until the next listen starts, so it can be saved
// as a file (save_capture()); the file is written on this thread too.
//
// The pretend runner (the Mac simulator, snapshots, and the made-up data
// sources) has no thread and never opens a socket: each take() works out
// the run from the LVGL time since the job started, with fixed made-up
// results (documentation addresses only; a listen reads a made-up capture,
// pretend_capture()), so the same keys and settle time always give the same
// picture. A speed test reads pretend_speed_test() (speed_test.h). It still adds the packets it pretends to send to the Sent count
// (none for a listen). A probe reads pretend_probe() (probe.h), a port
// scan pretend_port_scan() (port_scan.h). It never writes a file: a save reads as saved under
// the made-up name kPretendCaptureName (listen.h).
//
// All functions are called on the UI thread.
class ToolRunner {
public:
    virtual ~ToolRunner() = default;

    // Starts `job`, whose run then reads Running at once. Returns false and
    // changes nothing while a job runs or when the target is not valid for
    // the kind (model::valid_tool_target()).
    virtual bool start(const ToolJob& job) = 0;
    // Asks the running job to end; it then reads Stopped. Never waits; does
    // nothing without a job.
    virtual void stop() = 0;
    // Asks to save the latest listen run as a PCAP file (capture_folder(),
    // listen.h): while it runs, the file gets what was heard so far and
    // then grows until the run ends; after it, the file gets what the run
    // kept. The run's save fields (model::ListenRun::save...) then say how
    // it goes. Never waits. Returns false and changes nothing when there is
    // no listen run, nothing was kept, or the run was already saved.
    virtual bool save_capture() = 0;
    // Copies the latest runs into data.ping, data.dns, data.trace,
    // data.listen, data.probe, data.ports and data.speed, and the list's
    // date into data.list_date (data.sent is left alone) and returns true, but only when any changed
    // since the last take(); otherwise leaves `data` alone and returns
    // false.
    virtual bool take(model::ToolsData& data) = 0;
    // Stops the job and ends the thread, waiting for it. Safe to call more
    // than once; start() refuses every job afterwards.
    virtual void shutdown() = 0;
};

// The live runner on device builds when `live` is true; otherwise the
// pretend runner. Both check a finished port scan against the list of
// known security holes at `list_path` (check_port_services(), port_scan.h;
// empty: no list) and give its date in ToolsData::list_date.
std::unique_ptr<ToolRunner> make_tool_runner(bool live, const std::string& list_path);

} // namespace platform

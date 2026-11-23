/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "talk_data.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// "What is it talking to?" (model::TalkRun): the names the devices on
// LokiCTL's hotspot look up. The capture helper's fixed `names` command
// (src/helper/lokictl_capture.c) passes the DNS questions sent to the
// hotspot's own address on the hotspot's interface; everything here reads
// them without privileges and never sends.
namespace platform {

// One DNS question read from a frame.
struct TalkQuestion {
    std::string hardware;  // the sender's hardware address, "02:5e:11:7a:c3:01"
    std::string address;   // the sender's IPv4 address, "10.42.0.11"
    std::string name;      // the first question's name, see model::talk_clean_name()
};

// Reads the DNS question in one Ethernet frame the `names` filter passed:
// IPv4 (a whole header, not a fragment) with UDP or TCP to port 53. Over
// UDP the payload is the message; over TCP the segment's data must start
// with the two-byte length and hold the whole message (no reassembly: a
// message split over segments is not read). The message must be a
// standard query (not an answer, opcode 0) with at least one question and
// a name that is not the root. Every length is checked against the bytes
// captured (see dns::parse_message() for the message itself). False for
// anything else, also for a sender whose hardware address is a group
// address or all zeros.
bool read_talk_question(const uint8_t* frame, std::size_t size, TalkQuestion& out);

// Counts the questions per device. One frame at a time, with its time in
// milliseconds (any origin, as long as it does not go back). A question for
// the name a device asked within model::kTalkSameLookupMs of the last
// lookup counted for that name is the same lookup. A lookup moves its name
// to the top; at most model::kTalkNamesMax names per device (the oldest
// go) and model::kTalkDevicesMax devices (the one heard from longest ago
// goes), so a flood of made-up senders or names cannot make it grow.
class TalkCounter {
public:
    void note_frame(const uint8_t* frame, std::size_t size, int64_t at_ms);
    void note_question(const TalkQuestion& question, int64_t at_ms);

    // Copies what it holds into `run` (frames, lookups and devices).
    void fill(model::TalkRun& run) const;
    int32_t frames() const { return frames_; }
    int32_t lookups() const { return lookups_; }
    // True once after anything above changed.
    bool take_changed();

private:
    struct Name {
        std::string name;
        int32_t count{0};
        int64_t counted_ms{0};  // when the last lookup was counted
    };
    struct Device {
        std::string hardware;
        std::string address;
        std::vector<Name> names;  // the newest first
        int32_t lookups{0};
        int64_t last_ms{0};
    };

    std::vector<Device> devices_;
    int32_t frames_{0};
    int32_t lookups_{0};
    bool changed_{false};
};

// A made-up frame: the device `hardware` (6 bytes) at IPv4 `source` asks
// the hotspot at `destination` for `name` (type `type`) over UDP, or over
// TCP with the two-byte length when `tcp` is true. For the pretend runner
// and the checks (tests/).
std::vector<uint8_t> talk_question_frame(const uint8_t* hardware, uint32_t source, uint32_t destination,
                                         const std::string& name, uint16_t type = 1, bool tcp = false);

// The pretend runner's made-up lookups: the sample's three hotspot devices
// (phone, laptop, tv; see model::sample_home_data()) asking for names
// under example.com, .org and .net, in time order from the start.
struct TalkFrame {
    uint32_t at_ms{0};
    std::vector<uint8_t> bytes;
};
const std::vector<TalkFrame>& pretend_talk_frames();

// Runs the `names` capture on the hotspot's interface away from the UI
// thread and keeps its latest state for the screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first run and kept until shutdown(). It runs the helper's
// `names` command for model::kTalkHelperSeconds at a time, and again after
// a second each time it ends normally. While the interface has no IPv4
// address yet, or has gone (Share's own interface goes when Share stops),
// it tries again every few seconds and the run stays Running; a helper
// that is missing or may not run fails the run. Every wait is at most
// 100 ms, so a stop ends the helper within about 0.2 s. It sends nothing,
// keeps nothing on disk and never changes a network setting.
//
// The pretend runner (the Mac simulator and snapshots) has no thread and
// opens nothing: each take() feeds the made-up frames up to the LVGL time
// since start() through the same counter.
//
// All functions are called on the UI thread.
class TalkRunner {
public:
    virtual ~TalkRunner() = default;

    // Starts listening on `interface_name` (checked with
    // model::valid_interface_name()); the run then reads Running at once,
    // with nothing heard. Returns false and changes nothing while a run is
    // still under way or ending, and when the name is not valid.
    virtual bool start(const std::string& interface_name) = 0;
    // Asks the run to end; it then reads Stopped. Never waits.
    virtual void stop() = 0;
    // Copies the latest state into `run` and returns true, but only when it
    // changed since the last take().
    virtual bool take(model::TalkRun& run) = 0;
    // Stops the run and ends the thread, waiting for it. Safe to call more
    // than once; start() refuses every run afterwards.
    virtual void shutdown() = 0;
};

std::unique_ptr<TalkRunner> make_talk_runner(bool live);

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "survey_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The Wi-Fi survey (model::SurveyRun) with a USB Wi-Fi stick. The firewall
// helper's fixed `survey` command (src/helper/lokictl_helper.c) adds a
// listen-only (monitor mode) interface on the stick,
// model::kSurveyListenInterface, and hops it over the channels; the
// capture helper's fixed `survey` command (src/helper/lokictl_capture.c)
// passes on what it hears: whole beacons, probe answers and
// kick-off frames, and of every other frame only its radio header and its
// first two bytes (the frame kind). Everything here reads those without
// privileges and never sends.
namespace platform {

// What the radio header (radiotap) of one frame says.
struct RadioHeader {
    std::size_t length{0};      // bytes of the radio header; the Wi-Fi frame starts here
    int32_t frequency_mhz{0};   // 0 when not given
    int32_t signal_dbm{0};      // model::kSignalUnknown (0) when not given
    bool has_fcs{false};        // the frame ends in its 4-byte checksum
    bool bad_fcs{false};        // the checksum was wrong
};

// Reads the radiotap header at the start of `frame`: version 0, a length
// of at least 8 bytes that fits the frame, the chain of present words (bit
// 31 says another follows), then the fields of the first word in their
// order, each at its natural alignment from the header's start: TSFT (8
// bytes), flags (1), rate (1), channel (2 + 2), FHSS (2) and antenna
// signal (1, dBm). Later fields are not needed and not read. Every read is
// checked against the header's length. False when it is broken.
bool read_radio_header(const uint8_t* frame, std::size_t size, RadioHeader& out);

// What one Wi-Fi frame says, as far as the survey needs it.
struct WifiFrame {
    enum class Kind {
        Other,      // any other frame, or one too short to tell
        Beacon,     // a beacon or a probe answer: an access point
        Kickoff,    // a deauthentication or disassociation
    };
    Kind kind{Kind::Other};
    int32_t channel{0};          // where it was heard (radio header), 0 when not known
    int32_t signal_dbm{0};       // model::kSignalUnknown when not known
    // Beacon: the access point. Kickoff: the network named (address 3).
    std::string bssid;
    // Beacon only:
    std::string name;            // model::survey_clean_name()
    bool hidden{false};          // a name of length 0 or of zero bytes
    bool probe_answer{false};    // a probe answer, not a beacon
    int32_t own_channel{0};      // the channel the access point says it is on (DS element), 0 when not said
    std::string security;        // "open", "WEP", "WPA", "WPA2", "WPA2/3", "WPA3", "OWE", "WPA-EAP", ...
};

// Reads one frame the `survey` filter passed: the radio header, then the
// Wi-Fi frame (version 0). Every length is checked against the bytes
// captured; a frame with a wrong checksum, or one whose radio header is
// broken, is false (not counted). A frame cut short by the filter (only
// its kind) is an Other frame. Beacons and probe answers need the whole
// fixed part (24-byte header, 12-byte body); their elements are read as
// far as they are whole: the name (0), the channel (3), RSN (48) and the
// WPA vendor element (221, 00:50:f2 type 1). A kick-off needs the 24-byte
// header.
bool read_wifi_frame(const uint8_t* frame, std::size_t size, WifiFrame& out);

// Counts what the frames say into a model::SurveyRun: every frame on its
// channel, the access points (at most model::kSurveyNetworksMax; when full
// the one heard from longest ago makes room) and kick-off frames per
// channel and second (more than model::kSurveyKickoffsPerSecond in one
// second on one channel is an attack). One frame at a time, with its time:
// `seconds` from the capture (any origin, not going back) and `at_ms` on
// the runner's clock.
class SurveyCounter {
public:
    void note_frame(const uint8_t* frame, std::size_t size, uint32_t seconds, int64_t at_ms);
    void note(const WifiFrame& frame, uint32_t seconds, int64_t at_ms);

    // Copies what it holds into `run` (frames, channels, networks,
    // attacks); the state and times stay as they are.
    void fill(model::SurveyRun& run) const;
    // True once after anything above changed.
    bool take_changed();

private:
    struct Second {
        uint32_t second{0};
        int32_t count{0};
        bool valid{false};
    };
    model::SurveyRun run_;
    std::array<Second, model::kSurveyChannels> kickoff_seconds_{};
    bool changed_{false};
};

// Made-up frames for the pretend runner and the checks (tests/).
// A beacon of the access point `bssid` (6 bytes) named `name` (`hidden`:
// a name of zero length) on `channel`, heard at `signal_dbm`, with
// `security` ("open", "WEP", "WPA", "WPA2", "WPA2/3", "WPA3", "WPA2-EAP",
// "OWE").
std::vector<uint8_t> survey_beacon_frame(const uint8_t* bssid, const std::string& name, int32_t channel,
                                         int32_t signal_dbm, const std::string& security, bool hidden = false);
// A deauthentication from the access point `bssid` to everyone, heard on
// `channel`.
std::vector<uint8_t> survey_kickoff_frame(const uint8_t* bssid, int32_t channel);
// Any other frame (a data frame cut to its kind, as the filter passes it),
// heard on `channel`.
std::vector<uint8_t> survey_other_frame(int32_t channel);

// The pretend runner's made-up air: a few access points (a hidden one, an
// open copy of "Harbour-Cafe", the made-up joined network), a busy channel
// 6 and a kick-off attack on
// channel 11 from second 20 to 22. Adds the frames heard in second
// `second` of the run (0 is the first) to `out`, each timed from the start.
struct SurveyFrame {
    uint32_t at_ms{0};
    std::vector<uint8_t> bytes;
};
void pretend_survey_frames(uint32_t second, std::vector<SurveyFrame>& out);

// Runs a survey on a USB Wi-Fi stick away from the UI thread and keeps its
// latest state for the screen to take.
//
// The live runner (device builds only) has one thread of its own, started
// with the first job and kept until shutdown(). A survey starts the
// firewall helper's `survey` command (which adds the listening interface
// on the stick and hops; it ends and removes it when its standard input
// closes, on SIGTERM, or when LokiCTL dies) and, once it says it is ready,
// the capture helper's `survey` command on the listening interface, for
// model::kSurveySeconds in all. The stick's own interface is never
// changed. Before it starts it leaves a note (the stick's interface) in
// LokiCTL's runtime folder, and removes it once the listening interface
// is gone; a note left by a run that was killed makes recover() remove it
// with the helper's `survey-off`. Every wait is at most 100 ms, so a stop
// ends the capture at once and the listening interface is gone within
// about a second. It sends nothing and keeps nothing on disk but that
// note.
//
// The pretend runner (the Mac simulator and snapshots) has no thread and
// opens nothing: each take() feeds the made-up frames up to the LVGL time
// since start() through the same counter.
//
// All functions are called on the UI thread.
class SurveyRunner {
public:
    virtual ~SurveyRunner() = default;

    // Removes the listening interface a killed run left on the stick (live
    // only, on the runner's thread; only when its note is there). Call it
    // once, at the start.
    virtual void recover() = 0;
    // Starts a survey on `interface_name` (checked with
    // model::valid_interface_name(), never "wlan0" or
    // model::kSurveyListenInterface); the run then reads
    // Running at once. Returns false and changes nothing while a run is
    // under way or ending, or when the name is not allowed.
    virtual bool start(const std::string& interface_name) = 0;
    // Asks the run to end; it then reads Stopped. Never waits.
    virtual void stop() = 0;
    // Copies the latest state into `run` and returns true, but only when it
    // changed since the last take().
    virtual bool take(model::SurveyRun& run) = 0;
    // Stops the run (the listening interface goes) and ends the thread,
    // waiting for it. Safe to call more than once; start() refuses every
    // run afterwards.
    virtual void shutdown() = 0;
};

std::unique_ptr<SurveyRunner> make_survey_runner(bool live);

} // namespace platform

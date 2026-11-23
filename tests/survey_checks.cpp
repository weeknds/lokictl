/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the Wi-Fi survey (src/platform/survey.h and
// src/model/survey_data.h): radio headers and Wi-Fi frames read from
// made-up bytes, whole, cut short at every byte, broken and hostile (wrong
// versions and lengths, endless present words, elements past the end,
// hostile names, random bytes), and the counting: access points, hidden
// ones, look-alikes of the joined network, kick-off attacks, busy channels
// and the limits. Also the stick: which adapter a survey may use, and that
// the survey's own listening interface never stands in for the stick
// (src/platform/usb_wifi.h, on a made-up folder). Built on the Mac only,
// with the address and undefined-behaviour checkers, as
// lokictl_survey_tests; exits 1 when a check fails. Only made-up addresses
// and names. The one real piece is a radio header as a USB stick
// (ath9k_htc driver) wrote it, which holds no address or name.

#include "survey.h"
#include "usb_wifi.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

const uint8_t kHome[6] = {0x02, 0x00, 0x5e, 0x10, 0x00, 0x01};
const uint8_t kTwin[6] = {0x02, 0x00, 0x5e, 0x66, 0x00, 0x09};
const uint8_t kOffice[6] = {0x02, 0x00, 0x5e, 0x10, 0x00, 0x05};

platform::WifiFrame read(const Bytes& bytes, bool* ok = nullptr) {
    platform::WifiFrame frame;
    const bool read_ok = platform::read_wifi_frame(bytes.data(), bytes.size(), frame);
    if (ok) {
        *ok = read_ok;
    }
    return frame;
}

void check_good() {
    bool ok = false;
    platform::WifiFrame frame = read(platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -48, "WPA2"), &ok);
    expect(ok && frame.kind == platform::WifiFrame::Kind::Beacon, "a beacon reads as one");
    expect(frame.name == "Harbour-Cafe" && !frame.hidden, "its name");
    expect(frame.bssid == "02:00:5e:10:00:01", "its access point");
    expect(frame.channel == 6 && frame.own_channel == 6, "its channel");
    expect(frame.signal_dbm == -48, "its signal");
    expect(frame.security == "WPA2", "its security");

    const char* kinds[] = {"open", "WEP", "WPA", "WPA2", "WPA2/3", "WPA3", "WPA2-EAP", "OWE"};
    for (const char* kind : kinds) {
        expect(read(platform::survey_beacon_frame(kHome, "x", 1, -50, kind)).security == kind,
               std::string("security ") + kind);
    }

    frame = read(platform::survey_beacon_frame(kHome, "", 1, -71, "WPA2", true));
    expect(frame.kind == platform::WifiFrame::Kind::Beacon && frame.hidden && frame.name.empty(), "a hidden beacon");
    // A name of zero bytes hides it too.
    frame = read(platform::survey_beacon_frame(kHome, std::string(8, '\0'), 1, -71, "WPA2"));
    expect(frame.hidden && frame.name.empty(), "a name of zero bytes is hidden");

    frame = read(platform::survey_kickoff_frame(kOffice, 11));
    expect(frame.kind == platform::WifiFrame::Kind::Kickoff && frame.channel == 11 &&
               frame.bssid == "02:00:5e:10:00:05",
           "a kick-off frame");

    frame = read(platform::survey_other_frame(3), &ok);
    expect(ok && frame.kind == platform::WifiFrame::Kind::Other && frame.channel == 3, "a frame cut to its kind");

    // Channel 14 and a frequency off the 2.4 GHz grid.
    expect(model::survey_channel_of(2484) == 14 && model::survey_channel_of(2412) == 1 &&
               model::survey_channel_of(2472) == 13 && model::survey_channel_of(2413) == 0 &&
               model::survey_channel_of(5180) == 0,
           "frequencies to channels");

    // A checksum at the end: flags 0x10 strips it; 0x40 drops the frame.
    Bytes with_fcs = platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -48, "WPA2");
    with_fcs[8] = 0x10;
    with_fcs.insert(with_fcs.end(), {0xde, 0xad, 0xbe, 0xef});
    frame = read(with_fcs, &ok);
    expect(ok && frame.security == "WPA2" && frame.name == "Harbour-Cafe", "a checksum is not an element");
    with_fcs[8] = 0x50;
    read(with_fcs, &ok);
    expect(!ok, "a frame with a wrong checksum is not counted");
}

void check_broken() {
    bool ok = true;
    // Every cut of a beacon reads without running past the end.
    const Bytes beacon = platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -48, "WPA2/3");
    for (std::size_t size = 0; size <= beacon.size(); ++size) {
        const Bytes cut(beacon.begin(), beacon.begin() + static_cast<std::ptrdiff_t>(size));
        platform::WifiFrame frame;
        platform::read_wifi_frame(cut.data(), cut.size(), frame);
    }
    // Cut inside the radio header: not counted.
    read(Bytes(beacon.begin(), beacon.begin() + 7), &ok);
    expect(!ok, "a cut radio header");
    // A beacon without its fixed body is just a frame.
    platform::WifiFrame frame = read(Bytes(beacon.begin(), beacon.begin() + 15 + 30), &ok);
    expect(ok && frame.kind == platform::WifiFrame::Kind::Other, "a beacon cut before its body");

    // Radio header version 1, and lengths below 8 or past the end.
    Bytes bad = beacon;
    bad[0] = 1;
    read(bad, &ok);
    expect(!ok, "radio header version 1");
    bad = beacon;
    bad[2] = 4;
    read(bad, &ok);
    expect(!ok, "radio header shorter than 8");
    bad = beacon;
    bad[2] = 0xff;
    bad[3] = 0xff;
    read(bad, &ok);
    expect(!ok, "radio header longer than the frame");
    // Present words chained past the header's end.
    bad = Bytes{0x00, 0x00, 16, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    read(bad, &ok);
    expect(!ok, "endless present words");
    // A field past the header's end (channel bit set, header 9 bytes).
    bad = Bytes{0x00, 0x00, 9, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00};
    read(bad, &ok);
    expect(!ok, "a field past the header");
    // A Wi-Fi frame of another version is noise.
    bad = beacon;
    bad[15] = 0x81;
    read(bad, &ok);
    expect(!ok, "Wi-Fi version 1");

    // An element whose length runs past the end: the elements before it
    // count, it does not.
    Bytes running = platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -48, "open");
    running.push_back(48);
    running.push_back(200);
    running.push_back(0x01);
    frame = read(running);
    expect(frame.name == "Harbour-Cafe" && frame.security == "open", "an element running past the end");
    // An RSN element whose counts are far larger than it is.
    Bytes counts = platform::survey_beacon_frame(kHome, "x", 6, -48, "open");
    const uint8_t rsn[] = {48, 10, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0xff, 0xff, 0x00, 0x0f};
    counts.insert(counts.end(), rsn, rsn + sizeof(rsn));
    frame = read(counts, &ok);
    expect(ok && frame.security == "WPA2", "RSN counts past its end");
}

void check_hostile() {
    // Hostile names: control bytes, broken UTF-8, more than 32 bytes.
    platform::WifiFrame frame =
        read(platform::survey_beacon_frame(kHome, std::string("a\nb\x1b[2Jc\x7f"), 1, -50, "open"));
    expect(frame.name == "a?b?[2Jc?", "control bytes become ?");
    frame = read(platform::survey_beacon_frame(kHome, "caf\xc3\xa9", 1, -50, "open"));
    expect(frame.name == "caf\xc3\xa9", "valid UTF-8 is kept");
    frame = read(platform::survey_beacon_frame(kHome, "caf\xc3", 1, -50, "open"));
    expect(frame.name == "caf?", "cut UTF-8 becomes ?");
    frame = read(platform::survey_beacon_frame(kHome, "\xc0\xaf\xed\xa0\x80", 1, -50, "open"));
    expect(frame.name == "?????", "overlong and surrogate UTF-8 become ?");
    // A name element of 255 bytes is kept to 32.
    Bytes long_name = platform::survey_beacon_frame(kHome, "", 1, -50, "open", true);
    const std::size_t name_at = 15 + 24 + 12;
    long_name[name_at + 1] = 255;
    long_name.insert(long_name.begin() + static_cast<std::ptrdiff_t>(name_at + 2), 255, 'A');
    frame = read(long_name);
    expect(frame.name == std::string(32, 'A'), "a 255-byte name keeps 32");

    // Random bytes, and random bytes behind a good radio header.
    std::mt19937 random(913);
    for (int round = 0; round < 20000; ++round) {
        Bytes bytes(static_cast<std::size_t>(random() % 300));
        for (uint8_t& byte : bytes) {
            byte = static_cast<uint8_t>(random());
        }
        if (round % 2 == 0 && bytes.size() > 20) {
            const Bytes head = platform::survey_other_frame(1);
            std::copy(head.begin(), head.begin() + 15, bytes.begin());
        }
        platform::SurveyCounter counter;
        counter.note_frame(bytes.data(), bytes.size(), 1, 1);
    }
}

void check_counter() {
    platform::SurveyCounter counter;
    const auto feed = [&counter](const Bytes& bytes, uint32_t second, int64_t at_ms) {
        counter.note_frame(bytes.data(), bytes.size(), second, at_ms);
    };
    feed(platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -60, "WPA2"), 1, 1000);
    feed(platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -48, "WPA2"), 1, 1100);
    feed(platform::survey_beacon_frame(kTwin, "Harbour-Cafe", 11, -58, "open"), 1, 1200);
    feed(platform::survey_beacon_frame(kOffice, "", 1, -71, "WPA2", true), 1, 1300);
    expect(counter.take_changed() && !counter.take_changed(), "changed once");
    model::SurveyRun run;
    counter.fill(run);
    expect(run.networks.size() == 3 && run.networks_seen == 3 && run.frames == 4, "three access points");
    expect(run.networks[0].signal_dbm == -48 && run.networks[0].beacons == 2, "the strongest signal is kept");
    expect(run.networks[2].hidden && run.networks[2].name.empty(), "a hidden one");

    // A probe answer names the hidden one; it stays hidden.
    Bytes answer = platform::survey_beacon_frame(kOffice, "Back-Office", 1, -71, "WPA2");
    answer[15] = 0x50;
    feed(answer, 2, 2000);
    counter.fill(run);
    expect(run.networks[2].hidden && run.networks[2].name == "Back-Office", "a probe answer names a hidden one");

    // Look-alikes of the joined network: weaker security only.
    model::mark_survey_look_alikes(run, "Harbour-Cafe", "WPA2");
    expect(!run.networks[0].look_alike && run.networks[1].look_alike, "the open copy is a look-alike");
    model::mark_survey_look_alikes(run, "Harbour-Cafe", "open");
    expect(!run.networks[1].look_alike, "not weaker than an open joined network");
    model::mark_survey_look_alikes(run, "", "");
    expect(!run.networks[1].look_alike, "no joined network, no look-alike");
    model::SurveyRun same = run;
    same.networks[1].security = "WPA2";
    model::mark_survey_look_alikes(same, "Harbour-Cafe", "WPA2");
    expect(!same.networks[1].look_alike, "a second access point as strong is no look-alike");
    model::mark_survey_look_alikes(run, "Harbour-Cafe", "WPA2");
    const std::vector<std::size_t> order = model::survey_network_order(run);
    expect(order.size() == 3 && order[0] == 1, "look-alikes first");

    // Kick-off frames: 20 in one second are not an attack, 21 are.
    for (int index = 0; index < 20; ++index) {
        feed(platform::survey_kickoff_frame(kOffice, 11), 5, 5000 + index);
    }
    counter.fill(run);
    expect(run.attacks.empty() && run.kickoffs == 20, "20 kick-offs in a second are no attack");
    feed(platform::survey_kickoff_frame(kOffice, 11), 6, 6000);
    counter.fill(run);
    expect(run.attacks.empty(), "the count starts again each second");
    for (int index = 0; index < 21; ++index) {
        feed(platform::survey_kickoff_frame(kOffice, 11), 7, 7000 + index);
    }
    counter.fill(run);
    expect(run.attacks.size() == 1 && run.attacks[0].channel == 11 && run.attacks[0].peak == 21 &&
               run.attacks[0].bssid == "02:00:5e:10:00:05",
           "21 kick-offs in a second are an attack");
    for (int index = 0; index < 40; ++index) {
        feed(platform::survey_kickoff_frame(kOffice, 11), 8, 8000 + index);
    }
    counter.fill(run);
    expect(run.attacks.size() == 1 && run.attacks[0].peak == 40, "one attack per channel, the peak kept");
    expect(run.channels[10].kickoffs == 82, "kick-offs per channel");

    // Busy channels: frames per second of listening. (fill() leaves the
    // look-alike marks to the caller.)
    model::mark_survey_look_alikes(run, "Harbour-Cafe", "WPA2");
    run.elapsed_ms = 13000;  // one second on each channel
    run.channels[5].frames = 250;
    run.channels[0].frames = 40;
    run.channels[10].frames = 5;
    expect(model::survey_channel_load(run, 6) == model::ChannelLoad::Busy, "channel 6 busy");
    expect(model::survey_channel_load(run, 1) == model::ChannelLoad::InUse, "channel 1 in use");
    expect(model::survey_channel_load(run, 2) == model::ChannelLoad::Quiet, "channel 2 quiet");
    expect(model::survey_channel_level(run, 6) == 100 && model::survey_channel_level(run, 1) == 20, "bar levels");
    expect(model::survey_best_channel(run) != 6, "the best channel is not the busy one");
    const std::vector<model::SurveyFinding> findings = model::survey_findings(run);
    expect(findings.size() == 3 && findings[0].text == "Kick-off attack on ch 11" &&
               findings[1].text == "Look-alike of Harbour-Cafe" && findings[2].text == "Channel 6: busy",
           "the findings in order");
    expect(model::survey_warnings(run) == 2, "two warnings");
    model::SurveyRun fresh;
    expect(model::survey_channel_load(fresh, 6) == model::ChannelLoad::Unknown && model::survey_best_channel(fresh) == 0,
           "nothing to say before a second");

    // A flood of made-up access points never makes the list grow past its
    // limit; the oldest make room.
    platform::SurveyCounter flood;
    for (int index = 0; index < 1000; ++index) {
        const uint8_t bssid[6] = {0x02, 0x01, 0x00, 0x00, static_cast<uint8_t>(index >> 8),
                                  static_cast<uint8_t>(index)};
        const Bytes bytes = platform::survey_beacon_frame(bssid, "fake", 1 + index % 13, -70, "open");
        flood.note_frame(bytes.data(), bytes.size(), 1, index);
    }
    model::SurveyRun flooded;
    flood.fill(flooded);
    expect(flooded.networks.size() == model::kSurveyNetworksMax && flooded.networks_seen == 1000 &&
               flooded.networks.front().last_ms == 1000 - static_cast<int64_t>(model::kSurveyNetworksMax),
           "at most the limit, the newest kept");
}

void check_pretend() {
    platform::SurveyCounter counter;
    std::vector<platform::SurveyFrame> frames;
    for (uint32_t second = 0; second < 30; ++second) {
        frames.clear();
        platform::pretend_survey_frames(second, frames);
        for (const platform::SurveyFrame& frame : frames) {
            counter.note_frame(frame.bytes.data(), frame.bytes.size(), frame.at_ms / 1000, frame.at_ms);
        }
    }
    model::SurveyRun run;
    counter.fill(run);
    run.elapsed_ms = 30000;
    model::mark_survey_look_alikes(run, "Harbour-Cafe", "WPA2");
    expect(run.networks.size() == 7, "the made-up access points");
    expect(run.attacks.size() == 1 && run.attacks[0].channel == 11, "the made-up attack");
    expect(model::survey_channel_load(run, 6) == model::ChannelLoad::Busy, "the made-up busy channel");
    bool hidden = false;
    for (const model::SurveyNetwork& network : run.networks) {
        hidden = hidden || network.hidden;
    }
    expect(hidden, "a made-up hidden network");
    expect(model::survey_warnings(run) == 2, "the made-up warnings");
}

// Two radio headers (36 bytes) as a TL-WN722N v1 stick (ath9k_htc) wrote
// them in monitor mode: present words 0xa000402f and 0x00000820 (bit 31 of
// the first says another follows, for the per-antenna fields), padding to
// 16, TSFT (8 bytes), flags 0x10 (the frame ends in its checksum), rate,
// channel 2412 MHz (channel 1), the signal (-60 and -79 dBm), RX flags,
// then the second word's antenna signal and antenna.
const uint8_t kStickHeader[36] = {
    0x00, 0x00, 0x24, 0x00, 0x2f, 0x40, 0x00, 0xa0, 0x20, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x45, 0x3b, 0x4e, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x02, 0x6c, 0x09, 0xa0, 0x00, 0xc4, 0x00, 0x00, 0x00, 0xab, 0x00,
};
const uint8_t kStickHeaderOther[36] = {
    0x00, 0x00, 0x24, 0x00, 0x2f, 0x40, 0x00, 0xa0, 0x20, 0x08, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1f, 0x72, 0x4b, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x02, 0x6c, 0x09, 0xa0, 0x00, 0xb1, 0x00, 0x00, 0x00, 0xa9, 0x00,
};

void check_real_header() {
    platform::RadioHeader radio;
    expect(platform::read_radio_header(kStickHeader, sizeof(kStickHeader), radio), "a stick's radio header reads");
    expect(radio.length == 36 && radio.frequency_mhz == 2412 && radio.signal_dbm == -60, "its length, channel, signal");
    expect(radio.has_fcs && !radio.bad_fcs, "its checksum flag");

    // That header in front of a made-up open beacon (its own 15-byte
    // header dropped) and a checksum that would read as a security element
    // were it not stripped.
    const Bytes made_up = platform::survey_beacon_frame(kHome, "Harbour-Cafe", 6, -48, "open");
    Bytes frame(kStickHeader, kStickHeader + sizeof(kStickHeader));
    frame.insert(frame.end(), made_up.begin() + 15, made_up.end());
    frame.insert(frame.end(), {0x30, 0x02, 0x01, 0x00});
    bool ok = false;
    platform::WifiFrame read_frame = read(frame, &ok);
    expect(ok && read_frame.kind == platform::WifiFrame::Kind::Beacon, "a beacon behind a stick's header");
    expect(read_frame.channel == 1 && read_frame.own_channel == 6 && read_frame.signal_dbm == -60,
           "the channel heard on and the one it names");
    expect(read_frame.name == "Harbour-Cafe" && read_frame.bssid == "02:00:5e:10:00:01", "its name and access point");
    expect(read_frame.security == "open", "the checksum is stripped");
    for (std::size_t size = 0; size <= frame.size(); ++size) {
        platform::WifiFrame cut;
        platform::read_wifi_frame(frame.data(), size, cut);
    }

    // What the capture's filter passes of a data frame: the header and the
    // frame's kind (here a protected data frame from the access point).
    Bytes other(kStickHeaderOther, kStickHeaderOther + sizeof(kStickHeaderOther));
    other.insert(other.end(), {0x08, 0x62});
    read_frame = read(other, &ok);
    expect(ok && read_frame.kind == platform::WifiFrame::Kind::Other && read_frame.channel == 1 &&
               read_frame.signal_dbm == -79,
           "a data frame cut to its kind behind a stick's header");

    // The counter puts both on channel 1.
    platform::SurveyCounter counter;
    counter.note_frame(frame.data(), frame.size(), 0, 0);
    counter.note_frame(other.data(), other.size(), 0, 10);
    model::SurveyRun run;
    counter.fill(run);
    expect(run.frames == 2 && run.channels[0].frames == 2 && run.networks.size() == 1, "both counted on channel 1");
}

// read_usb_wifi() on a made-up folder: the stick's own interface (wlan1)
// and the survey's listening interface on the same stick. The stick keeps
// its name; the listening interface sorts first but is never read.
void check_listen_interface() {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path root = fs::temp_directory_path(error) / "lokictl-survey-checks";
    fs::remove_all(root, error);
    const fs::path usb = root / "sys/devices/usb1/1-1/1-1:1.0";
    fs::create_directories(usb, error);
    std::ofstream(usb / "uevent") << "DEVTYPE=usb_interface\nDRIVER=ath9k_htc\nPRODUCT=cf3/9271/108\n";
    for (const char* name : {"wlan1", "lokimon0"}) {
        const fs::path interface_dir = root / "sys/class/net" / name;
        fs::create_directories(interface_dir / "wireless", error);
        fs::create_directory_symlink(usb, interface_dir / "device", error);
    }
    std::vector<model::UsbWifiAdapter> adapters = platform::read_usb_wifi(root);
    expect(adapters.size() == 1 && adapters[0].interface_name == "wlan1" && adapters[0].usb_id == "0cf3:9271",
           "the listening interface never stands in for the stick");
    fs::remove_all(root, error);
}

void check_stick() {
    model::HomeData home;
    std::string why;
    expect(model::survey_stick(home, why).empty() && why == "none", "no stick");
    model::UsbWifiAdapter stick;
    stick.interface_name = "wlan1";
    stick.managed = model::Managed::Yes;
    home.usb_wifi.push_back(stick);
    expect(model::survey_stick(home, why).empty() && why == "managed", "a managed stick is not used");
    home.usb_wifi[0].managed = model::Managed::Unknown;
    expect(model::survey_stick(home, why).empty() && why == "unknown", "without the service it is not used");
    home.usb_wifi[0].managed = model::Managed::No;
    expect(model::survey_stick(home, why) == "wlan1" && why.empty(), "an unmanaged stick is used");
    home.usb_wifi[0].interface_name = "wlan0";
    expect(model::survey_stick(home, why).empty(), "never wlan0");
    home.usb_wifi[0].interface_name = "../x";
    expect(model::survey_stick(home, why).empty(), "never a bad name");
    home.usb_wifi[0].interface_name = "lokimon0";
    stick.managed = model::Managed::No;
    home.usb_wifi.push_back(stick);
    expect(model::survey_stick(home, why) == "wlan1", "never the survey's own listening interface");
}

void check_joined() {
    model::HomeData home;
    std::string name;
    std::string security;
    expect(!model::survey_joined_wifi(home, name, security) && name.empty(), "no joined network");
    home.uplink_kind = model::LinkKind::Wifi;
    home.wifi_name = "Harbour-Cafe";
    home.wifi_security = "WPA2";
    expect(model::survey_joined_wifi(home, name, security) && name == "Harbour-Cafe" && security == "WPA2",
           "the Wi-Fi uplink's network");
    // On a cable uplink the Wi-Fi values are empty; the scan list's joined
    // network stands in.
    home.uplink_kind = model::LinkKind::Cable;
    home.wifi_name.clear();
    home.wifi_security.clear();
    model::WifiNetwork other{"Library-Guest", "open", 40, 1, false};
    model::WifiNetwork joined{"Harbour-Cafe", "WPA2/3", 70, 6, true};
    home.wifi_scan.networks = {other, joined};
    expect(model::survey_joined_wifi(home, name, security) && name == "Harbour-Cafe" && security == "WPA2/3",
           "the scan list's joined network on a cable uplink");
}

} // namespace

int main() {
    check_good();
    check_broken();
    check_hostile();
    check_counter();
    check_pretend();
    check_real_header();
    check_listen_interface();
    check_stick();
    check_joined();
    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("survey checks: all passed\n");
    return 0;
}

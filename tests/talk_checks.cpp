/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of "What is it talking to?" (src/platform/talk.h and
// src/model/talk_data.h): DNS questions read from made-up frames over UDP
// and TCP, whole, cut short at every byte, broken and hostile (answers,
// other opcodes, compression loops, long and unprintable names, lengths
// past the end, random bytes), and the counting per device (the same
// lookup, the newest first, the limits). Built on the Mac only, with the
// address and undefined-behaviour checkers, as lokictl_talk_tests; exits 1
// when a check fails. Only made-up addresses and names: no real capture.

#include "talk.h"

#include <cstdint>
#include <cstdio>
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

const uint8_t kPhone[6] = {0x02, 0x5e, 0x11, 0x7a, 0xc3, 0x01};
constexpr uint32_t kHotspot = 0x0a2a0001;  // 10.42.0.1
constexpr uint32_t kPhoneAddress = 0x0a2a000b;  // 10.42.0.11

void put16(Bytes& out, std::size_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void put32(Bytes& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

// A DNS header: id, flags, then the four counts.
Bytes dns_header(uint16_t flags, uint16_t questions, uint16_t answers = 0) {
    Bytes out;
    put16(out, 0x1234);
    put16(out, flags);
    put16(out, questions);
    put16(out, answers);
    put16(out, 0);
    put16(out, 0);
    return out;
}

// A frame from the phone to the hotspot's port 53 around `payload`: over
// UDP, or over TCP (the payload as the segment's data, so it must carry
// the two-byte length itself).
Bytes frame_around(const Bytes& payload, bool tcp, const uint8_t* sender = kPhone) {
    Bytes frame{0x02, 0x00, 0x00, 0x00, 0x7f, 0x01};
    frame.insert(frame.end(), sender, sender + 6);
    put16(frame, 0x0800);
    const std::size_t transport = tcp ? 20 : 8;
    put16(frame, 0x4500);
    put16(frame, 20 + transport + payload.size());
    put16(frame, 1);
    put16(frame, 0x4000);
    frame.push_back(64);
    frame.push_back(tcp ? 6 : 17);
    put16(frame, 0);
    put32(frame, kPhoneAddress);
    put32(frame, kHotspot);
    put16(frame, 40000);
    put16(frame, 53);
    if (tcp) {
        put32(frame, 1);
        put32(frame, 1);
        frame.push_back(0x50);
        frame.push_back(0x18);
        put16(frame, 1000);
        put16(frame, 0);
        put16(frame, 0);
    } else {
        put16(frame, 8 + payload.size());
        put16(frame, 0);
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

Bytes with_length(const Bytes& message) {
    Bytes out;
    put16(out, message.size());
    out.insert(out.end(), message.begin(), message.end());
    return out;
}

bool read(const Bytes& frame, platform::TalkQuestion& question) {
    return platform::read_talk_question(frame.data(), frame.size(), question);
}

bool printable(const std::string& text) {
    for (const char character : text) {
        if (character < '!' || character > '~') {
            return false;
        }
    }
    return true;
}

void check_good() {
    platform::TalkQuestion question;
    const Bytes udp = platform::talk_question_frame(kPhone, kPhoneAddress, kHotspot, "Example.COM");
    expect(read(udp, question) && question.name == "example.com" && question.hardware == "02:5e:11:7a:c3:01" &&
               question.address == "10.42.0.11",
           "udp: a question reads, lowercase");
    const Bytes tcp = platform::talk_question_frame(kPhone, kPhoneAddress, kHotspot, "www.example.org", 28, true);
    expect(read(tcp, question) && question.name == "www.example.org", "tcp: a question with its length reads");

    // A frame with padding after the packet (short frames are padded).
    Bytes padded = udp;
    padded.insert(padded.end(), 20, 0);
    expect(read(padded, question) && question.name == "example.com", "udp: padding after the packet is ignored");

    // Every cut short: never read past the end, and nothing whole is made up.
    for (const Bytes* frame : {&udp, &tcp}) {
        for (std::size_t size = 0; size < frame->size(); ++size) {
            const Bytes cut(frame->begin(), frame->begin() + static_cast<std::ptrdiff_t>(size));
            expect(!read(cut, question), "cut at " + std::to_string(size) + " is not read");
        }
    }
}

void check_refused() {
    platform::TalkQuestion question;
    const Bytes good = platform::talk_question_frame(kPhone, kPhoneAddress, kHotspot, "example.com");
    const std::size_t dns = 14 + 20 + 8;

    Bytes answer = good;
    answer[dns + 2] |= 0x80;
    expect(!read(answer, question), "an answer is not a question");
    Bytes notify = good;
    notify[dns + 2] = static_cast<uint8_t>((notify[dns + 2] & 0x87) | (4 << 3));
    expect(!read(notify, question), "opcode 4 (notify) is not read");
    Bytes none = good;
    none[dns + 4] = 0;
    none[dns + 5] = 0;
    expect(!read(none, question), "no question is not read");

    Bytes root = dns_header(0x0100, 1);
    root.push_back(0);
    put16(root, 1);
    put16(root, 1);
    expect(!read(frame_around(root, false), question), "the root name is not read");

    Bytes fragment = good;
    fragment[14 + 6] = 0x20;  // more fragments
    expect(!read(fragment, question), "a fragment is not read");
    Bytes later = good;
    later[14 + 7] = 0x08;  // a fragment offset
    expect(!read(later, question), "a later fragment is not read");
    Bytes short_header = good;
    short_header[14] = 0x44;
    expect(!read(short_header, question), "an IPv4 header under 20 bytes is not read");
    Bytes long_header = good;
    long_header[14] = 0x4f;  // 60 bytes: past the UDP header, the port is then not 53
    expect(!read(long_header, question), "a header length past the packet is not read");
    Bytes version6 = good;
    version6[14] = 0x65;
    expect(!read(version6, question), "not IPv4 inside an IPv4 frame is not read");
    Bytes tiny_total = good;
    tiny_total[14 + 2] = 0;
    tiny_total[14 + 3] = 10;
    expect(!read(tiny_total, question), "a total length under the header is not read");
    Bytes other_port = good;
    other_port[14 + 20 + 3] = 54;
    expect(!read(other_port, question), "another port is not read");
    Bytes other_protocol = good;
    other_protocol[14 + 9] = 1;
    expect(!read(other_protocol, question), "ICMP is not read");
    Bytes other_type = good;
    other_type[12] = 0x86;
    other_type[13] = 0xdd;
    expect(!read(other_type, question), "IPv6 is not read");
    Bytes udp_short = good;
    udp_short[14 + 20 + 4] = 0;
    udp_short[14 + 20 + 5] = 7;
    expect(!read(udp_short, question), "a UDP length under 8 is not read");
    Bytes udp_cut = good;
    udp_cut[14 + 20 + 4] = 0;
    udp_cut[14 + 20 + 5] = 8 + 13;  // the message cut inside its question
    expect(!read(udp_cut, question), "a UDP length that cuts the message is not read");

    const uint8_t group[6] = {0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb};
    expect(!read(platform::talk_question_frame(group, kPhoneAddress, kHotspot, "example.com"), question),
           "a group sender is not read");
    const uint8_t zeros[6] = {0, 0, 0, 0, 0, 0};
    expect(!read(platform::talk_question_frame(zeros, kPhoneAddress, kHotspot, "example.com"), question),
           "an all-zero sender is not read");

    // TCP: the data offset, and the length in front of the message.
    const Bytes tcp = platform::talk_question_frame(kPhone, kPhoneAddress, kHotspot, "example.com", 1, true);
    Bytes small_offset = tcp;
    small_offset[14 + 20 + 12] = 0x40;
    expect(!read(small_offset, question), "tcp: a data offset under 20 is not read");
    Bytes big_offset = tcp;
    big_offset[14 + 20 + 12] = 0xf0;
    expect(!read(big_offset, question), "tcp: a data offset past the end is not read");
    Bytes long_length = tcp;
    long_length[14 + 40] = 0x01;  // 256 + the real length: more than the segment holds
    expect(!read(long_length, question), "tcp: a length past the segment is not read");
    Bytes message = dns_header(0x0100, 1);
    for (const char* label : {"example", "com"}) {
        message.push_back(static_cast<uint8_t>(std::string(label).size()));
        message.insert(message.end(), label, label + std::string(label).size());
    }
    message.push_back(0);
    put16(message, 1);
    put16(message, 1);
    expect(!read(frame_around(message, true), question), "tcp: a message without its length is not read");
    expect(read(frame_around(with_length(message), true), question) && question.name == "example.com",
           "tcp: the same message with its length reads");
    Bytes two = with_length(message);
    const Bytes second = with_length(message);
    two.insert(two.end(), second.begin(), second.end());
    expect(read(frame_around(two, true), question) && question.name == "example.com",
           "tcp: two messages in one segment: the first reads");
    expect(!read(frame_around({0x00}, true), question), "tcp: one byte of data is not read");
    expect(!read(frame_around({}, true), question), "tcp: no data (a bare ACK) is not read");
}

void check_hostile() {
    platform::TalkQuestion question;

    // A pointer to itself, and a pointer forwards: compression loops.
    Bytes self = dns_header(0x0100, 1);
    self.push_back(0xc0);
    self.push_back(12);
    put16(self, 1);
    put16(self, 1);
    expect(!read(frame_around(self, false), question), "a name pointing to itself is not read");
    Bytes forward = dns_header(0x0100, 1);
    forward.push_back(0xc0);
    forward.push_back(20);
    put16(forward, 1);
    put16(forward, 1);
    forward.insert(forward.end(), {3, 'c', 'o', 'm', 0});
    expect(!read(frame_around(forward, false), question), "a name pointing forwards is not read");
    // A pointer back into the header: read as labels, cleaned, never past
    // the header.
    Bytes back = dns_header(0x0100, 1);
    back[0] = 2;  // the id becomes a label of two bytes...
    back[3] = 0;  // ...and the flags' low byte ends the name
    back.push_back(0xc0);
    back.push_back(0);
    put16(back, 1);
    put16(back, 1);
    if (read(frame_around(back, false), question)) {
        expect(printable(question.name), "a name read from the header is printable");
    }
    // A reserved label kind.
    Bytes reserved = dns_header(0x0100, 1);
    reserved.insert(reserved.end(), {0x40, 'a', 0});
    put16(reserved, 1);
    put16(reserved, 1);
    expect(!read(frame_around(reserved, false), question), "a reserved label kind is not read");
    // A label running past the end.
    Bytes past = dns_header(0x0100, 1);
    past.insert(past.end(), {60, 'a', 'b'});
    expect(!read(frame_around(past, false), question), "a label past the end is not read");

    // Unprintable and markup bytes in a label.
    Bytes ugly = dns_header(0x0100, 1);
    const std::string label = std::string("ev\x01l\x1b[31m<b>\xff\x7f", 14);
    ugly.push_back(static_cast<uint8_t>(label.size()));
    ugly.insert(ugly.end(), label.begin(), label.end());
    ugly.insert(ugly.end(), {3, 'c', 'o', 'm', 0});
    put16(ugly, 1);
    put16(ugly, 1);
    expect(read(frame_around(ugly, false), question) && printable(question.name) &&
               question.name == "ev?l?[31m<b>??.com",
           "unprintable bytes become '?'");

    // The longest name there is (4 labels, 253 characters): kept short,
    // with its end.
    Bytes longest = dns_header(0x0100, 1);
    std::string text;
    for (int index = 0; index < 4; ++index) {
        const std::size_t size = index < 3 ? 63 : 61;
        longest.push_back(static_cast<uint8_t>(size));
        for (std::size_t at = 0; at < size; ++at) {
            longest.push_back(static_cast<uint8_t>('a' + index));
        }
    }
    longest.push_back(0);
    put16(longest, 1);
    put16(longest, 1);
    expect(read(frame_around(longest, false), question) && question.name.size() == model::kTalkNameMax &&
               question.name.rfind("..", 0) == 0 && question.name.substr(question.name.size() - 3) == "ddd",
           "a 253-character name is kept as its end");
    // One label more: over 255 bytes in wire form, refused.
    Bytes too_long = longest;
    too_long.resize(too_long.size() - 5);
    too_long.insert(too_long.end(), {1, 'e', 0});
    put16(too_long, 1);
    put16(too_long, 1);
    expect(!read(frame_around(too_long, false), question), "a name over 255 bytes is not read");

    // A huge answer count in a question: refused, nothing reserved.
    Bytes counts = dns_header(0x0100, 1, 0xffff);
    counts.insert(counts.end(), {3, 'c', 'o', 'm', 0});
    put16(counts, 1);
    put16(counts, 1);
    expect(!read(frame_around(counts, false), question), "a huge answer count is not read");

    // Random bytes behind a good header: never crashes; anything read is
    // printable and short.
    std::mt19937 random(911);
    for (int round = 0; round < 20000; ++round) {
        Bytes payload = dns_header(0x0100, static_cast<uint16_t>(1 + random() % 3));
        const std::size_t size = random() % 300;
        for (std::size_t at = 0; at < size; ++at) {
            payload.push_back(static_cast<uint8_t>(random()));
        }
        const bool tcp = (round % 3) == 0;
        const Bytes frame = frame_around(tcp ? with_length(payload) : payload, tcp);
        if (read(frame, question)) {
            expect(printable(question.name) && question.name.size() <= model::kTalkNameMax,
                   "random: what reads is printable and short");
        }
        // The whole frame random too.
        Bytes noise(14 + random() % 120);
        for (auto& byte : noise) {
            byte = static_cast<uint8_t>(random());
        }
        if (read(noise, question)) {
            expect(printable(question.name), "noise: what reads is printable");
        }
    }
}

void note(platform::TalkCounter& counter, const uint8_t* hardware, const std::string& name, int64_t at_ms,
          uint16_t type = 1) {
    const Bytes frame = platform::talk_question_frame(hardware, kPhoneAddress, kHotspot, name, type);
    counter.note_frame(frame.data(), frame.size(), at_ms);
}

void check_counter() {
    platform::TalkCounter counter;
    note(counter, kPhone, "example.com", 1000);
    note(counter, kPhone, "example.com", 1000, 28);   // AAAA: the same lookup
    note(counter, kPhone, "example.com", 2500, 65);   // a third type, still within 2 s
    note(counter, kPhone, "www.example.org", 3000);
    note(counter, kPhone, "example.com", 3000);       // 2 s after the first: a new lookup
    model::TalkRun run;
    counter.fill(run);
    expect(run.frames == 5 && run.lookups == 3, "frames and lookups counted");
    const model::TalkDevice* phone = model::talk_device(run, "02:5e:11:7a:c3:01");
    expect(phone && phone->names.size() == 2 && phone->names[0].name == "example.com" &&
               phone->names[0].count == 2 && phone->names[1].name == "www.example.org" &&
               phone->names[1].count == 1 && phone->lookups == 3 && phone->address == "10.42.0.11",
           "a new lookup moves its name to the top");
    expect(counter.take_changed() && !counter.take_changed(), "changed once");

    // At most 50 names: the oldest go.
    platform::TalkCounter many;
    for (int index = 0; index < 60; ++index) {
        note(many, kPhone, "n" + std::to_string(index) + ".example.com", index * 10);
    }
    many.fill(run);
    expect(run.devices.size() == 1 && run.devices[0].names.size() == model::kTalkNamesMax &&
               run.devices[0].names.front().name == "n59.example.com" &&
               run.devices[0].names.back().name == "n10.example.com" && run.devices[0].lookups == 60,
           "at most 50 names, the newest first");

    // At most 32 devices: the one heard from longest ago goes.
    platform::TalkCounter crowd;
    for (int index = 0; index < 40; ++index) {
        uint8_t hardware[6] = {0x02, 0, 0, 0, 0, static_cast<uint8_t>(index + 1)};
        note(crowd, hardware, "example.com", index * 10);
    }
    uint8_t first[6] = {0x02, 0, 0, 0, 0, 9};
    note(crowd, first, "example.org", 1000);  // device 9 heard again: it stays
    uint8_t newest[6] = {0x02, 0, 0, 0, 0, 99};
    note(crowd, newest, "example.net", 1001);
    crowd.fill(run);
    expect(run.devices.size() == model::kTalkDevicesMax, "at most 32 devices");
    expect(model::talk_device(run, "02:00:00:00:00:09") && model::talk_device(run, "02:00:00:00:00:63") &&
               !model::talk_device(run, "02:00:00:00:00:0a"),
           "the device heard from longest ago goes");

    // The pretend runner's lookups, all of them.
    platform::TalkCounter pretend;
    for (const auto& frame : platform::pretend_talk_frames()) {
        expect(!frame.bytes.empty(), "pretend frame built");
        pretend.note_frame(frame.bytes.data(), frame.bytes.size(), frame.at_ms);
    }
    pretend.fill(run);
    const model::TalkDevice* tv = model::talk_device(run, "02:5e:11:7a:c3:03");
    phone = model::talk_device(run, "02:5e:11:7a:c3:01");
    const model::TalkDevice* laptop = model::talk_device(run, "02:5e:11:7a:c3:02");
    expect(run.devices.size() == 3 && phone && laptop && tv, "pretend: three devices");
    expect(phone && phone->names.front().name == "chat.example.com" && phone->names[1].name == "example.com" &&
               phone->names[1].count == 3,
           "pretend: the phone's newest first, example.com x3");
    expect(tv && tv->names.front().name == "ads.example.com" && tv->names.front().count == 2 &&
               model::talk_device(run, "02:5e:11:7a:c3:03")->names.size() == 3,
           "pretend: the tv's names, one over TCP");
    expect(laptop && laptop->names.front().name == "example.com", "pretend: the laptop's newest first");
}

void check_words() {
    expect(model::talk_clean_name("A\x01Z") == "a?z", "clean: lowercase, unprintable '?'");
    expect(model::talk_clean_name("a b") == "a?b", "clean: a space is not printable here");
    expect(model::talk_tail("abcdef", 6) == "abcdef", "tail: fits");
    expect(model::talk_tail("abcdef", 5) == "..def", "tail: cut from the left");
    expect(model::talk_tail("abcdef", 2) == "ef", "tail: no room for dots");
    expect(model::talk_tail("abcdef", 0).empty(), "tail: no room");
}

} // namespace

int main() {
    check_good();
    check_refused();
    check_hostile();
    check_counter();
    check_words();
    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("talk checks: all passed\n");
    return 0;
}

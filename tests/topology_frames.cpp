/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Checks of the LLDP and CDP reading (src/platform/topology.h) and the
// Map's topology words (model::neighbour_* in tool_data.h): made-up frames,
// whole, cut short at every byte, with fields running past the end, with
// hostile texts and with random bytes. Built on the Mac only, with the
// address and undefined-behaviour checkers, as lokictl_topology_tests;
// exits 1 when a check fails. Only made-up addresses: no real capture.

#include "topology.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

namespace {

using platform::HeardNeighbours;
using platform::parse_cdp;
using platform::parse_lldp;

int failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }
}

using Bytes = std::vector<uint8_t>;

const Bytes kLldpGroup{0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e};
const Bytes kCdpGroup{0x01, 0x00, 0x0c, 0xcc, 0xcc, 0xcc};
const Bytes kSwitch{0x02, 0x5c, 0x00, 0x00, 0x00, 0x05};
const Bytes kRouter{0x02, 0x1a, 0x2b, 0x3c, 0x00, 0x01};

void put16(Bytes& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xffU));
}

Bytes text_bytes(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

Bytes join(Bytes head, const Bytes& tail) {
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

void lldp_field(Bytes& out, uint8_t type, const Bytes& value) {
    put16(out, static_cast<uint16_t>((type << 9) | (value.size() & 0x1ffU)));
    out.insert(out.end(), value.begin(), value.end());
}

Bytes lldp_head(const Bytes& sender) {
    Bytes frame = join(kLldpGroup, sender);
    put16(frame, 0x88cc);
    return frame;
}

// A full LLDP frame as a managed switch sends it.
Bytes lldp_frame(const std::string& name = "office-sw") {
    Bytes frame = lldp_head(kSwitch);
    lldp_field(frame, 1, join({4}, {0x02, 0x5c, 0x00, 0x00, 0x00, 0x00}));
    lldp_field(frame, 2, join({7}, text_bytes("5")));
    lldp_field(frame, 3, {0x00, 0x78});
    lldp_field(frame, 4, text_bytes("Port 5"));
    lldp_field(frame, 5, text_bytes(name));
    lldp_field(frame, 6, text_bytes("8-port managed switch\r\nfirmware 2.1"));
    lldp_field(frame, 8, {5, 1, 10, 0, 0, 2, 2, 0, 0, 0, 5, 0});
    lldp_field(frame, 127, {0x00, 0x80, 0xc2, 1, 0x00, 10});
    lldp_field(frame, 127, join({0x00, 0x80, 0xc2, 3, 0x00, 1, 7}, text_bytes("default")));
    lldp_field(frame, 127, join({0x00, 0x80, 0xc2, 3, 0x00, 20, 6}, text_bytes("guests")));
    lldp_field(frame, 0, {});
    return frame;
}

void cdp_field(Bytes& out, uint16_t type, const Bytes& value) {
    put16(out, type);
    put16(out, static_cast<uint16_t>(value.size() + 4));
    out.insert(out.end(), value.begin(), value.end());
}

// An IPv4 entry of a CDP address field, with a count of one.
Bytes cdp_address(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return {0, 0, 0, 1, 1, 1, 0xcc, 0, 4, a, b, c, d};
}

// A CDP frame with `body` (its fields) after the SNAP and CDP headers;
// the 802.3 length is the real one unless `length` is given.
Bytes cdp_frame(const Bytes& body, int length = -1, const Bytes& sender = kSwitch) {
    Bytes payload{0xaa, 0xaa, 0x03, 0x00, 0x00, 0x0c, 0x20, 0x00, 0x02, 0xb4, 0x00, 0x00};
    payload.insert(payload.end(), body.begin(), body.end());
    Bytes frame = join(kCdpGroup, sender);
    put16(frame, static_cast<uint16_t>(length < 0 ? payload.size() : static_cast<std::size_t>(length)));
    return join(frame, payload);
}

Bytes cdp_body() {
    Bytes body;
    cdp_field(body, 0x0001, text_bytes("core-sw.example"));
    cdp_field(body, 0x0002, cdp_address(192, 0, 2, 9));
    cdp_field(body, 0x0003, text_bytes("GigabitEthernet0/5"));
    cdp_field(body, 0x0006, text_bytes("cisco WS-C2960"));
    cdp_field(body, 0x000a, {0x00, 20});
    cdp_field(body, 0x0016, cdp_address(192, 0, 2, 10));
    return body;
}

bool clean(const model::Neighbour& n) {
    for (const std::string* text : {&n.protocol, &n.hardware_address, &n.chassis, &n.name, &n.port, &n.model,
                                     &n.address}) {
        if (text->size() > model::kNeighbourTextMax) {
            return false;
        }
        for (const char c : *text) {
            if (c < 0x20 || c > 0x7e) {
                return false;
            }
        }
    }
    return n.vlans.size() <= model::kNeighbourVlansMax && n.vlan >= 0 && n.vlan <= 4094;
}

void check_lldp() {
    const Bytes frame = lldp_frame();
    model::Neighbour n;
    expect(parse_lldp(frame.data(), frame.size(), n), "lldp: whole frame reads");
    expect(n.protocol == "LLDP", "lldp: protocol");
    expect(n.hardware_address == "02:5c:00:00:00:05", "lldp: sender");
    expect(n.chassis == "02:5c:00:00:00:00", "lldp: chassis");
    expect(n.port == "5", "lldp: port id wins over the description");
    expect(n.name == "office-sw", "lldp: name");
    expect(n.model == "8-port managed switch", "lldp: model is the first line");
    expect(n.address == "10.0.0.2", "lldp: management address");
    expect(n.vlan == 10, "lldp: port vlan");
    expect(n.vlans == std::vector<int32_t>({1, 20}), "lldp: vlan names, ascending");
    expect(!parse_cdp(frame.data(), frame.size(), n), "lldp is not cdp");

    // Every frame cut short reads without going past its end, and keeps
    // only what was whole.
    for (std::size_t size = 0; size < frame.size(); ++size) {
        Bytes cut(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(size));
        model::Neighbour part;
        const bool read = parse_lldp(cut.data(), cut.size(), part);
        expect(!read || clean(part), "lldp: cut at " + std::to_string(size) + " stays clean");
        if (read && size < 40) {
            expect(part.name.empty(), "lldp: cut at " + std::to_string(size) + " has no name yet");
        }
    }
    expect(!parse_lldp(nullptr, 0, n), "lldp: no frame");

    // A field running past the end: the fields before it stay.
    {
        Bytes bad = lldp_head(kSwitch);
        lldp_field(bad, 5, text_bytes("edge-sw"));
        put16(bad, static_cast<uint16_t>((4 << 9) | 300));  // says 300 bytes, has 3
        bad.insert(bad.end(), {'a', 'b', 'c'});
        model::Neighbour part;
        expect(parse_lldp(bad.data(), bad.size(), part) && part.name == "edge-sw" && part.port.empty(),
               "lldp: field past the end ends the reading");
    }
    // A sender that is a group address, and an empty frame body.
    {
        Bytes group = lldp_frame();
        group[6] = 0x01;
        expect(!parse_lldp(group.data(), group.size(), n), "lldp: group sender refused");
        Bytes empty = lldp_head(kSwitch);
        lldp_field(empty, 0, {});
        expect(!parse_lldp(empty.data(), empty.size(), n), "lldp: nothing said, nothing read");
    }
    // Hostile texts: control bytes, a very long name, only spaces.
    {
        Bytes hostile = lldp_head(kSwitch);
        lldp_field(hostile, 5, join(text_bytes("<sw>\x1b[31m"), Bytes(400, 'A')));
        lldp_field(hostile, 4, text_bytes("     "));
        lldp_field(hostile, 2, {5});  // a port ID with no text
        model::Neighbour h;
        expect(parse_lldp(hostile.data(), hostile.size(), h), "lldp: hostile frame reads");
        expect(clean(h) && h.name.size() == model::kNeighbourTextMax && h.name.rfind("<sw>?[31m", 0) == 0,
               "lldp: hostile name cleaned and cut");
        expect(h.port.empty(), "lldp: blank port stays empty");
    }
    // A port ID that is a hardware address: the description is used.
    {
        Bytes mac_port = lldp_head(kSwitch);
        lldp_field(mac_port, 2, join({3}, kSwitch));
        lldp_field(mac_port, 4, text_bytes("eth4"));
        model::Neighbour m;
        expect(parse_lldp(mac_port.data(), mac_port.size(), m) && m.port == "eth4", "lldp: mac port id, description used");
    }
    // Management addresses that no device can have, and a bad VLAN ID.
    {
        Bytes odd = lldp_head(kSwitch);
        lldp_field(odd, 5, text_bytes("sw"));
        lldp_field(odd, 8, {5, 1, 127, 0, 0, 1, 2, 0, 0, 0, 0, 0});
        lldp_field(odd, 8, {5, 1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0});
        lldp_field(odd, 8, {5, 1, 10, 0});  // too short
        lldp_field(odd, 127, {0x00, 0x80, 0xc2, 1, 0x0f, 0xff});
        lldp_field(odd, 127, {0x00, 0x80, 0xc2, 3, 0x00});  // too short
        model::Neighbour o;
        expect(parse_lldp(odd.data(), odd.size(), o) && o.address.empty() && o.vlan == 0 && o.vlans.empty(),
               "lldp: impossible address and vlan ignored");
    }
}

void check_cdp() {
    const Bytes frame = cdp_frame(cdp_body());
    model::Neighbour n;
    expect(parse_cdp(frame.data(), frame.size(), n), "cdp: whole frame reads");
    expect(n.protocol == "CDP" && n.chassis.empty(), "cdp: protocol");
    expect(n.name == "core-sw.example", "cdp: device id");
    expect(n.port == "GigabitEthernet0/5", "cdp: port");
    expect(n.model == "cisco WS-C2960", "cdp: platform");
    expect(n.address == "192.0.2.10", "cdp: management address first");
    expect(n.vlan == 20, "cdp: native vlan");
    expect(!parse_lldp(frame.data(), frame.size(), n), "cdp is not lldp");

    for (std::size_t size = 0; size < frame.size(); ++size) {
        Bytes cut(frame.begin(), frame.begin() + static_cast<std::ptrdiff_t>(size));
        model::Neighbour part;
        const bool read = parse_cdp(cut.data(), cut.size(), part);
        expect(!read || clean(part), "cdp: cut at " + std::to_string(size) + " stays clean");
    }

    // A field shorter than its own header: the reading ends (no endless loop).
    for (const uint16_t length : std::initializer_list<uint16_t>{0, 2, 3}) {
        Bytes body;
        cdp_field(body, 0x0001, text_bytes("sw-a"));
        put16(body, 0x0003);
        put16(body, length);
        cdp_field(body, 0x0006, text_bytes("never read"));
        const Bytes bad = cdp_frame(body);
        model::Neighbour part;
        expect(parse_cdp(bad.data(), bad.size(), part) && part.name == "sw-a" && part.model.empty(),
               "cdp: field length " + std::to_string(length) + " ends the reading");
    }
    // An address field with a huge count and too few bytes.
    {
        Bytes body;
        cdp_field(body, 0x0001, text_bytes("sw-b"));
        cdp_field(body, 0x0002, {0xff, 0xff, 0xff, 0xff, 1, 1, 0xcc, 0, 4, 10, 0});
        cdp_field(body, 0x0016, {0xff, 0xff, 0xff, 0xff, 1, 200});
        const Bytes bad = cdp_frame(body);
        model::Neighbour part;
        expect(parse_cdp(bad.data(), bad.size(), part) && part.address.empty(), "cdp: broken address fields");
    }
    // The 802.3 length ends CDP before the frame's padding; a length past
    // the frame reads only what is there.
    {
        Bytes body;
        cdp_field(body, 0x0001, text_bytes("sw-c"));
        Bytes padded = cdp_frame(body);
        const std::size_t real = padded.size();
        Bytes extra;
        cdp_field(extra, 0x0006, text_bytes("padding"));
        padded.insert(padded.end(), extra.begin(), extra.end());
        padded[12] = static_cast<uint8_t>((real - 14) >> 8);
        padded[13] = static_cast<uint8_t>((real - 14) & 0xffU);
        model::Neighbour part;
        expect(parse_cdp(padded.data(), padded.size(), part) && part.model.empty(), "cdp: padding not read");
        const Bytes longer = cdp_frame(body, 1400);
        expect(parse_cdp(longer.data(), longer.size(), part) && part.name == "sw-c", "cdp: long length, short frame");
    }
    // Other Cisco protocols on the same group (VTP, protocol 0x2003) and an
    // EtherType frame to the group are not CDP.
    {
        Bytes vtp = cdp_frame(cdp_body());
        vtp[21] = 0x03;
        expect(!parse_cdp(vtp.data(), vtp.size(), n), "cdp: vtp refused");
        Bytes typed = cdp_frame(cdp_body());
        typed[12] = 0x88;
        typed[13] = 0xcc;
        expect(!parse_cdp(typed.data(), typed.size(), n), "cdp: ethertype refused");
    }
}

void check_random() {
    // Random bytes behind real headers: never past the end (the checkers
    // catch that), every text clean.
    std::mt19937 random(20261123);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<int> length(0, 300);
    for (int round = 0; round < 20000; ++round) {
        Bytes frame = (round % 2 == 0) ? lldp_head(kSwitch) : cdp_frame({}, byte(random) * 4);
        const int extra = length(random);
        for (int index = 0; index < extra; ++index) {
            frame.push_back(static_cast<uint8_t>(byte(random)));
        }
        model::Neighbour n;
        if (parse_lldp(frame.data(), frame.size(), n) || parse_cdp(frame.data(), frame.size(), n)) {
            expect(clean(n), "random frame " + std::to_string(round) + " stays clean");
        }
    }
}

void check_heard() {
    HeardNeighbours heard;
    const Bytes lldp = lldp_frame();
    expect(heard.note_frame(lldp.data(), lldp.size(), 2), "heard: first lldp adds");
    expect(heard.note_frame(lldp.data(), lldp.size(), 32), "heard: heard again later changes");
    expect(!heard.note_frame(lldp.data(), lldp.size(), 32), "heard: same frame, same time, no change");
    const Bytes cdp = cdp_frame(cdp_body());
    expect(heard.note_frame(cdp.data(), cdp.size(), 40), "heard: cdp from the same sender merges");
    expect(heard.neighbours().size() == 1, "heard: one sender, one entry");
    const model::Neighbour& merged = heard.neighbours().front();
    expect(merged.chassis == "02:5c:00:00:00:00" && merged.name == "core-sw.example" && merged.vlan == 20 &&
               merged.vlans == std::vector<int32_t>({1, 20}) && merged.heard_s == 40,
           "heard: merge keeps lldp's chassis, takes cdp's name");
    for (uint8_t last = 0x10; last < 0x16; ++last) {
        Bytes other = cdp_frame(cdp_body(), -1, {0x02, 0x5c, 0x00, 0x00, 0x01, last});
        heard.note_frame(other.data(), other.size(), 50);
    }
    expect(heard.neighbours().size() == model::kNeighboursMax, "heard: at most kNeighboursMax");
    const Bytes junk(60, 0xab);
    expect(!heard.note_frame(junk.data(), junk.size(), 60), "heard: junk changes nothing");
}

void check_words() {
    model::Neighbour n;
    n.address = "10.0.0.1";
    n.chassis = "02:1a:2b:3c:00:01";
    expect(model::neighbour_is_router(n, "10.0.0.1", ""), "router: management address is the gateway");
    n.address = "10.0.0.2";
    expect(model::neighbour_is_router(n, "10.0.0.1", "02:1a:2b:3c:00:01"), "router: chassis is the router's");
    n.chassis.clear();
    n.hardware_address = "02:1a:2b:3c:00:01";
    expect(model::neighbour_is_router(n, "10.0.0.1", "02:1a:2b:3c:00:01"), "router: sender is the router's");
    n.hardware_address = "02:5c:00:00:00:05";
    expect(!model::neighbour_is_router(n, "10.0.0.1", "02:1a:2b:3c:00:01"), "router: a switch is not the router");
    expect(!model::neighbour_is_router(n, "10.0.0.1", ""), "router: no router address known");
    model::Neighbour blank;
    expect(!model::neighbour_is_router(blank, "", ""), "router: nothing known, not the router");

    model::Neighbour port;
    port.port = "5";
    expect(model::neighbour_port_text(port) == "port 5", "port: number");
    port.port = "Gi0/5";
    expect(model::neighbour_port_text(port) == "port Gi0/5", "port: name");
    port.port = "eth0";
    expect(model::neighbour_port_text(port) == "port eth0", "port: interface name");
    port.port = "Port 5";
    expect(model::neighbour_port_text(port) == "Port 5", "port: says it already");
    port.port.clear();
    expect(model::neighbour_port_text(port).empty(), "port: none");

    model::Neighbour vlan;
    expect(model::neighbour_vlan_text(vlan, 18).empty(), "vlan: none");
    vlan.vlan = 10;
    expect(model::neighbour_vlan_text(vlan, 18) == "Network 10 (VLAN)", "vlan: own network");
    vlan.vlans = {1, 10, 20};
    expect(model::neighbour_vlan_text(vlan, 18) == "Network 10 (VLAN)", "vlan: the own one wins");
    vlan.vlan = 0;
    expect(model::neighbour_vlan_text(vlan, 18) == "Networks 1, 10, 20", "vlan: several named");
    vlan.vlans = {1, 10, 20, 30, 40};
    expect(model::neighbour_vlan_text(vlan, 18) == "Networks 1, 10 +3", "vlan: cut with a count");
    vlan.vlans = {4094};
    expect(model::neighbour_vlan_text(vlan, 18) == "Network 4094", "vlan: only a named one, cut to fit");
    expect(model::neighbour_vlan_text(vlan, 5).empty(), "vlan: no room, nothing");
}

} // namespace

int main() {
    check_lldp();
    check_cdp();
    check_random();
    check_heard();
    check_words();
    // The pretend frames read like a real switch's.
    for (const auto& frame : platform::pretend_topology_frames()) {
        model::Neighbour n;
        expect(parse_lldp(frame.bytes.data(), frame.bytes.size(), n) && n.name == "office-sw" && n.vlan == 10,
               "pretend frame reads");
    }
    if (failures > 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("topology frames: all checks passed\n");
    return 0;
}

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_data.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Topology for the Map: reading what switches, access points and routers
// say about themselves in LLDP and CDP frames. The capture helper's
// `topology` command hears them (it only receives); everything here runs
// without privileges and reads only the captured bytes.
namespace platform {

// What one LLDP frame (EtherType 0x88cc) says about its sender: chassis ID
// (a hardware address, an IPv4 address or a name), port ID (or, when the
// port ID is only a hardware or network address, the port description),
// system name, the first line of the system description, the first IPv4
// management address, and from the IEEE 802.1 part the port's VLAN ID and
// the VLANs it names. `heard_s` is left at 0. Returns false for a frame
// that is not LLDP, comes from a group address, or says none of chassis,
// port and name. Every length is checked against `size`: a field running
// past the end ends the reading there, and what was whole before it is
// kept.
bool parse_lldp(const uint8_t* frame, std::size_t size, model::Neighbour& out);

// What one CDP frame (IEEE 802.3, LLC SNAP to 01:00:0c:cc:cc:cc, Cisco's
// organisation code, protocol 0x2000) says: device ID, port ID, platform,
// the first IPv4 address (management addresses first) and the native
// VLAN. The same rules as parse_lldp(); a field shorter than its own
// header ends the reading.
bool parse_cdp(const uint8_t* frame, std::size_t size, model::Neighbour& out);

// The switches and routers heard in one listen run: one entry per sending
// hardware address, at most model::kNeighboursMax, first heard first. A
// new frame from a known sender replaces each field it says something
// about and leaves the others.
class HeardNeighbours {
public:
    // Notes what one Ethernet frame says, heard `at_s` seconds into the
    // run. Returns true when the list changed (also when a known sender was
    // only heard again later).
    bool note_frame(const uint8_t* frame, std::size_t size, int32_t at_s);

    const std::vector<model::Neighbour>& neighbours() const { return neighbours_; }

private:
    std::vector<model::Neighbour> neighbours_;
};

// The pretend runner's made-up topology frames: what a cable port next to
// a small managed switch hears, at their times in milliseconds from the
// start of the run (one made-up switch with a documentation-style name and
// a locally administered hardware address on the sample's 10.0.0.x
// network, sending LLDP every 30 s).
struct PretendTopologyFrame {
    uint32_t at_ms;
    std::vector<uint8_t> bytes;
};
const std::vector<PretendTopologyFrame>& pretend_topology_frames();

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "health_check.h"
#include "tool_data.h"

#include <cstdint>
#include <string>
#include <vector>

// The health check's parts that touch the system or make things up: the
// cable's speed and duplex, the version an SSDP SERVER line gives, and the
// pretend runner's made-up check. Nothing here opens a socket; the tool
// runner sweeps and scans (tool_runner.cpp).
namespace platform {

// Reads the speed and duplex of the cable `interface_name` from
// /sys/class/net/<name>/speed and duplex into local.cable_mbps and
// local.cable_duplex ("full" or "half"); a value that cannot be read is
// left as it is. Only when local.cable is set.
void read_cable(const std::string& interface_name, model::HealthLocal& local);

// "10.0.0.0/24" as a network and netmask (host byte order); false when it
// is not one.
bool parse_network(const std::string& text, uint32_t& network, uint32_t& netmask);

// When `host` named a version in its SSDP SERVER line and no open port of
// `scan` said anything for port 1900, adds that line as what port 1900 said
// and checks `scan` again against the list of known holes at `list_path`
// (check_port_services()), so an old UPnP service (MiniUPnPd) shows. A
// scan that did not finish is left alone.
void add_ssdp_version(const model::HealthHost& host, model::PortRun& scan, const std::string& list_path);

// The pretend runner's made-up health check, worked out once as the job
// starts: the made-up probe of the sample's network (pretend_probe()), the
// devices it and `known` (the Discover page's list) found, each device's
// made-up port scan (pretend_port_scan()) checked against the list at
// `list_path`, and the findings. The made-up cable runs at half duplex when
// `local` gives none, so that rule shows too.
struct PretendHealth {
    model::HealthRun start;                              // the run as it starts (local findings in)
    model::HealthRun swept;                              // the run after the sweep, with the hosts to scan
    uint32_t sweep_sent{0};                              // packets of the sweep
    std::vector<std::vector<model::HealthFinding>> found;  // per device, its findings
    std::vector<uint32_t> host_sent;                     // per device, its packets
};

PretendHealth plan_pretend_health(const std::string& interface_name,
                                  model::HealthLocal local,
                                  const std::vector<model::HeardDevice>& known,
                                  const std::string& list_path);

// The made-up check `plan` at `elapsed_ms` into the run: the sweep takes
// kPretendHealthSweepMs, then each device kPretendHealthHostMs (shorter than
// a real run, so a snapshot can show the end). `sent` is the number of
// packets it has sent by then; `done` says the run has ended.
constexpr uint32_t kPretendHealthSweepMs = 4000;
constexpr uint32_t kPretendHealthHostMs = 600;
model::HealthRun pretend_health(const PretendHealth& plan, uint32_t elapsed_ms, uint32_t& sent, bool& done);

} // namespace platform

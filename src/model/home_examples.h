/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"

// Made-up situations for the home page, next to the router sample of
// sample_home_data(). Each is a whole model::HomeData block with
// HomeData::sample set, so the page marks it as sample data. They are the
// `wifi`, `cable`, `offline` and `tour` data sources, and they let every
// layout be looked at and tested in the situations the device can really be
// in. Clock and battery are the sample's in all of them.
namespace model {

// Joined to a Wi-Fi network like a phone: no access point of its own, so no
// clients. Its traffic histories are its own.
HomeData wifi_home_data();

// On a network cable: no access point, no clients, no Wi-Fi values. The
// traffic is the sample's.
HomeData cable_home_data();

// No network at all: no link, nothing known about an uplink, no traffic
// known, no access point and no clients.
HomeData offline_home_data();

// How many stops the tour has before it starts again.
constexpr int kHomeTourStops = 25;

// One stop of the tour. The tour is a fixed walk through the situations:
// first every situation after every other one (sample, Wi-Fi, cable and no
// network), then stops where only values change inside one situation (a
// weak signal, the widest texts, values that are not known), and last a
// router whose list of clients shrinks to one and then to none. Clock and
// battery never change. `index` is taken modulo kHomeTourStops; a negative
// index gives stop 0.
HomeData home_tour_stop(int index);

// Made-up Status page values that fit `data`, a made-up block: a cable port
// (eth0) and the built-in Wi-Fi (wlan0), each connected when it is the
// uplink, the Wi-Fi running the access point when `data` has one; the DNS
// server is the router; battery details that follow the block's charge and
// state; a fixed processor temperature, memory and time since start. The
// hardware addresses are made up (02:...) and the IPv6 addresses come from
// the range kept for examples (2001:db8::/32). Every made-up source fills
// HomeData::details with it, after all its other values are set.
StatusDetails example_status_details(const HomeData& data);

// The made-up password of the made-up hotspot (the sample's access point,
// and the one the pretend switch to Router starts). The simulator and
// snapshots show it; it is never written to a settings file.
constexpr const char* kExampleHotspotPassword = "fx7kq2mv9tpa";

} // namespace model

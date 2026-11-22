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

} // namespace model

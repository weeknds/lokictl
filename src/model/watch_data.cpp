/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "watch_data.h"

#include <algorithm>

namespace model {

const char* watch_alert_name(WatchAlertKind kind) {
    switch (kind) {
        case WatchAlertKind::NewDevice:
            return "New device";
        case WatchAlertKind::FakeRouter:
            return "Fake router?";
        case WatchAlertKind::SameAddress:
            return "Same address x2";
        case WatchAlertKind::SecondDhcp:
            return "Second DHCP";
        case WatchAlertKind::Ipv6Router:
            return "New IPv6 router";
        case WatchAlertKind::Scan:
            return "Scan on you";
        case WatchAlertKind::ArpFlood:
            return "ARP flood";
        case WatchAlertKind::Trap:
            return "Tried a trap";
    }
    return "Alert";
}

const char* watch_alert_about(WatchAlertKind kind) {
    switch (kind) {
        case WatchAlertKind::NewDevice:
            return "A device not on your known list joined the network. If it is yours, press 7 to mark it known.";
        case WatchAlertKind::FakeRouter:
            return "Someone may be pretending to be your router (ARP spoofing). Your traffic could be read or "
                   "changed.";
        case WatchAlertKind::SameAddress:
            return "Two devices answer for one address. A setup mistake, or someone taking over its traffic (ARP "
                   "spoofing).";
        case WatchAlertKind::SecondDhcp:
            return "A second device hands out addresses (a fake DHCP server?). It can send devices the wrong way.";
        case WatchAlertKind::Ipv6Router:
            return "An unknown device says it is a router for IPv6. A fake one can send traffic the wrong way.";
        case WatchAlertKind::Scan:
            return "Someone tried many ports on this Cardputer in a few seconds: a port scan, looking for weak "
                   "spots.";
        case WatchAlertKind::ArpFlood:
            return "Very many address questions (ARP) in one second. Something is broken, or someone floods the "
                   "network.";
        case WatchAlertKind::Trap:
            return "Someone connected to a fake service of the trap (honeypot). Nothing real runs there: someone "
                   "is probing.";
    }
    return "";
}

const char* watch_alert_log_name(WatchAlertKind kind) {
    switch (kind) {
        case WatchAlertKind::NewDevice:
            return "new device";
        case WatchAlertKind::FakeRouter:
            return "router changed";
        case WatchAlertKind::SameAddress:
            return "same address";
        case WatchAlertKind::SecondDhcp:
            return "second dhcp";
        case WatchAlertKind::Ipv6Router:
            return "ipv6 router";
        case WatchAlertKind::Scan:
            return "scan";
        case WatchAlertKind::ArpFlood:
            return "arp flood";
        case WatchAlertKind::Trap:
            return "trap";
    }
    return "alert";
}

bool operator==(const WatchAlert& left, const WatchAlert& right) {
    return left.kind == right.kind && left.time == right.time && left.address == right.address &&
           left.hardware == right.hardware && left.before == right.before && left.count == right.count;
}

bool operator!=(const WatchAlert& left, const WatchAlert& right) {
    return !(left == right);
}

bool operator==(const WatchRun& left, const WatchRun& right) {
    return left.state == right.state && left.error == right.error && left.reason == right.reason &&
           left.interface_name == right.interface_name && left.elapsed_s == right.elapsed_s &&
           left.learn_left_s == right.learn_left_s && left.frames == right.frames && left.raised == right.raised &&
           left.alerts == right.alerts && left.learned == right.learned;
}

bool operator!=(const WatchRun& left, const WatchRun& right) {
    return !(left == right);
}

bool operator==(const WatchData& left, const WatchData& right) {
    return left.run == right.run && left.known == right.known;
}

bool operator!=(const WatchData& left, const WatchData& right) {
    return !(left == right);
}

bool watch_known(const WatchData& data, const std::string& hardware) {
    return !hardware.empty() && std::find(data.known.begin(), data.known.end(), hardware) != data.known.end();
}

} // namespace model

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_data.h"
#include "network_manager.h"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace platform {

// The most USB Wi-Fi adapters read_usb_wifi() lists.
constexpr std::size_t kMaxUsbWifiAdapters = 4;

// The most bytes of an adapter's name (model::UsbWifiAdapter::name).
constexpr std::size_t kMaxUsbWifiName = 48;

// The USB Wi-Fi adapters plugged in now, read from the files the kernel
// offers to every user. It only reads: it never changes an interface,
// never scans and never writes a file. LokiCTL needs no adapter; this only
// lets it say whether one is there.
//
// `root` stands in for the root of the file system, as for read_network().
// - An adapter is an interface in "<root>/sys/class/net" that has a
//   "wireless" folder and whose "device/uevent" holds
//   "DEVTYPE=usb_interface". The built-in Wi-Fi sits on SDIO, and a USB
//   network cable port has no "wireless" folder, so neither counts.
// - Interfaces whose "device" leads to the same place (a second interface
//   on the same stick, such as a monitor one) are one adapter, named after
//   the first of them by name. That place is model::UsbWifiAdapter::device.
//   The survey's own listening interface (model::kSurveyListenInterface)
//   is never read, so the stick keeps its own name while a survey runs.
// - Driver: "DRIVER=" of that uevent. USB id: "PRODUCT=<vendor>/<product>/
//   <release>" in hex, written "0cf3:9271". Name: the USB device's
//   "manufacturer" and "product" (the folder above "device"), joined by a
//   space (only "product" when it already starts with the manufacturer);
//   anything but printable ASCII becomes '?', at most kMaxUsbWifiName
//   bytes. A value that cannot be read stays empty.
// - The managed state is left unknown (see apply_usb_wifi_managed()).
// Sorted by interface name, at most kMaxUsbWifiAdapters. Never throws.
std::vector<model::UsbWifiAdapter> read_usb_wifi(const std::filesystem::path& root);

// Sets each adapter's managed state from the network service: unknown when
// the service is not available, yes when its interface is one of
// `reading.managed_wifi`, otherwise no.
void apply_usb_wifi_managed(const NetworkManagerReading& reading, std::vector<model::UsbWifiAdapter>& adapters);

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "usb_wifi.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace platform {
namespace {

namespace fs = std::filesystem;

// The first line of a small file, without the line end; empty when it
// cannot be read.
std::string first_line(const fs::path& path) {
    std::ifstream file(path);
    std::string line;
    if (!file || !std::getline(file, line)) {
        return {};
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

// The values of a uevent file that matter here.
struct Uevent {
    bool usb{false};      // DEVTYPE=usb_interface
    std::string driver;   // DRIVER=
    std::string product;  // PRODUCT=
};

Uevent read_uevent(const fs::path& path) {
    Uevent uevent;
    std::ifstream file(path);
    std::string line;
    while (file && std::getline(file, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        if (!value.empty() && value.back() == '\r') {
            value.pop_back();
        }
        if (key == "DEVTYPE") {
            uevent.usb = value == "usb_interface";
        } else if (key == "DRIVER") {
            uevent.driver = std::move(value);
        } else if (key == "PRODUCT") {
            uevent.product = std::move(value);
        }
    }
    return uevent;
}

// One to four hex digits as four lowercase ones ("cf3" -> "0cf3"); empty
// for anything else.
std::string hex4(const std::string& text) {
    if (text.empty() || text.size() > 4) {
        return {};
    }
    std::string result(4 - text.size(), '0');
    for (const char character : text) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) {
            return {};
        }
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

// "cf3/9271/108" -> "0cf3:9271"; empty when it does not read like that.
std::string usb_id_text(const std::string& product) {
    const std::size_t first = product.find('/');
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t second = product.find('/', first + 1);
    const std::string vendor = hex4(product.substr(0, first));
    const std::string model =
        hex4(product.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1));
    return vendor.empty() || model.empty() ? std::string() : vendor + ":" + model;
}

// Trimmed, printable ASCII only (anything else becomes '?').
std::string clean_text(const std::string& text) {
    std::string result;
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        result += byte >= 0x20 && byte < 0x7F ? character : '?';
    }
    const std::size_t begin = result.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = result.find_last_not_of(' ');
    return result.substr(begin, end - begin + 1);
}

std::string adapter_name(const fs::path& usb_device) {
    const std::string manufacturer = clean_text(first_line(usb_device / "manufacturer"));
    const std::string product = clean_text(first_line(usb_device / "product"));
    std::string name;
    if (manufacturer.empty() || product.rfind(manufacturer, 0) == 0) {
        name = product;
    } else if (product.empty()) {
        name = manufacturer;
    } else {
        name = manufacturer + " " + product;
    }
    if (name.size() > kMaxUsbWifiName) {
        name.resize(kMaxUsbWifiName);
    }
    return name;
}

} // namespace

std::vector<model::UsbWifiAdapter> read_usb_wifi(const fs::path& root) {
    std::vector<model::UsbWifiAdapter> adapters;
    // Relative on purpose: an absolute path on the right would drop the root.
    const fs::path net = root / "sys/class/net";
    std::error_code error;
    std::vector<std::string> names;
    for (fs::directory_iterator it(net, error), end; !error && it != end; it.increment(error)) {
        names.push_back(it->path().filename().string());
    }
    std::sort(names.begin(), names.end());

    for (const std::string& interface_name : names) {
        const fs::path interface_dir = net / interface_name;
        if (!fs::is_directory(interface_dir / "wireless", error)) {
            continue;
        }
        const Uevent uevent = read_uevent(interface_dir / "device/uevent");
        if (!uevent.usb) {
            continue;
        }
        fs::path device = fs::canonical(interface_dir / "device", error);
        if (error) {
            device = interface_dir / "device";
        }
        const std::string device_text = device.string();
        const bool known = std::any_of(adapters.begin(), adapters.end(), [&](const model::UsbWifiAdapter& adapter) {
            return adapter.device == device_text;
        });
        if (known) {
            continue;
        }
        model::UsbWifiAdapter adapter;
        adapter.interface_name = interface_name;
        adapter.driver = clean_text(uevent.driver);
        adapter.usb_id = usb_id_text(uevent.product);
        adapter.name = adapter_name(device.parent_path());
        adapter.device = device_text;
        adapters.push_back(std::move(adapter));
        if (adapters.size() == kMaxUsbWifiAdapters) {
            break;
        }
    }
    return adapters;
}

void apply_usb_wifi_managed(const NetworkManagerReading& reading, std::vector<model::UsbWifiAdapter>& adapters) {
    for (model::UsbWifiAdapter& adapter : adapters) {
        if (!reading.available) {
            adapter.managed = model::Managed::Unknown;
            continue;
        }
        const bool managed = std::find(reading.managed_wifi.begin(), reading.managed_wifi.end(),
                                       adapter.interface_name) != reading.managed_wifi.end();
        adapter.managed = managed ? model::Managed::Yes : model::Managed::No;
    }
}

} // namespace platform

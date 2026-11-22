/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The connection to NetworkManager. Device builds (APP_USE_LIBNM) talk to it
// through libnm; every other build gets the stand-in at the end of this
// file, which is never available.
//
// Reads only: every libnm function used here is a getter, apart from
// creating and releasing the client and the GLib calls that run its main
// context. Nothing here activates, changes or deletes a connection,
// switches a radio, asks for a scan or asks for an internet check.

#include "network_manager.h"

#if APP_USE_LIBNM
#include <NetworkManager.h>
#endif

#include <utility>

namespace platform {

#if APP_USE_LIBNM

namespace {

using std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;

// One read() handles at most this many queued events, so a flood of
// messages never keeps the reader busy for long; the rest waits for the
// next read().
constexpr int kMaxEventsPerRead = 500;
// How long the teardown waits for a start to end and for libnm to let go of
// its context.
constexpr milliseconds kReleaseLimit{500};

model::Internet internet_of(NMConnectivityState state) {
    switch (state) {
        case NM_CONNECTIVITY_NONE:
            return model::Internet::None;
        case NM_CONNECTIVITY_PORTAL:
            return model::Internet::Portal;
        case NM_CONNECTIVITY_LIMITED:
            return model::Internet::Limited;
        case NM_CONNECTIVITY_FULL:
            return model::Internet::Full;
        default:
            return model::Internet::Unknown;
    }
}

// The strongest security the access point offers.
std::string security_text(NMAccessPoint* access_point) {
    const unsigned flags = nm_access_point_get_flags(access_point);
    const unsigned wpa = nm_access_point_get_wpa_flags(access_point);
    const unsigned rsn = nm_access_point_get_rsn_flags(access_point);
    if ((rsn & NM_802_11_AP_SEC_KEY_MGMT_SAE) != 0) {
        return (rsn & NM_802_11_AP_SEC_KEY_MGMT_PSK) != 0 ? "WPA2/3" : "WPA3";
    }
    if ((rsn & NM_802_11_AP_SEC_KEY_MGMT_PSK) != 0) {
        return "WPA2";
    }
    if ((wpa & NM_802_11_AP_SEC_KEY_MGMT_PSK) != 0) {
        return "WPA";
    }
    if ((rsn & NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192) != 0) {
        return "WPA3-EAP";
    }
    if ((rsn & NM_802_11_AP_SEC_KEY_MGMT_802_1X) != 0) {
        return "WPA2-EAP";
    }
    if ((wpa & NM_802_11_AP_SEC_KEY_MGMT_802_1X) != 0) {
        return "WPA-EAP";
    }
    if ((rsn & (NM_802_11_AP_SEC_KEY_MGMT_OWE | NM_802_11_AP_SEC_KEY_MGMT_OWE_TM)) != 0) {
        return "OWE";
    }
    if ((flags & NM_802_11_AP_FLAGS_PRIVACY) != 0) {
        return "WEP";
    }
    return "open";
}

std::string network_name(NMAccessPoint* access_point) {
    GBytes* ssid = nm_access_point_get_ssid(access_point);
    if (ssid == nullptr) {
        return {};
    }
    gsize size = 0;
    const auto* bytes = static_cast<const guint8*>(g_bytes_get_data(ssid, &size));
    if (bytes == nullptr || size == 0) {
        return {};
    }
    char* text = nm_utils_ssid_to_utf8(bytes, size);
    if (text == nullptr) {
        return {};
    }
    std::string name(text);
    g_free(text);
    return clean_network_name(name);
}

gboolean end_wait(gpointer /*unused*/) {
    return G_SOURCE_REMOVE;
}

} // namespace

struct NetworkManagerClient::Impl {
    GMainContext* context{nullptr};
    GCancellable* cancellable{nullptr};
    NMClient* client{nullptr};
    bool starting{false};

    // Called in `context` when the start has ended, well or not.
    static void on_started(GObject* source, GAsyncResult* result, gpointer user_data) {
        auto* self = static_cast<Impl*>(user_data);
        GError* error = nullptr;
        GObject* object = g_async_initable_new_finish(G_ASYNC_INITABLE(source), result, &error);
        self->starting = false;
        if (object != nullptr) {
            self->client = NM_CLIENT(object);
        }
        g_clear_error(&error);
    }

    // Handles the queued events, without waiting, at most `limit` of them.
    void handle_queued(int limit) {
        g_main_context_push_thread_default(context);
        for (int handled = 0; handled < limit && g_main_context_iteration(context, FALSE); ++handled) {
        }
        g_main_context_pop_thread_default(context);
    }

    // Handles events, waiting for them, until done() or until `limit` has
    // passed.
    template <typename Done>
    void handle_until(Done done, milliseconds limit) {
        const auto deadline = Clock::now() + limit;
        g_main_context_push_thread_default(context);
        while (!done()) {
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            if (left.count() <= 0) {
                break;
            }
            // A timer in the context ends a wait that no event ends.
            GSource* timer = g_timeout_source_new(static_cast<guint>(left.count()));
            g_source_set_callback(timer, end_wait, nullptr, nullptr);
            g_source_attach(timer, context);
            g_main_context_iteration(context, TRUE);
            g_source_destroy(timer);
            g_source_unref(timer);
        }
        g_main_context_pop_thread_default(context);
    }
};

NetworkManagerClient::NetworkManagerClient()
    : impl_(std::make_unique<Impl>()) {
    Impl& self = *impl_;
    self.context = g_main_context_new();
    self.cancellable = g_cancellable_new();
    self.starting = true;
    // The client keeps the main context that is the thread's default while
    // it is made. It starts without waiting, and without asking which
    // actions the app may take (a question that would go to polkit).
    g_main_context_push_thread_default(self.context);
    g_async_initable_new_async(NM_TYPE_CLIENT, G_PRIORITY_DEFAULT, self.cancellable, &Impl::on_started, &self,
                               NM_CLIENT_INSTANCE_FLAGS,
                               static_cast<guint>(NM_CLIENT_INSTANCE_FLAGS_NO_AUTO_FETCH_PERMISSIONS), nullptr);
    g_main_context_pop_thread_default(self.context);
}

NetworkManagerClient::~NetworkManagerClient() {
    Impl& self = *impl_;
    if (self.starting) {
        g_cancellable_cancel(self.cancellable);
        self.handle_until([&self] { return !self.starting; }, kReleaseLimit);
    }
    if (self.client != nullptr) {
        // libnm asks its user to run the context until its busy watcher is
        // gone, so nothing of the client is left behind.
        gpointer watcher = nm_client_get_context_busy_watcher(self.client);
        if (watcher != nullptr) {
            g_object_add_weak_pointer(G_OBJECT(watcher), &watcher);
        }
        g_object_unref(self.client);
        self.client = nullptr;
        self.handle_until([&watcher] { return watcher == nullptr; }, kReleaseLimit);
        if (watcher != nullptr) {
            g_object_remove_weak_pointer(G_OBJECT(watcher), &watcher);
        }
    }
    if (self.starting) {
        // The start did not end in time, so its callback still points at
        // `self`. Nothing runs the context again, but `self` and the context
        // are kept all the same: a small, one-time leak on the way out.
        static_cast<void>(impl_.release());
        return;
    }
    g_object_unref(self.cancellable);
    g_main_context_unref(self.context);
}

void NetworkManagerClient::read(NetworkManagerReading& reading) {
    reading = NetworkManagerReading{};
    Impl& self = *impl_;
    self.handle_queued(kMaxEventsPerRead);
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        return;
    }

    reading.available = true;
    reading.networking_on =
        nm_client_networking_get_enabled(client) && nm_client_get_state(client) != NM_STATE_ASLEEP;
    reading.wifi_on = nm_client_wireless_get_enabled(client) && nm_client_wireless_hardware_get_enabled(client);
    reading.internet = internet_of(nm_client_get_connectivity(client));

    const GPtrArray* devices = nm_client_get_devices(client);
    for (guint index = 0; devices != nullptr && index < devices->len; ++index) {
        auto* device = static_cast<NMDevice*>(g_ptr_array_index(devices, index));
        const NMDeviceType type = nm_device_get_device_type(device);
        if ((type != NM_DEVICE_TYPE_WIFI && type != NM_DEVICE_TYPE_ETHERNET) || !nm_device_get_managed(device)) {
            continue;
        }
        const NMDeviceState state = nm_device_get_state(device);
        if (state >= NM_DEVICE_STATE_PREPARE && state < NM_DEVICE_STATE_ACTIVATED) {
            reading.connecting = true;
        }
        if (type != NM_DEVICE_TYPE_WIFI || state != NM_DEVICE_STATE_ACTIVATED) {
            continue;
        }
        auto* wifi = NM_DEVICE_WIFI(device);
        // Only a client link; an access point of its own is not one.
        if (nm_device_wifi_get_mode(wifi) != NM_802_11_MODE_INFRA) {
            continue;
        }
        NetworkManagerReading::WifiLink link;
        const char* interface_name = nm_device_get_iface(device);
        link.interface_name = interface_name != nullptr ? interface_name : "";
        if (NMAccessPoint* access_point = nm_device_wifi_get_active_access_point(wifi)) {
            link.name = network_name(access_point);
            link.security = security_text(access_point);
        }
        // kbit/s, cut to whole Mbit/s the way nmcli shows it.
        const guint32 bitrate = nm_device_wifi_get_bitrate(wifi);
        if (bitrate >= 1000) {
            link.speed_mbps = static_cast<int32_t>(bitrate / 1000);
        }
        reading.wifi_links.push_back(std::move(link));
    }
}

bool NetworkManagerClient::starting() const {
    return impl_->starting;
}

void NetworkManagerClient::wait_started(std::chrono::milliseconds limit) {
    Impl& self = *impl_;
    if (self.starting) {
        self.handle_until([&self] { return !self.starting; }, limit);
    }
}

#else // !APP_USE_LIBNM

// The stand-in for builds without libnm: never available, never starting.
struct NetworkManagerClient::Impl {};

NetworkManagerClient::NetworkManagerClient() = default;

NetworkManagerClient::~NetworkManagerClient() = default;

void NetworkManagerClient::read(NetworkManagerReading& reading) {
    reading = NetworkManagerReading{};
}

bool NetworkManagerClient::starting() const {
    return false;
}

void NetworkManagerClient::wait_started(std::chrono::milliseconds /*limit*/) {}

#endif // APP_USE_LIBNM

} // namespace platform

/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

// The connection to NetworkManager. Device builds (APP_USE_LIBNM) talk to it
// through libnm; every other build gets the stand-in at the end of this
// file, which is never available and changes nothing.
//
// Reading uses only getters, apart from creating and releasing the client
// and the GLib calls that run its main context: read() and
// read_wifi_scan() never activate, change or delete a connection, switch a
// radio, ask for a scan or ask for an internet check. Only
// request_wifi_scan(), in the block marked "Wi-Fi scan", asks for a scan,
// which changes no setting.
//
// Every call that changes the network is in the block marked "Actions"
// below: adding, updating (in memory) and activating the in-memory hotspot
// profile, deleting LokiCTL's hotspot profiles and activating the Wi-Fi
// profile the hotspot interface was on before. Nothing else is ever added,
// changed or deleted, and only kHotspotInterface (Router) and
// kShareInterface (Share) are touched.

#include "network_manager.h"

#if APP_USE_LIBNM
#include <NetworkManager.h>
#endif

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <time.h>
#include <utility>
#include <vector>

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

// The user-data keys of LokiCTL's hotspot profile: the time it was made
// (seconds since 1970, also the mark that the profile is LokiCTL's) and the
// UUID of the Wi-Fi profile to bring back.
constexpr const char* kStartedKey = "lokictl.started";
constexpr const char* kRestoreKey = "lokictl.restore";

// The longest reason a failed switch shows.
constexpr std::size_t kMaxReason = 60;

const char* user_data(NMConnection* connection, const char* key) {
    if (connection == nullptr) {
        return nullptr;
    }
    auto* user = NM_SETTING_USER(nm_connection_get_setting_by_name(connection, NM_SETTING_USER_SETTING_NAME));
    return user != nullptr ? nm_setting_user_get_data(user, key) : nullptr;
}

// True for a profile LokiCTL made: its name and its own user-data mark.
bool is_hotspot_profile(NMConnection* connection) {
    if (connection == nullptr) {
        return false;
    }
    const char* id = nm_connection_get_id(connection);
    return id != nullptr && std::strcmp(id, kHotspotProfile) == 0 && user_data(connection, kStartedKey) != nullptr;
}

// Every profile LokiCTL made, as the client knows them now.
std::vector<NMRemoteConnection*> hotspot_profiles(NMClient* client) {
    std::vector<NMRemoteConnection*> profiles;
    const GPtrArray* connections = nm_client_get_connections(client);
    for (guint index = 0; connections != nullptr && index < connections->len; ++index) {
        auto* connection = static_cast<NMRemoteConnection*>(g_ptr_array_index(connections, index));
        if (is_hotspot_profile(NM_CONNECTION(connection))) {
            profiles.push_back(connection);
        }
    }
    return profiles;
}

// True when the service refused for lack of permission. The error domains
// are compared by name: libnm's headers declare their quark functions
// without C linkage, so C++ cannot link to them.
bool permission_denied(const GError* error) {
    if (error == nullptr) {
        return false;
    }
    const char* domain = g_quark_to_string(error->domain);
    if (domain != nullptr &&
        ((std::strcmp(domain, "nm-manager-error-quark") == 0 && error->code == NM_MANAGER_ERROR_PERMISSION_DENIED) ||
         (std::strcmp(domain, "nm-settings-error-quark") == 0 && error->code == NM_SETTINGS_ERROR_PERMISSION_DENIED))) {
        return true;
    }
    gchar* remote = g_dbus_error_get_remote_error(error);
    const bool denied = remote != nullptr && std::strstr(remote, "PermissionDenied") != nullptr;
    g_free(remote);
    return denied;
}

// A short, plain reason from an error: its message without the D-Bus
// prefix, one line, at most kMaxReason bytes.
std::string reason_of(GError* error) {
    if (error == nullptr || error->message == nullptr) {
        return "unknown error";
    }
    g_dbus_error_strip_remote_error(error);
    std::string reason;
    for (const char* character = error->message; *character != '\0' && *character != '\n'; ++character) {
        if (reason.size() >= kMaxReason) {
            break;
        }
        const auto byte = static_cast<unsigned char>(*character);
        reason += byte < 0x20 || byte == 0x7F ? '?' : *character;
    }
    return reason.empty() ? std::string("unknown error") : reason;
}

std::string reason_of(NMActiveConnectionStateReason reason) {
    switch (reason) {
        case NM_ACTIVE_CONNECTION_STATE_REASON_NO_SECRETS:
            return "password refused";
        case NM_ACTIVE_CONNECTION_STATE_REASON_CONNECT_TIMEOUT:
            return "timed out";
        case NM_ACTIVE_CONNECTION_STATE_REASON_IP_CONFIG_INVALID:
            return "address setup failed";
        case NM_ACTIVE_CONNECTION_STATE_REASON_DEVICE_DISCONNECTED:
            return "the hotspot did not start";
        case NM_ACTIVE_CONNECTION_STATE_REASON_DEVICE_REMOVED:
            return "Wi-Fi device removed";
        case NM_ACTIVE_CONNECTION_STATE_REASON_CONNECTION_REMOVED:
            return "hotspot removed";
        default:
            return "hotspot stopped (reason " + std::to_string(static_cast<int>(reason)) + ")";
    }
}

int64_t wall_clock_seconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

// LokiCTL's hotspot profile for `request`, with `uuid`, the Wi-Fi profile
// to bring back (`restore`, may be empty) and the time now as its start:
// an access point on the request's interface, 2.4 GHz (on the request's
// channel, if it names one), WPA2-PSK (CCMP), IPv4 shared, IPv6 off,
// autoconnect off. The caller owns the result.
NMConnection* hotspot_profile(const RouterRequest& request, const char* uuid, const std::string& restore) {
    NMConnection* connection = nm_simple_connection_new();
    auto* setting_connection = NM_SETTING_CONNECTION(nm_setting_connection_new());
    g_object_set(setting_connection,
                 NM_SETTING_CONNECTION_ID, kHotspotProfile,
                 NM_SETTING_CONNECTION_UUID, uuid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
                 NM_SETTING_CONNECTION_INTERFACE_NAME, request.interface_name.c_str(),
                 NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
                 nullptr);
    nm_connection_add_setting(connection, NM_SETTING(setting_connection));

    auto* user = NM_SETTING_USER(nm_setting_user_new());
    const std::string started = std::to_string(wall_clock_seconds());
    nm_setting_user_set_data(user, kStartedKey, started.c_str(), nullptr);
    if (!restore.empty()) {
        nm_setting_user_set_data(user, kRestoreKey, restore.c_str(), nullptr);
    }
    nm_connection_add_setting(connection, NM_SETTING(user));

    auto* wireless = NM_SETTING_WIRELESS(nm_setting_wireless_new());
    GBytes* ssid = g_bytes_new(request.name.data(), request.name.size());
    g_object_set(wireless,
                 NM_SETTING_WIRELESS_SSID, ssid,
                 NM_SETTING_WIRELESS_MODE, NM_SETTING_WIRELESS_MODE_AP,
                 NM_SETTING_WIRELESS_BAND, "bg",
                 nullptr);
    if (request.channel > 0) {
        g_object_set(wireless, NM_SETTING_WIRELESS_CHANNEL, static_cast<guint>(request.channel), nullptr);
    }
    g_bytes_unref(ssid);
    nm_connection_add_setting(connection, NM_SETTING(wireless));

    auto* security = NM_SETTING_WIRELESS_SECURITY(nm_setting_wireless_security_new());
    g_object_set(security,
                 NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
                 NM_SETTING_WIRELESS_SECURITY_PSK, request.password.c_str(),
                 nullptr);
    nm_setting_wireless_security_add_proto(security, "rsn");
    nm_setting_wireless_security_add_pairwise(security, "ccmp");
    nm_setting_wireless_security_add_group(security, "ccmp");
    nm_connection_add_setting(connection, NM_SETTING(security));

    auto* ipv4 = NM_SETTING_IP_CONFIG(nm_setting_ip4_config_new());
    g_object_set(ipv4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_SHARED, nullptr);
    nm_connection_add_setting(connection, NM_SETTING(ipv4));
    auto* ipv6 = NM_SETTING_IP_CONFIG(nm_setting_ip6_config_new());
    g_object_set(ipv6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_DISABLED, nullptr);
    nm_connection_add_setting(connection, NM_SETTING(ipv6));
    return connection;
}

} // namespace

struct NetworkManagerClient::Impl {
    GMainContext* context{nullptr};
    GCancellable* cancellable{nullptr};
    NMClient* client{nullptr};
    bool starting{false};

    // The switch between modes (see the Actions block).
    GCancellable* action_cancellable{nullptr};
    model::ModeSwitch status;            // what read() reports
    Clock::time_point switch_started{};
    bool router_waiting{false};          // a Router switch waits for its hotspot to be active
    bool add_pending{false};             // the hotspot's add-and-activate is not answered yet
    bool undo_when_added{false};         // undo the Router switch once the add is answered
    NMActiveConnection* activation{nullptr}; // the hotspot's activation (a reference)
    bool device_waiting{false};          // a Share switch waits for its interface to be ready
    RouterRequest share_request;         // what that Share switch adds then
    bool client_waiting{false};          // a Client switch waits for its answers
    int pending{0};                      // requests sent and not answered yet
    int deletes_pending{0};              // deletes of hotspot profiles not answered yet
    std::string restore_uuid;            // activated once those deletes are answered
    std::string restore_interface;
    bool closing{false};                 // no new requests: the client is going away

    // The Wi-Fi scan the user asked for (see the Wi-Fi scan block).
    bool scan_pending{false};            // the request is not answered yet
    bool scan_waiting{false};            // accepted: waits for the device's last-scan time to change
    gint64 scan_last_before{0};          // the device's last-scan time when it was asked
    Clock::time_point scan_started{};
    std::string scan_interface;
    model::WifiScanError scan_error{model::WifiScanError::None};
    std::string scan_reason;
    void check_scan();
    void drop_scan();
    static void on_scan_requested(GObject* source, GAsyncResult* result, gpointer user_data);

    // Actions; see the block below.
    void fail(model::SwitchError error, std::string reason);
    void release_activation();
    bool hotspot_active() const;
    void add_hotspot(const RouterRequest& request, NMDevice* device, const std::string& restore);
    void add_when_ready();
    void delete_profiles_and_restore();
    void check_progress();
    static void on_router_added(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_profile_deleted(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_leftover_deleted(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_restored(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_hotspot_updated(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_hotspot_reactivated(GObject* source, GAsyncResult* result, gpointer user_data);

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
    self.action_cancellable = g_cancellable_new();
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
    // Requests still waiting for an answer are given up; their callbacks
    // run with "cancelled" and only count down.
    self.closing = true;
    if (self.pending > 0 || self.scan_pending) {
        g_cancellable_cancel(self.action_cancellable);
        self.handle_until([&self] { return self.pending == 0 && !self.scan_pending; }, kReleaseLimit);
    }
    self.release_activation();
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
    if (self.starting || self.pending > 0 || self.scan_pending) {
        // The start or a request did not end in time, so its callback still
        // points at `self`. Nothing runs the context again, but `self` and
        // the context are kept all the same: a small, one-time leak on the
        // way out.
        static_cast<void>(impl_.release());
        return;
    }
    g_object_unref(self.action_cancellable);
    g_object_unref(self.cancellable);
    g_main_context_unref(self.context);
}

void NetworkManagerClient::read(NetworkManagerReading& reading) {
    reading = NetworkManagerReading{};
    Impl& self = *impl_;
    self.handle_queued(kMaxEventsPerRead);
    self.check_progress();
    self.check_scan();
    reading.mode_switch = self.status;
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
        if (type == NM_DEVICE_TYPE_WIFI) {
            if (const char* managed_name = nm_device_get_iface(device)) {
                reading.managed_wifi.emplace_back(managed_name);
            }
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

    // LokiCTL's hotspot, while its profile is active.
    const GPtrArray* actives = nm_client_get_active_connections(client);
    for (guint index = 0; actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        auto* profile = NM_CONNECTION(nm_active_connection_get_connection(active));
        if (!is_hotspot_profile(profile) || nm_active_connection_get_state(active) != NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
            continue;
        }
        NetworkManagerReading::Hotspot& hotspot = reading.hotspot;
        hotspot.active = true;
        const GPtrArray* active_devices = nm_active_connection_get_devices(active);
        if (active_devices != nullptr && active_devices->len > 0) {
            const char* name = nm_device_get_iface(static_cast<NMDevice*>(g_ptr_array_index(active_devices, 0)));
            hotspot.interface_name = name != nullptr ? name : "";
        }
        if (NMSettingWireless* wireless = nm_connection_get_setting_wireless(profile)) {
            if (GBytes* ssid = nm_setting_wireless_get_ssid(wireless)) {
                gsize size = 0;
                const auto* bytes = static_cast<const guint8*>(g_bytes_get_data(ssid, &size));
                if (bytes != nullptr && size > 0) {
                    if (char* text = nm_utils_ssid_to_utf8(bytes, size)) {
                        hotspot.name = clean_network_name(text);
                        g_free(text);
                    }
                }
            }
        }
        hotspot.security = nm_connection_get_setting_wireless_security(profile) != nullptr ? "WPA2" : "open";
        // NetworkManager shows its own access point as the device's active
        // one, with the frequency it picked.
        if (active_devices != nullptr && active_devices->len > 0) {
            auto* device = static_cast<NMDevice*>(g_ptr_array_index(active_devices, 0));
            if (NM_IS_DEVICE_WIFI(device)) {
                if (NMAccessPoint* own = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device))) {
                    const guint32 channel = nm_utils_wifi_freq_to_channel(nm_access_point_get_frequency(own));
                    hotspot.channel = channel > 0 && channel < 200 ? static_cast<int32_t>(channel) : 0;
                }
            }
        }
        if (const char* started = user_data(profile, kStartedKey)) {
            const int64_t since = g_ascii_strtoll(started, nullptr, 10);
            const int64_t uptime = wall_clock_seconds() - since;
            if (since > 0 && uptime >= 0 && uptime < INT32_MAX) {
                hotspot.uptime_seconds = static_cast<int32_t>(uptime);
            }
        }
        break;
    }
}

bool NetworkManagerClient::starting() const {
    return impl_->starting;
}

int32_t NetworkManagerClient::wifi_channel(const std::string& interface_name) {
    Impl& self = *impl_;
    self.handle_queued(kMaxEventsPerRead);
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        return 0;
    }
    NMDevice* device = nm_client_get_device_by_iface(client, interface_name.c_str());
    if (device == nullptr || !NM_IS_DEVICE_WIFI(device) || nm_device_get_state(device) != NM_DEVICE_STATE_ACTIVATED ||
        nm_device_wifi_get_mode(NM_DEVICE_WIFI(device)) != NM_802_11_MODE_INFRA) {
        return 0;
    }
    NMAccessPoint* access_point = nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device));
    if (access_point == nullptr) {
        return 0;
    }
    const guint32 channel = nm_utils_wifi_freq_to_channel(nm_access_point_get_frequency(access_point));
    return channel > 0 && channel < 200 ? static_cast<int32_t>(channel) : 0;
}

void NetworkManagerClient::wait_started(std::chrono::milliseconds limit) {
    Impl& self = *impl_;
    if (self.starting) {
        self.handle_until([&self] { return !self.starting; }, limit);
    }
}


bool NetworkManagerClient::can_change_network() {
    return true;
}

bool NetworkManagerClient::busy() const {
    const Impl& self = *impl_;
    return self.pending > 0 || self.status.state == model::SwitchState::Switching;
}

// ---------------------------------------------------------------------------
// Actions: the only calls that change the network. Each request runs with
// the client's context as the thread's default, so its answer arrives there
// and is handled by a later read() (or by leave()).
// ---------------------------------------------------------------------------

void NetworkManagerClient::Impl::fail(model::SwitchError error, std::string reason) {
    status.state = model::SwitchState::Failed;
    status.error = error;
    status.reason = error == model::SwitchError::Failed ? std::move(reason) : std::string();
}

void NetworkManagerClient::Impl::release_activation() {
    if (activation != nullptr) {
        g_object_unref(activation);
        activation = nullptr;
    }
}

// True while one of LokiCTL's hotspot profiles is active.
bool NetworkManagerClient::Impl::hotspot_active() const {
    const GPtrArray* actives = client != nullptr ? nm_client_get_active_connections(client) : nullptr;
    for (guint index = 0; actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        NMRemoteConnection* profile = nm_active_connection_get_connection(active);
        if (profile != nullptr && is_hotspot_profile(NM_CONNECTION(profile))) {
            return true;
        }
    }
    return false;
}

// Adds LokiCTL's hotspot profile for `request` in memory and activates it on
// `device`; on_router_added() takes the answer. Leftover profiles of
// LokiCTL's (none is active: the callers checked) are deleted first, and
// those answers only count down.
void NetworkManagerClient::Impl::add_hotspot(const RouterRequest& request, NMDevice* device,
                                             const std::string& restore) {
    g_main_context_push_thread_default(context);
    for (NMRemoteConnection* profile : hotspot_profiles(client)) {
        ++pending;
        nm_remote_connection_delete_async(profile, action_cancellable, &Impl::on_leftover_deleted, this);
    }

    char* uuid = nm_utils_uuid_generate();
    NMConnection* connection = hotspot_profile(request, uuid, restore);
    g_free(uuid);

    // Kept in memory only, never written to disk, and not tied to this
    // program: the hotspot stays up if LokiCTL is killed, and the next start
    // shows the mode and can undo it.
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "persist", g_variant_new_string("memory"));

    add_pending = true;
    undo_when_added = false;
    restore_interface = request.interface_name;
    ++pending;
    nm_client_add_and_activate_connection2(client, connection, device, nullptr, g_variant_builder_end(&options),
                                           action_cancellable, &Impl::on_router_added, this);
    g_main_context_pop_thread_default(context);
    g_object_unref(connection);
}

void NetworkManagerClient::start_router(const RouterRequest& request) {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.drop_scan();
    self.status = model::ModeSwitch{};
    self.status.target = model::NetMode::Router;
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        self.fail(model::SwitchError::Failed, "network service not running");
        return;
    }
    NMDevice* device = nm_client_get_device_by_iface(client, request.interface_name.c_str());
    if (device == nullptr || !NM_IS_DEVICE_WIFI(device)) {
        self.fail(model::SwitchError::Failed, "no Wi-Fi device " + request.interface_name);
        return;
    }
    if (self.hotspot_active()) {
        return;  // already on: nothing to do
    }

    // The Wi-Fi profile the interface is on now, to bring back later.
    std::string restore;
    if (NMActiveConnection* current = nm_device_get_active_connection(device)) {
        auto* profile = NM_CONNECTION(nm_active_connection_get_connection(current));
        const char* type = profile != nullptr ? nm_connection_get_connection_type(profile) : nullptr;
        if (type != nullptr && std::strcmp(type, NM_SETTING_WIRELESS_SETTING_NAME) == 0 &&
            !is_hotspot_profile(profile)) {
            const char* uuid = nm_connection_get_uuid(profile);
            restore = uuid != nullptr ? uuid : "";
        }
    }

    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.router_waiting = true;
    self.add_hotspot(request, device, restore);
}

void NetworkManagerClient::start_share(const RouterRequest& request) {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.drop_scan();
    self.status = model::ModeSwitch{};
    self.status.target = model::NetMode::Share;
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        self.fail(model::SwitchError::Failed, "network service not running");
        return;
    }
    if (self.hotspot_active()) {
        return;  // a hotspot of LokiCTL's runs: nothing to do
    }
    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.router_waiting = true;
    self.share_request = request;
    self.device_waiting = true;
    self.add_when_ready();
}

// A Share switch: adds the hotspot once the service shows the new interface
// as a Wi-Fi device that is ready (it takes about a second after the
// helper added it).
void NetworkManagerClient::Impl::add_when_ready() {
    if (!device_waiting || client == nullptr || closing) {
        return;
    }
    NMDevice* device = nm_client_get_device_by_iface(client, share_request.interface_name.c_str());
    if (device == nullptr || !NM_IS_DEVICE_WIFI(device) || nm_device_get_state(device) < NM_DEVICE_STATE_DISCONNECTED) {
        return;
    }
    device_waiting = false;
    add_hotspot(share_request, device, std::string());
    share_request = RouterRequest{};  // the password is not kept
}

void NetworkManagerClient::restart_router(const RouterRequest& request) {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.drop_scan();
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        return;
    }
    // The hotspot's profile, while it is active; otherwise nothing runs that
    // could start again.
    NMRemoteConnection* profile = nullptr;
    const GPtrArray* actives = nm_client_get_active_connections(client);
    for (guint index = 0; profile == nullptr && actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        NMRemoteConnection* candidate = nm_active_connection_get_connection(active);
        if (candidate != nullptr && is_hotspot_profile(NM_CONNECTION(candidate))) {
            profile = candidate;
        }
    }
    if (profile == nullptr) {
        return;
    }
    self.status = model::ModeSwitch{};
    self.status.target = request.interface_name == kShareInterface ? model::NetMode::Share : model::NetMode::Router;
    NMDevice* device = nm_client_get_device_by_iface(client, request.interface_name.c_str());
    if (device == nullptr || !NM_IS_DEVICE_WIFI(device)) {
        self.fail(model::SwitchError::Failed, "no Wi-Fi device " + request.interface_name);
        return;
    }

    // Built afresh (the client's copy of the profile holds no password),
    // with the same UUID, the same Wi-Fi profile to bring back and, unless
    // the request names one, the same channel.
    RouterRequest rebuilt = request;
    if (rebuilt.channel <= 0) {
        if (NMSettingWireless* wireless = nm_connection_get_setting_wireless(NM_CONNECTION(profile))) {
            rebuilt.channel = static_cast<int32_t>(nm_setting_wireless_get_channel(wireless));
        }
    }
    const char* restore = user_data(NM_CONNECTION(profile), kRestoreKey);
    NMConnection* connection =
        hotspot_profile(rebuilt, nm_connection_get_uuid(NM_CONNECTION(profile)), restore != nullptr ? restore : "");

    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.router_waiting = true;
    self.undo_when_added = false;
    self.restore_interface = request.interface_name;
    ++self.pending;
    // Updated in memory only (the profile stays under /run, never on disk);
    // on_hotspot_updated() then activates it again.
    g_main_context_push_thread_default(self.context);
    nm_remote_connection_update2(profile, nm_connection_to_dbus(connection, NM_CONNECTION_SERIALIZE_ALL),
                                 NM_SETTINGS_UPDATE2_FLAG_IN_MEMORY, nullptr, self.action_cancellable,
                                 &Impl::on_hotspot_updated, &self);
    g_main_context_pop_thread_default(self.context);
    g_object_unref(connection);
}

void NetworkManagerClient::start_client() {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.status = model::ModeSwitch{};
    self.status.target = model::NetMode::Client;
    if (self.client == nullptr || hotspot_profiles(self.client).empty()) {
        return;  // already Client: nothing is sent
    }
    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.client_waiting = true;
    self.restore_interface = kHotspotInterface;
    self.delete_profiles_and_restore();
}

// Deletes every profile of LokiCTL's hotspot; once all deletes are
// answered, on_profile_deleted() activates the recorded Wi-Fi profile.
void NetworkManagerClient::Impl::delete_profiles_and_restore() {
    if (client == nullptr || closing) {
        return;
    }
    const std::vector<NMRemoteConnection*> profiles = hotspot_profiles(client);
    restore_uuid.clear();
    for (NMRemoteConnection* profile : profiles) {
        const char* uuid = user_data(NM_CONNECTION(profile), kRestoreKey);
        if (uuid != nullptr && restore_uuid.empty()) {
            restore_uuid = uuid;
        }
    }
    if (restore_interface.empty()) {
        restore_interface = kHotspotInterface;
    }
    g_main_context_push_thread_default(context);
    for (NMRemoteConnection* profile : profiles) {
        ++pending;
        ++deletes_pending;
        nm_remote_connection_delete_async(profile, action_cancellable, &Impl::on_profile_deleted, this);
    }
    g_main_context_pop_thread_default(context);
}

void NetworkManagerClient::Impl::on_router_added(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    NMActiveConnection* active =
        nm_client_add_and_activate_connection2_finish(NM_CLIENT(source), result, nullptr, &error);
    --self->pending;
    self->add_pending = false;
    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error(&error);
        return;
    }
    if (active == nullptr) {
        self->router_waiting = false;
        if (self->status.state == model::SwitchState::Switching) {
            self->fail(permission_denied(error) ? model::SwitchError::NotAllowed : model::SwitchError::Failed,
                       reason_of(error));
        }
        g_clear_error(&error);
        // The profile may have been added all the same.
        self->delete_profiles_and_restore();
        return;
    }
    g_clear_error(&error);
    self->release_activation();
    self->activation = active;
    if (self->undo_when_added) {
        self->undo_when_added = false;
        self->router_waiting = false;
        self->release_activation();
        self->delete_profiles_and_restore();
    }
}

void NetworkManagerClient::Impl::on_profile_deleted(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(source), result, &error);
    --self->pending;
    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error(&error);
        return;
    }
    if (error != nullptr && self->client_waiting && self->status.state == model::SwitchState::Switching) {
        self->fail(permission_denied(error) ? model::SwitchError::NotAllowed : model::SwitchError::Failed,
                   reason_of(error));
    }
    g_clear_error(&error);
    if (--self->deletes_pending > 0 || self->restore_uuid.empty() || self->closing) {
        return;
    }
    NMRemoteConnection* restore = nm_client_get_connection_by_uuid(self->client, self->restore_uuid.c_str());
    NMDevice* device = nm_client_get_device_by_iface(self->client, self->restore_interface.c_str());
    self->restore_uuid.clear();
    if (restore == nullptr) {
        return;  // gone meanwhile: the service's autoconnect picks
    }
    g_main_context_push_thread_default(self->context);
    ++self->pending;
    nm_client_activate_connection_async(self->client, NM_CONNECTION(restore), device, nullptr,
                                        self->action_cancellable, &Impl::on_restored, self);
    g_main_context_pop_thread_default(self->context);
}

// A leftover profile deleted before a Router switch: only counted down; a
// failure shows up when the new profile is added.
void NetworkManagerClient::Impl::on_leftover_deleted(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(source), result, nullptr);
    --self->pending;
}

void NetworkManagerClient::Impl::on_restored(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    NMActiveConnection* active = nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
    --self->pending;
    if (active != nullptr) {
        g_object_unref(active);
    }
    if (error != nullptr && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) && self->client_waiting &&
        self->status.state == model::SwitchState::Switching) {
        self->fail(permission_denied(error) ? model::SwitchError::NotAllowed : model::SwitchError::Failed,
                   reason_of(error));
    }
    g_clear_error(&error);
}

// The restart's update has been answered: on success the profile is
// activated again on the hotspot's interface; on failure the switch is
// undone, so the page never shows a password the hotspot does not use.
void NetworkManagerClient::Impl::on_hotspot_updated(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    GVariant* answer = nm_remote_connection_update2_finish(NM_REMOTE_CONNECTION(source), result, &error);
    --self->pending;
    if (answer != nullptr) {
        g_variant_unref(answer);
    }
    if ((error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) || !self->router_waiting ||
        self->closing) {
        // Given up meanwhile (too long, or on the way out).
        g_clear_error(&error);
        return;
    }
    if (error != nullptr) {
        self->router_waiting = false;
        self->fail(permission_denied(error) ? model::SwitchError::NotAllowed : model::SwitchError::Failed,
                   reason_of(error));
        g_clear_error(&error);
        self->delete_profiles_and_restore();
        return;
    }
    NMDevice* device = nm_client_get_device_by_iface(self->client, self->restore_interface.c_str());
    g_main_context_push_thread_default(self->context);
    ++self->pending;
    nm_client_activate_connection_async(self->client, NM_CONNECTION(source), device, nullptr,
                                        self->action_cancellable, &Impl::on_hotspot_reactivated, self);
    g_main_context_pop_thread_default(self->context);
}

void NetworkManagerClient::Impl::on_hotspot_reactivated(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    NMActiveConnection* active = nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
    --self->pending;
    if ((error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) || !self->router_waiting ||
        self->closing) {
        if (active != nullptr) {
            g_object_unref(active);
        }
        g_clear_error(&error);
        return;
    }
    if (active == nullptr) {
        self->router_waiting = false;
        self->fail(permission_denied(error) ? model::SwitchError::NotAllowed : model::SwitchError::Failed,
                   reason_of(error));
        g_clear_error(&error);
        self->delete_profiles_and_restore();
        return;
    }
    g_clear_error(&error);
    // check_progress() ends the switch once this activation is active.
    self->release_activation();
    self->activation = active;
}

// Called by read(): ends a switch that has finished, failed or run too long.
void NetworkManagerClient::Impl::check_progress() {
    add_when_ready();
    if (router_waiting && activation != nullptr) {
        const NMActiveConnectionState state = nm_active_connection_get_state(activation);
        if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
            router_waiting = false;
            release_activation();
            status.state = model::SwitchState::Idle;
        } else if (state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATING ||
                   state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATED) {
            router_waiting = false;
            fail(model::SwitchError::Failed, reason_of(nm_active_connection_get_state_reason(activation)));
            release_activation();
            delete_profiles_and_restore();
        }
    }
    if (router_waiting && Clock::now() - switch_started >= NetworkManagerClient::kSwitchLimit) {
        router_waiting = false;
        fail(model::SwitchError::Failed, "no hotspot after 20 s");
        release_activation();
        device_waiting = false;
        share_request = RouterRequest{};
        if (add_pending) {
            undo_when_added = true;
        } else {
            delete_profiles_and_restore();
        }
    }
    if (client_waiting && pending == 0) {
        client_waiting = false;
        if (status.state == model::SwitchState::Switching) {
            status.state = model::SwitchState::Idle;
        }
    }
    if (client_waiting && Clock::now() - switch_started >= NetworkManagerClient::kSwitchLimit &&
        status.state == model::SwitchState::Switching) {
        fail(model::SwitchError::Failed, "no answer in 20 s");
    }
}

void NetworkManagerClient::leave(std::chrono::milliseconds limit) {
    Impl& self = *impl_;
    // A Share switch whose interface is not ready yet has added nothing.
    self.device_waiting = false;
    self.share_request = RouterRequest{};
    if (self.client == nullptr || (!self.add_pending && hotspot_profiles(self.client).empty())) {
        return;  // plain Client: nothing is sent
    }
    const auto deadline = Clock::now() + limit;
    const auto left = [deadline] {
        return std::max(milliseconds(0), std::chrono::duration_cast<milliseconds>(deadline - Clock::now()));
    };
    // An add that is still on its way must be answered first, or the
    // hotspot could come up after LokiCTL has gone.
    self.handle_until([&self] { return !self.add_pending; }, left());
    self.router_waiting = false;
    self.release_activation();
    self.handle_until([&self] { return self.deletes_pending == 0; }, left());
    self.delete_profiles_and_restore();
    self.handle_until([&self] { return self.pending == 0; }, left());
}

// ---------------------------------------------------------------------------
// Wi-Fi scan: asks the service to scan, which changes no setting, and reads
// the access points it lists. The request runs with the client's context as
// the thread's default, like the actions, so its answer is handled by a
// later read() or read_wifi_scan().
// ---------------------------------------------------------------------------

namespace {

// The managed Wi-Fi device `interface_name`, or nullptr.
NMDeviceWifi* managed_wifi(NMClient* client, const std::string& interface_name) {
    NMDevice* device = client != nullptr ? nm_client_get_device_by_iface(client, interface_name.c_str()) : nullptr;
    if (device == nullptr || !NM_IS_DEVICE_WIFI(device) || !nm_device_get_managed(device)) {
        return nullptr;
    }
    return NM_DEVICE_WIFI(device);
}

// The local time of day, in minutes since midnight, of `boottime_ms`, a
// time on the clock the service gives scan times in (CLOCK_BOOTTIME);
// kClockUnknown when it cannot be told.
int32_t clock_minutes_of(gint64 boottime_ms) {
    const gint64 now_ms = nm_utils_get_timestamp_msec();
    if (boottime_ms < 0 || now_ms < boottime_ms) {
        return model::kClockUnknown;
    }
    const std::time_t when = std::time(nullptr) - static_cast<std::time_t>((now_ms - boottime_ms) / 1000);
    std::tm local_time{};
    if (when < 0 || localtime_r(&when, &local_time) == nullptr) {
        return model::kClockUnknown;
    }
    return local_time.tm_hour * 60 + local_time.tm_min;
}

} // namespace

void NetworkManagerClient::request_wifi_scan(const std::string& interface_name) {
    Impl& self = *impl_;
    if (self.closing || self.scan_pending || self.scan_waiting || busy()) {
        return;
    }
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client) || self.hotspot_active()) {
        return;
    }
    NMDeviceWifi* wifi = managed_wifi(client, interface_name);
    if (wifi == nullptr) {
        return;
    }
    self.scan_error = model::WifiScanError::None;
    self.scan_reason.clear();
    self.scan_last_before = nm_device_wifi_get_last_scan(wifi);
    self.scan_started = Clock::now();
    self.scan_interface = interface_name;
    self.scan_pending = true;
    g_main_context_push_thread_default(self.context);
    nm_device_wifi_request_scan_async(wifi, self.action_cancellable, &Impl::on_scan_requested, &self);
    g_main_context_pop_thread_default(self.context);
}

void NetworkManagerClient::Impl::on_scan_requested(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    const gboolean accepted = nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(source), result, &error);
    self->scan_pending = false;
    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        g_clear_error(&error);
        return;
    }
    if (accepted) {
        // The scan runs now; check_scan() sees it end. Not when a switch
        // began in the meantime: the hotspot would cut it short.
        self->scan_waiting = self->status.state != model::SwitchState::Switching && !self->hotspot_active();
    } else if (permission_denied(error)) {
        self->scan_error = model::WifiScanError::NotAllowed;
    } else {
        // For example a scan right after another one: the list stays.
        self->scan_error = model::WifiScanError::Refused;
        self->scan_reason = reason_of(error);
    }
    g_clear_error(&error);
}

// Ends a scan once the device's last-scan time has changed, or once it has
// run longer than kScanLimit.
void NetworkManagerClient::Impl::check_scan() {
    if (!scan_waiting) {
        return;
    }
    NMDeviceWifi* wifi = managed_wifi(client, scan_interface);
    if (wifi != nullptr && nm_device_wifi_get_last_scan(wifi) != scan_last_before) {
        scan_waiting = false;
    } else if (Clock::now() - scan_started >= NetworkManagerClient::kScanLimit) {
        scan_waiting = false;
        scan_error = model::WifiScanError::TimedOut;
    }
}

// A switch that may bring up the hotspot begins: a scan still waiting
// would never see its result (and would later read as timed out), so it
// ends here without an error.
void NetworkManagerClient::Impl::drop_scan() {
    scan_waiting = false;
    scan_error = model::WifiScanError::None;
    scan_reason.clear();
}

bool NetworkManagerClient::scanning() const {
    return impl_->scan_pending || impl_->scan_waiting;
}

void NetworkManagerClient::forget_wifi_scan_error() {
    Impl& self = *impl_;
    if (!self.scan_pending && !self.scan_waiting) {
        self.scan_error = model::WifiScanError::None;
        self.scan_reason.clear();
    }
}

void NetworkManagerClient::read_wifi_scan(const std::string& interface_name, model::WifiScan& scan) {
    scan = model::WifiScan{};
    Impl& self = *impl_;
    self.handle_queued(kMaxEventsPerRead);
    self.check_scan();
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        scan.error = model::WifiScanError::NoService;
        return;
    }
    if (self.hotspot_active()) {
        scan.error = model::WifiScanError::Hotspot;
        return;
    }
    NMDeviceWifi* wifi = managed_wifi(client, interface_name);
    if (wifi == nullptr) {
        scan.error = model::WifiScanError::NoDevice;
        return;
    }
    if (!nm_client_wireless_get_enabled(client) || !nm_client_wireless_hardware_get_enabled(client)) {
        scan.error = model::WifiScanError::WifiOff;
        return;
    }
    scan.scanning = scanning();
    scan.error = self.scan_error;
    scan.reason = self.scan_reason;
    scan.last_scan_minutes = clock_minutes_of(nm_device_wifi_get_last_scan(wifi));

    // The access point it is joined to, as a client.
    NMAccessPoint* joined = nullptr;
    if (nm_device_get_state(NM_DEVICE(wifi)) == NM_DEVICE_STATE_ACTIVATED &&
        nm_device_wifi_get_mode(wifi) == NM_802_11_MODE_INFRA) {
        joined = nm_device_wifi_get_active_access_point(wifi);
    }
    const GPtrArray* access_points = nm_device_wifi_get_access_points(wifi);
    for (guint index = 0; access_points != nullptr && index < access_points->len; ++index) {
        auto* access_point = static_cast<NMAccessPoint*>(g_ptr_array_index(access_points, index));
        model::WifiNetwork network;
        network.name = network_name(access_point);
        network.security = security_text(access_point);
        network.strength = std::min<int32_t>(nm_access_point_get_strength(access_point), 100);
        const guint32 channel = nm_utils_wifi_freq_to_channel(nm_access_point_get_frequency(access_point));
        network.channel = channel > 0 && channel < 200 ? static_cast<int32_t>(channel) : 0;
        network.joined = joined != nullptr && access_point == joined;
        scan.networks.push_back(std::move(network));
    }
    scan.found = static_cast<int32_t>(scan.networks.size());
    model::sort_wifi_networks(scan.networks);
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

int32_t NetworkManagerClient::wifi_channel(const std::string& /*interface_name*/) {
    return 0;
}

void NetworkManagerClient::wait_started(std::chrono::milliseconds /*limit*/) {}

bool NetworkManagerClient::can_change_network() {
    return false;
}

void NetworkManagerClient::start_router(const RouterRequest& /*request*/) {}

void NetworkManagerClient::start_share(const RouterRequest& /*request*/) {}

void NetworkManagerClient::restart_router(const RouterRequest& /*request*/) {}

void NetworkManagerClient::start_client() {}

bool NetworkManagerClient::busy() const {
    return false;
}

void NetworkManagerClient::leave(std::chrono::milliseconds /*limit*/) {}

void NetworkManagerClient::request_wifi_scan(const std::string& /*interface_name*/) {}

void NetworkManagerClient::forget_wifi_scan_error() {}

bool NetworkManagerClient::scanning() const {
    return false;
}

void NetworkManagerClient::read_wifi_scan(const std::string& /*interface_name*/, model::WifiScan& scan) {
    scan = model::WifiScan{};
    scan.error = model::WifiScanError::NoService;
}

#endif // APP_USE_LIBNM

} // namespace platform

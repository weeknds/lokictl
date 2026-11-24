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
// profile, adding and activating the in-memory VPN profile, adding and
// activating the in-memory Laptop link profile, deleting LokiCTL's
// hotspot, VPN and Laptop link profiles and activating the Wi-Fi profile
// the hotspot interface was on before. Nothing else is ever added, changed
// or deleted (the cable's own profile is never touched: the service's
// autoconnect brings it back once Laptop link's profile is gone), and only
// kHotspotInterface (Router), kShareInterface (Share), kVpnInterface (VPN,
// which the service makes for the profile) and kLaptopInterface (Laptop
// link) are touched. The VPN profile's keys are only written, never read back.

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
#include <sys/socket.h>
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
// The VPN profile's own user data: the file it was made from and the
// server's host, as the pages show them.
constexpr const char* kFileKey = "lokictl.file";
constexpr const char* kServerKey = "lokictl.server";

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

// True for LokiCTL's VPN profile: its name and its own user-data mark.
bool is_vpn_profile(NMConnection* connection) {
    if (connection == nullptr) {
        return false;
    }
    const char* id = nm_connection_get_id(connection);
    return id != nullptr && std::strcmp(id, kVpnProfile) == 0 && user_data(connection, kStartedKey) != nullptr;
}

// True for LokiCTL's Laptop link profile: its name and its own user-data
// mark.
bool is_laptop_profile(NMConnection* connection) {
    if (connection == nullptr) {
        return false;
    }
    const char* id = nm_connection_get_id(connection);
    return id != nullptr && std::strcmp(id, kLaptopProfile) == 0 && user_data(connection, kStartedKey) != nullptr;
}

bool is_lokictl_profile(NMConnection* connection) {
    return is_hotspot_profile(connection) || is_vpn_profile(connection) || is_laptop_profile(connection);
}

// The profiles that pass `test`, as the client knows them now.
std::vector<NMRemoteConnection*> profiles_where(NMClient* client, bool (*test)(NMConnection*)) {
    std::vector<NMRemoteConnection*> profiles;
    const GPtrArray* connections = nm_client_get_connections(client);
    for (guint index = 0; connections != nullptr && index < connections->len; ++index) {
        auto* connection = static_cast<NMRemoteConnection*>(g_ptr_array_index(connections, index));
        if (test(NM_CONNECTION(connection))) {
            profiles.push_back(connection);
        }
    }
    return profiles;
}

// Every hotspot profile LokiCTL made.
std::vector<NMRemoteConnection*> hotspot_profiles(NMClient* client) {
    return profiles_where(client, is_hotspot_profile);
}

// Every profile LokiCTL made: hotspot, VPN and Laptop link.
std::vector<NMRemoteConnection*> lokictl_profiles(NMClient* client) {
    return profiles_where(client, is_lokictl_profile);
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

// LokiCTL's Laptop link profile, with `uuid` and the time now as its start:
// a cable profile on kLaptopInterface, IPv4 shared on the fixed address
// model::kLaptopLinkAddress (so the service hands the laptop an address
// and passes its traffic on through the uplink), IPv6 off, autoconnect
// off. The caller owns the result.
NMConnection* laptop_profile(const char* uuid) {
    NMConnection* connection = nm_simple_connection_new();
    auto* setting_connection = NM_SETTING_CONNECTION(nm_setting_connection_new());
    g_object_set(setting_connection,
                 NM_SETTING_CONNECTION_ID, kLaptopProfile,
                 NM_SETTING_CONNECTION_UUID, uuid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRED_SETTING_NAME,
                 NM_SETTING_CONNECTION_INTERFACE_NAME, kLaptopInterface,
                 NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
                 nullptr);
    nm_connection_add_setting(connection, NM_SETTING(setting_connection));

    auto* user = NM_SETTING_USER(nm_setting_user_new());
    const std::string started = std::to_string(wall_clock_seconds());
    nm_setting_user_set_data(user, kStartedKey, started.c_str(), nullptr);
    nm_connection_add_setting(connection, NM_SETTING(user));

    nm_connection_add_setting(connection, nm_setting_wired_new());

    auto* ipv4 = NM_SETTING_IP_CONFIG(nm_setting_ip4_config_new());
    g_object_set(ipv4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_SHARED, nullptr);
    if (NMIPAddress* address = nm_ip_address_new(AF_INET, model::kLaptopLinkAddress,
                                                 static_cast<guint>(model::kLaptopLinkPrefix), nullptr)) {
        nm_setting_ip_config_add_address(ipv4, address);
        nm_ip_address_unref(address);
    }
    nm_connection_add_setting(connection, NM_SETTING(ipv4));
    auto* ipv6 = NM_SETTING_IP_CONFIG(nm_setting_ip6_config_new());
    g_object_set(ipv6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_DISABLED, nullptr);
    nm_connection_add_setting(connection, NM_SETTING(ipv6));
    return connection;
}

// The DNS priority of the VPN's servers when a peer routes everything: a
// negative one makes the service use them alone, so names are not asked
// outside the tunnel.
constexpr int kVpnDnsPriority = -50;

void set_user_text(NMSettingUser* user, const char* key, const std::string& value) {
    // The service takes UTF-8 only; a name that is not stays out.
    if (!value.empty() && g_utf8_validate(value.c_str(), static_cast<gssize>(value.size()), nullptr)) {
        nm_setting_user_set_data(user, key, value.c_str(), nullptr);
    }
}

// LokiCTL's VPN profile for `request`, with `uuid` and the time now as its
// start: WireGuard on kVpnInterface with the file's keys, port, mark, MTU
// and peers; manual addresses (a family without one is disabled) and the
// file's DNS servers; autoconnect off. Peer routes and the default route
// are left to the service. The caller owns the result.
NMConnection* vpn_profile(const VpnRequest& request, const char* uuid) {
    const WireGuardConfig& config = request.config;
    NMConnection* connection = nm_simple_connection_new();
    auto* setting_connection = NM_SETTING_CONNECTION(nm_setting_connection_new());
    g_object_set(setting_connection,
                 NM_SETTING_CONNECTION_ID, kVpnProfile,
                 NM_SETTING_CONNECTION_UUID, uuid,
                 NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIREGUARD_SETTING_NAME,
                 NM_SETTING_CONNECTION_INTERFACE_NAME, kVpnInterface,
                 NM_SETTING_CONNECTION_AUTOCONNECT, FALSE,
                 nullptr);
    nm_connection_add_setting(connection, NM_SETTING(setting_connection));

    auto* user = NM_SETTING_USER(nm_setting_user_new());
    const std::string started = std::to_string(wall_clock_seconds());
    nm_setting_user_set_data(user, kStartedKey, started.c_str(), nullptr);
    set_user_text(user, kFileKey, request.file);
    for (const WireGuardPeer& peer : config.peers) {
        if (!peer.endpoint_host.empty()) {
            set_user_text(user, kServerKey, peer.endpoint_host);
            break;
        }
    }
    nm_connection_add_setting(connection, NM_SETTING(user));

    auto* wireguard = NM_SETTING_WIREGUARD(nm_setting_wireguard_new());
    g_object_set(wireguard,
                 NM_SETTING_WIREGUARD_PRIVATE_KEY, config.private_key.c_str(),
                 NM_SETTING_WIREGUARD_PRIVATE_KEY_FLAGS, static_cast<guint>(NM_SETTING_SECRET_FLAG_NONE),
                 NM_SETTING_WIREGUARD_LISTEN_PORT, static_cast<guint>(config.listen_port),
                 NM_SETTING_WIREGUARD_FWMARK, static_cast<guint>(config.fwmark),
                 NM_SETTING_WIREGUARD_MTU, static_cast<guint>(config.mtu),
                 nullptr);
    for (const WireGuardPeer& source : config.peers) {
        NMWireGuardPeer* peer = nm_wireguard_peer_new();
        nm_wireguard_peer_set_public_key(peer, source.public_key.c_str(), FALSE);
        if (!source.preshared_key.empty()) {
            nm_wireguard_peer_set_preshared_key(peer, source.preshared_key.c_str(), FALSE);
            nm_wireguard_peer_set_preshared_key_flags(peer, NM_SETTING_SECRET_FLAG_NONE);
        }
        if (!source.endpoint.empty()) {
            nm_wireguard_peer_set_endpoint(peer, source.endpoint.c_str(), FALSE);
        }
        for (const WireGuardAddress& allowed : source.allowed_ips) {
            const std::string text = allowed.address + "/" + std::to_string(allowed.prefix);
            nm_wireguard_peer_append_allowed_ip(peer, text.c_str(), FALSE);
        }
        nm_wireguard_peer_set_persistent_keepalive(peer, static_cast<guint16>(source.keepalive));
        nm_wireguard_peer_seal(peer);
        nm_setting_wireguard_append_peer(wireguard, peer);
        nm_wireguard_peer_unref(peer);
    }
    nm_connection_add_setting(connection, NM_SETTING(wireguard));

    auto* ipv4 = NM_SETTING_IP_CONFIG(nm_setting_ip4_config_new());
    auto* ipv6 = NM_SETTING_IP_CONFIG(nm_setting_ip6_config_new());
    bool has_ipv4 = false;
    bool has_ipv6 = false;
    for (const WireGuardAddress& address : config.addresses) {
        GError* error = nullptr;
        NMIPAddress* ip = nm_ip_address_new(address.ipv6 ? AF_INET6 : AF_INET, address.address.c_str(),
                                            static_cast<guint>(address.prefix), &error);
        g_clear_error(&error);
        if (ip != nullptr) {
            nm_setting_ip_config_add_address(address.ipv6 ? ipv6 : ipv4, ip);
            nm_ip_address_unref(ip);
            (address.ipv6 ? has_ipv6 : has_ipv4) = true;
        }
    }
    bool dns_ipv4 = false;
    bool dns_ipv6 = false;
    for (const std::string& server : config.dns) {
        const bool server_ipv6 = server.find(':') != std::string::npos;
        if (server_ipv6 ? has_ipv6 : has_ipv4) {
            nm_setting_ip_config_add_dns(server_ipv6 ? ipv6 : ipv4, server.c_str());
            (server_ipv6 ? dns_ipv6 : dns_ipv4) = true;
        }
    }
    const bool everything = routes_everything(config);
    g_object_set(ipv4, NM_SETTING_IP_CONFIG_METHOD,
                 has_ipv4 ? NM_SETTING_IP4_CONFIG_METHOD_MANUAL : NM_SETTING_IP4_CONFIG_METHOD_DISABLED, nullptr);
    g_object_set(ipv6, NM_SETTING_IP_CONFIG_METHOD,
                 has_ipv6 ? NM_SETTING_IP6_CONFIG_METHOD_MANUAL : NM_SETTING_IP6_CONFIG_METHOD_DISABLED, nullptr);
    if (everything && dns_ipv4) {
        g_object_set(ipv4, NM_SETTING_IP_CONFIG_DNS_PRIORITY, kVpnDnsPriority, nullptr);
    }
    if (everything && dns_ipv6) {
        g_object_set(ipv6, NM_SETTING_IP_CONFIG_DNS_PRIORITY, kVpnDnsPriority, nullptr);
    }
    nm_connection_add_setting(connection, NM_SETTING(ipv4));
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
    bool router_waiting{false};          // a Router, Share or VPN switch waits for its profile to be active
    bool add_pending{false};             // the hotspot's add-and-activate (or the tunnel's steps) is not answered yet
    bool undo_when_added{false};         // undo the Router switch once the add is answered
    NMActiveConnection* activation{nullptr}; // the hotspot's activation (a reference)
    bool device_waiting{false};          // a Share switch waits for its interface to be ready
    RouterRequest share_request;         // what that Share switch adds then
    bool client_waiting{false};          // a Client switch (or VPN off) waits for its answers
    // Which of LokiCTL's profiles a Client switch deletes, and a failed
    // switch undoes: a VPN switch only touches the tunnel and a hotspot
    // switch only the hotspot, so the other half of the VPN gateway stays;
    // a Laptop link switch only its own profile.
    enum class Scope { All, Hotspot, Vpn, Laptop };
    Scope scope{Scope::All};
    int pending{0};                      // requests sent and not answered yet
    int deletes_pending{0};              // deletes of hotspot profiles not answered yet
    std::string restore_uuid;            // activated once those deletes are answered
    std::string restore_interface;
    bool closing{false};                 // no new requests: the client is going away
    NMConnection* vpn_waiting{nullptr};  // the tunnel's profile, held until the leftovers' deletes are answered
    int vpn_deletes_pending{0};          // deletes of leftover VPN profiles not answered yet

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
    bool vpn_active() const;
    bool laptop_active() const;
    model::NetMode hotspot_mode() const;
    void add_hotspot(const RouterRequest& request, NMDevice* device, const std::string& restore);
    void add_vpn(const VpnRequest& request);
    void add_vpn_waiting();
    void drop_vpn_waiting();
    bool vpn_given_up();
    void vpn_refused(const GError* error);
    void add_when_ready();
    void delete_profiles_and_restore();
    void check_progress();
    static void on_router_added(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_profile_deleted(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_leftover_deleted(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_vpn_leftover_deleted(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_vpn_added(GObject* source, GAsyncResult* result, gpointer user_data);
    static void on_vpn_activated(GObject* source, GAsyncResult* result, gpointer user_data);
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
    self.drop_vpn_waiting();
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
    reading.internet_check =
        nm_client_connectivity_check_get_available(client) && nm_client_connectivity_check_get_enabled(client);

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

    // LokiCTL's Laptop link, while its profile is active.
    for (guint index = 0; actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        auto* profile = NM_CONNECTION(nm_active_connection_get_connection(active));
        if (!is_laptop_profile(profile) || nm_active_connection_get_state(active) != NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
            continue;
        }
        reading.laptop.active = true;
        const GPtrArray* active_devices = nm_active_connection_get_devices(active);
        if (active_devices != nullptr && active_devices->len > 0) {
            const char* name = nm_device_get_iface(static_cast<NMDevice*>(g_ptr_array_index(active_devices, 0)));
            reading.laptop.interface_name = name != nullptr ? name : "";
        }
        break;
    }

    // LokiCTL's VPN tunnel, while it is being set up or is up. Only the
    // file's name and the server's host are read, from its user data.
    for (guint index = 0; actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        auto* profile = NM_CONNECTION(nm_active_connection_get_connection(active));
        if (!is_vpn_profile(profile)) {
            continue;
        }
        const NMActiveConnectionState state = nm_active_connection_get_state(active);
        if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
            reading.vpn.state = model::VpnState::On;
        } else if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATING) {
            reading.vpn.state = model::VpnState::Connecting;
        } else {
            continue;
        }
        const char* file = user_data(profile, kFileKey);
        const char* server = user_data(profile, kServerKey);
        reading.vpn.file = file != nullptr ? clean_network_name(file) : std::string();
        reading.vpn.server = server != nullptr ? clean_network_name(server) : std::string();
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

namespace {

// True while a profile that passes `test` is active.
bool any_active(NMClient* client, bool (*test)(NMConnection*)) {
    const GPtrArray* actives = client != nullptr ? nm_client_get_active_connections(client) : nullptr;
    for (guint index = 0; actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        NMRemoteConnection* profile = nm_active_connection_get_connection(active);
        if (profile != nullptr && test(NM_CONNECTION(profile))) {
            return true;
        }
    }
    return false;
}

} // namespace

// True while one of LokiCTL's hotspot profiles is active.
bool NetworkManagerClient::Impl::hotspot_active() const {
    return any_active(client, is_hotspot_profile);
}

// True while LokiCTL's VPN profile is active (or being activated).
bool NetworkManagerClient::Impl::vpn_active() const {
    return any_active(client, is_vpn_profile);
}

// True while LokiCTL's Laptop link profile is active (or being activated).
bool NetworkManagerClient::Impl::laptop_active() const {
    return any_active(client, is_laptop_profile);
}

// Router or Share while one of LokiCTL's hotspot profiles is active (by its
// interface), otherwise Client.
model::NetMode NetworkManagerClient::Impl::hotspot_mode() const {
    const GPtrArray* actives = client != nullptr ? nm_client_get_active_connections(client) : nullptr;
    for (guint index = 0; actives != nullptr && index < actives->len; ++index) {
        auto* active = static_cast<NMActiveConnection*>(g_ptr_array_index(actives, index));
        NMRemoteConnection* profile = nm_active_connection_get_connection(active);
        if (profile == nullptr || !is_hotspot_profile(NM_CONNECTION(profile))) {
            continue;
        }
        const GPtrArray* devices = nm_active_connection_get_devices(active);
        const char* name = devices != nullptr && devices->len > 0
                               ? nm_device_get_iface(static_cast<NMDevice*>(g_ptr_array_index(devices, 0)))
                               : nullptr;
        return name != nullptr && std::strcmp(name, kShareInterface) == 0 ? model::NetMode::Share
                                                                          : model::NetMode::Router;
    }
    return model::NetMode::Client;
}

// Adds LokiCTL's VPN profile for `request` in memory and then activates it;
// the service makes kVpnInterface for it. Leftover VPN profiles of
// LokiCTL's (none is active: the caller checked), such as one the service
// took down by itself, are deleted first, and the add waits for those
// answers. Adding and activating are two steps, as for a stored profile:
// the service refuses add-and-activate while it still holds the leftover's
// WireGuard device. add_pending stays set until the last step is answered.
void NetworkManagerClient::Impl::add_vpn(const VpnRequest& request) {
    drop_vpn_waiting();
    char* uuid = nm_utils_uuid_generate();
    vpn_waiting = vpn_profile(request, uuid);
    g_free(uuid);

    add_pending = true;
    undo_when_added = false;
    g_main_context_push_thread_default(context);
    for (NMRemoteConnection* profile : profiles_where(client, is_vpn_profile)) {
        ++pending;
        ++vpn_deletes_pending;
        nm_remote_connection_delete_async(profile, action_cancellable, &Impl::on_vpn_leftover_deleted, this);
    }
    g_main_context_pop_thread_default(context);
    if (vpn_deletes_pending == 0) {
        add_vpn_waiting();
    }
}

// Adds the tunnel's held profile in memory only, like the hotspot: never
// written to disk, gone at the latest when the device starts again.
// on_vpn_added() then activates it.
void NetworkManagerClient::Impl::add_vpn_waiting() {
    if (vpn_waiting == nullptr) {
        vpn_refused(nullptr);
        return;
    }
    g_main_context_push_thread_default(context);
    ++pending;
    nm_client_add_connection2(client, nm_connection_to_dbus(vpn_waiting, NM_CONNECTION_SERIALIZE_ALL),
                              NM_SETTINGS_ADD_CONNECTION2_FLAG_IN_MEMORY, nullptr, TRUE, action_cancellable,
                              &Impl::on_vpn_added, this);
    g_main_context_pop_thread_default(context);
    drop_vpn_waiting();
}

// libnm wipes the keys it holds when the profile is freed.
void NetworkManagerClient::Impl::drop_vpn_waiting() {
    if (vpn_waiting != nullptr) {
        g_object_unref(vpn_waiting);
        vpn_waiting = nullptr;
    }
}

// True when the tunnel's switch was given up while a step was on its way
// (too long, or on the way out): the next step is not sent, and what was
// added goes.
bool NetworkManagerClient::Impl::vpn_given_up() {
    if (router_waiting && !undo_when_added && !closing) {
        return false;
    }
    add_pending = false;
    undo_when_added = false;
    router_waiting = false;
    drop_vpn_waiting();
    release_activation();
    delete_profiles_and_restore();
    return true;
}

// The tunnel's add or activation was refused: the switch fails and what
// was added goes. The service's own text is not shown: it could quote the
// profile.
void NetworkManagerClient::Impl::vpn_refused(const GError* error) {
    add_pending = false;
    undo_when_added = false;
    router_waiting = false;
    drop_vpn_waiting();
    if (status.state == model::SwitchState::Switching) {
        fail(permission_denied(error) ? model::SwitchError::NotAllowed : model::SwitchError::Failed,
             "tunnel refused");
    }
    delete_profiles_and_restore();
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
    self.scope = Impl::Scope::Hotspot;
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
    self.scope = Impl::Scope::Hotspot;
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
    self.scope = Impl::Scope::Hotspot;
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

void NetworkManagerClient::start_vpn(const VpnRequest& request) {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.drop_scan();
    self.status = model::ModeSwitch{};
    self.status.target = model::NetMode::Vpn;
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        self.fail(model::SwitchError::Failed, "network service not running");
        return;
    }
    if (self.vpn_active()) {
        return;  // a tunnel of LokiCTL's runs: nothing to do
    }
    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.router_waiting = true;
    self.scope = Impl::Scope::Vpn;
    self.add_vpn(request);
}

void NetworkManagerClient::start_laptop() {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.status = model::ModeSwitch{};
    self.status.target = model::NetMode::Laptop;
    NMClient* client = self.client;
    if (client == nullptr || !nm_client_get_nm_running(client)) {
        self.fail(model::SwitchError::Failed, "network service not running");
        return;
    }
    NMDevice* device = nm_client_get_device_by_iface(client, kLaptopInterface);
    if (device == nullptr || !NM_IS_DEVICE_ETHERNET(device)) {
        self.fail(model::SwitchError::Failed, std::string("no cable port ") + kLaptopInterface);
        return;
    }
    if (self.laptop_active()) {
        return;  // already on: nothing to do
    }

    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.router_waiting = true;
    self.scope = Impl::Scope::Laptop;
    self.undo_when_added = false;
    self.restore_interface = kLaptopInterface;

    g_main_context_push_thread_default(self.context);
    // Leftovers of LokiCTL's (none is active: checked above).
    for (NMRemoteConnection* profile : profiles_where(client, is_laptop_profile)) {
        ++self.pending;
        nm_remote_connection_delete_async(profile, self.action_cancellable, &Impl::on_leftover_deleted, &self);
    }
    char* uuid = nm_utils_uuid_generate();
    NMConnection* connection = laptop_profile(uuid);
    g_free(uuid);
    // In memory only, like the hotspot's: never written to disk, and gone
    // at the latest when the device starts again. Activating it on the
    // cable port takes the port over from the cable's own profile, which
    // is left as it is.
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&options, "{sv}", "persist", g_variant_new_string("memory"));
    self.add_pending = true;
    ++self.pending;
    nm_client_add_and_activate_connection2(client, connection, device, nullptr, g_variant_builder_end(&options),
                                           self.action_cancellable, &Impl::on_router_added, &self);
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
    if (self.client == nullptr || lokictl_profiles(self.client).empty()) {
        return;  // already Client: nothing is sent
    }
    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.client_waiting = true;
    self.scope = Impl::Scope::All;
    self.restore_interface = kHotspotInterface;
    self.delete_profiles_and_restore();
}

void NetworkManagerClient::stop_vpn() {
    Impl& self = *impl_;
    if (busy() || self.closing) {
        return;
    }
    self.status = model::ModeSwitch{};
    self.status.target = self.hotspot_mode();
    if (self.client == nullptr || profiles_where(self.client, is_vpn_profile).empty()) {
        return;  // no tunnel: nothing is sent
    }
    self.switch_started = Clock::now();
    self.status.state = model::SwitchState::Switching;
    self.client_waiting = true;
    self.scope = Impl::Scope::Vpn;
    self.delete_profiles_and_restore();
}

// Deletes the profiles of LokiCTL's that `scope` names (all, or only the
// hotspot's or the tunnel's); once all deletes are answered,
// on_profile_deleted() activates the Wi-Fi profile a hotspot profile
// recorded.
void NetworkManagerClient::Impl::delete_profiles_and_restore() {
    if (client == nullptr || closing) {
        return;
    }
    const std::vector<NMRemoteConnection*> profiles = scope == Scope::Hotspot  ? hotspot_profiles(client)
                                                      : scope == Scope::Vpn    ? profiles_where(client, is_vpn_profile)
                                                      : scope == Scope::Laptop ? profiles_where(client, is_laptop_profile)
                                                                               : lokictl_profiles(client);
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

// A leftover VPN profile deleted before the tunnel's is added: once the
// last of these deletes is answered, the new profile is added. A failed
// delete shows up when the new profile is added or activated.
void NetworkManagerClient::Impl::on_vpn_leftover_deleted(GObject* source, GAsyncResult* result,
                                                         gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(source), result, &error);
    --self->pending;
    const bool cancelled = error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    if (--self->vpn_deletes_pending > 0) {
        return;
    }
    if (cancelled) {
        self->add_pending = false;
        self->drop_vpn_waiting();
        return;
    }
    if (!self->vpn_given_up()) {
        self->add_vpn_waiting();
    }
}

// The tunnel's profile has been added: it is activated with no device
// named, so the service makes kVpnInterface (or takes the one it still
// holds) itself.
void NetworkManagerClient::Impl::on_vpn_added(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    NMRemoteConnection* profile = nm_client_add_connection2_finish(NM_CLIENT(source), result, nullptr, &error);
    --self->pending;
    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        self->add_pending = false;
        g_clear_error(&error);
        if (profile != nullptr) {
            g_object_unref(profile);
        }
        return;
    }
    if (profile == nullptr) {
        self->vpn_refused(error);
        g_clear_error(&error);
        return;
    }
    g_clear_error(&error);
    if (!self->vpn_given_up()) {
        g_main_context_push_thread_default(self->context);
        ++self->pending;
        nm_client_activate_connection_async(self->client, NM_CONNECTION(profile), nullptr, nullptr,
                                            self->action_cancellable, &Impl::on_vpn_activated, self);
        g_main_context_pop_thread_default(self->context);
    }
    g_object_unref(profile);
}

// The tunnel's activation has been answered: check_progress() ends the
// switch once it is active.
void NetworkManagerClient::Impl::on_vpn_activated(GObject* source, GAsyncResult* result, gpointer user_data) {
    auto* self = static_cast<Impl*>(user_data);
    GError* error = nullptr;
    NMActiveConnection* active = nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
    --self->pending;
    self->add_pending = false;
    if (error != nullptr && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        if (active != nullptr) {
            g_object_unref(active);
        }
        g_clear_error(&error);
        return;
    }
    if (active == nullptr) {
        self->vpn_refused(error);
        g_clear_error(&error);
        return;
    }
    g_clear_error(&error);
    if (self->vpn_given_up()) {
        g_object_unref(active);
        return;
    }
    self->release_activation();
    self->activation = active;
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
            fail(model::SwitchError::Failed, status.target == model::NetMode::Vpn
                                                 ? std::string("tunnel stopped")
                                                 : reason_of(nm_active_connection_get_state_reason(activation)));
            release_activation();
            delete_profiles_and_restore();
        }
    }
    if (router_waiting && Clock::now() - switch_started >= NetworkManagerClient::kSwitchLimit) {
        router_waiting = false;
        fail(model::SwitchError::Failed, status.target == model::NetMode::Vpn      ? "no tunnel after 20 s"
                                         : status.target == model::NetMode::Laptop ? "no laptop link after 20 s"
                                                                                   : "no hotspot after 20 s");
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
    if (self.client == nullptr || (!self.add_pending && lokictl_profiles(self.client).empty())) {
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
    self.scope = Impl::Scope::All;
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

void NetworkManagerClient::start_vpn(const VpnRequest& /*request*/) {}

void NetworkManagerClient::start_laptop() {}

void NetworkManagerClient::start_client() {}

void NetworkManagerClient::stop_vpn() {}

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

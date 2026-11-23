/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The Discover page, opened with the home page's key 5. Its step 1,
// Listen, lists the devices heard on the Cardputer's network (the uplink
// interface) from ARP, DHCP, mDNS and SSDP while sending nothing
// (model::ListenRun, in tools().listen). Its step 2 shows the Cardputer's
// address and router and what a probe would ask; step 3, Probe, asks every
// address of that private network (at most its /24) and the devices for
// their kinds and names (model::ProbeRun, in tools().probe), and sends.
// The list shows both runs' devices together (model::discover_devices()).
// Under the title bar:
// - the summary row: the interface, how many devices were found, and the
//   step ("step 1 of 3: listen", "step 2 of 3: your address" while a
//   probe waits for its confirmation, "step 3 of 3: probe" once one ran);
// - two lines per device, as many as fit: its name (the DHCP or mDNS host
//   name, "router" for the uplink's router when it gave none, "unnamed" in
//   the muted colour, or "this device" with the port in the accent colour
//   for the Cardputer's own ports) with its IPv4 address,
//   then its hardware address, its kind when known ("printer", from mDNS or
//   SSDP) and how long ago it was last heard; a thin
//   scroll mark at the right when not all fit. In place of the list, a note
//   says why there is none (no network, not listened yet, nothing heard);
//   while a probe waits for its confirmation, step 2 stands there instead:
//   the address, the router, what will be asked, and the confirmation
//   ("Only networks you own or may test. Press 8 again to probe.");
// - the status line: "Listening, 4:12 left", "Probing 120 of 253", how the
//   last run ended, what went wrong, or how saving goes ("Saving, 4:12
//   left, 14 KB", "Saved 20261123-075130.pcap", "Save failed: disk full"),
//   with the app-wide Sent count at its right (it does not move while
//   listening).
//
// Keys: 4 (Back) or a short ESC goes back (a run goes on); 6 (Start /
// Stop) starts a run of model::kListenSeconds on the uplink interface or
// stops it (switched off without a network and while another tool runs);
// 7 (Save) saves the latest run as a PCAP file (on while one runs, and
// after one that heard something, until it is saved; off while another
// tool runs); 8 (Probe / Stop) shows step 2, and a second 8 starts a probe
// on the uplink interface, or stops a probe (switched off without a network
// and while another tool runs; any other key, a short ESC, leaving the page
// or kConfirmMs cancels the confirmation, which nothing remembers); up and
// down (F/X) scroll the devices.
//
// The first device shown lives in the view model (discover_scroll_*), so it
// is kept across a theme switch.
class DiscoverScreen : public BaseScreen {
public:
    DiscoverScreen(viewmodel::BaseViewModel& view_model,
                   app::AssetManager& assets,
                   view::ThemeEngine& theme_engine);

    // Deletes the confirmation's timer.
    ~DiscoverScreen() override;

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    bool on_back() override;
    view::widgets::HelpContent help() const override;

private:
    // The page shows at most this many devices (fewer when the theme's
    // lines are taller).
    static constexpr std::size_t kEntries = 3;
    // A confirmation not given in this long is cancelled.
    static constexpr uint32_t kConfirmMs = 10000;

    struct Entry {
        lv_obj_t* name{nullptr};
        lv_obj_t* address{nullptr};
        lv_obj_t* hardware{nullptr};
        lv_obj_t* kind{nullptr};
        lv_obj_t* age{nullptr};
        std::string name_shown;
        std::string address_shown;
        std::string hardware_shown;
        std::string kind_shown;
        std::string age_shown;
        int32_t name_tone{-1};
        int32_t address_tone{-1};
        bool hidden{true};  // the entry's objects are hidden
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    // The interface a new run would listen on: the uplink, when it has a
    // name the helper takes; empty otherwise.
    std::string uplink() const;
    bool listening() const;
    // True when key 6 does something now: stop this run, or start one.
    bool can_listen() const;
    void listen_or_stop();
    // True when key 7 does something now: save the latest run.
    bool can_save() const;
    void save();
    bool probing() const;
    // True when key 8 does something now: stop this probe, or show step 2
    // (and then start a probe).
    bool can_probe() const;
    void probe_or_stop();
    // The uplink's IPv4 address when it is on a private network, which a
    // probe may ask; empty otherwise.
    std::string probe_address() const;
    // Cancels the confirmation, if one waits.
    void cancel_confirm();
    static void confirm_timer_cb(lv_timer_t* timer);
    void scroll(int32_t delta);
    // The first device shown, from the view model's scroll, clamped.
    int32_t first_entry() const;

    void refresh();
    void refresh_entries();
    void refresh_status();
    void set_entry_hidden(Entry& entry, bool hidden);

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* interface_caption_{nullptr};
    lv_obj_t* count_{nullptr};
    lv_obj_t* step_caption_{nullptr};
    std::array<Entry, kEntries> entries_{};
    int32_t visible_entries_{0};
    int32_t list_top_{0};
    int32_t list_height_{0};
    int32_t entry_height_{0};
    lv_obj_t* note_{nullptr};
    lv_obj_t* confirm_{nullptr};
    lv_obj_t* track_{nullptr};
    lv_obj_t* thumb_{nullptr};
    lv_obj_t* status_{nullptr};
    lv_obj_t* sent_caption_{nullptr};
    lv_obj_t* sent_{nullptr};

    std::string interface_shown_;
    std::string count_shown_;
    std::string note_shown_;
    int32_t note_tone_{-1};
    std::string confirm_shown_;
    std::string step_shown_;
    std::string status_shown_;
    int32_t status_tone_{-1};
    std::string sent_shown_;
    int32_t thumb_top_shown_{-1};
    int32_t thumb_height_shown_{-1};
    int32_t shown_key_{-1};  // what key 6 shows: 0 off, 1 Start, 2 Stop; -1 before the first
    int32_t shown_save_key_{-1};  // key 7: 0 off, 1 on; -1 before the first
    int32_t shown_probe_key_{-1};  // key 8: 0 off, 1 Probe, 2 Stop; -1 before the first
    // The list shown: both runs' devices, as of the last refresh().
    std::vector<model::DiscoverDevice> devices_;
    // A probe waits for its confirmation (the second 8); the timer cancels it.
    bool confirming_{false};
    lv_timer_t* confirm_timer_{nullptr};
};

} // namespace screen

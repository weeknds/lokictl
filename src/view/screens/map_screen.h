/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"
#include "map_parts.h"
#include "tool_data.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace screen {

// The Map page, opened with the Discover page's key 5: the network as a
// map. It has no title bar: the whole 320x140 area above the soft-key bar
// belongs to the page, as on the home page. The same layout serves every
// theme, coloured and set in type from the theme.
// - The map (x 0-187): the internet at the top; under it the router, joined
//   by a thin muted line; under that this device, joined to the router by
//   the uplink line in the accent colour. The devices Discover heard or
//   probed (model::discover_devices()) sit in six fixed slots around the
//   router, each joined to it by a thin muted line; those past six are
//   counted as "+N" at the bottom left. While the hotspot runs (Router or
//   Share), its clients sit in three fixed slots below this device, joined
//   to it by thin lines in the second accent colour. A device without a
//   name, or not heard for kQuietSeconds, is muted. The map writes no
//   names; brackets mark the selected node. Without a network only the
//   internet and a muted this device are left, with "Join a network
//   first" at the bottom; with a network and no devices yet the bottom
//   says "Press 6 to listen" (or "Listening…", "No devices heard").
// - The panel (x 188-319, behind a 1 px line): clock and battery, the
//   interface, the number of devices and the state of listening, and under
//   a rule the selected node: its name, kind, IPv4 address, hardware
//   address and when it was last heard (a client: its signal; this device:
//   its Wi-Fi signal or link speed).
// The Cardputer's own ports (HeardDevice::own_port) fold into this device
// and the entry with the router's address into the router; neither takes
// a slot.
//
// A device keeps its slot (by hardware address) while the page is open,
// also while it is not listed for a moment; a new one takes the first empty
// slot, and only when none is left a slot whose device is not listed now. The slots and the selection live in the view model
// (map_slots(), map_selection_*), so a theme switch keeps both; opening the
// page from Discover starts with empty slots and the router selected.
//
// Keys: 4 (Back) or a short ESC goes back (a run goes on); 6 (Start /
// Stop) starts the same passive listen run as the Discover page's key 6 or
// stops it (off without a network and while another tool runs); the arrows
// and F/X/Z/C step through the router, the network devices, this device and
// the clients, without wrapping; Enter on a client opens the Devices page
// on it. Opening the page sends nothing.
//
// Values change in place when the tools or the home values move; the page
// is built again only when the set of nodes changes (the network comes or
// goes, a slot fills or empties, the number of clients changes).
class MapScreen : public BaseScreen {
public:
    MapScreen(viewmodel::BaseViewModel& view_model,
              app::AssetManager& assets,
              view::ThemeEngine& theme_engine);

    static bool handles(model::AppPage page);
    static view::widgets::HelpContent help_content();

    bool on_key(uint32_t key) override;
    view::widgets::HelpContent help() const override;

    // A device not heard for this long is drawn muted.
    static constexpr int32_t kQuietSeconds = 120;

private:
    static constexpr std::size_t kSlotCount = static_cast<std::size_t>(viewmodel::kMapSlots);
    static constexpr std::size_t kClientCount = static_cast<std::size_t>(viewmodel::kHomeClientRows);

    // One label update() keeps up to date, with the text and the tone it
    // shows.
    struct Text {
        lv_obj_t* label{nullptr};
        std::string shown;
        int32_t tone{-1};
    };

    // What one node of the map stands for, as the panel describes it.
    struct NodeInfo {
        std::string name;
        int32_t name_tone{0};
        std::string kind;
        std::string address;
        int32_t address_tone{0};
        std::string hardware;
        std::string last;
    };

    // What the map shows, worked out from the view model.
    struct Nodes {
        bool linked{false};
        bool has_router_entry{false};
        model::DiscoverDevice router;   // the entry with the router's address
        bool has_own_entry{false};
        model::DiscoverDevice own;      // the Cardputer's own port (the uplink's when listed)
        std::array<int32_t, kSlotCount> slot_device{};  // index into network, -1 for a free slot
        std::vector<model::DiscoverDevice> network;  // every other device, in Discover's order
        int32_t overflow{0};            // network devices without a slot
        int32_t clients{0};             // hotspot clients shown, at most kClientCount
    };

    void build_content(lv_obj_t* content) override;
    view::widgets::SoftKeySet soft_keys() override;

    Nodes nodes();
    // The node codes on the map now, in stepping order (see kMapRouter).
    std::vector<int32_t> present(const Nodes& nodes) const;
    // The selected node as shown: the stored one when it is on the map,
    // else the next one, else the last.
    int32_t shown_selection(const Nodes& nodes) const;
    void step(int32_t delta);
    NodeInfo describe(const Nodes& nodes, int32_t node) const;

    bool listening() const;
    bool can_listen() const;
    void listen_or_stop();
    std::string uplink() const;

    void refresh();
    void build(const Nodes& nodes);
    void build_map(const Nodes& nodes);
    void build_panel();
    void update(const Nodes& nodes);
    void set_text(Text& text, const std::string& value, int32_t tone);
    // The same for a caption, which the theme may write in capitals.
    // Returns true when the text changed.
    bool set_caption(Text& text, const std::string& value, int32_t tone);

    static void observer_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* content_{nullptr};

    // The set of nodes the page was built for: rebuilt when it changes.
    bool built_{false};
    bool built_linked_{false};
    uint32_t built_slots_{0};   // one bit per filled slot
    int32_t built_clients_{0};

    // The map's parts update() changes.
    std::array<lv_obj_t*, kSlotCount> slot_dots_{};
    std::array<int32_t, kSlotCount> slot_tones_{};
    std::array<lv_obj_t*, kClientCount> client_dots_{};
    std::array<int32_t, kClientCount> client_tones_{};
    view::widgets::MapBrackets brackets_;
    int32_t brackets_shown_{-1};
    Text internet_;
    Text overflow_;
    Text note_;

    // The panel.
    Text clock_;
    Text battery_;
    bool battery_low_shown_{false};
    Text interface_;
    Text count_;
    Text listen_;
    Text name_;
    Text kind_;
    Text address_;
    Text hardware_;
    Text last_;

    int32_t shown_key_{-1};  // what key 6 shows: 0 off, 1 Listen, 2 Stop; -1 before the first
};

} // namespace screen

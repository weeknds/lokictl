/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "map_screen.h"

#include "home_format.h"
#include "home_layout.h"
#include "style.h"
#include "theme.h"

#include <algorithm>
#include <cstddef>

namespace screen {
namespace {

namespace parts = view::widgets;

using viewmodel::kMapDevice;
using viewmodel::kMapFirstClient;
using viewmodel::kMapFirstSlot;
using viewmodel::kMapRoute;
using viewmodel::kMapRouter;
using viewmodel::kMapSwitch;

// --- The map (x 0-187). The chain runs down the point between pixels 93
// and 94; even-sized shapes are centred on it.
constexpr int32_t kMapWidth = 188;

// The internet: a square at the top with its caption beside it.
constexpr int32_t kInternetX      = 89;
constexpr int32_t kInternetY      = 4;
constexpr int32_t kInternetSize   = 10;
constexpr int32_t kInternetLabelX = 105;
constexpr int32_t kInternetLabelY = 1;
// The caption's room, up to 2 px before the panel's line.
constexpr int32_t kInternetLabelRoom = kMapWidth - 2 - kInternetLabelX;

// A 1 px muted line from the internet down to the router (pixel 94).
constexpr int32_t kRouterLinkX = 94;

// The router: a ring.
constexpr int32_t kRouterX    = 88;
constexpr int32_t kRouterY    = 32;
constexpr int32_t kRouterSize = 12;

// The uplink: a 2 px line from the router down to this device (pixels
// 93-94).
constexpr int32_t kUplinkX     = 93;
constexpr int32_t kUplinkWidth = 2;

// This device: a ring with a dot in it.
constexpr int32_t kDeviceX       = 87;
constexpr int32_t kDeviceY       = 70;
constexpr int32_t kDeviceSize    = 14;
constexpr int32_t kDeviceBorder  = 2;
constexpr int32_t kDeviceDotSize = 6;

// The route's hops: up to three small dots on the internet's line, centred
// on the chain; their tops by how many are drawn.
constexpr std::size_t kHops = MapScreen::kRouteDots;
constexpr int32_t kHopX    = 92;
constexpr int32_t kHopSize = 4;
constexpr int32_t kHopTops[kHops][kHops] = {{21, 0, 0}, {18, 24, 0}, {15, 21, 27}};

// The switch: a box on the uplink between the router and this device.
constexpr int32_t kSwitchX      = 87;
constexpr int32_t kSwitchY      = 53;
constexpr int32_t kSwitchWidth  = 14;
constexpr int32_t kSwitchHeight = 8;

// Where the lines to the network devices and to the clients start; the
// router's and the device's rings cover these points.
constexpr int32_t kRouterCentreX = 94;
constexpr int32_t kRouterCentreY = 38;
constexpr int32_t kDeviceCentreX = 94;
constexpr int32_t kDeviceCentreY = 77;

struct Point {
    int32_t x;
    int32_t y;
};

// The network devices' slots, as the centres of their dots, in stepping
// order: clockwise round the router, from its lower left to its lower
// right. New devices take the free slots in kFillOrder: left and right of
// the router first, then above, then below, so a few devices stay
// balanced.
constexpr std::size_t kSlots = 6;
constexpr Point kSlotPoints[kSlots] = {{48, 60}, {32, 42}, {48, 24}, {139, 24}, {155, 42}, {139, 60}};
constexpr std::size_t kFillOrder[kSlots] = {1, 4, 2, 3, 0, 5};
constexpr int32_t kDotSize = 7;

// The clients' slots below this device, left to right; with fewer than
// three clients, kClientUse says which are used (as on the home page).
constexpr std::size_t kClients = 3;
constexpr Point kClientPoints[kClients] = {{54, 102}, {94, 110}, {134, 102}};
constexpr std::size_t kClientUse[kClients + 1][kClients] = {
    {0, 0, 0},
    {1, 0, 0},
    {0, 2, 0},
    {0, 1, 2},
};

// Dim dots, decoration only, clear of every slot, line and label
// (Constellation only, see build_map()).
constexpr Point kStars[] = {{16, 8}, {12, 88}, {176, 90}};

// The brackets: around a dot, the router, this device, the internet and
// the switch.
constexpr int32_t kDotBracketHeight      = 16;
constexpr int32_t kRouterBracketHeight   = 18;
constexpr int32_t kDeviceBracketHeight   = 20;
constexpr int32_t kInternetBracketHeight = 16;
constexpr int32_t kSwitchBracketHeight   = 14;

// The bottom row: "+N" at the left, or a note centred under the chain.
constexpr int32_t kBottomY    = 122;
constexpr int32_t kOverflowX  = 4;
constexpr int32_t kNoteX      = 4;
constexpr int32_t kNoteWidth  = 180;

static_assert(kInternetX + kInternetSize / 2 == kRouterLinkX && kRouterX + kRouterSize / 2 == kRouterLinkX &&
                  kDeviceX + kDeviceSize / 2 == kRouterLinkX && kUplinkX + kUplinkWidth / 2 == kRouterLinkX,
              "the chain's parts are centred on one line");
static_assert(kRouterY + kRouterSize < kDeviceY, "the uplink runs between the router and the device");
static_assert(kHopX + kHopSize / 2 == kRouterLinkX && kSwitchX + kSwitchWidth / 2 == kRouterLinkX,
              "the hops and the switch sit on the chain");
static_assert(kInternetY + kInternetSize < kHopTops[kHops - 1][0] &&
                  kHopTops[kHops - 1][kHops - 1] + kHopSize < kRouterY,
              "the hops sit between the internet and the router");
static_assert(kRouterY + kRouterSize < kSwitchY && kSwitchY + kSwitchHeight < kDeviceY,
              "the switch sits between the router and the device");
static_assert(kNoteX + kNoteWidth < kMapWidth, "the note stays on the map");
static_assert(kSlots == static_cast<std::size_t>(viewmodel::kMapSlots), "one slot per view model slot");
static_assert(kClients == static_cast<std::size_t>(viewmodel::kHomeClientRows), "one slot per client row");

// lv_line keeps a pointer to its points instead of copying them, so every
// line's points live here for the whole run of the app.
static const lv_point_precise_t kSlotLinks[kSlots][2] = {
    {{kRouterCentreX, kRouterCentreY}, {kSlotPoints[0].x, kSlotPoints[0].y}},
    {{kRouterCentreX, kRouterCentreY}, {kSlotPoints[1].x, kSlotPoints[1].y}},
    {{kRouterCentreX, kRouterCentreY}, {kSlotPoints[2].x, kSlotPoints[2].y}},
    {{kRouterCentreX, kRouterCentreY}, {kSlotPoints[3].x, kSlotPoints[3].y}},
    {{kRouterCentreX, kRouterCentreY}, {kSlotPoints[4].x, kSlotPoints[4].y}},
    {{kRouterCentreX, kRouterCentreY}, {kSlotPoints[5].x, kSlotPoints[5].y}},
};
static const lv_point_precise_t kClientLinks[kClients][2] = {
    {{kDeviceCentreX, kDeviceCentreY}, {kClientPoints[0].x, kClientPoints[0].y}},
    {{kDeviceCentreX, kDeviceCentreY}, {kClientPoints[1].x, kClientPoints[1].y}},
    {{kDeviceCentreX, kDeviceCentreY}, {kClientPoints[2].x, kClientPoints[2].y}},
};

// --- The panel (x 188-319, behind a 1 px line). Its text runs from x 192
// to 317, wide enough for a hardware address in the Small font of every
// theme (17 characters).
constexpr int32_t kPanelX       = kMapWidth;
constexpr int32_t kPanelWidth   = view::kScreenWidth - kPanelX;
constexpr int32_t kPanelBorder  = 1;
constexpr int32_t kTextX        = 192;
constexpr int32_t kTextEnd      = 317;
constexpr int32_t kTextWidth    = kTextEnd - kTextX;
constexpr int32_t kBatteryWidth = 35;  // right-aligned, room for "100%"
constexpr int32_t kStatusY      = 1;
constexpr int32_t kCaptionY     = 15;
constexpr int32_t kCountY       = 27;
constexpr int32_t kListenY      = 41;
constexpr int32_t kRuleY        = 57;
constexpr int32_t kNameY        = 61;
constexpr int32_t kKindY        = 77;
constexpr int32_t kAddressY     = 91;
constexpr int32_t kHardwareY    = 105;
constexpr int32_t kLastY        = 119;

constexpr int32_t kContentHeight = view::kScreenHeight - view::kSoftKeyBarHeight;

// The colours a text of this page can take.
enum Tone : int32_t {
    kToneText = 0,
    kToneMuted,
    kToneAccent,
    kToneAccent2,
    kToneWarning,
    kToneError,
};

lv_color_t tone_color(const view::Theme& theme, int32_t tone) {
    switch (tone) {
        case kToneMuted:
            return theme.colors.muted;
        case kToneAccent:
            return theme.colors.accent;
        case kToneAccent2:
            return theme.colors.accent2;
        case kToneWarning:
            return theme.colors.warning;
        case kToneError:
            return theme.colors.error;
        default:
            return theme.colors.text;
    }
}

lv_obj_t* plain_object(lv_obj_t* parent) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* plain_label(lv_obj_t* parent, const char* text) {
    auto* label = lv_label_create(parent);
    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text);
    return label;
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// "4:12": minutes and seconds.
std::string minutes_text(int32_t seconds) {
    seconds = std::max<int32_t>(seconds, 0);
    const int32_t rest = seconds % 60;
    return std::to_string(seconds / 60) + (rest < 10 ? ":0" : ":") + std::to_string(rest);
}

// When a device was last heard: "heard now", "heard 12 s ago", "heard 3 min
// ago".
std::string heard_text(int32_t seconds) {
    if (seconds < 2) {
        return "heard now";
    }
    if (seconds < 60) {
        return "heard " + std::to_string(seconds) + " s ago";
    }
    return "heard " + std::to_string(seconds / 60) + " min ago";
}

std::string or_unknown(const std::string& text) {
    return text.empty() ? std::string("--") : text;
}

// Whether `text` is an IPv4 address out on the internet: not a private,
// link-local, loopback or 0.x address.
bool internet_address(const std::string& text) {
    if (!model::is_ipv4_text(text) || model::port_scan_address(text)) {
        return false;
    }
    return text.rfind("127.", 0) != 0 && text.rfind("0.", 0) != 0;
}

// "14 ms", "0.9 ms": a hop's round trip.
std::string hop_time_text(int32_t time_us) {
    if (time_us < 10000) {
        const int32_t tenths = (time_us + 50) / 100;
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " ms";
    }
    return std::to_string((time_us + 500) / 1000) + " ms";
}

} // namespace

MapScreen::MapScreen(viewmodel::BaseViewModel& view_model,
                     app::AssetManager& assets,
                     view::ThemeEngine& theme_engine)
    : BaseScreen(view_model, assets, theme_engine) {
    init(Layout::NoTitleBar);
}

MapScreen::~MapScreen() {
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
        confirm_timer_ = nullptr;
    }
}

bool MapScreen::handles(model::AppPage page) {
    return page == model::AppPage::Map;
}

view::widgets::HelpContent MapScreen::help_content() {
    return {"MAP",
            {
                {"ESC / 4", "Back (listening goes on)"},
                {"F Z / X C", "Step through the map"},
                {"6", "Listen 5 min, sends none"},
                {"8 twice", "Route to internet, sends"},
                {"ENTER", "Devices, or every hop"},
            }};
}

view::widgets::HelpContent MapScreen::help() const {
    return help_content();
}

view::widgets::SoftKeySet MapScreen::soft_keys() {
    // The screen owns its soft-key bar, so the keys never outlive it.
    // refresh() switches Start and Route on and off and turns them into
    // Stop.
    auto& view_model = this->view_model();
    return {{
        {"Back", [&view_model] { view_model.go_back(); }},
        {},
        {"Start", [this] { listen_or_stop(); }, true},
        {},
        {"Route", [this] { route_or_stop(); }, false, false},
    }};
}

std::string MapScreen::uplink() const {
    const std::string& name = view_model().home().uplink_name;
    return model::valid_interface_name(name) ? name : std::string();
}

bool MapScreen::listening() const {
    return view_model().tools().listen.state == model::ToolState::Running;
}

bool MapScreen::can_listen() const {
    if (listening()) {
        return true;
    }
    return !model::tool_running(view_model().tools()) && !uplink().empty();
}

void MapScreen::listen_or_stop() {
    if (confirming_) {
        cancel_confirm();  // like any other key: only cancels
        return;
    }
    if (listening()) {
        view_model().request_tool_stop();
    } else if (can_listen()) {
        view_model().set_discover_scroll(0);
        view_model().request_tool_run(model::ToolKind::Listen, uplink());
    }
}

bool MapScreen::tracing() const {
    return view_model().tools().trace.state == model::ToolState::Running;
}

bool MapScreen::can_route() const {
    if (tracing()) {
        return true;
    }
    return !model::tool_running(view_model().tools()) && !uplink().empty() &&
           view_model().home().uplink_kind != model::LinkKind::None;
}

void MapScreen::route_or_stop() {
    if (tracing()) {
        cancel_confirm();
        view_model().request_tool_stop();
        return;
    }
    if (!can_route()) {
        return;
    }
    if (!confirming_) {
        // The first 8: the question; nothing is sent.
        confirming_ = true;
        if (!confirm_timer_) {
            confirm_timer_ = lv_timer_create(confirm_timer_cb, kConfirmMs, this);
        }
        view_model().set_map_selection(kMapRoute);
        refresh();
        return;
    }
    // The second 8: the confirmation, for this trace only. The Trace page
    // then shows the same target (its presets follow the router).
    cancel_confirm();
    const bool router = model::is_ipv4_text(view_model().home().gateway_address);
    view_model().set_tool_target(model::ToolKind::Trace, router ? 1 : 0);
    view_model().set_trace_scroll(viewmodel::BaseViewModel::kTraceFollow);
    view_model().request_tool_run(model::ToolKind::Trace, kRouteTarget);
}

void MapScreen::cancel_confirm() {
    if (confirm_timer_) {
        lv_timer_delete(confirm_timer_);
        confirm_timer_ = nullptr;
    }
    if (confirming_) {
        confirming_ = false;
        refresh();
    }
}

void MapScreen::confirm_timer_cb(lv_timer_t* timer) {
    auto* screen = static_cast<MapScreen*>(lv_timer_get_user_data(timer));
    if (screen) {
        screen->cancel_confirm();
    }
}

std::string MapScreen::fitting(const std::vector<std::string>& candidates, view::FontRole role) const {
    const lv_font_t* font = theme().font(role);
    for (const std::string& text : candidates) {
        lv_point_t size{};
        lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= kTextWidth) {
            return text;
        }
    }
    return candidates.empty() ? std::string() : candidates.back();
}

MapScreen::Nodes MapScreen::nodes() {
    const model::HomeData& home = view_model().home();
    const model::ToolsData& tools = view_model().tools();
    Nodes result;
    result.slot_device.fill(-1);
    result.linked = home.uplink_kind != model::LinkKind::None;
    if (home.ap_active) {
        result.clients = static_cast<int32_t>(std::min(home.clients.size(), kClientCount));
    }

    // Without a network the devices of an earlier run are not shown.
    if (result.linked) {
        for (const model::DiscoverDevice& entry : model::discover_devices(tools.listen, tools.probe)) {
            const model::HeardDevice& device = entry.device;
            if (!device.own_port.empty()) {
                // The uplink's own port, when it was heard, else the first.
                if (!result.has_own_entry || device.own_port == home.uplink_name) {
                    result.own = entry;
                    result.has_own_entry = true;
                }
                continue;
            }
            if (!device.address.empty() && device.address == home.gateway_address) {
                if (!result.has_router_entry) {
                    result.router = entry;
                    result.has_router_entry = true;
                }
                continue;
            }
            result.network.push_back(entry);
        }
    }

    // The switch or router next to the Cardputer, from the latest listen
    // run on the uplink. A switch's own entry folds into its box.
    const model::ListenRun& run = tools.listen;
    if (result.linked && !run.neighbours.empty() && run.interface_name == home.uplink_name) {
        result.has_neighbour = true;
        result.neighbour = run.neighbours.front();
        result.neighbour_age_s = std::max<int32_t>(run.elapsed_s - result.neighbour.heard_s, 0);
        const std::string router_hardware =
            result.has_router_entry ? result.router.device.hardware_address : std::string();
        result.neighbour_is_router =
            model::neighbour_is_router(result.neighbour, home.gateway_address, router_hardware);
        if (!result.neighbour_is_router) {
            const model::Neighbour& neighbour = result.neighbour;
            const auto same = std::find_if(result.network.begin(), result.network.end(),
                                           [&neighbour](const model::DiscoverDevice& entry) {
                                               const model::HeardDevice& device = entry.device;
                                               return (!neighbour.address.empty() && device.address == neighbour.address) ||
                                                      (!device.hardware_address.empty() &&
                                                       (device.hardware_address == neighbour.chassis ||
                                                        device.hardware_address == neighbour.hardware_address));
                                           });
            if (same != result.network.end()) {
                result.switch_entry = *same;
                result.has_switch_entry = true;
                result.network.erase(same);
            }
        }
    }

    // The route to the internet: the latest trace, when it went out to the
    // internet. The router and the target themselves are not hops between.
    const model::TraceRun& trace = tools.trace;
    if (result.linked && trace.state != model::ToolState::Idle &&
        (trace.address.empty() ? !model::is_ipv4_text(trace.target) : internet_address(trace.address))) {
        result.has_route = true;
        for (const model::TraceHop& hop : trace.hops) {
            if (hop.time_us == model::kPingWaiting || (!hop.address.empty() && hop.address == home.gateway_address) ||
                (!hop.address.empty() && hop.address == trace.address)) {
                continue;
            }
            result.route_hops.push_back(hop);
        }
        result.route_dots = static_cast<int32_t>(std::min(result.route_hops.size(), kRouteDots));
    }

    // A device keeps its slot while the page is open, also while it is not
    // listed (a new run starts with an empty list, the link drops for a
    // moment), so it comes back to the same place. A new device takes the
    // first empty slot of kFillOrder, and only when none is left one whose
    // device is not listed now.
    const auto& held = view_model().map_slots();
    std::array<std::string, kSlotCount> slots = held;
    int32_t placed = 0;
    std::vector<std::size_t> fresh;
    for (std::size_t index = 0; index < result.network.size(); ++index) {
        const std::string& address = result.network[index].device.hardware_address;
        if (address.empty()) {
            continue;  // no slot can be kept for it; counted in "+N"
        }
        const auto kept = std::find(slots.begin(), slots.end(), address);
        if (kept == slots.end()) {
            fresh.push_back(index);
            continue;
        }
        result.slot_device[static_cast<std::size_t>(kept - slots.begin())] = static_cast<int32_t>(index);
        ++placed;
    }
    for (const std::size_t index : fresh) {
        std::size_t chosen = kSlotCount;
        for (const std::size_t slot : kFillOrder) {
            if (slots[slot].empty()) {
                chosen = slot;
                break;
            }
        }
        if (chosen == kSlotCount) {
            for (const std::size_t slot : kFillOrder) {
                if (result.slot_device[slot] < 0) {
                    chosen = slot;
                    break;
                }
            }
        }
        if (chosen == kSlotCount) {
            break;  // every slot shows a listed device; the rest count in "+N"
        }
        slots[chosen] = result.network[index].device.hardware_address;
        result.slot_device[chosen] = static_cast<int32_t>(index);
        ++placed;
    }
    result.overflow = static_cast<int32_t>(result.network.size()) - placed;
    if (slots != held) {
        view_model().set_map_slots(slots);
    }
    return result;
}

std::vector<int32_t> MapScreen::present(const Nodes& nodes) const {
    std::vector<int32_t> list;
    if (nodes.linked) {
        list.push_back(kMapRoute);
        list.push_back(kMapRouter);
    }
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (nodes.slot_device[slot] >= 0) {
            list.push_back(kMapFirstSlot + static_cast<int32_t>(slot));
        }
    }
    if (nodes.has_switch()) {
        list.push_back(kMapSwitch);
    }
    list.push_back(kMapDevice);
    for (int32_t client = 0; client < nodes.clients; ++client) {
        list.push_back(kMapFirstClient + client);
    }
    return list;
}

int32_t MapScreen::shown_selection(const Nodes& nodes) const {
    const int32_t stored = view_model().map_selection();
    const std::vector<int32_t> list = present(nodes);
    for (const int32_t node : list) {
        if (node >= stored) {
            return node;
        }
    }
    return list.back();
}

void MapScreen::step(int32_t delta) {
    const Nodes current = nodes();
    const std::vector<int32_t> list = present(current);
    const int32_t shown = shown_selection(current);
    const auto at = static_cast<int32_t>(std::find(list.begin(), list.end(), shown) - list.begin());
    const int32_t next = std::clamp<int32_t>(at + delta, 0, static_cast<int32_t>(list.size()) - 1);
    view_model().set_map_selection(list[static_cast<std::size_t>(next)]);
}

MapScreen::NodeInfo MapScreen::describe(const Nodes& nodes, int32_t node) const {
    const model::HomeData& home = view_model().home();
    NodeInfo info;
    info.address_tone = kToneAccent2;
    const std::string port = nodes.has_neighbour ? model::neighbour_port_text(nodes.neighbour) : std::string();
    // The networks (VLANs), as many as fit the panel.
    std::string vlan;
    for (std::size_t chars = 24; nodes.has_neighbour && chars >= 8; --chars) {
        vlan = model::neighbour_vlan_text(nodes.neighbour, chars);
        if (vlan.empty() || fitting({vlan, std::string()}) == vlan) {
            break;
        }
    }
    if (node == kMapRoute) {
        const model::TraceRun& trace = view_model().tools().trace;
        info.name = fitting({"Route to internet", "To internet"}, view::FontRole::Strong);
        info.address = kRouteTarget;
        if (!nodes.has_route) {
            info.kind = "not traced yet";
            info.hardware = "Press 8 twice";
            return info;
        }
        info.address = or_unknown(trace.address.empty() ? trace.target : trace.address);
        info.hardware = "Enter: every hop";
        const auto hops = static_cast<int32_t>(trace.hops.size());
        switch (trace.state) {
            case model::ToolState::Running:
                info.kind = "tracing, hop " + std::to_string(std::max<int32_t>(hops, 1));
                break;
            case model::ToolState::Failed:
                info.kind = "trace failed";
                break;
            default: {
                std::string kind = std::to_string(hops) + (hops == 1 ? " hop" : " hops");
                if (trace.end == model::TraceEnd::Reached && !trace.hops.empty() && trace.hops.back().time_us >= 0) {
                    kind += ", " + hop_time_text(trace.hops.back().time_us);
                } else if (trace.state == model::ToolState::Stopped) {
                    kind += ", stopped";
                } else if (trace.end != model::TraceEnd::Reached) {
                    kind += ", no answer";
                }
                info.kind = kind;
                break;
            }
        }
        return info;
    }
    if (node == kMapSwitch) {
        const model::Neighbour& neighbour = nodes.neighbour;
        info.name = neighbour.name.empty() ? std::string("switch") : neighbour.name;
        info.kind = port.empty() ? std::string("switch") : "switch, " + port;
        const std::string address = !neighbour.address.empty()     ? neighbour.address
                                    : nodes.has_switch_entry        ? nodes.switch_entry.device.address
                                                                    : std::string();
        info.address = or_unknown(address);
        // What it is, in as many whole words as fit ("8-port managed").
        info.hardware = "--";
        if (!neighbour.model.empty()) {
            std::vector<std::string> words;
            for (std::size_t end = neighbour.model.size(); end != std::string::npos && end > 0;
                 end = neighbour.model.rfind(' ', end - 1)) {
                words.push_back(neighbour.model.substr(0, end));
            }
            info.hardware = fitting(words);
        } else if (!neighbour.chassis.empty()) {
            info.hardware = neighbour.chassis;
        }
        info.last = vlan.empty() ? heard_text(nodes.neighbour_age_s) : vlan;
        return info;
    }
    if (node == kMapRouter) {
        info.name = "router";
        info.address = or_unknown(home.gateway_address);
        if (nodes.has_router_entry) {
            const model::HeardDevice& device = nodes.router.device;
            info.kind = or_unknown(device.kind);
            info.hardware = or_unknown(device.hardware_address);
            info.last = heard_text(nodes.router.age_s);
        } else {
            info.kind = "--";
            info.hardware = "--";
            info.last = "not heard yet";
        }
        if (nodes.has_neighbour && nodes.neighbour_is_router) {
            // The Cardputer is plugged straight into the router.
            if (!port.empty()) {
                info.kind = "router, " + port;
            }
            if (!vlan.empty()) {
                info.last = vlan;
            }
        }
        return info;
    }
    if (node == kMapDevice) {
        info.name = "this device";
        info.name_tone = kToneAccent;
        info.kind = home::link_text(home);
        info.address = or_unknown(home.uplink_address);
        info.hardware = nodes.has_own_entry ? or_unknown(nodes.own.device.hardware_address) : std::string("--");
        if (!nodes.linked) {
            info.last = "no network";
        } else if (home.uplink_kind == model::LinkKind::Wifi) {
            info.last = "signal " + home::signal_text(home.wifi_signal_dbm);
        } else {
            info.last = "link " + home::speed_text(home.uplink_speed_mbps);
        }
        if (nodes.has_neighbour) {
            // Which switch (or the router) and port it is plugged into:
            // "via office-sw port 5", or when that is too wide "via
            // office-sw" with the port in front of the link line.
            const std::string who = nodes.neighbour_is_router ? std::string("router")
                                    : nodes.neighbour.name.empty() ? std::string("switch")
                                                                   : nodes.neighbour.name;
            const std::string whole = port.empty() ? std::string() : "via " + who + " " + port;
            if (!whole.empty() && fitting({whole, std::string()}) == whole) {
                info.kind = whole;
            } else {
                info.kind = fitting({"via " + who, port.empty() ? std::string("via switch") : "via " + port});
                if (!port.empty() && nodes.linked) {
                    info.last = fitting({port + ", " + info.last, info.last});
                }
            }
        }
        return info;
    }
    if (node >= kMapFirstClient) {
        const auto index = static_cast<std::size_t>(node - kMapFirstClient);
        if (index >= home.clients.size()) {
            return info;
        }
        const model::HomeClient& client = home.clients[index];
        info.name = home::client_name_text(client);
        info.name_tone = client.name.empty() ? kToneMuted : kToneText;
        info.kind = "hotspot client";
        info.address = or_unknown(client.address);
        info.hardware = or_unknown(client.hardware_address);
        info.last = "signal " + home::signal_text(client.signal_dbm);
        return info;
    }
    const auto slot = static_cast<std::size_t>(node - kMapFirstSlot);
    if (slot >= kSlotCount || nodes.slot_device[slot] < 0) {
        return info;
    }
    const model::DiscoverDevice& entry = nodes.network[static_cast<std::size_t>(nodes.slot_device[slot])];
    const model::HeardDevice& device = entry.device;
    info.name = device.name.empty() ? std::string("unnamed") : device.name;
    info.name_tone = device.name.empty() ? kToneMuted : kToneText;
    info.kind = or_unknown(device.kind);
    info.address = device.address.empty() ? std::string("no address") : device.address;
    info.address_tone = device.address.empty() ? kToneMuted : kToneAccent2;
    info.hardware = or_unknown(device.hardware_address);
    info.last = heard_text(entry.age_s);
    return info;
}

bool MapScreen::on_back() {
    if (confirming_) {
        cancel_confirm();
        return true;
    }
    return false;
}

bool MapScreen::on_key(uint32_t key) {
    if (confirming_) {
        // Any other key cancels the question, and does nothing else.
        cancel_confirm();
        return true;
    }
    switch (key) {
        case LV_KEY_LEFT:
        case LV_KEY_UP:
            step(-1);
            return true;
        case LV_KEY_RIGHT:
        case LV_KEY_DOWN:
            step(1);
            return true;
        case LV_KEY_ENTER: {
            const Nodes current = nodes();
            const int32_t node = shown_selection(current);
            if (node == kMapRoute && current.has_route) {
                view_model().open_tool(model::ToolKind::Trace);
                return true;
            }
            if (node < kMapFirstClient) {
                return false;
            }
            view_model().set_devices_selection(node - kMapFirstClient);
            view_model().open_page(model::AppPage::Devices);
            return true;
        }
        default:
            return false;
    }
}

void MapScreen::build_content(lv_obj_t* content) {
    content_ = content;
    // LVGL calls each observer once right away; the first call builds the
    // page. The page is built again from refresh() when the set of nodes
    // changes: the observers are bound to the content area, which stays,
    // and only its children go.
    lv_subject_add_observer_obj(view_model().tools_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().home_data_subject(), observer_cb, content, this);
    lv_subject_add_observer_obj(view_model().map_selection_subject(), observer_cb, content, this);
}

void MapScreen::build(const Nodes& nodes) {
    lv_obj_clean(content_);
    slot_dots_ = {};
    slot_tones_.fill(-1);
    client_dots_ = {};
    client_tones_.fill(-1);
    route_dots_ = {};
    route_tones_.fill(-1);
    brackets_ = {};
    brackets_shown_ = -1;
    internet_ = Text{};
    overflow_ = Text{};
    note_ = Text{};
    clock_ = Text{};
    battery_ = Text{};
    battery_low_shown_ = false;
    interface_ = Text{};
    count_ = Text{};
    listen_ = Text{};
    name_ = Text{};
    kind_ = Text{};
    address_ = Text{};
    hardware_ = Text{};
    last_ = Text{};

    build_map(nodes);
    build_panel();

    built_ = true;
    built_linked_ = nodes.linked;
    built_slots_ = 0;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (nodes.slot_device[slot] >= 0) {
            built_slots_ |= 1u << slot;
        }
    }
    built_clients_ = nodes.clients;
    built_switch_ = nodes.has_switch();
    built_route_dots_ = nodes.route_dots;
}

void MapScreen::build_map(const Nodes& nodes) {
    const auto& theme = this->theme();
    const auto& colors = theme.colors;
    lv_obj_t* map = content_;
    const auto clients = static_cast<std::size_t>(nodes.clients);

    // Drawn back to front: stars, lines, shapes, brackets, then labels.
    // The stars only in Constellation: in the other themes their colour
    // (the strong border) is as strong as a device's dot, so they would
    // read as devices.
    if (theme.id == view::ThemeId::Constellation) {
        for (const auto& star : kStars) {
            parts::map_star(map, star.x, star.y, theme);
        }
    }

    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (nodes.slot_device[slot] >= 0) {
            parts::map_line(map, kSlotLinks[slot], 2, 1, colors.muted);
        }
    }
    for (std::size_t client = 0; client < clients; ++client) {
        parts::map_line(map, kClientLinks[kClientUse[clients][client]], 2, 1, colors.accent2);
    }
    if (nodes.linked) {
        parts::map_upright(map, kRouterLinkX, kInternetY + kInternetSize, kRouterY - 1, 1, colors.muted);
        parts::map_upright(map, kUplinkX, kRouterY + kRouterSize, kDeviceY - 1, kUplinkWidth, colors.accent);
    }

    parts::map_internet(map, kInternetX, kInternetY, kInternetSize, theme);
    if (nodes.linked) {
        parts::map_ring(map, kRouterX, kRouterY, kRouterSize, 1, colors.text, theme);
    }
    // The route's hops on the internet's line; update() colours them.
    const auto dots = static_cast<std::size_t>(nodes.route_dots);
    for (std::size_t dot = 0; dot < dots; ++dot) {
        route_dots_[dot] = parts::map_dot(map, kHopX, kHopTops[dots - 1][dot], kHopSize, colors.text);
        route_tones_[dot] = kToneText;
    }
    if (nodes.has_switch()) {
        parts::map_switch(map, kSwitchX, kSwitchY, kSwitchWidth, kSwitchHeight, theme);
    }
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (nodes.slot_device[slot] >= 0) {
            const Point& point = kSlotPoints[slot];
            slot_dots_[slot] = parts::map_dot(map, point.x - kDotSize / 2, point.y - kDotSize / 2, kDotSize, colors.text);
            slot_tones_[slot] = kToneText;
        }
    }
    // This device; muted without a network.
    parts::map_device(map, kDeviceX, kDeviceY, kDeviceSize, kDeviceBorder, kDeviceDotSize,
                      nodes.linked ? colors.accent : colors.muted, theme);
    for (std::size_t client = 0; client < clients; ++client) {
        const Point& point = kClientPoints[kClientUse[clients][client]];
        client_dots_[client] = parts::map_dot(map, point.x - kDotSize / 2, point.y - kDotSize / 2, kDotSize, colors.text);
        client_tones_[client] = kToneText;
    }

    brackets_ = parts::make_map_brackets(map, kDotBracketHeight, theme);

    // The labels; update() fills them in.
    internet_.label = plain_label(map, "");
    view::style::caption(internet_.label, theme, "");
    lv_obj_set_pos(internet_.label, kInternetLabelX, kInternetLabelY);

    overflow_.label = plain_label(map, "");
    view::style::text(overflow_.label, theme, view::FontRole::Small, colors.muted);
    lv_obj_set_pos(overflow_.label, kOverflowX, kBottomY);
    lv_obj_add_flag(overflow_.label, LV_OBJ_FLAG_HIDDEN);

    note_.label = plain_label(map, "");
    view::style::text(note_.label, theme, view::FontRole::Small, colors.muted);
    fix_width(note_.label, kNoteWidth, theme.font(view::FontRole::Small));
    lv_obj_set_style_text_align(note_.label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_.label, kNoteX, kBottomY);
    lv_obj_add_flag(note_.label, LV_OBJ_FLAG_HIDDEN);
}

void MapScreen::build_panel() {
    const auto& theme = this->theme();
    const auto& colors = theme.colors;
    const lv_font_t* small = theme.font(view::FontRole::Small);

    auto* panel = plain_object(content_);
    lv_obj_set_pos(panel, kPanelX, 0);
    lv_obj_set_size(panel, kPanelWidth, kContentHeight);
    lv_obj_set_style_border_color(panel, colors.border, 0);
    lv_obj_set_style_border_width(panel, kPanelBorder, 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT, 0);

    // LVGL places children from the inside of the left border.
    const int32_t left = kTextX - kPanelX - kPanelBorder;

    const auto row = [&](Text& text, view::FontRole role, int32_t width, int32_t y) {
        text.label = plain_label(panel, "");
        view::style::text(text.label, theme, role);
        fix_width(text.label, width, theme.font(role));
        lv_obj_set_pos(text.label, left, y);
    };

    row(clock_, view::FontRole::Small, kTextWidth - kBatteryWidth, kStatusY);
    battery_.label = plain_label(panel, "");
    view::style::text(battery_.label, theme, view::FontRole::Small, colors.muted);
    fix_width(battery_.label, kBatteryWidth, small);
    lv_obj_set_style_text_align(battery_.label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(battery_.label, left + kTextWidth - kBatteryWidth, kStatusY);

    interface_.label = plain_label(panel, "");
    view::style::caption(interface_.label, theme, "");
    fix_width(interface_.label, kTextWidth, small);
    lv_obj_set_pos(interface_.label, left, kCaptionY);

    row(count_, view::FontRole::Small, kTextWidth, kCountY);
    row(listen_, view::FontRole::Small, kTextWidth, kListenY);

    auto* rule = plain_object(panel);
    lv_obj_set_pos(rule, left, kRuleY);
    lv_obj_set_size(rule, kTextWidth, 1);
    lv_obj_set_style_bg_color(rule, colors.border, 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    row(name_, view::FontRole::Strong, kTextWidth, kNameY);
    row(kind_, view::FontRole::Small, kTextWidth, kKindY);
    row(address_, view::FontRole::Small, kTextWidth, kAddressY);
    row(hardware_, view::FontRole::Small, kTextWidth, kHardwareY);
    row(last_, view::FontRole::Small, kTextWidth, kLastY);
}

void MapScreen::set_text(Text& text, const std::string& value, int32_t tone) {
    if (!text.label) {
        return;
    }
    if (value != text.shown) {
        text.shown = value;
        lv_label_set_text(text.label, text.shown.c_str());
    }
    if (tone != text.tone) {
        text.tone = tone;
        lv_obj_set_style_text_color(text.label, tone_color(theme(), tone), 0);
    }
}

bool MapScreen::set_caption(Text& text, const std::string& value, int32_t tone) {
    if (!text.label) {
        return false;
    }
    bool changed = false;
    if (value != text.shown) {
        text.shown = value;
        // caption() sets the muted colour again, so the tone follows.
        view::style::caption(text.label, theme(), text.shown.c_str());
        text.tone = kToneMuted;
        changed = true;
    }
    if (tone != text.tone) {
        text.tone = tone;
        lv_obj_set_style_text_color(text.label, tone_color(theme(), tone), 0);
    }
    return changed;
}

void MapScreen::update(const Nodes& nodes) {
    const auto& theme = this->theme();
    const model::HomeData& home = view_model().home();
    const model::ToolsData& tools = view_model().tools();

    // The map: a device without a name or not heard for a while is muted.
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (!slot_dots_[slot] || nodes.slot_device[slot] < 0) {
            continue;
        }
        const model::DiscoverDevice& entry = nodes.network[static_cast<std::size_t>(nodes.slot_device[slot])];
        const int32_t tone =
            entry.device.name.empty() || entry.age_s >= kQuietSeconds ? kToneMuted : kToneText;
        if (tone != slot_tones_[slot]) {
            slot_tones_[slot] = tone;
            lv_obj_set_style_bg_color(slot_dots_[slot], tone_color(theme, tone), 0);
        }
    }
    // A hop that gave no answer is muted; with more hops than dots, the
    // dots stand for all of them.
    for (std::size_t dot = 0; dot < kRouteDots; ++dot) {
        if (!route_dots_[dot]) {
            continue;
        }
        const bool silent = nodes.route_hops.size() <= kRouteDots && dot < nodes.route_hops.size() &&
                            nodes.route_hops[dot].address.empty();
        const int32_t tone = silent ? kToneMuted : kToneText;
        if (tone != route_tones_[dot]) {
            route_tones_[dot] = tone;
            lv_obj_set_style_bg_color(route_dots_[dot], tone_color(theme, tone), 0);
        }
    }
    for (std::size_t client = 0; client < kClientCount; ++client) {
        if (!client_dots_[client] || client >= home.clients.size()) {
            continue;
        }
        const int32_t tone = home.clients[client].name.empty() ? kToneMuted : kToneText;
        if (tone != client_tones_[client]) {
            client_tones_[client] = tone;
            lv_obj_set_style_bg_color(client_dots_[client], tone_color(theme, tone), 0);
        }
    }

    // "internet", or the note when the internet can not be reached. A
    // note too wide for the room left of the panel with the theme's caption
    // spacing ("NO INTERNET" in Instrument) loses the spacing.
    const std::string internet = home::internet_note(home);
    if (set_caption(internet_, internet.empty() ? std::string("internet") : internet,
                    internet.empty() ? kToneMuted : kToneWarning)) {
        const int32_t spacing = theme.metrics.caption_letter_space;
        lv_point_t size{};
        lv_text_get_size(&size, lv_label_get_text(internet_.label), theme.font(view::FontRole::Small), spacing, 0,
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_set_style_text_letter_space(internet_.label, size.x <= kInternetLabelRoom ? spacing : 0, 0);
    }

    // The route question takes the whole bottom row.
    set_hidden(overflow_.label, nodes.overflow <= 0 || confirming_);
    if (nodes.overflow > 0) {
        set_text(overflow_, "+" + std::to_string(nodes.overflow), kToneMuted);
    }

    std::string note;
    int32_t note_tone = kToneMuted;
    if (confirming_) {
        note = "Press 8 again to trace";
        note_tone = kToneWarning;
    } else if (!nodes.linked) {
        note = "Join a network first";
    } else if (nodes.network.empty()) {
        if (listening()) {
            note = "Listening…";
        } else if (tools.listen.state == model::ToolState::Idle && tools.probe.state == model::ToolState::Idle) {
            note = "Press 6 to listen";
        } else {
            note = "No devices heard";
        }
    }
    set_hidden(note_.label, note.empty());
    if (!note.empty()) {
        set_text(note_, note, note_tone);
    }

    // The brackets around the selected node.
    const int32_t selected = shown_selection(nodes);
    if (selected != brackets_shown_) {
        brackets_shown_ = selected;
        if (selected == kMapRoute) {
            parts::show_map_brackets(brackets_, kInternetX, kInternetX + kInternetSize,
                                     kInternetY + (kInternetSize - kInternetBracketHeight) / 2,
                                     kInternetBracketHeight);
        } else if (selected == kMapRouter) {
            parts::show_map_brackets(brackets_, kRouterX, kRouterX + kRouterSize,
                                     kRouterY + (kRouterSize - kRouterBracketHeight) / 2, kRouterBracketHeight);
        } else if (selected == kMapSwitch) {
            parts::show_map_brackets(brackets_, kSwitchX, kSwitchX + kSwitchWidth,
                                     kSwitchY + (kSwitchHeight - kSwitchBracketHeight) / 2, kSwitchBracketHeight);
        } else if (selected == kMapDevice) {
            parts::show_map_brackets(brackets_, kDeviceX, kDeviceX + kDeviceSize,
                                     kDeviceY + (kDeviceSize - kDeviceBracketHeight) / 2, kDeviceBracketHeight);
        } else {
            const Point& point = selected >= kMapFirstClient
                                     ? kClientPoints[kClientUse[static_cast<std::size_t>(nodes.clients)]
                                                               [static_cast<std::size_t>(selected - kMapFirstClient)]]
                                     : kSlotPoints[static_cast<std::size_t>(selected - kMapFirstSlot)];
            const int32_t dot_left = point.x - kDotSize / 2;
            parts::show_map_brackets(brackets_, dot_left, dot_left + kDotSize, point.y - kDotBracketHeight / 2,
                                     kDotBracketHeight);
        }
    }

    // The panel.
    set_text(clock_, home::clock_text(home.clock_minutes), kToneMuted);
    set_text(battery_, home::battery_text(home.battery_percent, home.battery_state), kToneMuted);
    set_battery_color(battery_.label, battery_low_shown_, home, theme, theme.colors.muted);
    const std::string interface_name = uplink();
    set_caption(interface_, interface_name.empty() ? std::string("no network") : interface_name, kToneMuted);
    set_text(count_, nodes.linked ? "devices " + std::to_string(nodes.network.size()) : std::string(), kToneText);

    const model::ListenRun& run = tools.listen;
    std::string listen;
    int32_t listen_tone = kToneMuted;
    if (run.state == model::ToolState::Running) {
        listen = "listening " + minutes_text(model::kListenSeconds - run.elapsed_s);
        listen_tone = kToneAccent;
    } else if (model::tool_running(tools)) {
        listen = "another tool runs";
    } else if (run.state == model::ToolState::Done) {
        listen = "listened " + minutes_text(run.elapsed_s);
    } else if (run.state == model::ToolState::Stopped) {
        listen = "stopped at " + minutes_text(run.elapsed_s);
    } else if (run.state == model::ToolState::Failed) {
        listen = "listen failed";
        listen_tone = kToneError;
    } else if (nodes.linked) {
        listen = "not listened yet";
    }
    set_text(listen_, listen, listen_tone);

    const NodeInfo info = describe(nodes, selected);
    set_text(name_, info.name, info.name_tone);
    set_text(kind_, info.kind, kToneMuted);
    set_text(address_, info.address, info.address_tone);
    set_text(hardware_, info.hardware, kToneMuted);
    set_text(last_, info.last, kToneMuted);
}

void MapScreen::refresh() {
    const Nodes current = nodes();
    uint32_t slots = 0;
    for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
        if (current.slot_device[slot] >= 0) {
            slots |= 1u << slot;
        }
    }
    if (!built_ || current.linked != built_linked_ || slots != built_slots_ || current.clients != built_clients_ ||
        current.has_switch() != built_switch_ || current.route_dots != built_route_dots_) {
        build(current);
    }
    update(current);

    const int32_t key = listening() ? 2 : can_listen() ? 1 : 0;
    if (key != shown_key_) {
        shown_key_ = key;
        if (auto* bar = soft_key_bar()) {
            bar->set_key(2, {key == 2 ? "Stop" : "Start", [this] { listen_or_stop(); }, true, key != 0});
        }
    }
    const int32_t route_key = tracing() ? 2 : can_route() ? 1 : 0;
    if (route_key != shown_route_key_) {
        shown_route_key_ = route_key;
        if (auto* bar = soft_key_bar()) {
            bar->set_key(4, {route_key == 2 ? "Stop" : "Route", [this] { route_or_stop(); }, false, route_key != 0});
        }
    }
}

void MapScreen::observer_cb(lv_observer_t* observer, lv_subject_t* /*subject*/) {
    auto* screen = static_cast<MapScreen*>(lv_observer_get_user_data(observer));
    if (screen) {
        screen->refresh();
    }
}

} // namespace screen

// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The dashboard's frame and its pages. On a phone: a large title, the page,
// and a tab bar (Home, Devices, Security, Tools, Modes). On a computer
// (900 px and wider): a sidebar with every page, a top bar with where you
// are and the command bar, and the page. Pages are picked by the address's
// #/name, so Back and links work as usual.

import { h, Component } from "./preact.module.js";
import { icon, logo, networkName, modeLine, internetWords, batteryWords } from "./ui.js";

// Every page: its #/name, its title, the icon, and the sentence its empty
// state says until its contents arrive.
export const PAGES = [
  { id: "home", title: "Overview", phoneTitle: "Home", icon: "home" },
  {
    id: "devices",
    title: "Devices",
    icon: "devices",
    about: "Every phone, computer and gadget on your network, what each one runs, and anything to fix.",
  },
  {
    id: "security",
    title: "Security",
    icon: "security",
    about: "What needs fixing on your network, the most urgent first, with plain steps. You can run a health check from here too.",
  },
  {
    id: "watch",
    title: "Watch and trap",
    icon: "watch",
    about: "New devices and scans as they happen, and who touched the trap.",
  },
  { id: "map", title: "Map", icon: "map", about: "How your devices connect: switches, ports and the way to the internet." },
  {
    id: "modes",
    title: "Modes and VPN",
    phoneTitle: "Modes",
    icon: "modes",
    about: "Switch between Client, Router and Share, see the hotspot, and turn the VPN on or off.",
  },
  {
    id: "tools",
    title: "Tools",
    icon: "tools",
    about: "Ping, look up names, trace a route, check ports and test your speed.",
  },
  { id: "reports", title: "Reports", icon: "reports", about: "Download the reports saved on the Cardputer." },
];

const PHONE_TABS = ["home", "devices", "security", "tools", "modes"];
const MAIN_NAV = ["home", "devices", "security", "watch", "map"];
const CONTROL_NAV = ["modes", "tools", "reports"];

export function findPage(id) {
  return PAGES.find((page) => page.id === id) || PAGES[0];
}

function href(page) {
  return page.id === "home" ? "#/" : "#/" + page.id;
}

// ---------- Frame ----------

function NavItem({ page, current, counts }) {
  const on = page.id === current;
  const count = counts && counts[page.id];
  let end = null;
  if (page.id === "security" && count) {
    end = h("span", { class: "nav-badge", "aria-label": count + " to fix soon" }, count);
  } else if (typeof count === "number") {
    end = h("span", { class: "nav-count" }, count);
  }
  return h(
    "a",
    { class: on ? "nav on" : "nav", href: href(page), "aria-current": on ? "page" : undefined },
    icon(page.icon, 16, 2),
    page.title,
    end
  );
}

function Sidebar({ current, status }) {
  const counts = status && status.counts;
  const cardputer = (status && status.cardputer) || {};
  const battery = typeof cardputer.battery === "number" ? cardputer.battery : null;
  return h(
    "nav",
    { class: "side", "aria-label": "Pages" },
    h(
      "div",
      { class: "ws" },
      h("span", { class: "ws-tile" }, logo(18)),
      h("span", { class: "ws-text" }, h("b", null, "LokiCTL"), h("span", null, networkName(status)))
    ),
    MAIN_NAV.map((id) => h(NavItem, { page: findPage(id), current, counts })),
    h("div", { class: "side-sec" }, "Control"),
    CONTROL_NAV.map((id) => h(NavItem, { page: findPage(id), current, counts })),
    h("a", { class: "nav", href: "/screen" }, icon("screen", 16, 2), "Cardputer screen"),
    h(
      "div",
      { class: "side-device" },
      h(
        "div",
        { class: "side-device-top" },
        h("b", null, "Cardputer"),
        h("span", { class: "mono" }, battery === null ? "--" : battery + "%")
      ),
      h(
        "div",
        { class: "side-device-line" },
        h("span", { class: "mono" }, cardputer.address || "--"),
        " · ",
        cardputer.charging ? "charging" : "connected"
      ),
      h(
        "div",
        { class: "bar", role: "img", "aria-label": battery === null ? "Battery not known" : "Battery " + battery + "%" },
        h("i", { style: { width: (battery || 0) + "%" } })
      )
    )
  );
}

class TopBar extends Component {
  constructor(props) {
    super(props);
    this.state = { hint: false };
    this.timer = 0;
    this.onCommand = () => {
      clearTimeout(this.timer);
      this.setState({ hint: true });
      this.timer = setTimeout(() => this.setState({ hint: false }), 3500);
    };
  }

  componentWillUnmount() {
    clearTimeout(this.timer);
  }

  render({ page, status }) {
    return h(
      "header",
      { class: "top" },
      h("div", { class: "crumb" }, networkName(status) + " / ", h("b", null, page.title)),
      h(
        "button",
        { class: "cmd", type: "button", onClick: this.onCommand, "aria-describedby": this.state.hint ? "cmd-hint" : undefined },
        icon("search", 15, 2.2),
        "Find a device or run a command",
        h("span", { class: "kbd", "aria-hidden": "true" }, "⌘K")
      ),
      this.state.hint
        ? h("p", { id: "cmd-hint", class: "cmd-hint", role: "status" }, "Search and commands come in a later update.")
        : null
    );
  }
}

function TabBar({ current }) {
  return h(
    "nav",
    { class: "tabs", "aria-label": "Pages" },
    PHONE_TABS.map((id) => {
      const page = findPage(id);
      const on = id === current;
      return h(
        "a",
        { class: on ? "tab on" : "tab", href: href(page), "aria-current": on ? "page" : undefined },
        icon(page.icon, 26, 1.8),
        page.phoneTitle || page.title
      );
    })
  );
}

function Banner({ lost }) {
  if (!lost) return null;
  return h(
    "div",
    { class: "banner", role: "status" },
    h("span", { class: "dot warn" }),
    "No answer from the Cardputer. Trying again…"
  );
}

// ---------- Pages ----------

function SampleTag({ status }) {
  return status && status.sample ? h("span", { class: "tag" }, "Sample data") : null;
}

// "Router + VPN · kill switch on" after a dot that says whether the
// internet works; the words say it too when it does not.
function StatusLine({ status }) {
  const line = modeLine(status);
  if (!line) return null;
  const [internet, tone] = internetWords(status);
  const words = status.network && status.network.internet !== "full" ? " · internet: " + internet.toLowerCase() : "";
  return [h("span", { class: "dot " + tone, "aria-hidden": "true" }), h("span", null, line + words)];
}

// The facts the Home page shows until its full overview arrives.
function facts(status) {
  const mode = (status && status.mode) || {};
  const [internet] = internetWords(status);
  let modeValue = mode.name || "--";
  if (mode.hotspot) {
    const n = mode.hotspot_devices || 0;
    modeValue += ", " + n + (n === 1 ? " device" : " devices") + " on the hotspot";
  }
  let vpn = "Off";
  if (mode.vpn === "on") vpn = "On";
  else if (mode.vpn === "connecting") vpn = "Connecting";
  if (mode.kill_switch) vpn += ", kill switch on";
  return [
    ["Mode", modeValue, false],
    ["Internet", internet, false],
    ["VPN", vpn, false],
    ["Battery", batteryWords(status), false],
    ["Address", (status && status.cardputer && status.cardputer.address) || "--", true],
  ];
}

function HomePhone({ status }) {
  return h(
    "div",
    { class: "phone-only" },
    h(
      "header",
      { class: "phone-head" },
      h("h1", { class: "large-title" }, networkName(status)),
      h("div", { class: "meta" }, h(StatusLine, { status }), h(SampleTag, { status }))
    ),
    h("div", { class: "sec" }, h("span", null, "Right now")),
    h(
      "div",
      { class: "group" },
      facts(status).map(([key, value, mono]) =>
        h("div", { class: "row" }, h("span", { class: "row-key" }, key), h("span", { class: mono ? "row-value mono" : "row-value" }, value))
      )
    ),
    h("div", { class: "sec" }, h("span", null, "Cardputer")),
    h(
      "div",
      { class: "group" },
      h(
        "a",
        { class: "row icon-row", href: "/screen" },
        h("span", { class: "tile" }, icon("screen", 18, 2)),
        h("span", { class: "row-text" }, h("b", null, "Cardputer screen"), h("span", null, "See its screen and press its keys")),
        h("span", { class: "chev" }, icon("chevron", 13, 2.4))
      )
    )
  );
}

function HomeDesk({ status }) {
  return h(
    "div",
    { class: "desk-only" },
    h("h1", { class: "page-title" }, networkName(status)),
    h("div", { class: "page-sub" }, h(StatusLine, { status }), h(SampleTag, { status })),
    h(
      "div",
      { class: "grid" },
      h(
        "section",
        { class: "card span-8", "aria-labelledby": "now-title" },
        h("div", { class: "card-head" }, h("h2", { id: "now-title" }, "Right now")),
        facts(status).map(([key, value, mono]) => h("div", { class: "kv" }, h("span", null, key), h("b", { class: mono ? "mono" : "" }, value)))
      ),
      h(
        "section",
        { class: "card span-4", "aria-labelledby": "screen-title" },
        h("div", { class: "card-head" }, h("h2", { id: "screen-title" }, "Cardputer screen")),
        h(
          "div",
          { class: "card-body" },
          h("p", null, "See the Cardputer's screen here and press its keys from your keyboard."),
          h("a", { class: "btn-ghost", href: "/screen" }, "Open the screen")
        )
      )
    )
  );
}

function EmptyPage({ page }) {
  return h(
    "div",
    null,
    h("h1", { class: "large-title phone-only" }, page.phoneTitle || page.title),
    h("h1", { class: "page-title desk-only" }, page.title),
    h(
      "section",
      { class: "empty" },
      h("span", { class: "tile" }, icon(page.icon, 22, 1.8)),
      h("h2", null, "Coming soon"),
      h("p", null, page.about)
    )
  );
}

// ---------- The whole frame ----------

export function Shell({ route, status, lost }) {
  const page = findPage(route);
  return h(
    "div",
    { class: "shell" },
    h(Sidebar, { current: page.id, status }),
    h(
      "div",
      { class: "main" },
      h(TopBar, { page, status }),
      h(
        "main",
        { class: "content", id: "content" },
        h(Banner, { lost }),
        h(
          "div",
          { class: "page", key: page.id },
          page.id === "home" ? [h(HomePhone, { status }), h(HomeDesk, { status })] : h(EmptyPage, { page })
        )
      )
    ),
    h(TabBar, { current: page.id })
  );
}

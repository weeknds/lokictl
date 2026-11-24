// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Devices page (a list with search and filters, grouped by what needs
// attention) and the page of one device (what is wrong, how to fix it, its
// services and holes, and Block or Allow on the hotspot). Everything comes
// from the Cardputer's status (overview): this page words it and sorts it
// into groups, it never counts again.

import { h, Component } from "./preact.module.js";
import { icon, post, refreshSoon, askCheck } from "./ui.js";

// ---------- Words ----------

function plural(n, one, many) {
  return n === 1 ? one : many;
}

export function deviceHref(id) {
  return "#/devices/" + encodeURIComponent(id);
}

export const TIER_WORD = { fix: "Fix soon", look: "Worth a look", know: "Good to know", ok: "Fine" };
export const TIER_PILL = { fix: "p-bad", look: "p-warn", know: "p-know" };
export const TIER_TILE = { fix: "bad", look: "warn", know: "know" };

// The pill of a device or a service: the status colour of its tier, blue
// for New, plain for the rest.
export function pillClass(tier, tag) {
  if (tier === "fix" || tier === "look") return TIER_PILL[tier];
  if (tag === "New") return "p-new";
  if (tier === "know" && tag !== "OK") return "p-know";
  return "p-ok";
}

function needsAttention(device) {
  return device.tier === "fix" || device.tier === "look";
}

// Which picture a device gets, from what it is or what it is called.
const PICTURES = [
  [/router|gateway|modem|fritz|unifi|access ?point/i, "router"],
  [/\btv\b|television|chromecast|roku|cast|shield|bravia/i, "display"],
  [/print|epson|canon|brother|laserjet|officejet/i, "printer"],
  [/\bnas\b|synology|qnap|storage|server|diskstation/i, "nas"],
  [/ipad|tablet|kindle/i, "tablet"],
  [/phone|android|pixel|galaxy|mobile|\bsm-/i, "phone"],
  [/laptop|macbook|notebook|thinkpad|book/i, "laptop"],
  [/desktop|\bpc\b|imac|mac ?mini|computer|workstation/i, "screen"],
  [/camera|\bcam\b|doorbell|ring/i, "camera"],
  [/speaker|sonos|homepod|echo|airplay|audio/i, "speaker"],
];

export function pictureOf(device) {
  if (device.router) return "router";
  const text = (device.kind || "") + " " + (device.name || "");
  for (const [pattern, name] of PICTURES) {
    if (pattern.test(text)) return name;
  }
  return "devices";
}

// "Hotspot" or "Cable" or "Wi-Fi": how the Cardputer reaches it.
function linkWord(device) {
  if (device.hotspot) return "Its hotspot";
  return device.ring === "wireless" ? "Wi-Fi" : "Cable";
}

// "2026-11-23" as "23 Nov 2026".
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
export function dateWords(text) {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text || "");
  if (!m || +m[2] < 1 || +m[2] > 12) return "";
  return +m[3] + " " + MONTHS[+m[2] - 1] + " " + m[1];
}

// A hole's severity: the word, its tone, and how many of the 10 cells light.
function severity(score) {
  if (typeof score !== "number" || score < 0) return { word: "Not rated", tone: "", cells: 0 };
  const cells = Math.max(score > 0 ? 1 : 0, Math.min(10, Math.round(score / 10)));
  if (score >= 90) return { word: "Very serious", tone: "bad", cells };
  if (score >= 70) return { word: "Serious", tone: "bad", cells };
  if (score >= 40) return { word: "Moderate", tone: "warn", cells };
  return { word: "Minor", tone: "", cells };
}

function scoreText(score) {
  return (score / 10).toFixed(1) + " of 10";
}

// ---------- The list ----------

const FILTERS = [
  { id: "all", label: "All", count: (ov) => ov.total, test: () => true, none: "No devices yet" },
  {
    id: "issues",
    label: "Issues",
    count: (ov) => ov.device_fix + ov.device_look,
    test: needsAttention,
    none: "No device needs attention",
  },
  { id: "new", label: "New", count: (ov) => ov.device_new, test: (d) => d.new, none: "No new devices" },
  { id: "hotspot", label: "Hotspot", count: (ov) => ov.hotspot, test: (d) => d.hotspot, none: "Nobody on the hotspot" },
];

// The search and the filter stay while you look at a device and come back.
const remembered = { filter: "all", query: "" };

function matches(device, query) {
  if (!query) return true;
  const q = query.trim().toLowerCase();
  return [device.name, device.address, device.services, device.kind].some((text) => (text || "").toLowerCase().includes(q));
}

// Needs attention, then the checked rest, then those nobody checked yet: a
// device nobody checked never counts as fine.
function groupsOf(list) {
  const groups = [
    { id: "attention", title: "Needs attention", list: list.filter(needsAttention) },
    { id: "fine", title: "Looks fine", list: list.filter((d) => !needsAttention(d) && d.checked) },
    { id: "unchecked", title: "Not checked yet", list: list.filter((d) => !needsAttention(d) && !d.checked) },
  ];
  return groups.filter((group) => group.list.length > 0);
}

function DeviceTile({ device, size }) {
  return h("span", { class: "dev-tile" }, icon(pictureOf(device), size || 18, 1.8));
}

function SecondLine({ device }) {
  return h(
    "span",
    { class: "dev-line" },
    h("span", { class: "mono" }, device.address || "no address"),
    device.hotspot ? h("span", { class: "dev-words" }, " · hotspot") : null,
    device.blocked ? h("span", { class: "dev-words" }, " · blocked") : null
  );
}

function DeviceRow({ device }) {
  return h(
    "a",
    { class: "row dev-row", href: deviceHref(device.id) },
    h(DeviceTile, { device }),
    h("span", { class: "row-text" }, h("b", { class: "one-line" }, device.name), h(SecondLine, { device })),
    h("span", { class: "pill " + pillClass(device.tier, device.tag) }, device.tag),
    h("span", { class: "chev" }, icon("chevron", 13, 2.4))
  );
}

function Search({ query, onInput }) {
  return h(
    "label",
    { class: "search" },
    icon("search", 17, 2.2),
    h("input", {
      type: "search",
      value: query,
      placeholder: "Search devices",
      "aria-label": "Search devices",
      autocomplete: "off",
      autocapitalize: "off",
      spellcheck: false,
      onInput,
    })
  );
}

function Filters({ ov, filter, onPick }) {
  return h(
    "div",
    { class: "seg", role: "group", "aria-label": "Show" },
    FILTERS.map((f) => {
      const on = f.id === filter;
      const count = ov ? f.count(ov) : 0;
      return h(
        "button",
        { type: "button", class: on ? "on" : "", "aria-pressed": on ? "true" : "false", onClick: () => onPick(f.id) },
        f.label,
        h("b", { class: "mono" }, String(count || 0))
      );
    })
  );
}

function NoDevices({ ov, filter, query }) {
  let title = "No devices yet";
  let text = "Discover or a health check on the Cardputer finds the devices on your network.";
  if (ov && ov.total > 0) {
    if (query) {
      title = "No device matches";
      text = "Nothing called or at “" + query.trim() + "”. Try another name or address.";
    } else {
      title = FILTERS.find((f) => f.id === filter).none;
      text = "Pick All to see every device.";
    }
  }
  return h(
    "section",
    { class: "empty" },
    h("span", { class: "tile" }, icon("devices", 22, 1.8)),
    h("h2", null, title),
    h("p", null, text),
    ov && ov.total > 0 ? null : h("a", { class: "btn-ghost empty-act", href: "#/security" }, "Run a health check")
  );
}

function ListPhone({ ov, shown, filter, query, onInput, onPick }) {
  return h(
    "div",
    { class: "phone-only" },
    h(
      "header",
      { class: "list-head" },
      h("h1", { class: "large-title" }, "Devices"),
      h("span", { class: "count" }, ov ? ov.total + " found" : "")
    ),
    h(Search, { query, onInput }),
    h(Filters, { ov, filter, onPick }),
    shown.length
      ? groupsOf(shown).map((group) => [
          h("div", { class: "sec", key: group.id + "-s" }, h("span", null, group.title)),
          h(
            "div",
            { class: "group", key: group.id },
            group.list.map((device) => h(DeviceRow, { key: device.id, device }))
          ),
        ])
      : h(NoDevices, { ov, filter, query })
  );
}

function TableRows({ list, title }) {
  return [
    h("tr", { class: "grp" }, h("th", { colspan: 5, scope: "colgroup" }, title, h("span", { class: "t" }, String(list.length)))),
    list.map((device) =>
      h(
        "tr",
        { key: device.id, class: "dev-tr" },
        h(
          "td",
          { class: "dev-name" },
          h("a", { href: deviceHref(device.id) }, h(DeviceTile, { device, size: 15 }), h("span", { class: "one-line" }, device.name))
        ),
        h("td", { class: "mono" }, device.address || "--"),
        h("td", { class: device.checked ? "" : "muted" }, device.services + (device.hotspot ? " · hotspot" : "")),
        h("td", null, device.seen),
        h("td", null, h("span", { class: "pill " + pillClass(device.tier, device.tag) }, device.tag))
      )
    ),
  ];
}

function ListDesk({ ov, shown, filter, query, onInput, onPick }) {
  return h(
    "div",
    { class: "desk-only" },
    h(
      "div",
      { class: "page-head" },
      h(
        "div",
        null,
        h("h1", { class: "page-title" }, "Devices"),
        h("p", { class: "page-sub" }, ov ? listSummary(ov) : "")
      ),
      h("div", { class: "tools-d" }, h(Search, { query, onInput }), h(Filters, { ov, filter, onPick }))
    ),
    shown.length
      ? h(
          "section",
          { class: "card dev-card", "aria-label": "Devices" },
          h(
            "table",
            { class: "dev-table dev-table-full" },
            h(
              "thead",
              null,
              h("tr", null, ["Name", "Address", "Open services", "Seen", "Status"].map((name) => h("th", { scope: "col" }, name)))
            ),
            h(
              "tbody",
              null,
              groupsOf(shown).map((group) => h(TableRows, { key: group.id, list: group.list, title: group.title }))
            )
          )
        )
      : h(NoDevices, { ov, filter, query })
  );
}

function listSummary(ov) {
  const parts = [ov.total + plural(ov.total, " device", " devices")];
  const attention = ov.device_fix + ov.device_look;
  if (attention) parts.push(attention + " need" + plural(attention, "s", "") + " attention");
  if (ov.fine) parts.push(ov.fine + plural(ov.fine, " looks fine", " look fine"));
  if (ov.unchecked) parts.push(ov.unchecked + " not checked yet");
  return parts.join(" · ");
}

export class DevicesList extends Component {
  constructor() {
    super();
    this.state = { filter: remembered.filter, query: remembered.query };
    this.onInput = (event) => {
      remembered.query = event.currentTarget.value;
      this.setState({ query: remembered.query });
    };
    this.onPick = (filter) => {
      remembered.filter = filter;
      this.setState({ filter });
    };
  }

  render({ status }, { filter, query }) {
    const ov = status && status.overview;
    const all = (ov && ov.devices) || [];
    const test = FILTERS.find((f) => f.id === filter).test;
    const shown = all.filter((device) => test(device) && matches(device, query));
    const props = { ov, shown, filter, query, onInput: this.onInput, onPick: this.onPick };
    return [h(ListPhone, props), h(ListDesk, props)];
  }
}

// ---------- One device ----------

// Block and Allow: asked of the Cardputer, then shown from the status.
// What was asked waits here until the status says it happened, a failure
// comes back, or 10 s pass.
const BLOCK_WAIT_MS = 10000;

class BlockControl extends Component {
  constructor() {
    super();
    this.state = { want: null, since: 0, failures: 0, message: "", sending: false };
    this.timer = 0;
    this.onPress = () => this.ask();
  }

  componentWillUnmount() {
    clearTimeout(this.timer);
  }

  async ask() {
    const { device, status } = this.props;
    if (this.state.sending || this.state.want !== null) return;
    const want = !device.blocked;
    const failures = (status.block && status.block.failures) || 0;
    this.setState({ sending: true, message: "" });
    let code = 0;
    try {
      code = await post("/api/v1/block", { id: device.address, on: want ? "1" : "0" });
    } catch (err) {
      code = 0;
    }
    if (code !== 204) {
      const words = {
        401: "Your session ended. Pair again to change this.",
        403: "The Cardputer refused this. Reload the page and try again.",
        429: "Too many changes at once. Wait a moment.",
      };
      this.setState({ sending: false, message: words[code] || "No answer from the Cardputer. Try again." });
      return;
    }
    this.setState({ sending: false, want, since: Date.now(), failures });
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.setState({}), BLOCK_WAIT_MS + 100);
    refreshSoon();
  }

  // What the status says about the last request, worked out on every new status.
  componentDidUpdate() {
    const { want, since, failures } = this.state;
    if (want === null) return;
    const { device, status } = this.props;
    const block = status.block || {};
    if (device.blocked === want) {
      this.setState({ want: null, message: want ? "Blocked." : "Allowed again." });
    } else if ((block.failures || 0) > failures) {
      this.setState({ want: null, message: block.message || "The Cardputer couldn't change this." });
    } else if (Date.now() - since > BLOCK_WAIT_MS) {
      this.setState({ want: null, message: "No change yet. Is the hotspot still on?" });
    }
  }

  render({ device }, { want, sending, message }) {
    const busy = sending || want !== null;
    const label = busy ? (device.blocked ? "Allowing…" : "Blocking…") : device.blocked ? "Allow" : "Block";
    return h(
      "div",
      { class: "block-row" },
      h(
        "span",
        { class: "row-text" },
        h("b", null, device.blocked ? "Blocked on your hotspot" : "On your hotspot"),
        h(
          "span",
          null,
          device.blocked
            ? "It stays connected but can't use the hotspot until you allow it."
            : "Block it if you don't know it. It stays blocked until you allow it."
        ),
        message ? h("span", { class: "block-msg", role: "status" }, message) : null
      ),
      h(
        "button",
        { type: "button", class: "btn-ghost block-btn", onClick: this.onPress, disabled: busy, "aria-busy": busy ? "true" : "false" },
        icon(device.blocked ? "check" : "block", 16, 2.2),
        label
      )
    );
  }
}

export function Steps({ steps }) {
  return h(
    "ol",
    { class: "steps" },
    steps.map((step, i) => h("li", { key: i }, step))
  );
}

function Meter({ cells }) {
  const out = [];
  for (let i = 0; i < 10; i++) out.push(h("i", { key: i, class: i < cells ? "" : "off" }));
  return h("span", { class: "meter", "aria-hidden": "true" }, out);
}

export function Hole({ hole }) {
  const sev = severity(hole.score);
  return h(
    "div",
    { class: "hole" },
    h(
      "div",
      { class: "sev" },
      h("b", { class: "sev-" + (sev.tone || "plain") }, sev.word),
      typeof hole.score === "number" ? h("span", { class: "mono sev-score" }, scoreText(hole.score)) : null,
      h(Meter, { cells: sev.cells })
    ),
    h("p", null, hole.text),
    h("div", { class: "hole-id" }, "Official number ", h("span", { class: "mono" }, hole.cve))
  );
}

export function holesNote(device, ov) {
  const parts = [];
  if (device.more_holes > 0) parts.push(device.more_holes + plural(device.more_holes, " more known hole", " more known holes") + " not listed here.");
  const date = dateWords(ov.list_date);
  parts.push("Found by version: what its services report, checked against LokiCTL's built-in list" + (date ? " from " + date : "") + ".");
  return parts.join(" ");
}

function PortRows({ device }) {
  if (!device.checked) return null;
  return [
    device.ports.length
      ? device.ports.map((port) =>
          h(
            "div",
            { class: "row port-row", key: port.port },
            h(
              "span",
              { class: "row-text" },
              h("b", { class: "one-line" }, port.name),
              h("span", { class: "mono one-line" }, "port " + port.port + (port.version ? " · " + port.version : ""))
            ),
            h("span", { class: "pill " + pillClass(port.tier, port.tag) }, port.tag)
          )
        )
      : h("div", { class: "row" }, h("span", { class: "row-key muted" }, "No open services found")),
    device.more_ports > 0
      ? h("div", { class: "row" }, h("span", { class: "row-key muted" }, device.more_ports + plural(device.more_ports, " more open port", " more open ports")))
      : null,
  ];
}

function IssueRows({ issues }) {
  return issues.map((issue, i) =>
    h(
      "div",
      { class: "row icon-row issue-row", key: i },
      h("span", { class: "tile " + (TIER_TILE[issue.tier] || "know") }, icon(issue.tier === "know" ? "help" : "alert", 18, 2)),
      h("span", { class: "row-text" }, h("b", null, issue.title), h("span", null, issue.steps[0] || issue.about)),
      h("span", { class: "pill " + pillClass(issue.tier, "") }, TIER_WORD[issue.tier])
    )
  );
}

// The phone's top card: the worst issue and its fix, or that nothing was
// found, or that nobody checked it yet.
class AlertPhone extends Component {
  constructor() {
    super();
    this.state = { open: false };
    this.onSteps = () => this.setState({ open: !this.state.open });
  }

  render({ device }, { open }) {
    const top = device.issues[0];
    if (top && (top.tier === "fix" || top.tier === "look")) {
      const more = top.steps.length > 1;
      return [
        h(
          "section",
          { class: "alert " + top.tier, "aria-label": "Problem" },
          h("h2", null, top.title),
          top.about ? h("p", null, top.about) : null,
          top.steps.length
            ? h(
                "div",
                { class: "fixline" },
                open && more ? null : h("span", { class: "fix-ic" }, icon("check", 18, 2.2)),
                open && more ? h(Steps, { steps: top.steps }) : h("span", null, h("b", null, "Fix: "), top.steps[0])
              )
            : null
        ),
        h(
          "div",
          { class: "acts-p" },
          more
            ? h(
                "button",
                { type: "button", class: "bp or", onClick: this.onSteps, "aria-expanded": open ? "true" : "false" },
                open ? "Show less" : "Fix it step by step"
              )
            : null,
          h("a", { class: "bp gh" + (more ? "" : " wide"), href: "#/security", onClick: askCheck }, "Check again")
        ),
      ];
    }
    const checked = device.checked;
    return [
      h(
        "section",
        { class: "alert calm", "aria-label": checked ? "Looks fine" : "Not checked" },
        h("h2", null, checked ? "Nothing to fix" : "Not checked yet"),
        h(
          "p",
          null,
          checked
            ? "The last check found nothing to fix on this device."
            : "LokiCTL hasn't scanned this device yet. A health check shows what it runs and whether anything needs fixing."
        )
      ),
      h("div", { class: "acts-p" }, h("a", { class: "bp gh wide", href: "#/security", onClick: askCheck }, checked ? "Check again" : "Run a health check")),
    ];
  }
}

function DeviceHead({ device }) {
  return h(
    "div",
    { class: "dev-head" },
    h("span", { class: "dev-big" }, icon(pictureOf(device), 28, 1.6)),
    h(
      "div",
      { class: "dev-head-text" },
      h("h1", { class: "dev-title" }, device.name),
      h(
        "div",
        { class: "dev-meta" },
        h("span", { class: "mono" }, device.address || "no address"),
        h("span", null, " · seen " + device.seen),
        device.hotspot ? h("span", null, " · hotspot") : null
      )
    )
  );
}

function DevicePhone({ device, status }) {
  const ov = status.overview || {};
  const others = device.issues.filter((issue, i) => i > 0 || !(issue.tier === "fix" || issue.tier === "look"));
  return h(
    "div",
    { class: "phone-only dev-page" },
    h("a", { class: "back", href: "#/devices" }, icon("back", 22, 2.4), "Devices"),
    h(DeviceHead, { device }),
    h(AlertPhone, { device, key: device.id }),
    others.length ? [h("div", { class: "sec" }, h("span", null, "Also found")), h("div", { class: "group" }, h(IssueRows, { issues: others }))] : null,
    device.hotspot && device.address
      ? [h("div", { class: "sec" }, h("span", null, "Hotspot")), h("div", { class: "group" }, h(BlockControl, { device, status }))]
      : null,
    device.checked
      ? [h("div", { class: "sec" }, h("span", null, "Open services")), h("div", { class: "group" }, h(PortRows, { device }))]
      : null,
    device.holes.length
      ? [
          h("div", { class: "sec" }, h("span", null, "Security holes")),
          h("div", { class: "group" }, device.holes.map((hole) => h(Hole, { hole, key: hole.cve }))),
          h("p", { class: "note" }, holesNote(device, ov)),
        ]
      : null
  );
}

function DetailDesk({ device }) {
  const top = device.issues[0];
  const problem = top && (top.tier === "fix" || top.tier === "look");
  const others = device.issues.filter((issue, i) => i > 0 || !problem);
  return h(
    "section",
    { class: "card detail span-8", "aria-label": "What was found" },
    problem
      ? [
          h("h2", { class: "detail-title" }, top.title),
          h(
            "div",
            { class: "tagrow" },
            h("span", { class: "pill " + TIER_PILL[top.tier] }, TIER_WORD[top.tier]),
            top.port ? h("span", { class: "mono" }, "port " + top.port) : null
          ),
          top.about ? h("p", { class: "detail-about" }, top.about) : null,
          top.steps.length ? [h("h3", { class: "h3" }, "How to fix it"), h(Steps, { steps: top.steps })] : null,
          h("div", { class: "acts-d" }, h("a", { class: "btn or lg", href: "#/security", onClick: askCheck }, "I've fixed it, check again")),
        ]
      : [
          h("h2", { class: "detail-title" }, device.checked ? "Nothing to fix" : "Not checked yet"),
          h(
            "p",
            { class: "detail-about" },
            device.checked
              ? "The last check found nothing to fix on this device."
              : "LokiCTL hasn't scanned this device yet. A health check shows what it runs and whether anything needs fixing."
          ),
          h("div", { class: "acts-d" }, h("a", { class: "btn gh lg", href: "#/security", onClick: askCheck }, device.checked ? "Check again" : "Run a health check")),
        ],
    others.length
      ? [
          h("h3", { class: "h3" }, "Also found"),
          others.map((issue, i) =>
            h(
              "div",
              { class: "other", key: i },
              h("span", { class: "sq " + (TIER_TILE[issue.tier] || "know") }),
              h("span", { class: "rt" }, h("b", null, issue.title), h("span", null, issue.about)),
              h("span", { class: "pill " + pillClass(issue.tier, "") }, TIER_WORD[issue.tier])
            )
          ),
        ]
      : null,
    device.holes.length
      ? [
          h("h3", { class: "h3 holes-h" }, "The holes"),
          device.holes.map((hole) => h(Hole, { hole, key: hole.cve })),
        ]
      : null
  );
}

function FactsDesk({ device, status }) {
  return h(
    "div",
    { class: "span-4 side-col" },
    h(
      "section",
      { class: "card col", "aria-labelledby": "facts-d" },
      h("div", { class: "card-head" }, h("h2", { id: "facts-d" }, "This device"), h("span", { class: "r pill " + pillClass(device.tier, device.tag) }, device.tag)),
      h("div", { class: "kv" }, h("span", null, "Address"), h("b", { class: "mono" }, device.address || "--")),
      h("div", { class: "kv" }, h("span", null, "Reached by"), h("b", null, linkWord(device))),
      h("div", { class: "kv" }, h("span", null, "Seen"), h("b", null, device.seen)),
      device.kind ? h("div", { class: "kv" }, h("span", null, "Looks like"), h("b", null, device.kind)) : null,
      device.hotspot && device.address ? h("div", { class: "kv-block" }, h(BlockControl, { device, status })) : null
    ),
    device.checked
      ? h(
          "section",
          { class: "card col", "aria-labelledby": "ports-d" },
          h("div", { class: "card-head" }, h("h2", { id: "ports-d" }, "Open services"), h("span", { class: "t r" }, String(device.ports.length + device.more_ports))),
          h("div", { class: "ports-d" }, h(PortRows, { device }))
        )
      : null
  );
}

function DeviceDesk({ device, status }) {
  const ov = status.overview || {};
  return h(
    "div",
    { class: "desk-only" },
    h(
      "div",
      { class: "page-head" },
      h("a", { class: "back-d", href: "#/devices" }, icon("back", 16, 2.4), "All devices"),
      h(DeviceHead, { device })
    ),
    h(
      "div",
      { class: "grid" },
      h(DetailDesk, { device }),
      h(FactsDesk, { device, status }),
      device.holes.length ? h("p", { class: "note span-12" }, holesNote(device, ov)) : null
    )
  );
}

export function DevicePage({ status, id }) {
  const ov = status && status.overview;
  if (!ov) return null;
  const device = (ov.devices || []).find((d) => d.id === id);
  if (!device) {
    return h(
      "div",
      null,
      h("a", { class: "back phone-only", href: "#/devices" }, icon("back", 22, 2.4), "Devices"),
      h(
        "section",
        { class: "empty" },
        h("span", { class: "tile" }, icon("devices", 22, 1.8)),
        h("h2", null, "This device isn't in the list now"),
        h("p", null, "It may have left the network, or a new check is running. The list shows what the Cardputer knows now."),
        h("a", { class: "btn-ghost empty-act", href: "#/devices" }, "All devices")
      )
    );
  }
  return [h(DevicePhone, { device, status }), h(DeviceDesk, { device, status })];
}

// The name the top bar shows for a device page.
export function deviceName(status, id) {
  const ov = status && status.overview;
  const device = ov && (ov.devices || []).find((d) => d.id === id);
  return device ? device.name : "";
}

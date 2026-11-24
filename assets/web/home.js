// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Home page (phone) and the Overview (computer): the verdict, the
// constellation, what needs attention, the live traffic, the mode and
// Watch, and on a computer the devices. Every number comes from the
// Cardputer's status (overview, counts): this page only words them, it
// never counts again. Both layouts are built; the style sheet shows one.

import { h } from "./preact.module.js";
import { icon, batteryWords } from "./ui.js";

// ---------- Words ----------

function plural(n, one, many) {
  return n === 1 ? one : many;
}

function things(n) {
  return n + " " + plural(n, "thing", "things");
}

function deviceHref(id) {
  return id ? "#/devices/" + encodeURIComponent(id) : "#/security";
}

const TIER_WORD = { fix: "Fix soon", look: "Worth a look", know: "Good to know" };
const TILE = { fix: "bad", look: "warn", know: "know" };
const THING_ICON = { shield: "alert", lock: "unlock", screen: "display", wifi: "wifi", cable: "cable", eye: "watch" };

// The verdict: the count in orange and the rest, the one thing to fix (a
// link), and that thing.
function verdictOf(ov) {
  if (!ov) return { em: "", rest: "Not checked yet", top: null };
  const first = (tier) => (ov.things || []).find((thing) => thing.tier === tier) || null;
  if (ov.fix > 0) return { em: things(ov.fix), rest: " to fix", top: first("fix") };
  if (ov.look > 0) return { em: things(ov.look), rest: " worth a look", top: first("look") };
  if (ov.checked) return { em: "Nothing", rest: " to fix", top: null };
  return { em: "", rest: "Not checked yet", top: null };
}

// The line under the verdict on a phone: "2 more worth a look · 9 look fine".
function subLine(ov) {
  if (!ov) return "";
  const parts = [];
  if (ov.fix > 0 && ov.look > 0) parts.push(ov.look + " more worth a look");
  if (ov.checked) {
    if (ov.fine > 0) parts.push(ov.fine + plural(ov.fine, " looks fine", " look fine"));
    if (ov.unchecked > 0) parts.push(ov.unchecked + " not checked");
  } else if (ov.checking) {
    parts.push("Checking now");
  } else {
    parts.push(ov.fix + ov.look > 0 ? "Not checked for weak spots yet" : "No health check has run yet");
  }
  return parts.join(" · ");
}

// The same in sentences, for the computer's hero.
function subText(ov) {
  if (!ov) return "";
  const out = [];
  if (ov.fix > 0 && ov.look > 0) out.push(ov.look + " more worth a look.");
  if (ov.checked) {
    if (ov.fine > 0) {
      const start = ov.device_fix + ov.device_look > 0 ? "The other " : ov.fine === ov.total ? "All " : "";
      out.push(start + ov.fine + plural(ov.fine, " device looks fine.", " devices look fine."));
    }
    if (ov.unchecked > 0) out.push(ov.unchecked + plural(ov.unchecked, " device", " devices") + " not checked yet.");
    if (ov.checked_at) out.push("Checked at " + ov.checked_at + ".");
  } else if (ov.checking) {
    out.push("A health check is running now.");
  } else {
    out.push("LokiCTL hasn't checked this network for weak spots yet.");
  }
  return out.join(" ");
}

function thingLink(top, ov) {
  if (top) return { text: top.action, href: deviceHref(top.device) };
  if (ov && !ov.checked && !ov.checking) return { text: "Run a health check", href: "#/security" };
  return null;
}

// "42.1" and "Mb/s" (or "640" and "kb/s"); "--" when not known.
function rate(kbps) {
  if (typeof kbps !== "number" || kbps < 0) return ["--", ""];
  if (kbps < 1000) return [String(kbps), "kb/s"];
  const mbps = kbps / 1000;
  return [mbps >= 100 ? mbps.toFixed(0) : mbps.toFixed(1), "Mb/s"];
}

function viaWords(status) {
  const link = status.network && status.network.link;
  const vpn = status.mode && status.mode.vpn === "on";
  if (link === "none") return "No link right now";
  return (link === "wifi" ? "Through Wi-Fi" : "Through the cable") + (vpn ? ", then the VPN" : "");
}

function modeRows(mode) {
  const vpnOn = mode.vpn === "on";
  let vpn = ["Off", ""];
  if (vpnOn) {
    vpn = typeof mode.vpn_reply_s === "number" ? ["Reply " + mode.vpn_reply_s + " s ago", "ok"] : ["On, no reply yet", ""];
  } else if (mode.vpn === "connecting") {
    vpn = ["Connecting", ""];
  }
  return [
    ["Running", (mode.name || "--") + (vpnOn && mode.id !== "vpn" ? " + VPN" : ""), ""],
    ["Hotspot", mode.hotspot ? mode.hotspot_name || "On" : "Off", mode.hotspot && mode.hotspot_name ? "mono" : ""],
    ["VPN", vpn[0], vpn[1]],
    ["Kill switch", mode.kill_switch ? "On" : "Off", ""],
    ["Keep after quitting", mode.keep ? "On" : "Off", ""],
  ];
}

function trapWords(watch) {
  if (!watch.trap) return "Off";
  return "On, " + watch.trap_ports + plural(watch.trap_ports, " fake service", " fake services");
}

function constellationLabel(ov) {
  if (!ov || !ov.total) return "No devices seen yet";
  const parts = [];
  if (ov.device_fix) parts.push(ov.device_fix + " to fix");
  if (ov.device_look) parts.push(ov.device_look + " worth a look");
  if (ov.fine) parts.push(ov.fine + " fine");
  if (ov.unchecked) parts.push(ov.unchecked + " not checked");
  return ov.total + plural(ov.total, " device", " devices") + " around the Cardputer: " + parts.join(", ");
}

// ---------- The constellation ----------

// The two sizes: where the Cardputer sits, the rings, how many dots fit on
// each ring, the label size and how many devices get a label.
const LAYOUTS = {
  p: { box: [0, 18, 354, 178], cx: 177, cy: 104, glow: 86, inner: [80, 52], outer: [152, 82], cap: [10, 14], font: 11, labels: 4, dot: [4.5, 4, 5, 6] },
  d: { box: [0, 0, 480, 380], cx: 250, cy: 190, glow: 140, inner: [118, 82], outer: [205, 146], cap: [12, 18], font: 12, labels: 7, dot: [5.5, 4.5, 5.5, 7] },
};

// Dots by id (addresses in number order), so they stay put between polls.
function idOrder(a, b) {
  const key = (id) => {
    const parts = /^(\d+)\.(\d+)\.(\d+)\.(\d+)$/.exec(id || "");
    return parts ? parts.slice(1).reduce((sum, part) => sum * 256 + Number(part), 0) : Infinity;
  };
  const ka = key(a.id);
  const kb = key(b.id);
  if (ka !== kb) return ka < kb ? -1 : 1;
  return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
}

function place(devices, L) {
  const spots = [];
  const rings = [
    { list: devices.filter((d) => d.ring !== "wireless"), r: L.inner, cap: L.cap[0], start: -2.4, size: 0 },
    { list: devices.filter((d) => d.ring === "wireless"), r: L.outer, cap: L.cap[1], start: -1.95, size: 1 },
  ];
  for (const ring of rings) {
    // The list comes worst first: cut it there, so a red dot is never left out.
    const list = ring.list.slice(0, ring.cap).sort(idOrder);
    const step = (Math.PI * 2) / Math.max(list.length, 1);
    list.forEach((device, index) => {
      const angle = ring.start + index * step;
      spots.push({
        device,
        x: L.cx + Math.cos(angle) * ring.r[0],
        y: L.cy + Math.sin(angle) * ring.r[1],
        size: ring.size,
      });
    });
  }
  return spots;
}

// Which dots get a name: the ones to fix, then worth a look, the router,
// new ones, then the rest, up to the layout's count. Below those to fix or
// look at, a device known only by its address gets none, so long
// addresses never pile up.
function labelled(spots, count) {
  const rank = (s) => (s.device.tier === "fix" ? 0 : s.device.tier === "look" ? 1 : s.device.router ? 2 : s.device.new ? 3 : 4);
  return spots
    .filter((s) => rank(s) < 2 || s.device.name !== s.device.address)
    .sort((a, b) => rank(a) - rank(b))
    .slice(0, count);
}

function cardputer(L, p) {
  const s = p === "d" ? 1.4 : 1;
  const w = 54 * s;
  const hgt = 36 * s;
  const x = L.cx - w / 2;
  const y = L.cy - hgt / 2;
  const keys = [];
  for (let i = 0; i < 5; i++) {
    keys.push(h("rect", { class: "cp-key", x: x + (8 + i * 8) * s, y: y + 30 * s, width: 6 * s, height: 2.5 * s, rx: 1 }));
  }
  return h(
    "g",
    null,
    h("rect", { class: "cp-body", x, y, width: w, height: hgt, rx: 8 * s }),
    h("rect", { class: "cp-screen", x: x + 6 * s, y: y + 5 * s, width: 42 * s, height: 22 * s, rx: 3 * s }),
    h("rect", { class: "cp-accent", x: x + 10 * s, y: y + 10 * s, width: 20 * s, height: 3 * s, rx: 1.5 * s }),
    h("rect", { class: "cp-line", x: x + 10 * s, y: y + 16 * s, width: 30 * s, height: 2.5 * s, rx: 1.25 * s }),
    h("rect", { class: "cp-line", x: x + 10 * s, y: y + 21 * s, width: 14 * s, height: 2.5 * s, rx: 1.25 * s }),
    keys
  );
}

function Constellation({ ov, p }) {
  const L = LAYOUTS[p];
  const devices = (ov && ov.devices) || [];
  const spots = place(devices, L);
  const named = labelled(spots, L.labels);
  const [bx, by, bw, bh] = L.box;
  const glowId = "glow-" + p;
  const redId = "red-" + p;
  const charW = L.font * 0.58;
  const svgProps = {
    class: "const-" + p,
    viewBox: L.box.join(" "),
    role: "img",
    "aria-label": constellationLabel(ov),
  };
  if (p === "d") {
    svgProps.preserveAspectRatio = "xMidYMid meet";
  }
  return h(
    "svg",
    svgProps,
    h(
      "defs",
      null,
      h(
        "radialGradient",
        { id: glowId, cx: "50%", cy: "50%", r: "50%" },
        h("stop", { offset: "0", class: "st-or", "stop-opacity": p === "d" ? ".42" : ".34" }),
        h("stop", { offset: "1", class: "st-or", "stop-opacity": "0" })
      ),
      h(
        "radialGradient",
        { id: redId, cx: "50%", cy: "50%", r: "50%" },
        h("stop", { offset: "0", class: "st-bad", "stop-opacity": ".6" }),
        h("stop", { offset: "1", class: "st-bad", "stop-opacity": "0" })
      )
    ),
    h("circle", { cx: L.cx, cy: L.cy, r: L.glow, fill: "url(#" + glowId + ")" }),
    h("ellipse", { class: "ring-in", cx: L.cx, cy: L.cy, rx: L.inner[0], ry: L.inner[1] }),
    h("ellipse", { class: "ring-out", cx: L.cx, cy: L.cy, rx: L.outer[0], ry: L.outer[1] }),
    h(
      "g",
      { class: "spokes" },
      spots.map((s) => h("line", { key: "l" + s.device.id, x1: L.cx, y1: L.cy, x2: s.x.toFixed(1), y2: s.y.toFixed(1) }))
    ),
    spots
      .filter((s) => s.device.tier === "fix")
      .map((s) =>
        h("circle", { key: "g" + s.device.id, class: "pulse", cx: s.x.toFixed(1), cy: s.y.toFixed(1), r: p === "d" ? 34 : 28, fill: "url(#" + redId + ")" })
      ),
    spots.map((s) => {
      const tier = s.device.tier;
      const r = tier === "fix" ? L.dot[3] : tier === "look" ? L.dot[2] : L.dot[s.size];
      const tone = tier === "fix" ? "n-fix" : tier === "look" ? "n-look" : "n-ok";
      return h("circle", { key: "n" + s.device.id, class: tone, cx: s.x.toFixed(1), cy: s.y.toFixed(1), r });
    }),
    cardputer(L, p),
    h(
      "g",
      { class: "c-label", "font-size": L.font },
      named.map((s) => {
        const text = s.device.name.length > 14 ? s.device.name.slice(0, 13) + "…" : s.device.name;
        const half = (text.length * charW) / 2;
        const x = Math.min(Math.max(s.x, bx + half + 2), bx + bw - half - 2);
        let y = s.y + L.font + 9;
        if (y > by + bh - 3) y = s.y - 11;
        return h("text", { key: "t" + s.device.id, x: x.toFixed(1), y: y.toFixed(1), "text-anchor": "middle" }, text);
      }),
      p === "d" ? h("text", { class: "c-center", x: L.cx, y: L.cy + 46, "text-anchor": "middle" }, "Cardputer") : null
    ),
    p === "d"
      ? h(
          "g",
          { class: "legend", "aria-hidden": "true" },
          h("line", { class: "lg-cable", x1: 368, y1: 346, x2: 384, y2: 346 }),
          h("line", { class: "lg-air", x1: 368, y1: 362, x2: 384, y2: 362 }),
          h("text", { x: 390, y: 350 }, "Cable"),
          h("text", { x: 390, y: 366 }, "Wireless")
        )
      : null
  );
}

// ---------- The traffic chart ----------

// A smooth line through the points (Catmull-Rom as Béziers).
function smooth(points) {
  if (points.length < 2) return "";
  let d = "M" + points[0][0].toFixed(1) + " " + points[0][1].toFixed(1);
  for (let i = 0; i < points.length - 1; i++) {
    const p0 = points[i - 1] || points[i];
    const p1 = points[i];
    const p2 = points[i + 1];
    const p3 = points[i + 2] || p2;
    const c1x = p1[0] + (p2[0] - p0[0]) / 6;
    const c1y = p1[1] + (p2[1] - p0[1]) / 6;
    const c2x = p2[0] - (p3[0] - p1[0]) / 6;
    const c2y = p2[1] - (p3[1] - p1[1]) / 6;
    d += " C" + [c1x, c1y, c2x, c2y, p2[0], p2[1]].map((v) => v.toFixed(1)).join(" ");
  }
  return d;
}

const CHART_SLOTS = 60;

function TrafficChart({ history, p, height }) {
  const W = 600;
  const H = 150;
  const list = Array.isArray(history) ? history.slice(-CHART_SLOTS) : [];
  let peak = 100;
  for (const [down, up] of list) peak = Math.max(peak, down, up);
  const top = 12;
  const at = (value) => H - 4 - (Math.min(value, peak) / peak) * (H - 4 - top);
  const xs = (i) => W - (list.length - 1 - i) * (W / (CHART_SLOTS - 1));
  const down = list.map((pair, i) => [xs(i), at(pair[0])]);
  const up = list.map((pair, i) => [xs(i), at(pair[1])]);
  const downPath = smooth(down);
  const areaId = "area-" + p;
  return h(
    "svg",
    { class: "chart", viewBox: "0 0 " + W + " " + H, preserveAspectRatio: "none", height, "aria-hidden": "true", focusable: "false" },
    h(
      "defs",
      null,
      h(
        "linearGradient",
        { id: areaId, x1: "0", y1: "0", x2: "0", y2: "1" },
        h("stop", { offset: "0", class: "st-or", "stop-opacity": ".38" }),
        h("stop", { offset: "1", class: "st-or", "stop-opacity": "0" })
      )
    ),
    h(
      "g",
      { class: "grid-lines" },
      h("line", { x1: 0, y1: 37, x2: W, y2: 37 }),
      h("line", { x1: 0, y1: 75, x2: W, y2: 75 }),
      h("line", { x1: 0, y1: 112, x2: W, y2: 112 })
    ),
    downPath
      ? h("path", {
          d: downPath + " L" + down[down.length - 1][0].toFixed(1) + " " + H + " L" + down[0][0].toFixed(1) + " " + H + "Z",
          fill: "url(#" + areaId + ")",
        })
      : null,
    downPath ? h("path", { class: "line-down", d: downPath, "vector-effect": "non-scaling-stroke" }) : null,
    up.length > 1 ? h("path", { class: "line-up", d: smooth(up), "vector-effect": "non-scaling-stroke" }) : null
  );
}

function Rates({ status }) {
  const t = status.traffic || {};
  const [down, downUnit] = rate(t.down_kbps);
  const [up, upUnit] = rate(t.up_kbps);
  return h(
    "div",
    { class: "rates" },
    h("div", null, h("div", { class: "rate-key" }, "Down"), h("div", { class: "num" }, down, h("small", null, downUnit))),
    h("div", null, h("div", { class: "rate-key" }, "Up"), h("div", { class: "num up" }, up, h("small", null, upUnit))),
    h("div", { class: "via" }, viaWords(status))
  );
}

// ---------- Rows ----------

function thingHint(thing) {
  if (thing.tier === "fix" && thing.hint) return TIER_WORD.fix + " · " + thing.hint;
  return TIER_WORD[thing.tier] || "";
}

function ThingTile({ thing, size }) {
  return h("span", { class: "tile " + (TILE[thing.tier] || "know") }, icon(THING_ICON[thing.icon] || "alert", size, 2));
}

function attention(ov) {
  return ((ov && ov.things) || []).filter((thing) => thing.tier === "fix" || thing.tier === "look");
}

// ---------- Phone ----------

function HeroPhone({ ov }) {
  const v = verdictOf(ov);
  const link = thingLink(v.top, ov);
  return h(
    "section",
    { class: "hero-p", "aria-labelledby": "verdict-p" },
    h("h2", { class: "verdict", id: "verdict-p" }, v.em ? h("em", null, v.em) : null, v.rest),
    link
      ? h("a", { class: "thing", href: link.href }, link.text, h("span", { class: "thing-chev" }, icon("chevron", 14, 2.6)))
      : null,
    h("p", { class: "sub" }, subLine(ov)),
    h(Constellation, { ov, p: "p" })
  );
}

function AttentionPhone({ ov }) {
  const list = attention(ov).slice(0, 4);
  return [
    h("div", { class: "sec" }, h("span", null, "Needs attention"), h("a", { class: "sec-link", href: "#/devices" }, "See all")),
    h(
      "div",
      { class: "group" },
      list.length
        ? list.map((thing, i) =>
            h(
              "a",
              { key: i, class: "row icon-row", href: deviceHref(thing.device) },
              h(ThingTile, { thing, size: 18 }),
              h("span", { class: "row-text" }, h("b", null, thing.title), h("span", null, thingHint(thing))),
              h("span", { class: "chev" }, icon("chevron", 13, 2.4))
            )
          )
        : h(
            "div",
            { class: "row icon-row" },
            h("span", { class: "tile ok" }, icon("check", 18, 2.4)),
            h(
              "span",
              { class: "row-text" },
              h("b", null, "Nothing needs attention"),
              h("span", null, ov && ov.checked ? "The last check found nothing to fix" : "Run a health check to be sure")
            )
          )
    ),
  ];
}

function BlocksPhone({ ov }) {
  const v = verdictOf(ov);
  const fix = ov && ov.fix > 0 && v.top;
  const again = ov && (ov.checked || ov.checking) ? "Check again" : "Check now";
  return h(
    "div",
    { class: "blocks" },
    fix
      ? h(
          "a",
          { class: "blk or", href: deviceHref(v.top.device) },
          icon("wrench", 22, 2),
          h("span", null, h("b", null, v.top.fix), h("small", null, "Step by step"))
        )
      : h(
          "a",
          { class: "blk dk", href: "#/devices" },
          icon("devices", 22, 2),
          h("span", null, h("b", null, "All devices"), h("small", null, ov ? ov.total + plural(ov.total, " device", " devices") : "--"))
        ),
    h(
      "a",
      { class: "blk dk", href: "#/security" },
      icon("refresh", 22, 2),
      h("span", null, h("b", null, again), h("small", null, "Health check"))
    )
  );
}

function TrafficPhone({ status }) {
  return [
    h("div", { class: "sec" }, h("span", null, "Traffic"), h("span", { class: "sec-note" }, "Last minute")),
    h("div", { class: "group traffic-p" }, h(Rates, { status }), h(TrafficChart, { history: (status.traffic || {}).history, p: "p", height: 96 })),
  ];
}

function ModePhone({ status }) {
  const mode = status.mode || {};
  return [
    h("div", { class: "sec" }, h("span", null, "Mode"), h("a", { class: "sec-link", href: "#/modes" }, "Change")),
    h(
      "div",
      { class: "group" },
      modeRows(mode).map(([key, value, tone]) =>
        h("div", { class: "row" }, h("span", { class: "row-key" }, key), h("span", { class: "row-value " + tone }, value))
      )
    ),
  ];
}

function WatchPhone({ status }) {
  const watch = status.watch || {};
  const feed = (watch.feed || []).slice(0, 3);
  return [
    h("div", { class: "sec" }, h("span", null, "Watch"), h(LiveTag, { on: watch.on })),
    h(
      "div",
      { class: "group" },
      feed.length
        ? feed.map((line, i) =>
            h("div", { key: i, class: "row feed-row" }, h("span", { class: "feed-time mono" }, line.time), h("span", { class: "feed-text" }, line.text))
          )
        : h("div", { class: "row feed-row" }, h("span", { class: "feed-text muted" }, watchEmpty(watch))),
      h("div", { class: "row" }, h("span", { class: "row-key" }, "Trap"), h("span", { class: "row-value" }, trapWords(watch)))
    ),
  ];
}

function watchEmpty(watch) {
  return watch.on ? "Nothing heard yet." : "Watch is off. Start it from Tools on the Cardputer.";
}

function LiveTag({ on }) {
  return on ? h("span", { class: "live" }, h("i", { "aria-hidden": "true" }), "Listening") : h("span", { class: "live off" }, "Off");
}

function CardputerPhone({ status }) {
  const address = (status.cardputer && status.cardputer.address) || "--";
  return [
    h("div", { class: "sec" }, h("span", null, "Cardputer")),
    h(
      "div",
      { class: "group" },
      h("div", { class: "row" }, h("span", { class: "row-key" }, "Battery"), h("span", { class: "row-value" }, batteryWords(status))),
      h("div", { class: "row" }, h("span", { class: "row-key" }, "Address"), h("span", { class: "row-value mono" }, address)),
      h(
        "a",
        { class: "row icon-row", href: "/screen" },
        h("span", { class: "tile" }, icon("screen", 18, 2)),
        h("span", { class: "row-text" }, h("b", null, "Cardputer screen"), h("span", null, "See its screen and press its keys")),
        h("span", { class: "chev" }, icon("chevron", 13, 2.4))
      )
    ),
  ];
}

export function HomePhone({ status, head }) {
  const ov = status && status.overview;
  if (!status) return h("div", { class: "phone-only" }, head);
  return h(
    "div",
    { class: "phone-only" },
    head,
    h(HeroPhone, { ov }),
    h(AttentionPhone, { ov }),
    h(BlocksPhone, { ov }),
    h(TrafficPhone, { status }),
    h(ModePhone, { status }),
    h(WatchPhone, { status }),
    h(CardputerPhone, { status })
  );
}

// ---------- Computer ----------

function HeroDesk({ status, ov }) {
  const v = verdictOf(ov);
  const link = thingLink(v.top, ov);
  const mode = status.mode || {};
  const chips = [h("span", { class: "chip" }, h("i", { class: "c-or" }), (mode.name || "--") + (mode.vpn === "on" && mode.id !== "vpn" ? " + VPN" : ""))];
  if (mode.kill_switch) chips.push(h("span", { class: "chip" }, h("i", { class: "c-ok" }), "Kill switch on"));
  if (mode.hotspot) chips.push(h("span", { class: "chip" }, h("i", { class: "c-dim" }), mode.hotspot_devices + " on the hotspot"));
  if (status.sample) chips.push(h("span", { class: "chip" }, "Sample data"));
  const fix = ov && ov.fix > 0 && v.top;
  return h(
    "section",
    { class: "card hero-d span-8", "aria-labelledby": "verdict-d" },
    h(
      "div",
      { class: "hero-txt" },
      h("h2", { class: "verdict-d", id: "verdict-d" }, v.em ? h("em", null, v.em) : null, v.rest),
      link
        ? h("a", { class: "thing-d", href: link.href }, link.text, h("span", { class: "thing-chev" }, icon("chevron", 15, 2.6)))
        : null,
      h("p", { class: "hero-sub" }, subText(ov)),
      h("div", { class: "chips" }, chips),
      h(
        "div",
        { class: "acts" },
        fix ? h("a", { class: "btn or lg", href: deviceHref(v.top.device) }, "See the fix") : null,
        h("a", { class: "btn gh lg", href: "#/devices" }, "All devices")
      )
    ),
    h("div", { class: "hero-pic" }, h(Constellation, { ov, p: "d" }))
  );
}

function AttentionDesk({ ov }) {
  const list = attention(ov).slice(0, 4);
  const count = ov ? ov.fix + ov.look : 0;
  return h(
    "section",
    { class: "card att span-4", "aria-labelledby": "att-d" },
    h("div", { class: "card-head" }, h("h2", { id: "att-d" }, "Needs attention"), h("span", { class: "t r" }, String(count))),
    list.length
      ? list.map((thing, i) =>
          h(
            "a",
            { key: i, class: "att-row", href: deviceHref(thing.device) },
            h(ThingTile, { thing, size: 17 }),
            h("span", { class: "rt" }, h("b", null, thing.title), h("span", null, thingHint(thing))),
            thing.time ? h("span", { class: "t" }, thing.time) : null
          )
        )
      : h("p", { class: "att-none" }, ov && ov.checked ? "Nothing needs attention." : "Nothing found yet. Run a health check to be sure."),
    h(
      "div",
      { class: "fine" },
      ov && ov.checked && ov.fine > 0
        ? [h("span", { class: "fine-ic" }, icon("check", 16, 2.4)), ov.fine + plural(ov.fine, " device looks fine", " devices look fine")]
        : h("span", null, fineFallback(ov)),
      h("a", { href: "#/security" }, "Open security")
    )
  );
}

function fineFallback(ov) {
  if (!ov || !ov.checked) return "Not checked yet";
  if (ov.unchecked > 0) return ov.unchecked + " not checked yet";
  return ov.checked_at ? "Checked at " + ov.checked_at : "Checked";
}

function TrafficDesk({ status }) {
  return h(
    "section",
    { class: "card traffic-d span-6", "aria-labelledby": "traffic-d" },
    h("div", { class: "card-head" }, h("h2", { id: "traffic-d" }, "Traffic"), h("span", { class: "t r" }, "Last minute")),
    h(Rates, { status }),
    h(TrafficChart, { history: (status.traffic || {}).history, p: "d", height: 150 })
  );
}

function ModeDesk({ status }) {
  const rows = modeRows(status.mode || {});
  const last = rows.pop();
  return h(
    "section",
    { class: "card col span-3", "aria-labelledby": "mode-d" },
    h("div", { class: "card-head" }, h("h2", { id: "mode-d" }, "Mode"), h("a", { class: "r", href: "#/modes" }, "Change")),
    rows.map(([key, value, tone]) => h("div", { class: "kv" }, h("span", null, key), h("b", { class: tone }, value))),
    h("div", { class: "kv kv-end" }, h("span", null, last[0]), h("b", null, last[1]))
  );
}

function WatchDesk({ status }) {
  const watch = status.watch || {};
  const feed = (watch.feed || []).slice(0, 3);
  return h(
    "section",
    { class: "card col span-3", "aria-labelledby": "watch-d" },
    h("div", { class: "card-head" }, h("h2", { id: "watch-d" }, "Watch"), h("span", { class: "r" }, h(LiveTag, { on: watch.on }))),
    feed.length
      ? h("div", { class: "feed" }, feed.map((line) => [h("span", { class: "t" }, line.time), h("span", null, line.text)]))
      : h("p", { class: "feed-none" }, watchEmpty(watch)),
    h("div", { class: "kv kv-end" }, h("span", null, "Trap"), h("b", null, trapWords(watch)))
  );
}

const PILL = { fix: "p-bad", look: "p-warn" };

function pillOf(device) {
  if (PILL[device.tier]) return PILL[device.tier];
  if (device.tag === "New") return "p-new";
  return "p-ok";
}

function DevicesDesk({ ov }) {
  const list = ((ov && ov.devices) || []).slice(0, 6);
  return h(
    "section",
    { class: "card span-12", "aria-labelledby": "devices-d" },
    h(
      "div",
      { class: "card-head" },
      h("h2", { id: "devices-d" }, "Devices"),
      h("span", { class: "t" }, String(ov ? ov.total : 0)),
      h("a", { class: "r", href: "#/devices" }, "See all")
    ),
    list.length
      ? h(
          "table",
          { class: "dev-table" },
          h(
            "thead",
            null,
            h("tr", null, ["Name", "Address", "Open services", "Seen", "Status"].map((name) => h("th", { scope: "col" }, name)))
          ),
          h(
            "tbody",
            null,
            list.map((device) =>
              h(
                "tr",
                { key: device.id },
                h("td", { class: "dev-name" }, h("a", { href: deviceHref(device.id) }, device.name)),
                h("td", { class: "mono" }, device.address || "--"),
                h("td", { class: device.checked ? "" : "muted" }, device.services),
                h("td", null, device.seen),
                h("td", null, h("span", { class: "pill " + pillOf(device) }, device.tag))
              )
            )
          )
        )
      : h("p", { class: "card-body" }, "No devices seen yet. Discover or a health check on the Cardputer finds them.")
  );
}

export function HomeDesk({ status, head }) {
  const ov = status && status.overview;
  if (!status) return h("div", { class: "desk-only" }, head);
  return h(
    "div",
    { class: "desk-only" },
    head,
    h(
      "div",
      { class: "grid" },
      h(HeroDesk, { status, ov }),
      h(AttentionDesk, { ov }),
      h(TrafficDesk, { status }),
      h(ModeDesk, { status }),
      h(WatchDesk, { status }),
      h(DevicesDesk, { ov })
    )
  );
}

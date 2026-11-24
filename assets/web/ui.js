// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// Small helpers every part of the dashboard uses: the line icons, and the
// requests to the Cardputer. Everything comes from the Cardputer itself.

import { h } from "./preact.module.js";

// Line icons on a 24-unit grid, drawn with a 2 px round stroke in the
// current text colour. Each is a list of [element, attributes].
const ICONS = {
  home: [["path", { d: "M3.5 10.5L12 3.5l8.5 7V19a1.5 1.5 0 0 1-1.5 1.5h-4v-6h-6v6H5A1.5 1.5 0 0 1 3.5 19z" }]],
  devices: [
    ["rect", { x: 2.5, y: 5, width: 13, height: 10, rx: 2 }],
    ["path", { d: "M7 19h4" }],
    ["rect", { x: 17.5, y: 8, width: 4, height: 11, rx: 1.2 }],
  ],
  security: [["path", { d: "M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6z" }]],
  tools: [
    ["path", { d: "M4 7h10M18 7h2M4 17h4M12 17h8" }],
    ["circle", { cx: 16, cy: 7, r: 2 }],
    ["circle", { cx: 10, cy: 17, r: 2 }],
  ],
  modes: [
    ["path", { d: "M5 12.5a7 7 0 0 1 14 0M8.5 12.5a3.5 3.5 0 0 1 7 0" }],
    ["circle", { cx: 12, cy: 17, r: 1.6 }],
  ],
  watch: [
    ["path", { d: "M2.5 12s3.5-6.5 9.5-6.5 9.5 6.5 9.5 6.5-3.5 6.5-9.5 6.5S2.5 12 2.5 12z" }],
    ["circle", { cx: 12, cy: 12, r: 3 }],
  ],
  map: [
    ["circle", { cx: 12, cy: 12, r: 2.5 }],
    ["circle", { cx: 4.5, cy: 6, r: 1.8 }],
    ["circle", { cx: 19.5, cy: 6, r: 1.8 }],
    ["circle", { cx: 19, cy: 19, r: 1.8 }],
    ["path", { d: "M6 7.2l4.2 3.3M18 7.2l-4.2 3.3M17.6 17.6l-3.8-3.8" }],
  ],
  reports: [
    ["path", { d: "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z" }],
    ["path", { d: "M14 3v5h5M9 13h6M9 17h4" }],
  ],
  screen: [
    ["rect", { x: 3, y: 4, width: 18, height: 12, rx: 2 }],
    ["path", { d: "M9 20h6M12 16v4" }],
  ],
  search: [
    ["circle", { cx: 11, cy: 11, r: 7 }],
    ["path", { d: "M20 20l-3.5-3.5" }],
  ],
  chevron: [["path", { d: "M9 6l6 6-6 6" }]],
  help: [
    ["circle", { cx: 12, cy: 12, r: 9 }],
    ["path", { d: "M9.6 9.3a2.5 2.5 0 0 1 4.8.9c0 1.7-2.4 2.1-2.4 3.6M12 17h.01" }],
  ],
  lock: [
    ["rect", { x: 5, y: 11, width: 14, height: 9, rx: 2 }],
    ["path", { d: "M8 11V8a4 4 0 0 1 8 0v3" }],
  ],
  unlock: [
    ["rect", { x: 5, y: 11, width: 14, height: 9, rx: 2 }],
    ["path", { d: "M8 11V8a4 4 0 0 1 7.5-2" }],
  ],
  alert: [["path", { d: "M12 3l8 3v6c0 5-3.5 8-8 9-4.5-1-8-4-8-9V6zM12 8v5M12 16.5v.01" }]],
  display: [
    ["rect", { x: 3, y: 4, width: 18, height: 12, rx: 2 }],
    ["path", { d: "M9 20h6" }],
  ],
  wifi: [
    ["path", { d: "M2.5 9a14 14 0 0 1 19 0M6 12.5a9 9 0 0 1 12 0M9.5 16a4 4 0 0 1 5 0" }],
    ["circle", { cx: 12, cy: 19.2, r: 0.9 }],
  ],
  cable: [["path", { d: "M9 3v4M15 3v4M7 7h10v4a5 5 0 0 1-10 0zM12 16v5" }]],
  check: [["path", { d: "M5 12.5l4.5 4.5L19 7.5" }]],
  wrench: [["path", { d: "M14.7 6.3a4 4 0 0 0-5.4 5.4L3.5 17.5l3 3 5.8-5.8a4 4 0 0 0 5.4-5.4l-2.6 2.6-2.4-.6-.6-2.4z" }]],
  refresh: [["path", { d: "M20 12a8 8 0 1 1-2.3-5.6M20 4v4.5h-4.5" }]],
};

export function icon(name, size = 16, stroke = 2) {
  const parts = ICONS[name] || [];
  return h(
    "svg",
    {
      class: "ic",
      width: size,
      height: size,
      viewBox: "0 0 24 24",
      fill: "none",
      stroke: "currentColor",
      "stroke-width": stroke,
      "stroke-linecap": "round",
      "stroke-linejoin": "round",
      "aria-hidden": "true",
      focusable: "false",
    },
    parts.map(([tag, attrs]) => h(tag, attrs))
  );
}

// The LokiCTL mark, in the current text colour (dark on the orange tile).
export function logo(size = 18) {
  return h(
    "svg",
    { width: size, height: size, viewBox: "0 0 28 28", "aria-hidden": "true", focusable: "false" },
    h(
      "g",
      { fill: "none", stroke: "currentColor", "stroke-width": 2.6, "stroke-linecap": "round" },
      h("circle", { cx: 8, cy: 7.5, r: 2.6, fill: "currentColor" }),
      h("circle", { cx: 8, cy: 20, r: 2.8 }),
      h("circle", { cx: 20.5, cy: 20, r: 2.4, fill: "currentColor" }),
      h("path", { d: "M8 10.5v6.5M11 20h7" })
    )
  );
}

// ---------- Requests ----------

// A Cardputer that is off or out of reach never answers: give up after 5 s.
function limit() {
  return typeof AbortSignal !== "undefined" && AbortSignal.timeout ? AbortSignal.timeout(5000) : undefined;
}

// The session's request token (GET /api/v1/session). Every state-changing
// request sends it in this header; the session cookie itself stays out of
// reach of scripts.
const TOKEN_HEADER = "X-LokiCTL-Token";
let token = "";

export function setToken(value) {
  token = typeof value === "string" ? value : "";
}

// A GET that answers {status, data}: data is the parsed JSON of a 200
// answer, else null. A network failure or timeout throws.
export async function getJson(path) {
  const r = await fetch(path, { cache: "no-store", credentials: "same-origin", signal: limit() });
  let data = null;
  if (r.status === 200) {
    data = await r.json();
  }
  return { status: r.status, data };
}

// A state-changing request: always POST, form-encoded, with the request
// token. Answers the HTTP status; a network failure or timeout throws.
export async function post(path, fields) {
  const body = new URLSearchParams(fields).toString();
  const headers = { "Content-Type": "application/x-www-form-urlencoded" };
  if (token) {
    headers[TOKEN_HEADER] = token;
  }
  const r = await fetch(path, {
    method: "POST",
    headers,
    body,
    cache: "no-store",
    credentials: "same-origin",
    signal: limit(),
  });
  return r.status;
}

// ---------- Plain words for the status ----------

export function networkName(status) {
  const network = status && status.network;
  if (network && network.name) return network.name;
  if (network && network.link === "cable") return "Cable network";
  return "Your network";
}

export function modeLine(status) {
  const mode = status && status.mode;
  if (!mode) return "";
  let text = mode.name || "";
  if (mode.vpn === "on" && mode.id !== "vpn") text += " + VPN";
  if (mode.kill_switch) text += " · kill switch on";
  else if (mode.hotspot) text += " · hotspot on";
  return text;
}

// The internet in plain words, and the status colour that goes with them.
export function internetWords(status) {
  switch (status && status.network && status.network.internet) {
    case "full":
      return ["Works", "ok"];
    case "limited":
      return ["Only your network", "warn"];
    case "portal":
      return ["Wants you to sign in", "warn"];
    case "none":
      return ["No internet", "bad"];
    default:
      return ["Not known yet", ""];
  }
}

export function batteryWords(status) {
  const cardputer = status && status.cardputer;
  if (!cardputer || typeof cardputer.battery !== "number") return "Not known";
  return cardputer.battery + "%" + (cardputer.charging ? ", charging" : "");
}

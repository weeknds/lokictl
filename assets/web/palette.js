// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The command palette: ⌘K (Ctrl+K off Apple devices) or the top bar's
// search field on a computer, the search button on a phone. One field
// finds pages, tools, devices and actions, and takes the Cardputer's own
// `:` commands (`ping 9.9.9.9`, `mode router`, `health`, ...).
//
// It sends nothing itself. An item opens its page; an action is handed to
// the page that owns its button (askPage(), askCheck()), which acts as on
// a press of that button, with the same question first: a health check,
// a port scan, a mode switch and the VPN always ask there. Arrows move,
// Enter opens, Esc (or Cancel) closes and gives the focus back.

import { h, Component } from "./preact.module.js";
import { icon, askPage, askCheck, isApple } from "./ui.js";
import { TOOLS, targetError, cleanTarget, isIpv4 } from "./tools.js";
import { deviceHref, pictureOf, pillClass } from "./devices.js";

const OPEN_EVENT = "lokictl-palette";
const MAX_DEVICES = 8;
const MAX_PICKS = 6;
const FIELD_ID = "pal-field";
const LIST_ID = "pal-list";

// Opens it; `event` is the click on the button that opened it, which gets
// the focus back on close (Safari doesn't focus a clicked button).
let opener = null;
export function openPalette(event) {
  opener = event && event.currentTarget ? event.currentTarget : null;
  window.dispatchEvent(new Event(OPEN_EVENT));
}

// The keys that open it, as this computer names them.
export function paletteKeys() {
  return isApple() ? "⌘K" : "Ctrl K";
}

// The words each page and tool also answers to: the Cardputer's `:`
// commands and what people may call them.
const PAGE_WORDS = {
  home: "home overview start network",
  devices: "devices discover list",
  security: "security findings holes fix health",
  watch: "watch trap alerts",
  map: "map",
  modes: "modes mode vpn hotspot client router share laptop link leak",
  tools: "tools",
  reports: "reports report download",
  screen: "screen cardputer remote keys view",
};
const TOOL_WORDS = {
  ping: "ping",
  dns: "dns lookup name",
  trace: "trace traceroute route",
  ports: "ports scan open services",
  speed: "speed internet test",
  lan: "lan iperf speed",
  survey: "survey wifi channels stick",
};

// The Cardputer's own `:` command for a page, so the item it means leads.
const PAGE_CMDS = {
  home: ["home"],
  devices: ["discover"],
  watch: ["watch", "trap"],
  map: ["map"],
  modes: ["leak"],
  tools: ["tools"],
  reports: ["report"],
};

// A typed command's verb, and how its item reads.
const TYPED = { ping: "Ping", dns: "Look up", trace: "Trace route to", ports: "Scan ports on" };

const GROUP_ORDER = ["Run", "Pages", "Actions", "Tools", "Devices"];

// ---------- Items ----------

function item(fields) {
  const hay = [fields.title, fields.words || "", fields.hint || ""].join(" ").toLowerCase();
  return Object.assign({ hay, list: hay.split(/[\s·,/()-]+/).filter(Boolean) }, fields);
}

function pageItems(pages) {
  return pages.map((page) =>
    item({
      key: "p-" + page.id,
      group: "Pages",
      title: page.title,
      hint: page.id === "home" ? "Is my network OK?" : "",
      icon: page.icon,
      words: (page.phoneTitle || "") + " " + (PAGE_WORDS[page.id] || ""),
      cmds: PAGE_CMDS[page.id],
      href: page.id === "home" ? "#/" : "#/" + page.id,
    })
  );
}

function toolItems() {
  return TOOLS.map((tool) =>
    item({
      key: "t-" + tool.id,
      group: "Tools",
      title: tool.name,
      hint: tool.hint,
      icon: tool.icon,
      words: TOOL_WORDS[tool.id] || "",
      cmds: [tool.id],
      href: "#/tools/" + tool.id,
    })
  );
}

// The actions behind a page's button. Each one opens that page's question.
function actionItems(status) {
  const items = [
    item({
      key: "a-check",
      group: "Actions",
      title: "Check my network",
      hint: "Health check, asks first",
      icon: "security",
      words: "health check scan run",
      cmds: ["health"],
      href: "#/security",
      go: askCheck,
    }),
  ];
  const mode = status && status.mode;
  if (!mode) return items;
  const on = mode.id === "vpn" ? "client" : mode.id;
  for (const [id, name] of [
    ["client", "Client"],
    ["router", "Router"],
    ["share", "Share"],
    ["laptop", "Laptop link"],
  ]) {
    if (id === on) continue;
    items.push(
      item({
        key: "a-mode-" + id,
        group: "Actions",
        title: id === "client" ? "Back to Client" : "Switch to " + name,
        hint: "Mode, asks first",
        icon: "modes",
        words: "mode switch " + id,
        cmds: ["mode " + id],
        href: "#/modes",
        ask: { page: "modes", kind: "mode", mode: id },
      })
    );
  }
  if (mode.vpn_active || mode.gateway_down) {
    items.push(
      item({
        key: "a-vpn-off",
        group: "Actions",
        title: "Turn the VPN off",
        hint: "Asks first",
        icon: "vpn",
        words: "vpn off mode",
        cmds: ["vpn off"],
        href: "#/modes",
        ask: { page: "modes", kind: "vpn-off" },
      })
    );
  } else if ((mode.vpn_files || []).length) {
    items.push(
      item({
        key: "a-vpn-on",
        group: "Actions",
        title: "Turn the VPN on",
        hint: "Asks first",
        icon: "vpn",
        words: "vpn on mode",
        cmds: ["vpn on", "mode vpn"],
        href: "#/modes",
        ask: { page: "modes", kind: "vpn-on" },
      })
    );
  }
  return items;
}

function devicesOf(status) {
  const ov = status && status.overview;
  return (ov && ov.devices) || [];
}

function deviceItems(status) {
  return devicesOf(status).map((device) =>
    item({
      key: "d-" + device.id,
      group: "Devices",
      title: device.name || device.address || device.id,
      hint: device.address || "",
      mono: true,
      icon: pictureOf(device),
      words: [device.services, device.kind, device.hotspot ? "hotspot" : ""].join(" "),
      href: deviceHref(device.id),
      tag: device.tag,
      pill: pillClass(device.tier, device.tag),
      worst: device.tier === "fix" ? 0 : device.tier === "look" ? 1 : 2,
    })
  );
}

// `ping 9.9.9.9`, `dns example.com`, `trace router`, `ports nas`: the
// command with its target, then other targets it could take (the router,
// the presets, the devices), as TAB offers them on the Cardputer. A
// target with spaces can only be a device's name.
function typedItems(query, status) {
  const match = /^(ping|dns|trace|ports)\s+(.*)$/i.exec(query.replace(/^\s+/, ""));
  if (!match) return [];
  const tool = match[1].toLowerCase();
  const typed = match[2].trim().toLowerCase();
  const tools = (status && status.tools) || {};
  const own = status && status.cardputer && status.cardputer.address;
  const takesAddress = tool !== "dns";
  const choices = [];
  if (takesAddress && tools.router) choices.push({ word: "router", name: "the router", target: tools.router });
  if (takesAddress && own) choices.push({ word: "this", name: "this Cardputer", target: own });
  if (tool === "ping" || tool === "trace" || tool === "dns") {
    const presets = (tools.presets && tools.presets[tool === "dns" ? "dns" : "ping"]) || [];
    presets.forEach((p) => choices.push({ word: p, name: "", target: p }));
  }
  if (takesAddress) {
    devicesOf(status).forEach((d) => {
      if (d.address && isIpv4(d.address)) choices.push({ word: (d.name || "").toLowerCase(), name: d.name, target: d.address });
    });
  }
  const runItem = (target, name) => {
    const clean = cleanTarget(target);
    const why = targetError(tool, clean, tools.max || 63);
    return item({
      key: "r-" + tool + "-" + clean,
      group: "Run",
      title: TYPED[tool] + " " + (name || clean),
      hint: (name ? clean : "") + (tool === "ports" ? (name ? ", asks first" : "Asks first") : ""),
      mono: !!name,
      titleMono: !name,
      icon: TOOLS.find((t) => t.id === tool).icon,
      href: "#/tools/" + tool,
      ask: { page: "tools", sub: tool, target: clean, run: true },
      why: why ? why : "",
    });
  };
  const items = [];
  const seen = new Set();
  if (typed) {
    // A word or a device's name stands for its address, as on the Cardputer.
    const exact = isIpv4(typed) ? null : choices.find((c) => c.word === typed && c.name);
    const target = exact ? exact.target : typed;
    items.push(runItem(target, exact ? exact.name : ""));
    seen.add(cleanTarget(target));
  }
  for (const c of choices) {
    if (items.length > MAX_PICKS) break;
    if (seen.has(c.target)) continue;
    if (typed && !c.word.startsWith(typed) && !c.target.startsWith(typed)) continue;
    if (tool === "ports" && targetError("ports", c.target, 63)) continue;
    seen.add(c.target);
    items.push(runItem(c.target, c.name));
  }
  return items;
}

// ---------- Finding ----------

// A word's score in an item: the start of the title, the start of one of
// its words, inside its text, or its letters in order in the title.
function wordScore(word, it) {
  const title = it.title.toLowerCase();
  if (title.startsWith(word)) return 4;
  if (it.list.some((w) => w.startsWith(word))) return 3;
  if (it.hay.includes(word)) return 2;
  if (word.length > 2) {
    let at = 0;
    for (const c of title) {
      if (c === word[at]) at += 1;
      if (at === word.length) return 1;
    }
  }
  return 0;
}

// Every word typed must fit somewhere.
function score(query, it) {
  const words = query.toLowerCase().split(/\s+/).filter(Boolean);
  let total = 0;
  for (const word of words) {
    const s = wordScore(word, it);
    if (!s) return 0;
    total += s;
  }
  const q = words.join(" ");
  if (it.title.toLowerCase() === q) total += 4;
  // The start of the Cardputer's own command for it: `mode sh`, `health`.
  if (q.length > 1 && it.cmds && it.cmds.some((cmd) => cmd.startsWith(q))) total += 5;
  return total;
}

// The groups to show for `query`, each {label, items}.
function results(query, status, pages) {
  const devices = deviceItems(status);
  const q = query.trim();
  if (!q) {
    const worst = devices.slice().sort((a, b) => a.worst - b.worst).slice(0, 5);
    return [
      { label: "Pages", items: pageItems(pages) },
      { label: "Actions", items: actionItems(status) },
      { label: "Devices", items: worst },
      { label: "Tools", items: toolItems() },
    ].filter((g) => g.items.length);
  }
  const typed = typedItems(query, status);
  const all = pageItems(pages).concat(actionItems(status), toolItems(), devices);
  const scored = all.map((it, index) => ({ it, s: score(q, it), index })).filter((x) => x.s > 0);
  const groups = [];
  if (typed.length) groups.push({ label: "Run", items: typed, top: Infinity });
  for (const label of GROUP_ORDER) {
    if (label === "Run") continue;
    const mine = scored.filter((x) => x.it.group === label).sort((a, b) => b.s - a.s || a.index - b.index);
    if (!mine.length) continue;
    const items = mine.map((x) => x.it).slice(0, label === "Devices" ? MAX_DEVICES : undefined);
    groups.push({ label, items, top: mine[0].s });
  }
  // The best match leads; the order above breaks ties.
  return groups
    .map((g, i) => ({ g, i }))
    .sort((a, b) => b.g.top - a.g.top || a.i - b.i)
    .map((x) => x.g);
}

// ---------- The palette ----------

export class Palette extends Component {
  constructor() {
    super();
    this.state = { open: false, query: "", active: 0, note: "" };
    this.opener = null;
    this.field = null;
    this.closer = null;
    this.flat = [];
    this.activeIndex = 0;
    this.onOpen = () => this.open();
    // ⌘K / Ctrl+K anywhere, also to close it again.
    this.onGlobalKey = (event) => {
      const k = (event.key || "").toLowerCase();
      if (k === "k" && (event.metaKey || event.ctrlKey) && !event.altKey && !event.shiftKey) {
        event.preventDefault();
        if (this.state.open) this.close(true);
        else this.open();
      }
    };
    this.onInput = (event) => this.setState({ query: event.target.value, active: 0, note: "" });
    this.onKey = (event) => this.key(event);
    this.onCancel = () => this.close(true);
    this.onBack = (event) => {
      if (event.target === event.currentTarget) this.close(true);
    };
    this.keepFocus = (event) => event.preventDefault();
  }

  componentDidMount() {
    window.addEventListener(OPEN_EVENT, this.onOpen);
    document.addEventListener("keydown", this.onGlobalKey);
  }

  componentWillUnmount() {
    window.removeEventListener(OPEN_EVENT, this.onOpen);
    document.removeEventListener("keydown", this.onGlobalKey);
    this.shut(false);
  }

  componentDidUpdate(prevProps, prevState) {
    if (this.state.open && !prevState.open) {
      this.setInert(true);
      if (this.field) this.field.focus();
    }
    if (this.state.open && (this.state.active !== prevState.active || this.state.query !== prevState.query)) {
      const option = document.getElementById("pal-o-" + this.activeIndex);
      if (option && option.scrollIntoView) option.scrollIntoView({ block: "nearest" });
    }
  }

  // Everything else on the page can't be reached while it is open.
  setInert(on) {
    const shell = this.base && this.base.parentNode;
    const parts = shell ? shell.children : document.querySelectorAll(".shell > *");
    for (const part of parts) {
      if (part === this.base) continue;
      if (on) part.setAttribute("inert", "");
      else part.removeAttribute("inert");
    }
    document.documentElement.classList.toggle("pal-open", on);
  }

  open() {
    if (this.state.open) return;
    this.opener = opener || document.activeElement;
    opener = null;
    this.setState({ open: true, query: "", active: 0, note: "" });
  }

  shut(restore) {
    if (!this.state.open) return;
    this.setInert(false);
    const opener = this.opener;
    this.opener = null;
    if (restore && opener && opener.isConnected && opener.focus) opener.focus();
  }

  close(restore) {
    this.shut(restore);
    this.setState({ open: false });
  }

  // Open the item's page, after leaving its request for that page.
  pick(it) {
    if (!it) return;
    if (it.why) {
      this.setState({ note: it.why + "." });
      return;
    }
    this.close(false);
    if (it.go) it.go();
    if (it.ask) askPage(it.ask);
    if (location.hash !== it.href) location.hash = it.href;
    // The page's question takes the focus when there is one; otherwise
    // the page's contents do.
    setTimeout(() => {
      const now = document.activeElement;
      if (!now || now === document.body) {
        const content = document.getElementById("content");
        if (content) content.focus({ preventScroll: true });
      }
    }, 80);
  }

  key(event) {
    const count = this.flat.length;
    const active = this.activeIndex;
    const k = event.key;
    if (event.isComposing) return;
    // Arrows and Enter act on the list only from the field; Enter on the
    // close button closes.
    const inField = event.target === this.field;
    if (inField && (k === "ArrowDown" || (event.ctrlKey && k === "n"))) {
      event.preventDefault();
      if (count) this.setState({ active: (active + 1) % count });
    } else if (inField && (k === "ArrowUp" || (event.ctrlKey && k === "p"))) {
      event.preventDefault();
      if (count) this.setState({ active: (active - 1 + count) % count });
    } else if (inField && k === "Enter") {
      event.preventDefault();
      this.pick(this.flat[active]);
    } else if (k === "Escape") {
      event.preventDefault();
      event.stopPropagation();
      this.close(true);
    } else if (k === "Tab") {
      // The field and the close button: Tab goes round between them.
      event.preventDefault();
      const next = document.activeElement === this.field ? this.closer : this.field;
      if (next) next.focus();
    }
  }

  option(it, index) {
    const on = index === this.activeIndex;
    return h(
      "div",
      {
        id: "pal-o-" + index,
        key: it.key,
        role: "option",
        "aria-selected": on ? "true" : "false",
        "aria-disabled": it.why ? "true" : undefined,
        class: "pal-opt" + (on ? " on" : ""),
        onMouseDown: this.keepFocus,
        onMouseMove: () => {
          if (this.activeIndex !== index) this.setState({ active: index });
        },
        onClick: () => this.pick(it),
      },
      h("span", { class: "pal-tile" }, icon(it.icon, 16, 2)),
      h(
        "span",
        { class: "pal-text" },
        h("b", { class: it.titleMono ? "mono" : "" }, it.title),
        it.why
          ? h("span", { class: "pal-why" }, it.why)
          : it.hint
            ? h("span", { class: it.mono ? "mono" : "" }, it.hint)
            : null
      ),
      it.tag ? h("span", { class: "pill " + (it.pill || "p-know") }, it.tag) : null,
      h("span", { class: "pal-enter", "aria-hidden": "true" }, icon("enter", 15, 2))
    );
  }

  render({ status, pages }, { open, query, note }) {
    if (!open) return null;
    const groups = results(query, status, pages || []);
    this.flat = [];
    groups.forEach((g) => g.items.forEach((it) => this.flat.push(it)));
    const active = Math.min(this.state.active, Math.max(0, this.flat.length - 1));
    this.activeIndex = active;
    let index = 0;
    const count = this.flat.length;
    const said = query.trim() ? (count ? count + (count === 1 ? " result" : " results") : "Nothing found") : "";
    const label = "Find a device or run a command";
    return h(
      "div",
      { class: "pal-back", onMouseDown: this.onBack },
      h(
        "div",
        { class: "pal", role: "dialog", "aria-modal": "true", "aria-label": label, onKeyDown: this.onKey },
        h(
          "div",
          { class: "pal-head" },
          icon("search", 18, 2.2),
          h("input", {
            id: FIELD_ID,
            class: "pal-field",
            type: "text",
            role: "combobox",
            "aria-expanded": count ? "true" : "false",
            "aria-controls": LIST_ID,
            "aria-autocomplete": "list",
            "aria-activedescendant": count ? "pal-o-" + active : undefined,
            "aria-label": label,
            placeholder: label,
            value: query,
            autoComplete: "off",
            autoCapitalize: "off",
            autoCorrect: "off",
            spellCheck: false,
            enterKeyHint: "go",
            maxLength: 80,
            onInput: this.onInput,
            ref: (el) => (this.field = el),
          }),
          h(
            "button",
            { type: "button", class: "pal-close", "aria-label": "Close", onClick: this.onCancel, ref: (el) => (this.closer = el) },
            h("span", { class: "pal-cancel", "aria-hidden": "true" }, "Cancel"),
            h("span", { class: "kbd pal-esc", "aria-hidden": "true" }, "esc")
          )
        ),
        note ? h("p", { class: "pal-note", role: "alert" }, note) : null,
        h(
          "div",
          { id: LIST_ID, class: "pal-list", role: "listbox", "aria-label": "Results" },
          groups.map((g, gi) =>
            h(
              "div",
              { role: "group", "aria-labelledby": "pal-g-" + gi, key: g.label },
              h("div", { class: "pal-sec", id: "pal-g-" + gi, role: "presentation" }, g.label),
              g.items.map((it) => this.option(it, index++))
            )
          )
        ),
        count ? null : h("p", { class: "pal-none" }, "Nothing found. Try a device's name, a page, or ", h("span", { class: "mono" }, "ping 9.9.9.9"), "."),
        h("p", { class: "sr", role: "status" }, said),
        h(
          "div",
          { class: "pal-foot", "aria-hidden": "true" },
          h("span", null, h("span", { class: "kbd" }, "↑"), h("span", { class: "kbd" }, "↓"), " move"),
          h("span", null, h("span", { class: "kbd" }, "↵"), " open"),
          h("span", null, h("span", { class: "kbd" }, "esc"), " close"),
          h("span", { class: "pal-foot-end" }, "Commands as on the Cardputer: ", h("span", { class: "mono" }, "ping 9.9.9.9"))
        )
      )
    );
  }
}

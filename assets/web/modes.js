// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Modes page: Client, Router or Share, the hotspot (its QR code, name
// and password), Laptop link (a laptop on the Cardputer's cable port), the
// VPN (on or off, its file, the kill switch, the leak test, adding a
// WireGuard file) and whether the mode stays after LokiCTL quits. Every control asks the Cardputer, which applies it the way its own
// Modes, Hotspot and Leak pages do, with the same rules; the page then
// follows the status. It asks before each switch and before a new password.

import { h, Component } from "./preact.module.js";
import { icon, post, postFile, getJson, refreshSoon, ASK_EVENT, takePageAsk } from "./ui.js";

// How long the page waits for the status to show a request's result.
const WAIT_MS = 10000;
const FILE_MAX = 16 * 1024;
const NO_ANSWER = "No answer from the Cardputer. Try again.";
const REFUSED = {
  401: "Your session ended. Pair again to do this.",
  403: "The Cardputer refused this. Reload the page and try again.",
  429: "Too many requests at once. Wait a moment.",
};

const TILES = [
  { id: "client", name: "Client", hint: "Joins Wi-Fi" },
  { id: "router", name: "Router", hint: "Shares cable" },
  { id: "share", name: "Share", hint: "Wi-Fi + hotspot" },
];

// Every mode the page can ask for, by its id.
const NAMES = { client: "Client", router: "Router", share: "Share", laptop: "Laptop link" };

const LEAK_DOT = { pass: "ok", fail: "bad", unknown: "warn" };

// The tile that is on: the VPN alone is Client with the tunnel. With
// Laptop link on, no tile is.
function tileOn(mode) {
  return mode.id === "vpn" ? "client" : mode.id;
}

// True when this browser reached the Cardputer through its own hotspot, or
// through the cable while Laptop link runs: closing or restarting that
// side then ends this page's session.
function viaHotspot(status) {
  const own = status.cardputer && status.cardputer.address;
  const mode = status.mode || {};
  return !status.sample && (!!mode.hotspot || mode.id === "laptop") && !!own && location.hostname !== own;
}

// Where such a browser is connected, in the page's words.
function sideWord(status) {
  return status.mode && status.mode.id === "laptop" ? "the cable" : "the hotspot";
}

// "home" or "home-vpn" from "home-vpn.conf": the name the Cardputer saves
// the file under (letters, digits, '.', '_' and '-', at most 32).
function uploadName(fileName) {
  const name = String(fileName || "")
    .replace(/\.conf$/i, "")
    .replace(/[^A-Za-z0-9._-]+/g, "-")
    .replace(/^[.-]+/, "")
    .slice(0, 32);
  return name || "vpn";
}

function switchKey(mode) {
  const change = mode.switch || {};
  return change.state + "/" + change.target + "/" + change.error;
}

// ---------- Small parts ----------

function Switch({ on, label, disabled, onClick }) {
  return h("button", {
    class: on ? "sw on" : "sw",
    type: "button",
    role: "switch",
    "aria-checked": on ? "true" : "false",
    "aria-label": label,
    disabled,
    onClick,
  });
}

// The QR code from the Cardputer's own encoder: one row of hex digits a
// line, the first module in the highest bit. Dark runs become one path.
function Qr({ qr }) {
  if (!qr || !qr.size || !Array.isArray(qr.rows)) return null;
  const size = qr.size;
  let d = "";
  qr.rows.forEach((row, y) => {
    let x = 0;
    while (x < size) {
      const dark = (x) => (parseInt(row[x >> 2], 16) >> (3 - (x & 3))) & 1;
      if (!dark(x)) {
        x++;
        continue;
      }
      let run = 1;
      while (x + run < size && dark(x + run)) run++;
      d += "M" + x + " " + y + "h" + run + "v1h-" + run + "z";
      x += run;
    }
  });
  // The quiet zone the QR code standard asks for: 4 modules.
  const margin = 4;
  return h(
    "svg",
    {
      class: "qr",
      viewBox: -margin + " " + -margin + " " + (size + 2 * margin) + " " + (size + 2 * margin),
      role: "img",
      "aria-label": "QR code to join the hotspot",
      "shape-rendering": "crispEdges",
    },
    h("path", { d })
  );
}

function Message({ msg, where }) {
  if (!msg || msg.where !== where) return null;
  return h("p", { class: "mode-line " + (msg.tone || ""), role: "status" }, msg.text);
}

// The question before a switch or a new password: what will happen, then
// the one orange action and Cancel.
function Ask({ ask, status, sending, onYes, onNo }) {
  const mode = status.mode || {};
  const name = mode.hotspot_name || mode.next_hotspot_name || "LokiCTL";
  let title = "";
  let text = "";
  let yes = "";
  let losesPage = false;
  if (ask.kind === "mode") {
    if (ask.mode === "client") {
      title = "Back to Client?";
      text = mode.hotspot
        ? "The hotspot closes and the devices on it lose the internet. The Cardputer joins Wi-Fi as usual."
        : mode.id === "laptop"
        ? "The laptop link stops: the laptop loses the Cardputer on the cable. Then you can plug the cable back into your router."
        : "The Cardputer joins Wi-Fi as usual.";
      if (mode.vpn_active) text += " The VPN goes off too.";
      losesPage = mode.hotspot || mode.id === "laptop";
    } else if (ask.mode === "laptop") {
      title = "Switch to Laptop link?";
      text =
        "The laptop plugged into the Cardputer's cable port gets an address and can use the Cardputer's Wi-Fi for the internet. On the laptop, open " +
        (mode.laptop_address || "10.42.0.1") +
        ":8780 in a browser.";
    } else if (ask.mode === "router") {
      title = "Switch to Router?";
      text =
        "The Cardputer shares its cable's internet as a Wi-Fi hotspot called " + name + ". Its own Wi-Fi leaves your network while it runs.";
    } else {
      title = "Switch to Share?";
      text = "The Cardputer stays on your Wi-Fi and shares it as a hotspot called " + name + ".";
    }
    yes = "Switch";
  } else if (ask.kind === "vpn-on") {
    title = "Turn the VPN on?";
    text = mode.vpn_file
      ? "The Cardputer's traffic goes through the VPN from the file " + mode.vpn_file + "."
      : "The Cardputer's traffic goes through the VPN.";
    if (mode.hotspot) text += " Hotspot devices go through it too, and the kill switch keeps them offline if it drops.";
    yes = "Turn on";
  } else if (ask.kind === "vpn-off") {
    title = "Turn the VPN off?";
    text = "Traffic goes straight out again, without the VPN.";
    if (mode.kill_switch) text += " Hotspot devices get the internet again without it.";
    yes = "Turn off";
  } else {
    title = "Make a new password?";
    text = "The hotspot starts again with a new random password. Devices on it need the new one to join again.";
    yes = "Make new password";
    losesPage = mode.hotspot;
  }
  return h(
    "section",
    { class: "check-card-p ask-card", "aria-labelledby": "mode-ask" },
    h("h2", { id: "mode-ask", tabIndex: -1 }, title),
    h("p", null, text),
    losesPage && viaHotspot(status)
      ? h(
          "p",
          { class: "check-own" },
          icon("alert", 18, 2),
          h("span", null, "You're connected through " + sideWord(status) + ", so this page will lose the Cardputer. Pair again from your own network.")
        )
      : null,
    h(
      "div",
      { class: "check-acts" },
      h("button", { type: "button", class: "bp or", onClick: onYes, disabled: sending }, sending ? "Asking…" : yes),
      h("button", { type: "button", class: "bp gh", onClick: onNo }, "Cancel")
    )
  );
}

// ---------- The page ----------

export class ModesPage extends Component {
  constructor() {
    super();
    this.state = {
      ask: null, // {kind: "mode" | "vpn-on" | "vpn-off" | "password", mode}
      sending: false,
      want: null, // a request waiting for the status: {kind, value, from, since, where}
      msg: null, // {where, text, tone}
      editing: false,
      draft: "",
      shown: false,
      secret: null, // GET /api/v1/hotspot: {name, password, qr}
      picking: false,
      uploading: false,
    };
    this.timer = 0;
    this.loading = false;
    this.secretKey = "";
    this.seenStatus = null;
    this.onNo = () => this.setState({ ask: null });
    this.onYes = () => this.answer();
    this.onShow = () => {
      if (!this.state.shown && !this.state.secret) this.loadSecret();
      this.setState({ shown: !this.state.shown });
    };
    this.onEdit = () => {
      const mode = this.mode();
      this.setState({ editing: !this.state.editing, draft: mode.hotspot_name || mode.next_hotspot_name || "", msg: null });
    };
    this.onDraft = (event) => this.setState({ draft: event.target.value });
    this.onRename = (event) => {
      event.preventDefault();
      this.rename();
    };
    this.onPassword = () => {
      // The password before the change, to see the new one arrive.
      if (!this.state.secret) this.loadSecret();
      this.openAsk({ kind: "password" });
    };
    this.onFile = (event) => this.upload(event.target);
    this.onKeep = () => {
      const on = !this.mode().keep;
      this.send("/api/v1/keep", { on: on ? "1" : "0" }, { kind: "keep", value: on, where: "keep" });
    };
    this.onVpn = () => {
      const mode = this.mode();
      this.openAsk({ kind: mode.vpn_active || mode.gateway_down ? "vpn-off" : "vpn-on" });
    };
    this.onVpnAgain = () => this.openAsk({ kind: "vpn-on" });
    this.onVpnOff = () => this.openAsk({ kind: "vpn-off" });
    this.onPick = () => this.setState({ picking: !this.state.picking });
    this.onLeak = () => {
      const running = this.mode().leak && this.mode().leak.state === "running";
      this.send("/api/v1/leak", { run: running ? "0" : "1" }, { kind: "leak", where: "vpn" });
    };
    this.onAsk = () => this.takeAsk();
  }

  componentDidMount() {
    window.addEventListener(ASK_EVENT, this.onAsk);
    this.follow();
  }

  componentWillUnmount() {
    clearTimeout(this.timer);
    window.removeEventListener(ASK_EVENT, this.onAsk);
  }

  // A mode tile pressed: the switch's question, or why it can't switch.
  // `said`: say so also when nothing would happen (the palette asked).
  pickMode(id, said) {
    const mode = this.mode();
    const change = mode.switch || {};
    const switching = change.state === "switching";
    const why = (mode.why || {})[id];
    const where = id === "laptop" ? "laptop" : "mode";
    if (tileOn(mode) === id) {
      if (said) this.setState({ msg: { where, text: NAMES[id] + " is on already.", tone: "" } });
      return;
    }
    if (why && !switching) {
      this.setState({ msg: { where, text: why + ".", tone: "warn" } });
      return;
    }
    if (switching || this.state.want) {
      if (said) this.setState({ msg: { where, text: "Wait until the switch is done.", tone: "warn" } });
      return;
    }
    this.openAsk({ kind: "mode", mode: id });
  }

  // A request from the command palette: a mode tile or the VPN switch, as
  // if pressed here, so the same question comes first.
  takeAsk() {
    const status = this.props.status;
    if (!status || !status.mode || this.state.sending) return;
    const ask = takePageAsk("modes");
    if (!ask) return;
    if (ask.kind === "mode" && NAMES[ask.mode]) {
      this.pickMode(ask.mode, true);
      return;
    }
    if (ask.kind !== "vpn-on" && ask.kind !== "vpn-off") return;
    const mode = this.mode();
    const on = !!mode.vpn_active || !!mode.gateway_down;
    const files = mode.vpn_files || [];
    let why = "";
    if ((mode.switch && mode.switch.state === "switching") || this.state.want) why = "Wait until the switch is done.";
    else if (ask.kind === "vpn-on" && on) why = "The VPN is on already.";
    else if (ask.kind === "vpn-off" && !on) why = "The VPN is off already.";
    else if (ask.kind === "vpn-on" && !files.length) why = "Add a VPN file first.";
    else if (ask.kind === "vpn-on" && (mode.why || {}).vpn) why = mode.why.vpn + ".";
    if (why) {
      this.setState({ msg: { where: "vpn", text: why, tone: "warn" } });
      return;
    }
    this.openAsk({ kind: ask.kind });
  }

  componentDidUpdate() {
    this.follow();
  }

  mode() {
    return (this.props.status && this.props.status.mode) || {};
  }

  // The hotspot's secrets only while its code shows (or after Show), asked
  // again when the hotspot or its name changes.
  follow() {
    const status = this.props.status;
    if (!status || !status.mode) return;
    this.takeAsk();
    const mode = status.mode;
    const key = (mode.hotspot ? "on" : "off") + "/" + (mode.hotspot_name || mode.next_hotspot_name) + "/" + switchKey(mode);
    if ((mode.hotspot || this.state.shown) && key !== this.secretKey) {
      this.secretKey = key;
      this.loadSecret();
    }
    const want = this.state.want;
    if (!want) return;
    if (status !== this.seenStatus) {
      this.seenStatus = status;
      // While a request waits, ask for each next status soon, not in 2 s.
      refreshSoon();
      if (want.kind === "password") this.loadSecret();
    }
    if (this.settled(want, mode)) {
      this.setState({ want: null });
    } else if (Date.now() - want.since > WAIT_MS) {
      this.setState({ want: null, msg: { where: want.where, text: this.late(want, mode), tone: "warn" } });
    }
  }

  settled(want, mode) {
    const leak = mode.leak || {};
    switch (want.kind) {
      case "mode":
        return switchKey(mode) !== want.from || tileOn(mode) === want.value;
      case "vpn-on":
        return switchKey(mode) !== want.from || mode.vpn_active;
      case "vpn-off":
        return switchKey(mode) !== want.from || (!mode.vpn_active && !mode.gateway_down);
      case "keep":
        return mode.keep === want.value;
      case "file":
        return mode.vpn_file === want.value;
      case "name":
        return (mode.hotspot_name || mode.next_hotspot_name) !== want.from;
      case "password":
        return !!this.state.secret && this.state.secret.password !== want.from;
      case "leak":
        return leak.state !== want.from;
      default:
        return true;
    }
  }

  late(want, mode) {
    const why = mode.why || {};
    switch (want.kind) {
      case "mode":
      case "vpn-on":
      case "vpn-off": {
        const reason = want.kind === "mode" ? why[want.value] : want.kind === "vpn-on" ? why.vpn : "";
        return "The Cardputer didn't switch." + (reason ? " " + reason + "." : " Try again.");
      }
      case "leak": {
        const reason = mode.leak && mode.leak.why;
        return "The leak test didn't start." + (reason ? " " + reason + "." : " Try again.");
      }
      case "name":
      case "password":
        return "The hotspot didn't change. Try again.";
      default:
        return "Nothing changed. Try again.";
    }
  }

  async loadSecret() {
    if (this.loading) return;
    this.loading = true;
    try {
      const answer = await getJson("/api/v1/hotspot");
      if (answer.status === 200 && answer.data) this.setState({ secret: answer.data });
    } catch (err) {
      // The next change asks again.
    }
    this.loading = false;
  }

  openAsk(ask) {
    this.setState({ ask, msg: null, editing: false }, () => {
      const heading = document.getElementById("mode-ask");
      if (heading) {
        heading.focus();
        if (heading.scrollIntoView) heading.scrollIntoView({ block: "nearest" });
      }
    });
  }

  answer() {
    const ask = this.state.ask;
    const mode = this.mode();
    if (!ask) return;
    if (ask.kind === "mode") {
      const where = ask.mode === "laptop" || (ask.mode === "client" && mode.id === "laptop") ? "laptop" : "mode";
      this.send("/api/v1/mode", { mode: ask.mode }, { kind: "mode", value: ask.mode, from: switchKey(mode), where });
    } else if (ask.kind === "vpn-on" || ask.kind === "vpn-off") {
      const on = ask.kind === "vpn-on";
      this.send("/api/v1/vpn", { on: on ? "1" : "0" }, { kind: ask.kind, from: switchKey(mode), where: "vpn" });
    } else {
      const password = this.state.secret ? this.state.secret.password : "";
      this.send("/api/v1/hotspot", { password: "new" }, { kind: "password", from: password, where: "hotspot" });
    }
  }

  rename() {
    const mode = this.mode();
    const name = this.state.draft.trim();
    const current = mode.hotspot_name || mode.next_hotspot_name || "";
    if (name && !/^[\x20-\x7e]{1,32}$/.test(name)) {
      this.setState({ msg: { where: "hotspot", text: "Use up to 32 plain letters, digits or signs.", tone: "warn" } });
      return;
    }
    if (name === current) {
      this.setState({ editing: false });
      return;
    }
    this.send("/api/v1/hotspot", { name }, { kind: "name", from: current, where: "hotspot" }, { editing: false });
  }

  pick(file) {
    this.setState({ picking: false });
    if (file === this.mode().vpn_file) return;
    this.send("/api/v1/vpn", { file }, { kind: "file", value: file, where: "vpn" });
  }

  // POST, then wait for the status to show the result.
  async send(path, fields, want, extra) {
    if (this.state.sending) return;
    if (want.kind === "leak") want.from = (this.mode().leak || {}).state;
    this.setState({ sending: true, msg: null });
    let code = 0;
    try {
      code = await post(path, fields);
    } catch (err) {
      code = 0;
    }
    if (code !== 204) {
      this.setState({ sending: false, ask: null, msg: { where: want.where, text: REFUSED[code] || NO_ANSWER, tone: "bad" } });
      return;
    }
    this.setState({ sending: false, ask: null, want: Object.assign({ since: Date.now() }, want), ...(extra || {}) });
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.setState({}), WAIT_MS + 100);
    refreshSoon();
  }

  async upload(input) {
    const file = input.files && input.files[0];
    input.value = "";
    if (!file || this.state.uploading) return;
    if (file.size > FILE_MAX) {
      this.setState({ msg: { where: "vpn", text: "This file is too large for a WireGuard file (16 KB at most).", tone: "bad" } });
      return;
    }
    const name = uploadName(file.name);
    this.setState({ uploading: true, msg: { where: "vpn", text: "Adding " + name + "…", tone: "" } });
    let answer = null;
    try {
      const text = await file.text();
      answer = await postFile("/api/v1/vpn/file?name=" + encodeURIComponent(name), text);
    } catch (err) {
      answer = null;
    }
    let msg;
    if (answer && answer.status === 200 && answer.data) {
      const server = answer.data.server ? " (server " + answer.data.server + ")" : "";
      msg = { where: "vpn", text: "Added " + answer.data.name + server + ". Pick it under File, then turn the VPN on.", tone: "ok" };
      refreshSoon();
    } else if (answer && answer.data && answer.data.error) {
      msg = { where: "vpn", text: answer.data.error, tone: "bad" };
    } else if (answer && answer.status === 413) {
      msg = { where: "vpn", text: "This file is too large for a WireGuard file (16 KB at most).", tone: "bad" };
    } else {
      msg = { where: "vpn", text: (answer && REFUSED[answer.status]) || NO_ANSWER, tone: "bad" };
    }
    this.setState({ uploading: false, msg });
  }

  // ---------- Parts of the page ----------

  tiles(mode) {
    const on = tileOn(mode);
    const change = mode.switch || {};
    const switching = change.state === "switching";
    const waiting = this.state.want && this.state.want.kind === "mode" ? this.state.want.value : "";
    const why = mode.why || {};
    let line = null;
    if (change.target === "laptop") {
      // Laptop link says it under its own row.
    } else if (switching) {
      line = h("p", { class: "mode-line", role: "status" }, "Switching…");
    } else if (change.state === "failed" && change.error) {
      line = h("p", { class: "mode-line warn", role: "status" }, change.error);
    }
    return [
      h(
        "div",
        { class: "mode-tiles", role: "radiogroup", "aria-label": "Mode" },
        TILES.map((tile) => {
          const selected = tile.id === on;
          const busy = (switching && change.target === tile.id) || waiting === tile.id;
          const blocked = !selected && !!why[tile.id] && !switching;
          return h(
            "button",
            {
              class: "mode-tile" + (selected ? " on" : "") + (busy ? " busy" : ""),
              type: "button",
              role: "radio",
              "aria-checked": selected ? "true" : "false",
              "aria-disabled": selected || switching || blocked ? "true" : undefined,
              onClick: () => this.pickMode(tile.id, false),
            },
            selected
              ? h(
                  "svg",
                  { class: "mode-mark", width: 20, height: 20, viewBox: "0 0 24 24", "aria-hidden": "true" },
                  h("circle", { cx: 12, cy: 12, r: 10 }),
                  h("path", { d: "M7.5 12.5l3 3 6-6.5" })
                )
              : h("span", { class: "mode-mark" }),
            h("span", null, h("b", null, tile.name), h("small", null, busy ? "Switching…" : tile.hint))
          );
        })
      ),
      line,
      h(Message, { msg: this.state.msg, where: "mode" }),
    ];
  }

  // Laptop link: one row with a switch and a plain sentence. On asks to
  // switch to it, off asks to go back to Client.
  laptop(mode) {
    const on = mode.id === "laptop";
    const change = mode.switch || {};
    const switching = change.state === "switching";
    const waiting = !!this.state.want && this.state.want.kind === "mode" && this.state.want.where === "laptop";
    const why = (mode.why || {}).laptop;
    let value = ["Off", ""];
    if (on) value = ["On", "ok"];
    if ((switching && change.target === "laptop") || waiting) value = ["Switching…", ""];
    const address = mode.laptop_address || "10.42.0.1";
    const canToggle = !switching && !this.state.want && (on || !why);
    const failed = change.state === "failed" && change.target === "laptop" && change.error;
    return [
      h("div", { class: "sec" }, h("span", null, "Laptop link")),
      h(
        "div",
        { class: "group" },
        h(
          "div",
          { class: "row icon-row" },
          h("span", { class: "tile" }, icon("laptop", 17, 1.9)),
          h(
            "span",
            { class: "row-text" },
            h("b", { id: "laptop-label" }, "Laptop link"),
            h("span", null, "Share this Cardputer with a laptop on the cable")
          ),
          h("span", { class: "row-value " + value[1] }, value[0]),
          h(Switch, {
            on,
            label: "Laptop link",
            disabled: !canToggle,
            onClick: () => this.pickMode(on ? "client" : "laptop", false),
          })
        )
      ),
      failed ? h("p", { class: "mode-line warn", role: "status" }, change.error) : null,
      h(Message, { msg: this.state.msg, where: "laptop" }),
      on
        ? h("p", { class: "note" }, "On the laptop, open " + address + ":8780 in a browser. Go back to Client before you plug the cable into a router again.")
        : why
        ? h("p", { class: "note" }, why + ".")
        : h("p", { class: "note" }, "Plug the cable straight into your laptop. It gets an address, and the dashboard opens at " + address + ":8780."),
    ];
  }

  hotspot(status, mode) {
    const secret = this.state.secret;
    const name = mode.hotspot ? mode.hotspot_name : mode.next_hotspot_name;
    const password = secret && secret.password ? secret.password : "";
    const switching = mode.switch && mode.switch.state === "switching";
    const busy = !!this.state.want && (this.state.want.kind === "name" || this.state.want.kind === "password");
    const head = mode.hotspot
      ? "Hotspot · " + mode.hotspot_devices + (mode.hotspot_devices === 1 ? " device" : " devices")
      : "Hotspot · off";
    const card = h(
      "section",
      { class: "hot", "aria-label": "Hotspot" },
      mode.hotspot && secret && secret.qr
        ? h(Qr, { qr: secret.qr })
        : h("div", { class: "qr-off" }, mode.hotspot ? "Loading the code…" : "The join code shows while the hotspot runs"),
      h(
        "div",
        { class: "hot-text" },
        h("div", { class: "hot-head" }, head),
        h("div", { class: "hot-name" }, name || "--"),
        h(
          "div",
          { class: this.state.shown && password ? "hot-pw shown" : "hot-pw", "aria-label": this.state.shown ? "Password" : "Password hidden" },
          this.state.shown ? password || (secret ? "Made at the first switch to Router" : "…") : "••••••••••••"
        ),
        h(
          "div",
          { class: "links" },
          h("button", { type: "button", onClick: this.onShow, "aria-pressed": this.state.shown ? "true" : "false" }, this.state.shown ? "Hide" : "Show"),
          h("button", { type: "button", onClick: this.onEdit, disabled: switching || busy, "aria-expanded": this.state.editing ? "true" : "false" }, "Change")
        )
      )
    );
    const edit = this.state.editing
      ? h(
          "form",
          { class: "check-card-p edit-card", onSubmit: this.onRename },
          h("label", { class: "edit-label", for: "hot-name" }, "Hotspot name"),
          h("input", {
            id: "hot-name",
            class: "field",
            type: "text",
            value: this.state.draft,
            maxLength: 32,
            autoComplete: "off",
            autoCapitalize: "off",
            spellCheck: false,
            onInput: this.onDraft,
          }),
          h(
            "p",
            { class: "edit-hint" },
            "Leave it empty for the Cardputer's own name." + (mode.hotspot ? " The hotspot starts again with the new name." : "")
          ),
          mode.hotspot && viaHotspot(status)
            ? h("p", { class: "check-own" }, icon("alert", 18, 2), h("span", null, "You're connected through the hotspot, so this page will lose the Cardputer."))
            : null,
          h(
            "div",
            { class: "check-acts" },
            h("button", { type: "submit", class: "bp gh", disabled: this.state.sending }, "Save name"),
            h("button", { type: "button", class: "bp gh", onClick: this.onPassword, disabled: this.state.sending }, "New password")
          )
        )
      : null;
    return [
      card,
      edit,
      busy ? h("p", { class: "mode-line", role: "status" }, "Changing the hotspot…") : null,
      h(Message, { msg: this.state.msg, where: "hotspot" }),
    ];
  }

  vpn(status, mode) {
    const files = mode.vpn_files || [];
    const switching = mode.switch && mode.switch.state === "switching";
    const waiting = this.state.want && (this.state.want.kind === "vpn-on" || this.state.want.kind === "vpn-off");
    const leak = mode.leak || {};
    let state = ["Off", ""];
    if (mode.gateway_down) state = ["Down", "warn"];
    else if (mode.vpn === "on") state = [typeof mode.vpn_reply_s === "number" ? "Connected" : "On, no reply yet", typeof mode.vpn_reply_s === "number" ? "ok" : ""];
    else if (mode.vpn === "connecting" || mode.vpn_active) state = ["Connecting…", ""];
    if (waiting) state = ["Asking…", ""];
    const canToggle =
      !switching && !waiting && !this.state.want && (mode.vpn_active || mode.gateway_down || (files.length > 0 && !(mode.why || {}).vpn));
    const pickable = !mode.vpn_active && files.length > 1;

    let leakValue = ["Not tested", ""];
    if (leak.state === "running") leakValue = ["Testing…", ""];
    else if (leak.shown && leak.fails > 0) leakValue = [leak.fails + (leak.fails === 1 ? " leak found" : " leaks found"), "bad"];
    else if (leak.shown && leak.unknown > 0) leakValue = ["Not all checked", "warn"];
    else if (leak.shown) leakValue = ["No leaks found", "ok"];
    else if (leak.state === "failed") leakValue = ["Didn't finish", "warn"];
    const leakLines = (leak.shown || leak.state === "running") && leak.lines ? leak.lines : [];

    const addText = "Add a VPN file from this " + (window.matchMedia && window.matchMedia("(min-width: 900px)").matches ? "computer" : "phone");
    const rows = [
      h(
        "div",
        { class: "row icon-row" },
        h("span", { class: "tile vpn" }, icon("vpn", 17, 2)),
        h("span", { class: "row-key" }, "VPN"),
        h("span", { class: "row-value " + state[1] }, state[0]),
        h(Switch, { on: !!mode.vpn_active || !!mode.gateway_down, label: "VPN", disabled: !canToggle, onClick: this.onVpn })
      ),
      h(
        pickable ? "button" : "div",
        pickable
          ? { class: "row icon-row row-button", type: "button", onClick: this.onPick, "aria-expanded": this.state.picking ? "true" : "false" }
          : { class: "row icon-row" },
        h("span", { class: "tile" }, icon("file", 17, 1.9)),
        h("span", { class: "row-key" }, "File"),
        h("span", { class: mode.vpn_file || files.length ? "row-value mono" : "row-value" }, mode.vpn_file || (files.length ? files[0] : "None yet")),
        pickable ? h("span", { class: "chev" }, icon("chevron", 13, 2.4)) : null
      ),
      this.state.picking && pickable
        ? files.map((file) =>
            h(
              "button",
              {
                class: "row pick-row",
                type: "button",
                role: "radio",
                "aria-checked": file === mode.vpn_file ? "true" : "false",
                onClick: () => this.pick(file),
                key: "f-" + file,
              },
              h("span", { class: "row-key mono" }, file),
              file === mode.vpn_file ? h("span", { class: "pick-mark" }, icon("check", 16, 2.4)) : null
            )
          )
        : null,
      h(
        "div",
        { class: "row icon-row" },
        h("span", { class: "tile ok" }, icon("lock", 17, 2)),
        h("span", { class: "row-key" }, "Kill switch"),
        h("span", { class: "row-value " + (mode.gateway_down ? "warn" : "") }, mode.gateway_down ? "Holding devices back" : mode.kill_switch ? "On" : "Off")
      ),
      h(
        "div",
        { class: "row icon-row leak-row" },
        h("span", { class: "tile" }, icon("drop", 17, 2)),
        h(
          "span",
          { class: "row-text" },
          h("b", null, "Leak test"),
          h("span", { class: "leak-value " + leakValue[1] }, !leak.can && leak.state !== "running" && leak.why ? leak.why : leakValue[0])
        ),
        h(
          "button",
          {
            type: "button",
            class: "row-btn",
            onClick: this.onLeak,
            disabled: this.state.sending || (leak.state !== "running" && !leak.can) || (this.state.want && this.state.want.kind === "leak"),
          },
          leak.state === "running" ? "Stop" : "Test"
        )
      ),
      leakLines.length
        ? h(
            "ul",
            { class: "leak-lines" },
            leakLines.map((line) => h("li", null, h("span", { class: "dot " + (LEAK_DOT[line.verdict] || "") }), h("span", null, line.text)))
          )
        : null,
      mode.vpn_add
        ? h(
            "label",
            { class: "row icon-row add-row" + (this.state.uploading ? " busy" : "") },
            h("span", { class: "tile or" }, icon("plus", 17, 2.2)),
            h("span", { class: "row-key" }, this.state.uploading ? "Adding…" : addText),
            h("input", { class: "sr", type: "file", onChange: this.onFile, disabled: this.state.uploading })
          )
        : h(
            "div",
            { class: "row icon-row add-row off" },
            h("span", { class: "tile" }, icon("plus", 17, 2.2)),
            h("span", { class: "row-text" }, h("b", null, addText), h("span", null, "Only on the Cardputer itself, not with sample data"))
          ),
    ];
    return [
      mode.gateway_down
        ? h(
            "section",
            { class: "alert look gw-alert", role: "status" },
            h("h2", null, "VPN down: hotspot devices blocked"),
            h("p", null, "The kill switch keeps the hotspot's devices off the internet until the VPN is back, or until you turn it off."),
            h(
              "div",
              { class: "check-acts" },
              h("button", { type: "button", class: this.state.ask ? "bp gh" : "bp or", onClick: this.onVpnAgain, disabled: !!this.state.want || switching }, "Turn the VPN on again"),
              h("button", { type: "button", class: "bp gh", onClick: this.onVpnOff, disabled: !!this.state.want || switching }, "Turn the VPN off")
            )
          )
        : null,
      h("div", { class: "sec" }, h("span", null, "VPN")),
      h("div", { class: "group" }, rows),
      h(Message, { msg: this.state.msg, where: "vpn" }),
      !files.length && !mode.vpn_active
        ? h("p", { class: "note" }, "Add a WireGuard file from your VPN provider to use the VPN.")
        : h("p", { class: "note" }, "The kill switch turns on by itself when the VPN and the hotspot run together: hotspot devices then only get out through the VPN."),
    ];
  }

  keep(mode) {
    return [
      h("div", { class: "sec" }, h("span", null, "When LokiCTL quits")),
      h(
        "div",
        { class: "group" },
        h(
          "div",
          { class: "row" },
          h("span", { class: "row-key", id: "keep-label" }, "Keep this mode running"),
          h(Switch, { on: !!mode.keep, label: "Keep this mode running", disabled: this.state.sending || (this.state.want && this.state.want.kind === "keep"), onClick: this.onKeep })
        )
      ),
      h(Message, { msg: this.state.msg, where: "keep" }),
    ];
  }

  render({ status }, { ask, sending }) {
    const titles = [
      h("h1", { class: "large-title phone-only", key: "p" }, "Modes"),
      h("h1", { class: "page-title desk-only", key: "d" }, "Modes and VPN"),
    ];
    if (!status || !status.mode || !status.mode.switch) return titles;
    const mode = status.mode;
    const question = ask ? h(Ask, { ask, status, sending, onYes: this.onYes, onNo: this.onNo }) : null;
    const askAtMode = ask && (ask.kind === "mode" || ask.kind === "password");
    // Laptop link's question sits under its own row.
    const atLaptop = !!ask && ask.kind === "mode" && (ask.mode === "laptop" || (ask.mode === "client" && mode.id === "laptop"));
    return [
      titles,
      h(
        "div",
        { class: "modes-cols" },
        h(
          "div",
          { class: "modes-col" },
          h("div", { class: "sec desk-only" }, h("span", null, "Mode")),
          this.tiles(mode),
          askAtMode && ask.kind === "mode" && !atLaptop ? question : null,
          this.hotspot(status, mode),
          askAtMode && ask.kind === "password" ? question : null,
          this.laptop(mode),
          atLaptop ? question : null
        ),
        h("div", { class: "modes-col" }, ask && !askAtMode ? question : null, this.vpn(status, mode), this.keep(mode))
      ),
    ];
  }
}

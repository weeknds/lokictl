// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Tools page: ping, name lookup, trace, open ports, internet speed,
// LAN speed and the Wi-Fi survey. Each one asks the Cardputer, which starts
// it the way its own page (or its `:` command) would, with the same rules:
// one tool at a time, a target it takes, the own-network question before a
// port scan. The results are the Cardputer's own texts from the status.
// On a phone: a list, and each tool on its own page (#/tools/ping). On a
// computer: the list beside the open tool.

import { h, Component } from "./preact.module.js";
import { icon, post, refreshSoon } from "./ui.js";

// How long the page waits for the status to show that a request took.
const WAIT_MS = 8000;
const NO_ANSWER = "No answer from the Cardputer. Try again.";
const REFUSED = {
  401: "Your session ended. Pair again to do this.",
  403: "The Cardputer refused this. Reload the page and try again.",
  429: "Too many requests at once. Wait a moment.",
};

// Each tool: its #/tools/<id>, name, icon, the list's short hint, the
// sentence under its title, and the label of its target field.
export const TOOLS = [
  {
    id: "ping",
    name: "Ping",
    icon: "pulse",
    hint: "Is it there, and how fast",
    about: "Is a device or site there, and how fast does it answer?",
    field: "Address or name",
  },
  {
    id: "dns",
    name: "Look up a name",
    icon: "search",
    hint: "The addresses behind a name",
    about: "The addresses behind a name, and which server gave them.",
    field: "Name",
  },
  {
    id: "trace",
    name: "Trace route",
    icon: "route",
    hint: "Every stop on the way",
    about: "Every stop on the way to a device or site.",
    field: "Address or name",
  },
  {
    id: "ports",
    name: "Open ports",
    icon: "security",
    hint: "Services on one of your devices",
    about: "Which of 50 common services one device on your network offers.",
    field: "Device address",
  },
  { id: "speed", name: "Internet speed", icon: "gauge", hint: "Download and upload", about: "How fast your internet downloads and uploads." },
  { id: "lan", name: "LAN speed", icon: "laptop", hint: "To a phone or computer here", about: "The speed to a phone or computer on your network." },
  {
    id: "survey",
    name: "Wi-Fi survey",
    icon: "wifi",
    hint: "Busy channels, look-alikes",
    about: "Busy channels and look-alike networks, heard with a USB Wi-Fi stick.",
  },
];

const TARGETED = { ping: true, dns: true, trace: true, ports: true };
const TONE = { muted: "muted", ok: "ok", warn: "warn", bad: "bad", accent: "", "": "" };
const TAG_PILL = { bad: "p-bad", warn: "p-warn", accent: "p-know", muted: "p-know", ok: "p-ok" };

export function findTool(id) {
  return TOOLS.find((tool) => tool.id === id) || null;
}

// ---------- Targets, checked as the Cardputer checks them ----------

function isIpv4(text) {
  const parts = text.split(".");
  return parts.length === 4 && parts.every((part) => /^(0|[1-9][0-9]{0,2})$/.test(part) && Number(part) <= 255);
}

// The private and link-local networks a port scan or a LAN test may ask.
function isLocal(text) {
  if (!isIpv4(text)) return false;
  const [a, b] = text.split(".").map(Number);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 169 && b === 254);
}

// Why the Cardputer would refuse `text` for `tool`, in its own words;
// empty when it takes it.
function targetError(tool, text, max) {
  if (!text) return tool === "dns" ? "Type a name first." : "Type an address or a name first.";
  if (tool === "dns" && isIpv4(text)) return "DNS needs a name, not an address.";
  if (tool === "ports") return isLocal(text) ? "" : isIpv4(text) ? "Ports needs a private address." : "Ports needs an IPv4 address.";
  const shape =
    text.length <= max && /^[a-z0-9.]+$/.test(text) && !/^\.|\.$|\.\./.test(text) && (!/^[0-9.]+$/.test(text) || isIpv4(text));
  return shape ? "" : "Not a valid target: " + text;
}

function cleanTarget(text) {
  return String(text || "").trim().toLowerCase();
}

// ---------- Small parts ----------

function Line({ t }) {
  if (!t || !t.line) return null;
  return h("p", { class: "tool-line " + (TONE[t.tone] || ""), role: "status" }, t.line);
}

function Message({ msg, tool }) {
  if (!msg || msg.tool !== tool) return null;
  return h("p", { class: "mode-line " + (msg.tone || ""), role: "status" }, msg.text);
}

// A run's samples as thin bars, the newest on the right.
function Bars({ samples, label, up }) {
  if (!samples || !samples.length) return null;
  const top = Math.max(1, ...samples);
  const width = 200;
  const step = width / 20;
  return h(
    "svg",
    { class: "bars" + (up ? " up" : ""), viewBox: "0 0 " + width + " 40", preserveAspectRatio: "none", role: "img", "aria-label": label },
    samples.slice(-20).map((value, i) => {
      const height = Math.max(1, (value / top) * 38);
      return h("rect", { x: i * step + 1, y: 40 - height, width: step - 2, height, rx: 1.5, key: i });
    })
  );
}

function Rates({ t, labels }) {
  return h(
    "div",
    { class: "tool-rates" },
    h(
      "div",
      null,
      h("span", { class: "rate-key" }, labels[0]),
      h("div", { class: "num" }, t.down, h("small", null, "Mbit/s")),
      h(Bars, { samples: t.down_samples, label: labels[0] + " over time" })
    ),
    h(
      "div",
      null,
      h("span", { class: "rate-key" }, labels[1]),
      h("div", { class: "num up" }, t.up, h("small", null, "Mbit/s")),
      h(Bars, { samples: t.up_samples, label: labels[1] + " over time", up: true })
    )
  );
}

// ---------- Results, one kind per tool ----------

function PingResult({ t }) {
  if (!t.replies || !t.replies.length) return null;
  return [
    h("div", { class: "sec" }, h("span", null, "Replies from ", h("span", { class: "mono" }, t.address || t.target))),
    h(
      "div",
      { class: "group" },
      t.replies.map((reply) =>
        h(
          "div",
          { class: "row", key: reply.seq },
          h("span", { class: "row-key" }, "Echo ", h("span", { class: "mono" }, String(reply.seq))),
          h("span", { class: "row-value mono " + (TONE[reply.tone] || "") }, /^[0-9.]+$/.test(reply.text) ? reply.text + " ms" : reply.text)
        )
      )
    ),
    t.counts ? h("p", { class: "note mono-note" }, t.counts) : null,
    t.times ? h("p", { class: "note mono-note" }, t.times) : null,
  ];
}

function DnsResult({ t }) {
  if (!t.addresses || !t.addresses.length) return null;
  const via = [t.server ? "Asked " + t.server : "", t.time_ms >= 0 ? t.time_ms + " ms" : ""].filter(Boolean).join(" · ");
  return [
    h("div", { class: "sec" }, h("span", null, "Addresses of ", h("span", { class: "mono" }, t.target))),
    h("div", { class: "group" }, t.addresses.map((address) => h("div", { class: "row", key: address }, h("span", { class: "row-key mono" }, address)))),
    via ? h("p", { class: "note mono-note" }, via) : null,
  ];
}

function TraceResult({ t }) {
  if (!t.hops || !t.hops.length) return null;
  return [
    h("div", { class: "sec" }, h("span", null, "The way to ", h("span", { class: "mono" }, t.address || t.target))),
    h(
      "div",
      { class: "group" },
      t.hops.map((hop) =>
        h(
          "div",
          { class: "row hop-row", key: hop.n },
          h("span", { class: "hop-n mono" }, hop.n),
          h(
            "span",
            { class: "row-text" },
            h("b", { class: "mono " + (TONE[hop.address_tone] || "") }, hop.address),
            hop.name ? h("span", null, hop.name) : null
          ),
          h("span", { class: "row-value mono " + (TONE[hop.time_tone] || "") }, hop.time)
        )
      )
    ),
  ];
}

function PortsResult({ t }) {
  const running = t.state === "running";
  const parts = [];
  if (running && t.total > 0) {
    parts.push(
      h(
        "div",
        { class: "check-bar", role: "progressbar", "aria-valuemin": 0, "aria-valuemax": t.total, "aria-valuenow": t.asked, "aria-label": "Ports asked" },
        h("i", { style: { width: Math.min(100, Math.round((t.asked / t.total) * 100)) + "%" } })
      )
    );
  }
  if (t.open && t.open.length) {
    parts.push(
      h("div", { class: "sec" }, h("span", null, "Open on ", h("span", { class: "mono" }, t.target))),
      h(
        "div",
        { class: "group" },
        t.open.map((port) =>
          h(
            "div",
            { class: "row port-row", key: port.port },
            h(
              "span",
              { class: "row-text" },
              h("b", null, h("span", { class: "mono inl" }, port.text), port.plain ? " · " + port.plain : ""),
              port.said ? h("span", { class: "mono" }, port.said) : null
            ),
            port.tag ? h("span", { class: "pill " + (TAG_PILL[port.tag_tone] || "p-know") }, port.tag) : null
          )
        )
      )
    );
  }
  if (t.counts) parts.push(h("p", { class: "note mono-note" }, t.counts));
  return parts;
}

function SpeedResult({ t }) {
  if (t.state === "idle") return null;
  return [
    h(Rates, { t, labels: ["Download", "Upload"] }),
    h("p", { class: "note" }, "Server ", h("span", { class: "mono" }, t.server || "--"), " · ", t.down_bytes, " down, ", t.up_bytes, " up"),
  ];
}

// ---------- The page ----------

export class ToolsPage extends Component {
  constructor() {
    super();
    this.state = {
      drafts: {},
      ask: null, // a port scan waiting for the own-network answer: {target}
      sending: false,
      want: null, // a request waiting for the status: {tool, from, since}
      msg: null, // {tool, text, tone}
    };
    this.timer = 0;
    this.seenStatus = null;
    this.lastSub = undefined;
    this.onSubmit = (event) => {
      event.preventDefault();
      this.run(this.current());
    };
    this.onNo = () => this.setState({ ask: null });
    this.onScan = () => {
      const ask = this.state.ask;
      if (ask) this.send("/api/v1/tool", { run: "ports", target: ask.target, own: "1" }, "ports");
    };
  }

  componentDidMount() {
    this.follow();
  }

  componentDidUpdate() {
    this.follow();
  }

  componentWillUnmount() {
    clearTimeout(this.timer);
  }

  tools() {
    return (this.props.status && this.props.status.tools) || null;
  }

  // The tool shown: the one in the address, else on a computer the one
  // that runs, else Ping.
  current() {
    const sub = this.props.sub;
    if (findTool(sub)) return sub;
    const tools = this.tools();
    const running = tools && tools.running;
    if (findTool(running)) return running;
    if (tools && tools.lan && tools.lan.state === "running") return "lan";
    if (tools && tools.survey && tools.survey.state === "running") return "survey";
    return "ping";
  }

  // While a request waits or a tool runs, ask for each next status soon.
  follow() {
    if (this.props.sub !== this.lastSub) {
      // Another tool: its own question and message, not this one's.
      this.lastSub = this.props.sub;
      if (this.state.ask || this.state.msg) this.setState({ ask: null, msg: null });
    }
    const status = this.props.status;
    const tools = this.tools();
    if (!tools) return;
    const running = !!tools.running || tools.lan.state === "running" || tools.survey.state === "running";
    if (status !== this.seenStatus) {
      this.seenStatus = status;
      if (running || this.state.want) refreshSoon();
    }
    const want = this.state.want;
    if (!want) return;
    const t = tools[want.tool] || {};
    if (t.state !== want.from || t.line !== want.line) {
      this.setState({ want: null });
    } else if (Date.now() - want.since > WAIT_MS) {
      this.setState({ want: null, msg: { tool: want.tool, text: "Nothing happened." + (t.why ? " " + t.why + "." : " Try again."), tone: "warn" } });
    }
  }

  draft(tool) {
    const drafts = this.state.drafts;
    if (typeof drafts[tool] === "string") return drafts[tool];
    const t = this.tools() && this.tools()[tool];
    return (t && t.target) || "";
  }

  setDraft(tool, value) {
    this.setState({ drafts: Object.assign({}, this.state.drafts, { [tool]: value }), msg: null, ask: null });
  }

  run(tool) {
    const tools = this.tools();
    if (!tools || this.state.sending) return;
    const t = tools[tool];
    if (t.state === "running") {
      this.send("/api/v1/tool", { stop: tool }, tool);
      return;
    }
    if (!TARGETED[tool]) {
      this.send("/api/v1/tool", { run: tool }, tool);
      return;
    }
    const target = cleanTarget(this.draft(tool));
    const error = targetError(tool, target, tools.max || 63);
    if (error) {
      this.setState({ msg: { tool, text: error, tone: "warn" } });
      return;
    }
    if (tool === "ports") {
      // The first press only asks, as the Cardputer's first 6 does.
      this.setState({ ask: { target }, msg: null }, () => {
        const heading = document.getElementById("ports-ask-" + (window.matchMedia("(min-width: 900px)").matches ? "d" : "p"));
        if (heading) heading.focus();
      });
      return;
    }
    this.send("/api/v1/tool", { run: tool, target }, tool);
  }

  lanRun() {
    const lan = this.tools().lan;
    if (lan.state === "running") {
      this.send("/api/v1/lan", { stop: "1" }, "lan");
      return;
    }
    const target = cleanTarget(this.draft("lan"));
    if (!isLocal(target)) {
      this.setState({ msg: { tool: "lan", text: target ? "Use a local address." : "Type the address of a phone or computer first.", tone: "warn" } });
      return;
    }
    this.send("/api/v1/lan", { test: target }, "lan");
  }

  // POST, then wait for the status to show it took.
  async send(path, fields, tool) {
    if (this.state.sending) return;
    const t = this.tools()[tool] || {};
    this.setState({ sending: true, msg: null });
    let code = 0;
    try {
      code = await post(path, fields);
    } catch (err) {
      code = 0;
    }
    if (code !== 204) {
      this.setState({ sending: false, ask: null, msg: { tool, text: REFUSED[code] || NO_ANSWER, tone: "bad" } });
      return;
    }
    this.setState({ sending: false, ask: null, want: { tool, from: t.state, line: t.line, since: Date.now() } });
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.setState({}), WAIT_MS + 100);
    refreshSoon();
  }

  // ---------- One tool ----------

  // The typed target with its quick picks, for Ping, DNS, Trace, Ports
  // and the LAN test.
  targetForm(tool, spec, t, where, disabled) {
    const tools = this.tools();
    const picks = [];
    if (tool !== "dns" && tools.router) picks.push(["Router", tools.router]);
    if ((tool === "ping" || tool === "trace") && tools.presets) tools.presets.ping.forEach((p) => picks.push([p, p]));
    if (tool === "dns" && tools.presets) tools.presets.dns.forEach((p) => picks.push([p, p]));
    const id = "target-" + tool + "-" + where;
    return h(
      "form",
      { class: "tool-form", onSubmit: tool === "lan" ? (e) => (e.preventDefault(), this.lanRun()) : this.onSubmit },
      h("label", { class: "edit-label", for: id }, spec.field || "Address"),
      h("input", {
        id,
        class: "field",
        type: "text",
        inputMode: "url",
        value: this.draft(tool),
        maxLength: tools.max || 63,
        placeholder: tool === "dns" ? "example.com" : tool === "ports" || tool === "lan" ? "192.168.1.20" : "9.9.9.9 or example.com",
        autoComplete: "off",
        autoCapitalize: "off",
        autoCorrect: "off",
        spellCheck: false,
        disabled,
        onInput: (event) => this.setDraft(tool, event.target.value),
      }),
      picks.length
        ? h(
            "div",
            { class: "picks" },
            picks.map(([label, value]) =>
              h(
                "button",
                {
                  type: "button",
                  class: "pick" + (cleanTarget(this.draft(tool)) === value ? " on" : ""),
                  disabled,
                  onClick: () => this.setDraft(tool, value),
                  key: label,
                },
                label === value ? h("span", { class: "mono" }, label) : label
              )
            )
          )
        : null
    );
  }

  // The one main button: Run (orange) or Stop, and why it can't run.
  runButton(tool, t, onClick, words) {
    const running = t.state === "running";
    const waiting = this.state.want && this.state.want.tool === tool;
    const blocked = !running && !t.can;
    const asking = tool === "ports" && this.state.ask;
    return [
      h(
        "div",
        { class: "tool-acts" },
        h(
          "button",
          {
            type: tool === "lan" || !TARGETED[tool] ? "button" : "submit",
            form: undefined,
            class: "bp " + (running || asking ? "gh" : "or"),
            onClick,
            disabled: this.state.sending || waiting || blocked,
          },
          waiting ? "Asking…" : running ? "Stop" : words || "Run"
        )
      ),
      blocked && t.why ? h("p", { class: "mode-line warn" }, t.why + ".") : null,
    ];
  }

  portsAsk(where) {
    const ask = this.state.ask;
    if (!ask) return null;
    return h(
      "section",
      { class: "check-card-p ask-card", "aria-labelledby": "ports-ask-" + where },
      h("h2", { id: "ports-ask-" + where, tabIndex: -1 }, "Scan ", h("span", { class: "mono" }, ask.target), "?"),
      h(
        "p",
        { class: "check-own" },
        icon("alert", 18, 2),
        h("span", null, "Only devices you own or may test. A scan can look like an attack to the device's owner.")
      ),
      h("p", null, "The Cardputer asks 50 common ports, one at a time, and reads what each open one says about itself."),
      h(
        "div",
        { class: "check-acts" },
        h("button", { type: "button", class: "bp or", onClick: this.onScan, disabled: this.state.sending }, this.state.sending ? "Asking…" : "Scan"),
        h("button", { type: "button", class: "bp gh", onClick: this.onNo }, "Cancel")
      )
    );
  }

  // The LAN page's test side. The Cardputer as the server stays on the
  // Cardputer: it listens only while its LAN page is on its screen.
  lanPanel(t, where) {
    const running = t.state === "running";
    const mine = !running || t.mode === "client";
    const gate = { state: mine ? t.state : "idle", can: running ? mine : t.can, why: running && !mine ? "The server is on" : t.why };
    return [
      h(Line, { t }),
      h("p", { class: "note tool-hint" }, "The other device runs iperf3 as a server (iperf3 -s). The test takes 10 s each way."),
      this.targetForm("lan", { field: "Device address" }, t, where, running),
      this.runButton("lan", gate, () => this.lanRun(), "Run test"),
      mine && t.state !== "idle" ? h(Rates, { t, labels: ["From it", "To it"] }) : null,
      h("p", { class: "note" }, "To make the Cardputer the server a phone or computer tests against, open LAN on the Cardputer itself."),
    ];
  }

  surveyPanel(t) {
    const running = t.state === "running";
    const channels = t.channels || [];
    const top = 40;
    const width = channels.length * 20;
    const kickoff = new Set((t.findings || []).filter((f) => f.kind === "kickoff").map((f) => f.channel));
    return [
      h(Line, { t }),
      this.runButton("survey", t, () => this.send("/api/v1/survey", { run: running ? "0" : "1" }, "survey"), "Start listening"),
      h("p", { class: "note" }, "The stick only listens: it sends nothing, and nothing is kept after the survey."),
      t.state !== "idle"
        ? [
            h("div", { class: "sec" }, h("span", null, "Channels"), t.best ? h("span", null, "Best: ", h("span", { class: "mono" }, String(t.best))) : null),
            h(
              "div",
              { class: "group chan-box" },
              h(
                "svg",
                { class: "chan", viewBox: "0 0 " + width + " " + (top + 16), role: "img", "aria-label": "How busy each channel is, 1 to " + channels.length },
                channels.map((c, i) => {
                  const height = Math.max(1, (c.level / 100) * top);
                  const n = i + 1;
                  return h(
                    "g",
                    { key: n },
                    h("rect", { class: kickoff.has(n) ? "bad" : n === t.best ? "best" : "", x: i * 20 + 3, y: top - height, width: 14, height, rx: 2 }),
                    h("text", { x: i * 20 + 10, y: top + 12 }, String(n))
                  );
                })
              )
            ),
            t.findings.length
              ? [
                  h("div", { class: "sec" }, h("span", null, "Found")),
                  h(
                    "div",
                    { class: "group" },
                    t.findings.map((f, i) =>
                      h(
                        "div",
                        { class: "row", key: i },
                        h("span", { class: "dot " + (f.kind === "kickoff" ? "bad" : "warn") }),
                        h("span", { class: "row-key" }, f.text)
                      )
                    )
                  ),
                ]
              : null,
            t.networks.length
              ? [
                  h("div", { class: "sec" }, h("span", null, "Networks heard · ", h("span", { class: "mono" }, String(t.networks_seen)))),
                  h(
                    "div",
                    { class: "group" },
                    t.networks.map((n, i) =>
                      h(
                        "div",
                        { class: "row", key: i },
                        h(
                          "span",
                          { class: "row-text" },
                          h("b", { class: n.hidden && !n.name ? "muted" : "" }, n.name || "Hidden network"),
                          h("span", { class: "mono" }, "ch " + (n.channel || "?") + " · " + (n.security || "?") + (n.signal < 0 ? " · " + n.signal + " dBm" : ""))
                        ),
                        n.look_alike ? h("span", { class: "pill p-warn" }, "Look-alike") : null
                      )
                    )
                  ),
                  t.more_networks > 0 ? h("p", { class: "note" }, t.more_networks + " more not listed here") : null,
                ]
              : null,
          ]
        : null,
    ];
  }

  panel(tool, where) {
    const tools = this.tools();
    const spec = findTool(tool);
    const t = tools[tool];
    const running = t.state === "running";
    let body;
    if (tool === "lan") {
      body = this.lanPanel(t, where);
    } else if (tool === "survey") {
      body = this.surveyPanel(t);
    } else if (tool === "speed") {
      body = [
        h(Line, { t }),
        this.runButton("speed", t, () => this.run("speed"), "Start"),
        h("p", { class: "note" }, tools.data_note + ", 10 s each way, from ", h("span", { class: "mono" }, t.server || "--"), "."),
        h(SpeedResult, { t }),
      ];
    } else {
      const asking = tool === "ports" && this.state.ask;
      body = [
        h(Line, { t }),
        this.targetForm(tool, spec, t, where, running),
        asking ? this.portsAsk(where) : this.runButton(tool, t, () => this.run(tool), tool === "ports" ? "Scan" : "Run"),
        tool === "ping" ? h(PingResult, { t }) : null,
        tool === "dns" ? h(DnsResult, { t }) : null,
        tool === "trace" ? h(TraceResult, { t }) : null,
        tool === "ports" ? h(PortsResult, { t }) : null,
      ];
    }
    return [body, h(Message, { msg: this.state.msg, tool })];
  }

  // ---------- The list ----------

  stateWord(tool) {
    const t = this.tools()[tool];
    if (!t) return "";
    if (t.state === "running") return "Running";
    return "";
  }

  listPhone() {
    return [
      h(
        "div",
        { class: "group" },
        TOOLS.map((tool) =>
          h(
            "a",
            { class: "row icon-row", href: "#/tools/" + tool.id, key: tool.id },
            h("span", { class: "tile" }, icon(tool.icon, 17, 2)),
            h("span", { class: "row-text" }, h("b", null, tool.name), h("span", { class: "one-line" }, tool.hint)),
            this.stateWord(tool.id) ? h("span", { class: "row-value run-word" }, this.stateWord(tool.id)) : null,
            h("span", { class: "chev" }, icon("chevron", 13, 2.4))
          )
        )
      ),
      h("div", { class: "sec" }, h("span", null, "Alerts")),
      h(
        "div",
        { class: "group" },
        h(
          "a",
          { class: "row icon-row", href: "#/watch" },
          h("span", { class: "tile" }, icon("watch", 17, 2)),
          h("span", { class: "row-text" }, h("b", null, "Watch and trap"), h("span", null, "New devices and scans as they happen")),
          h("span", { class: "chev" }, icon("chevron", 13, 2.4))
        )
      ),
    ];
  }

  listDesk(current) {
    return h(
      "nav",
      { class: "card find-list span-4", "aria-label": "Tools" },
      TOOLS.map((tool) => {
        const on = tool.id === current;
        return h(
          "a",
          { class: on ? "item on" : "item", href: "#/tools/" + tool.id, key: tool.id, "aria-current": on ? "true" : undefined },
          h("span", { class: "item-ic" }, icon(tool.icon, 16, 2)),
          h("span", { class: "rt" }, h("b", null, tool.name), this.stateWord(tool.id) ? h("span", { class: "run-word" }, this.stateWord(tool.id)) : null)
        );
      }),
      h("a", { class: "item", href: "#/watch" }, h("span", { class: "item-ic" }, icon("watch", 16, 2)), h("span", { class: "rt" }, h("b", null, "Watch and trap")))
    );
  }

  render({ status, sub }) {
    const tools = this.tools();
    const titles = [h("h1", { class: "page-title desk-only", key: "d" }, "Tools")];
    if (!tools) return [h("h1", { class: "large-title phone-only", key: "p" }, "Tools"), titles];
    const current = this.current();
    const spec = findTool(current);
    const phone = findTool(sub)
      ? h(
          "div",
          { class: "phone-only", key: "p" },
          h("a", { class: "back", href: "#/tools" }, icon("back", 22, 2.4), "Tools"),
          h("h1", { class: "large-title" }, spec.name),
          h("p", { class: "tool-about" }, spec.about),
          this.panel(current, "p")
        )
      : h("div", { class: "phone-only", key: "p" }, h("h1", { class: "large-title" }, "Tools"), this.listPhone());
    const desk = h(
      "div",
      { class: "desk-only", key: "d" },
      h("h1", { class: "page-title" }, "Tools"),
      h(
        "div",
        { class: "grid" },
        this.listDesk(current),
        h(
          "section",
          { class: "card detail tool-card span-8", "aria-label": spec.name },
          h("h2", { class: "detail-title" }, spec.name),
          h("p", { class: "detail-about" }, spec.about),
          this.panel(current, "d")
        )
      )
    );
    return [phone, desk];
  }
}

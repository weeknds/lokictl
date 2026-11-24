// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Watch and trap page. Watch listens to the network and warns about
// new devices, scans, a fake router and more; it never sends anything. The
// trap pretends to offer a few services, so anyone who connects is probing
// the network. Both turn on and off from here the way the Cardputer's own
// Watch and Trap pages do it, and their alerts come from the status.
//
// It also keeps the optional "tell me about new alerts": a browser
// notification for each new alert while a dashboard page is open and in
// front (a hidden page asks the Cardputer for nothing). Browsers
// only allow that on secure pages, which the Cardputer's plain address is
// not, so there the dashboard shows new alerts at the top of the page.

import { h, Component } from "./preact.module.js";
import { icon, post, refreshSoon } from "./ui.js";

const WAIT_MS = 8000;
const NO_ANSWER = "No answer from the Cardputer. Try again.";
const REFUSED = {
  401: "Your session ended. Pair again to do this.",
  403: "The Cardputer refused this. Reload the page and try again.",
  429: "Too many requests at once. Wait a moment.",
};
const TONE = { muted: "muted", ok: "ok", warn: "warn", bad: "bad", accent: "", "": "" };
const PORT_WORD = { open: "Open", off: "Off", "in use": "In use", "no helper": "No helper" };

// ---------- New alerts, for this browser only ----------

const PREF = "lokictl-alerts";
let notifyOn = readPref();
let seen = null; // the alerts already there; null until the first status

function readPref() {
  try {
    return window.localStorage.getItem(PREF) === "on";
  } catch (err) {
    return false;
  }
}

function writePref(on) {
  try {
    window.localStorage.setItem(PREF, on ? "on" : "off");
  } catch (err) {
    // Only this browser forgets the choice.
  }
}

function alertKey(alert) {
  return alert.time + "|" + alert.text + "|" + alert.who;
}

// Whether this browser may show notifications from this page at all.
export function canNotify() {
  return !!window.isSecureContext && "Notification" in window;
}

export function alertsOn() {
  return notifyOn;
}

// The alerts in `status` that were not there before, the newest first;
// none while "tell me" is off, and none at the first status (those were
// there already when the page opened).
export function newAlerts(status) {
  const watch = status && status.tools && status.tools.watch;
  if (!watch || !Array.isArray(watch.alerts)) return [];
  const keys = watch.alerts.map(alertKey);
  const fresh = seen && notifyOn ? watch.alerts.filter((alert) => !seen.has(alertKey(alert))) : [];
  seen = new Set(keys);
  return fresh;
}

// Shows `fresh` as notifications when the browser allows it; false when
// it doesn't, and the page shows them itself.
export function announce(fresh) {
  if (!fresh.length || !canNotify() || Notification.permission !== "granted") return false;
  try {
    const shown = fresh.length > 3 ? [{ text: fresh.length + " new alerts", time: fresh[0].time, who: "" }] : fresh;
    shown.forEach((alert) => {
      const note = new Notification("LokiCTL", { body: alert.text + " · " + alert.time, tag: alertKey(alert) });
      note.onclick = () => {
        window.focus();
        location.hash = "#/watch";
      };
    });
    return true;
  } catch (err) {
    // Some phones only allow notifications from an installed app.
    return false;
  }
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

function Message({ msg, where }) {
  if (!msg || msg.where !== where) return null;
  return h("p", { class: "mode-line " + (msg.tone || ""), role: "status" }, msg.text);
}

// ---------- The page ----------

export class WatchPage extends Component {
  constructor() {
    super();
    this.state = { sending: false, want: null, msg: null, open: "", notify: notifyOn };
    this.timer = 0;
    this.seenStatus = null;
    this.onWatch = () => this.toggle("watch");
    this.onTrap = () => this.toggle("trap");
    this.onNotify = () => this.setNotify(!this.state.notify);
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

  part(which) {
    const tools = this.props.status && this.props.status.tools;
    return (tools && tools[which]) || null;
  }

  follow() {
    const want = this.state.want;
    if (!want) return;
    const status = this.props.status;
    if (status !== this.seenStatus) {
      this.seenStatus = status;
      refreshSoon();
    }
    const part = this.part(want.which) || {};
    if (part.state !== want.from) {
      this.setState({ want: null });
    } else if (Date.now() - want.since > WAIT_MS) {
      const why = part.why ? " " + part.why + "." : " Try again.";
      this.setState({ want: null, msg: { where: want.which, text: "Nothing changed." + why, tone: "warn" } });
    }
  }

  async toggle(which) {
    const part = this.part(which);
    if (!part || this.state.sending) return;
    const on = part.state !== "running";
    this.setState({ sending: true, msg: null });
    let code = 0;
    try {
      code = await post("/api/v1/" + which, { on: on ? "1" : "0" });
    } catch (err) {
      code = 0;
    }
    if (code !== 204) {
      this.setState({ sending: false, msg: { where: which, text: REFUSED[code] || NO_ANSWER, tone: "bad" } });
      return;
    }
    this.setState({ sending: false, want: { which, from: part.state, since: Date.now() } });
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.setState({}), WAIT_MS + 100);
    refreshSoon();
  }

  // On at once; the browser's question (asked once, from this tap) only
  // decides whether new alerts become notifications or a note on the page.
  setNotify(on) {
    notifyOn = on;
    writePref(on);
    this.setState({ notify: on, msg: on ? this.notifyNote() : null });
    if (on && canNotify() && Notification.permission === "default") {
      try {
        const asked = Notification.requestPermission();
        if (asked && asked.then) asked.then(() => this.setState({ msg: notifyOn ? this.notifyNote() : null }), () => {});
      } catch (err) {
        // An old browser: the page shows new alerts itself.
      }
    }
  }

  notifyNote() {
    if (!canNotify()) {
      return { where: "notify", text: "This browser only allows notifications from secure pages, so new alerts show at the top of this page instead.", tone: "" };
    }
    if (Notification.permission === "default") {
      return { where: "notify", text: "Answer the browser's question to get notifications. Until then, new alerts show at the top of this page.", tone: "" };
    }
    if (Notification.permission !== "granted") {
      return { where: "notify", text: "The browser said no to notifications, so new alerts show at the top of this page instead.", tone: "" };
    }
    return null;
  }

  watchPart(watch) {
    const running = watch.state === "running";
    const waiting = this.state.want && this.state.want.which === "watch";
    const alerts = watch.alerts || [];
    let empty = "";
    if (!alerts.length) empty = running ? "No alerts yet." : watch.can ? "Not watching." : "No network to watch.";
    return [
      h("div", { class: "sec" }, h("span", null, "Watch"), running ? h("span", { class: "live" }, h("i", { "aria-hidden": "true" }), "Listening") : null),
      h(
        "div",
        { class: "group" },
        h(
          "div",
          { class: "row icon-row" },
          h("span", { class: "tile" }, icon("watch", 17, 2)),
          h(
            "span",
            { class: "row-text" },
            h("b", { id: "watch-label" }, "Watch the network"),
            h("span", { class: "tool-line-s " + (TONE[watch.tone] || "") }, waiting ? "Asking…" : watch.line)
          ),
          h(Switch, {
            on: running,
            label: "Watch the network",
            disabled: this.state.sending || waiting || (!running && !watch.can),
            onClick: this.onWatch,
          })
        )
      ),
      h(Message, { msg: this.state.msg, where: "watch" }),
      h("div", { class: "sec" }, h("span", null, "Alerts"), alerts.length ? h("span", { class: "mono" }, String(alerts.length)) : null),
      h(
        "div",
        { class: "group" },
        alerts.length
          ? alerts.map((alert) => {
              const key = alertKey(alert);
              const open = this.state.open === key;
              return h(
                "div",
                { class: "alert-item", key },
                h(
                  "button",
                  {
                    type: "button",
                    class: "row row-button alert-row",
                    "aria-expanded": open ? "true" : "false",
                    onClick: () => this.setState({ open: open ? "" : key }),
                  },
                  h("span", { class: "feed-time mono" }, alert.time),
                  h(
                    "span",
                    { class: "row-text" },
                    h("b", null, alert.text),
                    h("span", null, [alert.who, alert.fact].filter(Boolean).join(" · "))
                  ),
                  alert.serious ? h("span", { class: "pill p-warn" }, alert.name) : h("span", { class: "pill p-new" }, alert.name)
                ),
                open ? h("p", { class: "alert-about" }, alert.about) : null
              );
            })
          : h("div", { class: "row" }, h("span", { class: "row-key muted" }, empty))
      ),
      h("p", { class: "note" }, "Watch only listens. It warns you and never blocks or sends anything."),
    ];
  }

  trapPart(trap) {
    const running = trap.state === "running";
    const waiting = this.state.want && this.state.want.which === "trap";
    const hits = trap.hits || [];
    return [
      h("div", { class: "sec" }, h("span", null, "Trap")),
      h(
        "div",
        { class: "group" },
        h(
          "div",
          { class: "row icon-row" },
          h("span", { class: "tile" }, icon("lock", 17, 2)),
          h(
            "span",
            { class: "row-text" },
            h("b", null, "Fake services"),
            h("span", { class: "tool-line-s " + (TONE[trap.tone] || "") }, waiting ? "Asking…" : trap.line)
          ),
          h(Switch, { on: running, label: "Trap", disabled: this.state.sending || waiting, onClick: this.onTrap })
        ),
        h(
          "div",
          { class: "trap-ports" },
          (trap.ports || []).map((port) =>
            h(
              "span",
              { class: "trap-port" + (port.state === "open" ? " open" : ""), key: port.port, title: port.name },
              h("span", { class: "mono" }, String(port.port)),
              " ",
              PORT_WORD[port.state] || port.state
            )
          )
        )
      ),
      h(Message, { msg: this.state.msg, where: "trap" }),
      trap.helper ? h("p", { class: "mode-line warn" }, "Low ports closed: " + trap.helper + ".") : null,
      h("div", { class: "sec" }, h("span", null, "Who touched it"), hits.length ? h("span", { class: "mono" }, String(trap.raised)) : null),
      h(
        "div",
        { class: "group" },
        hits.length
          ? hits.map((hit, i) =>
              h(
                "div",
                { class: "row alert-row", key: i },
                h("span", { class: "feed-time mono" }, hit.time),
                h(
                  "span",
                  { class: "row-text" },
                  h("b", null, h("span", { class: "mono inl" }, hit.who), " tried " + hit.word),
                  h("span", null, hit.name + ", port " + hit.port + " · " + (hit.sent ? "sent: " + hit.sent : "sent nothing"))
                )
              )
            )
          : h("div", { class: "row" }, h("span", { class: "row-key muted" }, running ? "Waiting for snoopers." : "Nobody yet."))
      ),
      h("p", { class: "note" }, "Nothing real runs behind the trap. Each visitor gets a harmless greeting and is let go."),
    ];
  }

  notifyPart() {
    const on = this.state.notify;
    return [
      h("div", { class: "sec" }, h("span", null, "On this browser")),
      h(
        "div",
        { class: "group" },
        h(
          "div",
          { class: "row" },
          h(
            "span",
            { class: "row-text" },
            h("b", null, "Tell me about new alerts"),
            h("span", null, on ? "While a dashboard page is open and in front." : "Off.")
          ),
          h(Switch, { on, label: "Tell me about new alerts", onClick: this.onNotify })
        )
      ),
      h(Message, { msg: this.state.msg, where: "notify" }),
    ];
  }

  render({ status }) {
    const titles = [
      h("h1", { class: "large-title phone-only", key: "p" }, "Watch"),
      h("h1", { class: "page-title desk-only", key: "d" }, "Watch and trap"),
    ];
    const watch = this.part("watch");
    const trap = this.part("trap");
    if (!watch || !trap) return titles;
    return [
      titles,
      status.sample ? h("p", { class: "meta" }, h("span", { class: "tag" }, "Sample data")) : null,
      h(
        "div",
        { class: "modes-cols" },
        h("div", { class: "modes-col" }, this.watchPart(watch)),
        h("div", { class: "modes-col" }, this.trapPart(trap), this.notifyPart())
      ),
    ];
  }
}

// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Cardputer screen page (#/screen): the Cardputer's screen, scaled to
// fit, with its keys laid out like the device's: 4 to 8 right under the
// screen, then ESC, the arrows, Enter and Help. A key here presses that key
// on the Cardputer. While the screen itself has the focus, typing on the
// computer types on the Cardputer too; Tab still leaves it, and nothing is
// sent while the command palette is open (it has the focus then).
//
// Pictures come from GET /screen.png?since=<frame>, one request at a time:
// about every 250 ms for 2 s after a key, then every second, every 2 s once
// 30 s passed without a key or a new picture, and none while the tab is
// hidden. They are drawn on a canvas, so the dashboard's policy needs no
// exception for pictures. When the Cardputer stops answering, the last
// picture stays, dimmed, and the page tries again every 3 s.

import { h, Component } from "./preact.module.js";
import { icon, post, refreshSoon } from "./ui.js";

const QUICK = 250;
const NORMAL = 1000;
const RESTING = 2000;
const RETRY = 3000;
const MAX_KEYS = 16;
const WIDTH = 320;
const HEIGHT = 170;

// Keys of the computer's keyboard the Cardputer knows by name; printable
// characters go as they are.
const NAMES = {
  Escape: "esc",
  Enter: "enter",
  Backspace: "backspace",
  ArrowUp: "up",
  ArrowDown: "down",
  ArrowLeft: "left",
  ArrowRight: "right",
  F1: "help",
};

// The soft keys, centred under the screen where their labels are drawn
// (x = 49, 105, 161, 217 and 273 of 320).
const SOFT = ["4", "5", "6", "7", "8"];

// The pad: [key name, what it shows, its label for screen readers, place].
const PAD = [
  ["esc", "ESC", "Escape", "k-esc"],
  ["up", null, "Up", "k-up"],
  ["help", "Help", "Help", "k-help"],
  ["left", null, "Left", "k-left"],
  ["down", null, "Down", "k-down"],
  ["right", null, "Right", "k-right"],
  ["enter", "Enter", "Enter", "k-enter"],
];
const ARROW_ICON = { up: "up", down: "down", left: "back", right: "chevron" };

function limit() {
  return typeof AbortSignal !== "undefined" && AbortSignal.timeout ? AbortSignal.timeout(5000) : undefined;
}

export class ScreenPage extends Component {
  constructor() {
    super();
    // shown: a picture arrived; lost: the Cardputer stopped answering;
    // typing: the screen has the focus; note: a short problem with a key.
    this.state = { shown: false, lost: false, typing: false, note: "" };
    this.canvas = null;
    this.panel = null;
    // A computer with a mouse most likely has a keyboard to type with too.
    this.keyboard = !!(window.matchMedia && window.matchMedia("(pointer: fine)").matches);
    this.gone = false;
    this.frame = 0;
    this.timer = 0;
    this.noteTimer = 0;
    this.polling = false;
    this.quick = false;
    this.keys = [];
    this.sending = false;
    this.lastKey = -1e9;
    this.lastChange = 0;
    this.onVisible = () => {
      if (document.hidden) clearTimeout(this.timer);
      else if (this.polling) this.quick = true;
      else this.poll();
    };
    this.onPanelKey = (event) => this.typed(event);
    this.onFocus = () => this.setState({ typing: true });
    this.onBlur = () => this.setState({ typing: false });
    // A click on a key keeps the focus where it is (on the screen, when
    // typing), so the next typed key still goes to the Cardputer.
    this.keepFocus = (event) => event.preventDefault();
  }

  componentDidMount() {
    this.lastChange = performance.now();
    document.addEventListener("visibilitychange", this.onVisible);
    this.poll();
    // On a computer the screen is ready to type on.
    if (this.panel && this.keyboard) {
      this.panel.focus({ preventScroll: true });
    }
  }

  componentWillUnmount() {
    this.gone = true;
    this.keys = [];
    clearTimeout(this.timer);
    clearTimeout(this.noteTimer);
    document.removeEventListener("visibilitychange", this.onVisible);
  }

  // A problem that passes: the line goes away after 3 s.
  warn(text) {
    clearTimeout(this.noteTimer);
    this.setState({ note: text });
    this.noteTimer = setTimeout(() => this.setState({ note: "" }), 3000);
  }

  // ---------- Pictures ----------

  schedule(ms) {
    clearTimeout(this.timer);
    if (!this.gone && !document.hidden) this.timer = setTimeout(() => this.poll(), ms);
  }

  pause() {
    const now = performance.now();
    if (now - this.lastKey < 2000) return QUICK;
    if (now - Math.max(this.lastKey, this.lastChange) >= 30000) return RESTING;
    return NORMAL;
  }

  // A picture soon, for example after a key.
  soon() {
    this.lastKey = performance.now();
    if (this.polling) this.quick = true;
    else this.schedule(150);
  }

  async poll() {
    if (this.polling || this.gone || document.hidden) return;
    this.polling = true;
    const started = performance.now();
    let next = 0;
    try {
      const r = await fetch("/screen.png?since=" + this.frame, { cache: "no-store", credentials: "same-origin", signal: limit() });
      if (r.status === 401) {
        // The session ended: the dashboard asks for the new code.
        this.polling = false;
        refreshSoon();
        return;
      }
      if (r.status === 200) {
        const frame = Number(r.headers.get("X-Frame")) || 0;
        const picture = await createImageBitmap(await r.blob());
        if (!this.gone && this.canvas) {
          this.canvas.getContext("2d").drawImage(picture, 0, 0, WIDTH, HEIGHT);
          this.frame = frame;
          this.lastChange = performance.now();
          if (!this.state.shown) this.setState({ shown: true });
        }
        if (picture.close) picture.close();
      } else if (r.status !== 204) {
        next = NORMAL;
      }
      if (this.state.lost && !this.gone) this.setState({ lost: false });
    } catch (err) {
      if (!this.gone) this.setState({ lost: true });
      next = RETRY;
    }
    this.polling = false;
    if (this.gone) return;
    // The usual pauses count from the start of this request.
    this.schedule(this.quick ? 150 : next || Math.max(50, this.pause() - (performance.now() - started)));
    this.quick = false;
  }

  // ---------- Keys ----------

  // Keys go one at a time, so they arrive in the order they were pressed.
  press(name) {
    if (this.keys.length >= MAX_KEYS) {
      this.warn("Too many keys at once.");
      return;
    }
    this.keys.push(name);
    this.pump();
  }

  async pump() {
    if (this.sending) return;
    this.sending = true;
    while (this.keys.length && !this.gone) {
      const k = this.keys.shift();
      let code = 0;
      try {
        code = await post("/key", { k });
      } catch (err) {
        this.keys = [];
        if (!this.gone) this.setState({ lost: true });
        break;
      }
      if (code === 401) {
        this.keys = [];
        refreshSoon();
        break;
      }
      if (code === 429) {
        this.keys = [];
        this.warn("Too many keys at once. Slow down a little.");
      } else if (code === 403) {
        this.warn("The Cardputer refused a key. Reload the page and try again.");
      } else if (code !== 204) {
        this.warn("The Cardputer didn't take a key (answer " + code + ").");
      }
      this.soon();
    }
    this.sending = false;
  }

  // A key typed while the screen has the focus. Shortcuts with Cmd or Ctrl
  // stay with the browser (⌘K opens the palette); AltGr and Option still
  // type characters; Tab moves on.
  typed(event) {
    if (event.target !== event.currentTarget || event.metaKey || event.isComposing) return;
    const altGraph = event.getModifierState && event.getModifierState("AltGraph");
    if (event.ctrlKey && !altGraph) return;
    let k = "";
    if (event.key.length === 1) {
      const c = event.key.charCodeAt(0);
      if (c >= 32 && c <= 126) k = "c" + c;
    } else if (!event.altKey && !event.ctrlKey) {
      k = NAMES[event.key] || "";
    }
    if (!k) return;
    event.preventDefault();
    if (!event.repeat) this.press(k);
  }

  key(name, label, shown, place) {
    return h(
      "button",
      {
        type: "button",
        class: "cap " + place,
        "aria-label": label,
        onMouseDown: this.keepFocus,
        onClick: () => this.press(name),
        key: name,
      },
      shown === null ? icon(ARROW_ICON[name], 20, 2.4) : shown
    );
  }

  render(props, { shown, lost, typing, note }) {
    let hint = "Tap a key to press it on the Cardputer.";
    if (typing) hint = "Typing goes to the Cardputer. Tab leaves the screen.";
    else if (this.keyboard) hint = "Select the screen to type on the Cardputer, or press its keys here.";
    let state = h("span", { class: "live" }, h("i", { "aria-hidden": "true" }), "Live");
    if (lost) state = h("span", { class: "live warn" }, h("i", { "aria-hidden": "true" }), "No answer");
    else if (!shown) state = h("span", { class: "live off" }, "Waiting");
    return h(
      "div",
      { class: "scr-page" },
      h("h1", { class: "large-title phone-only" }, "Cardputer screen"),
      h("h1", { class: "page-title desk-only" }, "Cardputer screen"),
      h(
        "div",
        { class: "scr-grid" },
        h(
          "section",
          { class: "scr-main", "aria-label": "The Cardputer's screen and keys" },
          h(
            "div",
            { class: "scr-device" },
            h(
              "div",
              {
                class: "scr-panel" + (typing ? " typing" : ""),
                tabIndex: 0,
                role: "application",
                "aria-roledescription": "remote screen",
                "aria-label": "The Cardputer's screen. While it has the focus, typing types on the Cardputer.",
                "aria-describedby": "scr-hint",
                onKeyDown: this.onPanelKey,
                onFocus: this.onFocus,
                onBlur: this.onBlur,
                ref: (el) => (this.panel = el),
              },
              h("canvas", {
                class: "scr-canvas" + (lost ? " stale" : "") + (shown ? "" : " empty"),
                width: WIDTH,
                height: HEIGHT,
                role: "img",
                "aria-label": lost ? "The last picture of the Cardputer's screen" : "The Cardputer's screen",
                ref: (el) => (this.canvas = el),
              })
            ),
            h(
              "div",
              { class: "scr-soft", role: "group", "aria-label": "Soft keys 4 to 8" },
              SOFT.map((digit, i) => this.key("c" + (52 + i), "Key " + digit, h("span", { class: "mono" }, digit), "sk sk-" + digit))
            )
          ),
          h(
            "div",
            { class: "scr-pad", role: "group", "aria-label": "Keys" },
            PAD.map(([name, shownText, label, place]) => this.key(name, label, shownText, place))
          ),
          h("div", { class: "scr-state" }, h("span", { role: "status" }, state), h("p", { id: "scr-hint" }, hint)),
          note ? h("p", { class: "mode-line warn", role: "alert" }, note) : null
        ),
        h(
          "aside",
          { class: "scr-notes", "aria-label": "About the screen" },
          h("h2", { class: "scr-notes-title" }, "Keys"),
          h(
            "p",
            null,
            "Keys 4 to 8 sit right under the Cardputer's screen and do what the screen shows above them. F, X, Z and C move like arrows."
          ),
          h(
            "p",
            { class: "desk-only" },
            "Typing on this computer types on the Cardputer: letters, digits, Enter, Esc, Backspace, the arrows, and F1 for help."
          ),
          h("p", null, "A key from here is a short press. Holding ESC and fn+J work only on the Cardputer itself."),
          h("p", null, "When its screen has gone dark, the first key only wakes it.")
        )
      )
    );
  }
}

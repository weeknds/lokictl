// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The dashboard's start: is this browser paired? Then the frame and its
// pages, with the status asked for every 2 s while the page is visible and
// not at all while it is hidden. Otherwise the pairing screen. New Watch
// alerts (when this browser asked for them) become notifications, or a
// note at the top of the page and a count in the tab's title.

import { h, render, Component } from "./preact.module.js";
import { getJson, setToken, REFRESH_EVENT } from "./ui.js";
import { Pair } from "./pair.js";
import { Shell, findPage } from "./shell.js";
import { newAlerts, announce } from "./watch.js";

const POLL_MS = 2000;
const RETRY_MS = 3000;
const REFRESH_MS = 700;
const ENDED = "Your session ended. Type the new code from the Cardputer.";

// "#/devices/10.0.0.43": the page ("devices") and what it shows (one
// device, by its id; on Security one finding, by its id); the sub-path is
// empty for the page itself.
function routeFromHash() {
  const parts = location.hash.replace(/^#\/?/, "").split("?")[0].split("/");
  const route = findPage(parts[0] || "home").id;
  let sub = "";
  if ((route === "devices" || route === "security" || route === "tools") && parts[1]) {
    try {
      sub = decodeURIComponent(parts[1]);
    } catch (err) {
      sub = "";
    }
  }
  return { route, sub };
}

class App extends Component {
  constructor() {
    super();
    // "start": asking whether this browser is paired; then "pair" or "shell".
    const where = routeFromHash();
    this.state = { view: "start", route: where.route, sub: where.sub, status: null, lost: false, message: "", bad: false, toast: null };
    this.onToastClose = () => this.setState({ toast: null });
    this.timer = 0;
    this.asking = false;
    this.onHash = () => {
      const { route, sub } = routeFromHash();
      if (route !== this.state.route || sub !== this.state.sub) {
        this.setState({ route, sub, toast: route === "watch" ? null : this.state.toast });
        window.scrollTo(0, 0);
      }
    };
    // A page that changed something (Block, Allow) asks for the news soon.
    this.onRefresh = () => {
      if (this.state.view === "shell") this.schedule(REFRESH_MS);
    };
    this.onVisible = () => {
      if (document.hidden) {
        clearTimeout(this.timer);
      } else if (this.state.view === "shell") {
        this.poll();
      }
    };
    this.onPaired = () => this.start();
  }

  componentDidMount() {
    window.addEventListener("hashchange", this.onHash);
    document.addEventListener("visibilitychange", this.onVisible);
    window.addEventListener(REFRESH_EVENT, this.onRefresh);
    this.start();
  }

  componentWillUnmount() {
    clearTimeout(this.timer);
    window.removeEventListener("hashchange", this.onHash);
    document.removeEventListener("visibilitychange", this.onVisible);
    window.removeEventListener(REFRESH_EVENT, this.onRefresh);
  }

  componentDidUpdate() {
    const page = findPage(this.state.route);
    const toast = this.state.toast;
    const count = toast ? "(" + toast.count + ") " : "";
    document.title = this.state.view === "shell" ? count + page.title + " · LokiCTL" : "Connect · LokiCTL";
  }

  // The session's request token: there is one only when this browser is
  // paired, so it also says which screen to show.
  async start() {
    let answer;
    try {
      answer = await getJson("/api/v1/session");
    } catch (err) {
      this.showPair("No answer from the Cardputer. Is Remote view on?", true);
      return;
    }
    if (answer.status === 200 && answer.data && typeof answer.data.token === "string") {
      setToken(answer.data.token);
      // Preact applies state later: poll once the frame is up.
      this.setState({ view: "shell", lost: false }, () => this.poll());
      return;
    }
    this.showPair(this.state.view === "shell" ? ENDED : "", this.state.view === "shell");
  }

  showPair(message, bad) {
    clearTimeout(this.timer);
    setToken("");
    this.setState({ view: "pair", status: null, lost: false, message, bad });
  }

  schedule(ms) {
    clearTimeout(this.timer);
    if (this.state.view === "shell" && !document.hidden) {
      this.timer = setTimeout(() => this.poll(), ms);
    }
  }

  async poll() {
    if (this.asking || document.hidden || this.state.view !== "shell") return;
    this.asking = true;
    clearTimeout(this.timer);
    let next = POLL_MS;
    try {
      const answer = await getJson("/api/v1/status");
      if (answer.status === 401) {
        this.asking = false;
        this.showPair(ENDED, true);
        return;
      }
      if (answer.status === 200 && answer.data) {
        const fresh = newAlerts(answer.data);
        let toast = this.state.toast;
        // On the Watch page itself the new alerts are in front already.
        if (fresh.length && this.state.route !== "watch" && !announce(fresh)) {
          const count = (toast ? toast.count : 0) + fresh.length;
          toast = { count, text: fresh[0].text, time: fresh[0].time };
        }
        this.setState({ status: answer.data, lost: false, toast });
      }
    } catch (err) {
      this.setState({ lost: true });
      next = RETRY_MS;
    }
    this.asking = false;
    this.schedule(next);
  }

  render(props, { view, route, sub, status, lost, message, bad, toast }) {
    if (view === "pair") {
      return h(Pair, { message, bad, onPaired: this.onPaired });
    }
    if (view === "shell") {
      return h(Shell, { route, sub, status, lost, toast, onToastClose: this.onToastClose });
    }
    return null;
  }
}

render(h(App), document.getElementById("app"));

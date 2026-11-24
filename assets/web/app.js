// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The dashboard's start: is this browser paired? Then the frame and its
// pages, with the status asked for every 2 s while the page is visible and
// not at all while it is hidden. Otherwise the pairing screen.

import { h, render, Component } from "./preact.module.js";
import { getJson, setToken } from "./ui.js";
import { Pair } from "./pair.js";
import { Shell, findPage } from "./shell.js";

const POLL_MS = 2000;
const RETRY_MS = 3000;
const ENDED = "Your session ended. Type the new code from the Cardputer.";

function routeFromHash() {
  const name = location.hash.replace(/^#\/?/, "").split(/[/?]/)[0];
  return findPage(name || "home").id;
}

class App extends Component {
  constructor() {
    super();
    // "start": asking whether this browser is paired; then "pair" or "shell".
    this.state = { view: "start", route: routeFromHash(), status: null, lost: false, message: "", bad: false };
    this.timer = 0;
    this.asking = false;
    this.onHash = () => {
      const route = routeFromHash();
      if (route !== this.state.route) {
        this.setState({ route });
        window.scrollTo(0, 0);
      }
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
    this.start();
  }

  componentWillUnmount() {
    clearTimeout(this.timer);
    window.removeEventListener("hashchange", this.onHash);
    document.removeEventListener("visibilitychange", this.onVisible);
  }

  componentDidUpdate() {
    const page = findPage(this.state.route);
    document.title = this.state.view === "shell" ? page.title + " · LokiCTL" : "Connect · LokiCTL";
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
        this.setState({ status: answer.data, lost: false });
      }
    } catch (err) {
      this.setState({ lost: true });
      next = RETRY_MS;
    }
    this.asking = false;
    this.schedule(next);
  }

  render(props, { view, route, status, lost, message, bad }) {
    if (view === "pair") {
      return h(Pair, { message, bad, onPaired: this.onPaired });
    }
    if (view === "shell") {
      return h(Shell, { route, status, lost });
    }
    return null;
  }
}

render(h(App), document.getElementById("app"));

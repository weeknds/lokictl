// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The pairing screen: a drawing of the Cardputer, six code cells over one
// real number field, Continue, where to find the code, and who can connect.
// The browser never knows the code: the drawing shows what you type, so you
// can hold it next to the Cardputer's screen.

import { h, Component } from "./preact.module.js";
import { icon, post } from "./ui.js";

const DIGITS = 6;

export class Pair extends Component {
  constructor(props) {
    super(props);
    this.state = {
      digits: "",
      focused: false,
      busy: false,
      message: props.message || "",
      bad: !!props.bad,
      help: false,
    };
    this.field = null;
    this.onInput = (e) => {
      const digits = e.currentTarget.value.replace(/\D/g, "").slice(0, DIGITS);
      e.currentTarget.value = digits;
      this.setState({ digits, message: "", bad: false });
    };
    this.onSubmit = (e) => {
      e.preventDefault();
      this.send();
    };
    this.onFocus = () => this.setState({ focused: true });
    this.onBlur = () => this.setState({ focused: false });
    this.onHelp = () => this.setState({ help: !this.state.help });
  }

  componentDidMount() {
    // On a computer the field is ready to type into. A phone opens its
    // keyboard only when the cells are tapped.
    if (this.field && window.matchMedia && window.matchMedia("(pointer: fine)").matches) {
      this.field.focus();
    }
  }

  say(message, bad) {
    this.setState({ message, bad });
  }

  async send() {
    const { digits, busy } = this.state;
    if (busy) return;
    if (digits.length !== DIGITS) {
      this.say("The code has 6 digits.", true);
      return;
    }
    this.setState({ busy: true, message: "Connecting…", bad: false });
    let status = 0;
    try {
      status = await post("/pair", { code: digits });
    } catch (err) {
      this.setState({ busy: false });
      this.say("No answer from the Cardputer. Is Remote view still on?", true);
      return;
    }
    this.setState({ busy: false });
    if (status === 200) {
      this.say("", false);
      this.props.onPaired();
    } else if (status === 403) {
      if (this.field) this.field.value = "";
      this.setState({ digits: "" });
      this.say("That code didn't match. Check the Cardputer's screen and try again. After 5 wrong tries Remote view turns off.", true);
    } else if (status === 409) {
      this.say("Another browser is already connected to this Cardputer.", true);
    } else {
      this.say("Couldn't connect (answer " + status + "). Try again.", true);
    }
  }

  // "482 913" while typing: the digits so far, dots for the rest.
  drawnCode() {
    const padded = (this.state.digits + "······").slice(0, DIGITS);
    return padded.slice(0, 3) + " " + padded.slice(3);
  }

  render() {
    const { digits, focused, busy, message, bad, help } = this.state;
    const active = Math.min(digits.length, DIGITS - 1);
    const cells = [];
    for (let i = 0; i < DIGITS; i++) {
      const on = focused && i === active;
      cells.push(
        h(
          "div",
          { class: on ? "cell on" : "cell", "aria-hidden": "true" },
          digits[i] || (on && digits.length < DIGITS ? h("span", { class: "caret" }) : "")
        )
      );
    }
    return h(
      "main",
      { class: "pair" },
      h(
        "div",
        { class: "pair-in" },
        h(
          "div",
          { class: "device", role: "img", "aria-label": "The Cardputer's screen shows the code under Remote view" },
          h(
            "div",
            { class: "device-screen" },
            h("span", { class: "device-label" }, "REMOTE VIEW"),
            h("span", { class: "device-code", "aria-hidden": "true" }, this.drawnCode()),
            h("span", { class: "device-label device-address" }, location.host)
          )
        ),
        h("h1", null, "Connect to your Cardputer"),
        h("p", { class: "pair-sub" }, "Type the 6-digit code shown on its screen."),
        h(
          "form",
          { class: "pair-form", onSubmit: this.onSubmit, autocomplete: "off", novalidate: true },
          h(
            "div",
            { class: "cells" },
            cells,
            h("label", { for: "code", class: "sr" }, "6-digit code"),
            h("input", {
              id: "code",
              class: "code-input",
              type: "text",
              inputmode: "numeric",
              pattern: "[0-9]*",
              autocomplete: "one-time-code",
              maxlength: 7,
              spellcheck: false,
              "aria-describedby": "pair-msg",
              ref: (el) => (this.field = el),
              onInput: this.onInput,
              onFocus: this.onFocus,
              onBlur: this.onBlur,
            })
          ),
          h("p", { id: "pair-msg", class: bad ? "pair-msg bad" : "pair-msg", role: "status", "aria-live": "polite" }, message),
          h("button", { class: "btn-continue", type: "submit", disabled: busy }, "Continue")
        ),
        h(
          "div",
          { class: help ? "help open" : "help" },
          h(
            "button",
            { class: "help-row", type: "button", "aria-expanded": help ? "true" : "false", onClick: this.onHelp },
            icon("help", 22, 1.8),
            h("span", { class: "help-text" }, h("b", null, "Where is the code?"), h("span", null, "Settings, then Remote view")),
            h("span", { class: "help-chev" }, icon("chevron", 14, 2.4))
          ),
          help
            ? h(
                "p",
                { class: "help-more" },
                "On the Cardputer, open Settings, go down to Remote view and press 6 (On). The code and this address show on its screen for 2 minutes."
              )
            : null
        ),
        h(
          "p",
          { class: "pair-foot" },
          icon("lock", 16, 2),
          h("span", null, "Only phones and computers on your network or on the Cardputer's hotspot can connect. A new code every time.")
        )
      )
    );
  }
}

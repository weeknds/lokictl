/*
 * SPDX-FileCopyrightText: 2026 LokiCTL contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "remote/remote_page.h"

namespace platform::remote {
namespace {

constexpr std::string_view kSecurityPolicy =
    "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src blob:; "
    "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

// The page. Keys go to POST /key as k=<name> (see key_from_name()), one at a
// time so that typed text keeps its order, each with the session's request
// token (GET /api/v1/session) in X-LokiCTL-Token; pictures come from
// GET /screen.png?since=<frame>, one request at a time: about every 250 ms
// for 2 s after a key, then every second, every 2 s once 30 s passed without
// a key or a new picture, and none while the tab is hidden. When the
// Cardputer stops answering (an error, or no answer within 5 s), the last
// picture stays, dimmed, and the page tries again every 3 s; only a 401
// brings back the code form.
constexpr std::string_view kPage = R"page(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LokiCTL remote view</title>
<style>
:root{color-scheme:dark;--bg:#0f1113;--fg:#e6e8ea;--dim:#9aa1a7;--key:#202428;--hi:#2c3136;--line:#3a4046;--go:#5ccfb8;--bad:#f2867c}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--fg)}
body{padding:16px;font:15px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
main{margin:0 auto;width:min(100%,960px,max(320px,calc((100vh - 260px) * 1.88)))}
h1{margin:0 0 10px;font-size:15px;font-weight:600;color:var(--dim)}
.hide{display:none!important}
#msg{min-height:1.45em;margin:0 0 10px;color:var(--dim)}
#msg.bad{color:var(--bad)}
form{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
input{width:11ch;padding:8px 10px;font:inherit;font-size:22px;letter-spacing:.15em;color:var(--fg);background:var(--key);border:1px solid var(--line);border-radius:6px}
button{padding:8px 12px;font:inherit;color:var(--fg);background:var(--key);border:1px solid var(--line);border-radius:6px;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:manipulation}
button:hover{background:var(--hi)}
button:active{border-color:var(--go);color:var(--go)}
button:disabled{opacity:.5;cursor:default}
#screen{display:block;width:100%;aspect-ratio:320/170;image-rendering:pixelated;background:#000;border:1px solid var(--line);border-radius:4px}
#screen.stale{opacity:.4}
.soft{position:relative;height:42px;margin-top:6px}
.soft button{position:absolute;top:0;width:12%;height:100%;padding:0;transform:translateX(-50%)}
.pad{display:grid;grid-template-columns:repeat(3,52px) 1fr 96px;grid-template-rows:repeat(2,44px);gap:6px;margin-top:14px}
.pad button{padding:0}
.note{margin:14px 0 0;font-size:13px;color:var(--dim)}
.reports{margin-top:20px;padding-top:12px;border-top:1px solid var(--line)}
.reports h2{margin:0 0 6px;font-size:15px;font-weight:600;color:var(--dim)}
#reports{list-style:none;margin:8px 0 0;padding:0}
#reports li{margin:4px 0;overflow-wrap:anywhere}
a{color:var(--go)}
</style>
</head>
<body>
<main>
<h1>LokiCTL remote view</h1>
<p id="msg"></p>
<form id="pair" class="hide" autocomplete="off">
<input id="code" inputmode="numeric" maxlength="7" placeholder="000000" aria-label="Code from the Cardputer">
<button type="submit">Pair</button>
</form>
<div id="view" class="hide">
<img id="screen" alt="The Cardputer's screen">
<div class="soft">
<button tabindex="-1" data-k="c52" style="left:15.31%">4</button>
<button tabindex="-1" data-k="c53" style="left:32.81%">5</button>
<button tabindex="-1" data-k="c54" style="left:50.31%">6</button>
<button tabindex="-1" data-k="c55" style="left:67.81%">7</button>
<button tabindex="-1" data-k="c56" style="left:85.31%">8</button>
</div>
<div class="pad">
<button tabindex="-1" data-k="esc" style="grid-area:1/1">ESC</button>
<button tabindex="-1" data-k="up" style="grid-area:1/2" aria-label="Up">&uarr;</button>
<button tabindex="-1" data-k="help" style="grid-area:1/5">Help</button>
<button tabindex="-1" data-k="left" style="grid-area:2/1" aria-label="Left">&larr;</button>
<button tabindex="-1" data-k="down" style="grid-area:2/2" aria-label="Down">&darr;</button>
<button tabindex="-1" data-k="right" style="grid-area:2/3" aria-label="Right">&rarr;</button>
<button tabindex="-1" data-k="enter" style="grid-area:2/5">Enter</button>
</div>
<p class="note">Typing here types on the Cardputer: letters, digits, Enter, Esc, Backspace, the arrows, F1 for help.</p>
<section class="reports">
<h2>Reports</h2>
<p class="note">Saved on the Cardputer with key 8 on Health, R on Watch or Discover, or the command report. Click one to download it.</p>
<button type="button" id="refresh">Show saved reports</button>
<ul id="reports"></ul>
</section>
</div>
<p class="note">Plain HTTP: use Remote view only on a network you trust. <a href="/">Open the dashboard</a></p>
</main>
<script>
"use strict";
const $ = (id) => document.getElementById(id);
const form = $("pair"), code = $("code"), view = $("view"), img = $("screen"), msg = $("msg");
const list = $("reports");
const FORM = {"Content-Type": "application/x-www-form-urlencoded"};
const NAMES = {Escape: "esc", Enter: "enter", Backspace: "backspace", ArrowUp: "up",
  ArrowDown: "down", ArrowLeft: "left", ArrowRight: "right", F1: "help"};
const ENDED = "Remote view ended. Switch it on again on the Cardputer and type the new code.";
const LOST = "No answer from the Cardputer. Trying again...";
// Milliseconds between pictures: soon after a key, normally, when nothing
// happened for a while, and while the Cardputer does not answer.
const QUICK = 250, NORMAL = 1000, RESTING = 2000, RETRY = 3000;
// A Cardputer that is off or out of range never answers: give up after 5 s.
const limit = () => (AbortSignal.timeout ? AbortSignal.timeout(5000) : undefined);
let state = "probe"; // "probe", then "form" or "live"
let frame = 0, timer = 0, warnTimer = 0, blobUrl = "", polling = false, quick = false;
let keys = [], sending = false, lost = false, token = "";
let lastKey = -1e9, lastChange = 0;

function say(text, bad) {
  clearTimeout(warnTimer);
  msg.textContent = text || "";
  msg.className = bad ? "bad" : "";
}

// A problem that passes: the message goes away after 3 s.
function warn(text) {
  say(text, true);
  warnTimer = setTimeout(() => say(lost ? LOST : "", lost), 3000);
}

// The picture is out of date while the Cardputer does not answer.
function setLost(value) {
  if (lost === value) return;
  lost = value;
  img.classList.toggle("stale", lost);
  say(lost ? LOST : "", lost);
}

function showForm(text, bad) {
  state = "form";
  clearTimeout(timer);
  keys = [];
  token = "";
  lost = false;
  img.classList.remove("stale");
  view.classList.add("hide");
  form.classList.remove("hide");
  say(text, bad);
  code.focus();
}

function showView() {
  state = "live";
  lastChange = performance.now();
  form.classList.add("hide");
  view.classList.remove("hide");
  say("");
}

// The saved reports, as download links. Names come from the Cardputer and
// are only ever set as text.
async function loadReports() {
  try {
    const r = await fetch("/reports", {cache: "no-store", signal: limit()});
    if (r.status === 401) {
      showForm(ENDED, true);
      return;
    }
    if (r.status !== 200) {
      warn("Could not list the reports (answer " + r.status + ").");
      return;
    }
    const data = await r.json();
    const items = data && Array.isArray(data.reports) ? data.reports : [];
    list.textContent = "";
    if (!items.length) {
      const li = document.createElement("li");
      li.textContent = "No reports saved yet.";
      list.append(li);
      return;
    }
    for (const item of items) {
      const name = String(item.name);
      const li = document.createElement("li");
      const a = document.createElement("a");
      a.href = "/report?name=" + encodeURIComponent(name);
      a.download = name;
      a.textContent = name;
      li.append(a, " (" + Math.max(1, Math.round(Number(item.bytes) / 1024)) + " KB)");
      list.append(li);
    }
  } catch (err) {
    warn("Could not list the reports.");
  }
}

// No timer while the tab is hidden: becoming visible asks at once.
function schedule(ms) {
  clearTimeout(timer);
  if (state === "live" && !document.hidden) timer = setTimeout(poll, ms);
}

function pause() {
  const now = performance.now();
  if (now - lastKey < 2000) return QUICK;
  if (now - Math.max(lastKey, lastChange) >= 30000) return RESTING;
  return NORMAL;
}

// A picture soon, for example after a key.
function soon() {
  lastKey = performance.now();
  if (polling) quick = true;
  else schedule(150);
}

async function poll() {
  if (polling || document.hidden) return;
  polling = true;
  const started = performance.now();
  let next = 0;
  try {
    const r = await fetch("/screen.png?since=" + frame, {cache: "no-store", signal: limit()});
    if (r.status === 401) {
      polling = false;
      if (state === "probe") showForm("Type the code shown on the Cardputer.", false);
      else showForm(ENDED, true);
      return;
    }
    if (r.status === 200) {
      const blob = await r.blob();
      frame = Number(r.headers.get("X-Frame")) || 0;
      lastChange = performance.now();
      const url = URL.createObjectURL(blob);
      img.src = url;
      if (blobUrl) URL.revokeObjectURL(blobUrl);
      blobUrl = url;
    } else if (r.status !== 204) {
      next = NORMAL;
    }
    if (state === "probe") {
      showView();
      loadReports();
    }
    setLost(false);
  } catch (err) {
    if (state === "probe") {
      polling = false;
      showForm("No answer from the Cardputer. Is Remote view on?", true);
      return;
    }
    setLost(true);
    next = RETRY;
  }
  polling = false;
  // The usual pauses count from the start of this request.
  schedule(quick ? 150 : next || Math.max(50, pause() - (performance.now() - started)));
  quick = false;
}

document.addEventListener("visibilitychange", () => {
  if (document.hidden) clearTimeout(timer);
  else if (polling) quick = true;
  else if (state !== "form") poll();
});

form.addEventListener("submit", async (e) => {
  e.preventDefault();
  const digits = code.value.replace(/\D/g, "");
  if (digits.length !== 6) {
    say("The code has 6 digits.", true);
    return;
  }
  const button = form.querySelector("button");
  button.disabled = true;
  say("Pairing...");
  try {
    const r = await fetch("/pair", {method: "POST", headers: FORM, body: "code=" + digits,
      signal: limit()});
    if (r.status === 200) {
      code.value = "";
      frame = 0;
      showView();
      poll();
      loadReports();
    } else if (r.status === 403) {
      say("Wrong code. 5 wrong codes switch Remote view off.", true);
    } else if (r.status === 409) {
      say("Another browser is already paired.", true);
    } else {
      say("Could not pair (answer " + r.status + ").", true);
    }
  } catch (err) {
    say("No answer from the Cardputer. Is Remote view on?", true);
  }
  button.disabled = false;
});

// The session's request token, which every key carries.
async function getToken() {
  try {
    const r = await fetch("/api/v1/session", {cache: "no-store", signal: limit()});
    if (r.status === 401) {
      showForm(ENDED, true);
      return "";
    }
    if (r.status === 200) {
      const data = await r.json();
      token = data && typeof data.token === "string" ? data.token : "";
    }
  } catch (err) {
    setLost(true);
  }
  return token;
}

// Keys go one at a time, so they arrive in the order they were pressed.
function press(name) {
  if (state !== "live") return;
  if (keys.length >= 16) {
    warn("Too many keys at once.");
    return;
  }
  keys.push(name);
  pump();
}

async function pump() {
  if (sending) return;
  sending = true;
  while (keys.length && state === "live") {
    if (!token && !(await getToken())) {
      keys = [];
      break;
    }
    const k = keys.shift();
    try {
      const r = await fetch("/key", {method: "POST", headers: {...FORM, "X-LokiCTL-Token": token},
        body: "k=" + k, referrerPolicy: "same-origin", signal: limit()});
      if (r.status === 401) {
        sending = false;
        showForm(ENDED, true);
        return;
      }
      if (r.status === 403) {
        token = "";
        warn("The Cardputer did not take a key. Try again.");
      } else if (r.status === 429) {
        keys = [];
        warn("Too many keys at once. Slow down a little.");
      } else if (r.status !== 204) {
        warn("The Cardputer did not take a key (answer " + r.status + ").");
      }
    } catch (err) {
      keys = [];
      setLost(true);
    }
    soon();
  }
  sending = false;
}

$("refresh").addEventListener("click", loadReports);
view.addEventListener("mousedown", (e) => {
  if (e.target.closest("button")) e.preventDefault();
});
view.addEventListener("click", (e) => {
  const b = e.target.closest("button[data-k]");
  if (b) press(b.dataset.k);
});
document.addEventListener("keydown", (e) => {
  if (state !== "live" || e.metaKey || e.isComposing) return;
  // Enter and space on a report link or the list button act on them here.
  if ((e.key === "Enter" || e.key === " ") && e.target.closest && e.target.closest("a,#refresh")) return;
  // AltGr (Ctrl+Alt on Windows) and Option still type characters.
  const altGraph = e.getModifierState && e.getModifierState("AltGraph");
  if (e.ctrlKey && !altGraph) return;
  let k = "";
  if (e.key.length === 1) {
    const c = e.key.charCodeAt(0);
    if (c >= 32 && c <= 126) k = "c" + c;
  } else if (!e.altKey && !e.ctrlKey) {
    k = NAMES[e.key] || "";
  }
  if (!k) return;
  e.preventDefault();
  if (!e.repeat) press(k);
});

poll();
</script>
</body>
</html>
)page";

} // namespace

std::string_view page_html() {
    return kPage;
}

std::string_view page_security_policy() {
    return kSecurityPolicy;
}

} // namespace platform::remote

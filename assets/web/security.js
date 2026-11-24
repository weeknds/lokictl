// SPDX-FileCopyrightText: 2026 LokiCTL contributors
//
// SPDX-License-Identifier: MIT

// The Security page: one verdict sentence, what was found grouped Fix soon /
// Worth a look / Good to know, and for the one picked its plain
// explanation, numbered fix steps and known holes. A health check starts
// from here after the same own-network confirmation as on the Cardputer,
// shows its progress and can be stopped. The reports saved on the
// Cardputer are listed to download (also on the Reports page). Every
// finding, count and gate comes from the Cardputer's status (overview,
// check): this page words them and never counts or judges again.

import { h, Component } from "./preact.module.js";
import { icon, post, getJson, refreshSoon, CHECK_EVENT, takeCheckAsk } from "./ui.js";
import { TIER_WORD, TIER_PILL, TIER_TILE, Steps, Hole, holesNote, deviceHref, dateWords } from "./devices.js";

// ---------- Words ----------

function plural(n, one, many) {
  return n === 1 ? one : many;
}

function things(n) {
  return n + " " + plural(n, "thing", "things");
}

const GROUPS = [
  ["fix", "Fix soon"],
  ["look", "Worth a look"],
  ["know", "Good to know"],
];
const COUNT_PILL = { fix: "p-bad", look: "p-warn", know: "p-know" };
const THING_ICON = { shield: "alert", lock: "unlock", screen: "display", wifi: "wifi", cable: "cable", eye: "watch" };

function securityHref(id) {
  return "#/security/" + encodeURIComponent(id);
}

function upperFirst(text) {
  return text ? text.charAt(0).toUpperCase() + text.slice(1) : "";
}

// "1 min 27 s", "45 s"; "" when not known.
function duration(seconds) {
  if (typeof seconds !== "number" || seconds < 0) return "";
  const minutes = Math.floor(seconds / 60);
  const rest = seconds % 60;
  if (minutes === 0) return rest + " s";
  return minutes + " min" + (rest ? " " + rest + " s" : "");
}

// The verdict sentence: the part in bold, then the rest. After a stopped
// or failed check the line under it says so.
function verdict(ov, check) {
  if (!ov) return ["", "Waiting for the Cardputer…"];
  const ended = check && (check.state === "stopped" || check.state === "failed");
  let tail = "";
  if (ov.checked) {
    if (ov.fine > 0) {
      const start = ov.device_fix + ov.device_look > 0 ? "The other " : ov.fine === ov.total ? "All " : "";
      tail = " " + start + ov.fine + plural(ov.fine, " device looks fine.", " devices look fine.");
    }
  } else if (ov.checking) {
    tail = " A check is running now.";
  } else if (!ended) {
    tail = " No health check has run yet.";
  }
  if (ov.fix > 0) return [things(ov.fix) + " to fix", (ov.look > 0 ? " and " + ov.look + " worth a look." : ".") + tail];
  if (ov.look > 0) return [things(ov.look) + " worth a look", "." + tail];
  if (ov.checked) return ["Nothing to fix", "." + tail];
  if (ov.checking) return ["", "Checking your network now."];
  if (ended) return ["Nothing found", " before the check ended."];
  return ["Not checked yet", ". A health check looks for weak spots on your own network."];
}

// "checked 21:40 · 1 min 27 s", or how the last check ended.
function checkedLine(ov, check) {
  if (check && check.state === "stopped") return "last check stopped";
  if (check && check.state === "failed") return "last check failed: " + (check.reason || "not known");
  if (ov && ov.checked_at) return "checked " + ov.checked_at + (typeof ov.took_s === "number" ? " · " + duration(ov.took_s) : "");
  return "";
}

// "10.0.0.1-254" as "10.0.0.1 to 10.0.0.254".
function rangeWords(asks) {
  const m = /^(\d+\.\d+\.\d+\.)1-254$/.exec(asks || "");
  return m ? m[1] + "1 to " + m[1] + "254" : "";
}

// Where a running check is, in words and as a part of the whole: finding
// the devices is the first 30 %, checking them the rest.
function progress(check) {
  if (check.step === "ports") {
    const hosts = Math.max(check.hosts, 1);
    return {
      words: "Checking device " + Math.min(check.scanned + 1, hosts) + " of " + check.hosts,
      part: 0.3 + 0.7 * Math.min(check.scanned / hosts, 1),
    };
  }
  if (!check.total) return { words: "Finding devices", part: 0 };
  return {
    words: "Finding devices, " + check.asked + " of " + check.total,
    part: 0.3 * Math.min(check.asked / check.total, 1),
  };
}

// The list's second line: what to do, else who it is about.
function secondLine(thing) {
  return thing.hint ? upperFirst(thing.hint) : thing.where;
}

// An address and port in mono; words ("Your Wi-Fi") in the text face.
function whereClass(thing) {
  return /^[0-9a-f]{1,3}[.:]/i.test(thing.where || "") ? "mono" : "";
}

function findDevice(ov, id) {
  return id ? (ov.devices || []).find((d) => d.id === id) || null : null;
}

const REFUSED = {
  401: "Your session ended. Pair again to do this.",
  403: "The Cardputer refused this. Reload the page and try again.",
  429: "Too many requests at once. Wait a moment.",
};
const NO_ANSWER = "No answer from the Cardputer. Try again.";

// ---------- Reports ----------

// The saved reports, shared by every part of the page that shows them.
const reports = { list: null, loading: false, saving: false, message: "", subs: new Set() };

function reportsChanged() {
  reports.subs.forEach((update) => update());
}

async function loadReports() {
  if (reports.loading) return reports.list || [];
  reports.loading = true;
  try {
    const answer = await getJson("/reports");
    if (answer.status === 200 && answer.data && Array.isArray(answer.data.reports)) {
      reports.list = answer.data.reports;
    }
  } catch (err) {
    // The list stays as it was; the status banner says the Cardputer is out of reach.
  }
  reports.loading = false;
  reportsChanged();
  return reports.list || [];
}

function reportHref(name) {
  return "/report?name=" + encodeURIComponent(name);
}

function newestPage(list) {
  return (list || []).find((file) => /\.html$/.test(file.name)) || null;
}

function download(name) {
  const link = document.createElement("a");
  link.href = reportHref(name);
  link.download = name;
  document.body.appendChild(link);
  link.click();
  link.remove();
}

const SAVE_WAIT_MS = 8000;

// Asks the Cardputer to save a new report of what it knows now, waits for
// it to appear in the list, then downloads its page.
async function saveReport() {
  if (reports.saving) return;
  reports.saving = true;
  reports.message = "";
  reportsChanged();
  const before = newestPage(reports.list || (await loadReports()));
  let code = 0;
  try {
    code = await post("/api/v1/report", { save: "1" });
  } catch (err) {
    code = 0;
  }
  if (code !== 204) {
    reports.saving = false;
    reports.message = REFUSED[code] || NO_ANSWER;
    reportsChanged();
    return;
  }
  const started = Date.now();
  while (Date.now() - started < SAVE_WAIT_MS) {
    await new Promise((done) => setTimeout(done, 800));
    const newest = newestPage(await loadReports());
    if (newest && (!before || newest.name !== before.name)) {
      reports.saving = false;
      reports.message = "Saved on the Cardputer. If it didn't download, use its link below.";
      reportsChanged();
      download(newest.name);
      return;
    }
  }
  reports.saving = false;
  reports.message = "No new report yet. Wait a few seconds and try again.";
  reportsChanged();
}

// "lokictl-20261123-075130.html" and "...-devices.csv": one report, saved at one time.
const REPORT_NAME = /^lokictl-(\d{4})(\d{2})(\d{2})-(\d{2})(\d{2})(\d{2})(-\d)?(-devices)?\.(html|csv)$/;

function reportGroups(list) {
  const out = [];
  const byStamp = new Map();
  for (const file of list || []) {
    const m = REPORT_NAME.exec(file.name);
    if (!m) continue;
    const stamp = m.slice(1, 8).join("");
    let group = byStamp.get(stamp);
    if (!group) {
      const date = dateWords(m[1] + "-" + m[2] + "-" + m[3]);
      group = { stamp, when: (date ? date + ", " : "") + m[4] + ":" + m[5], page: null, table: null };
      byStamp.set(stamp, group);
      out.push(group);
    }
    if (m[8]) group.table = file;
    else group.page = file;
  }
  return out;
}

function size(bytes) {
  return bytes < 1024 ? bytes + " B" : Math.round(bytes / 1024) + " KB";
}

function ReportLinks({ group }) {
  return [
    group.page
      ? h(
          "a",
          { class: "rep-link", href: reportHref(group.page.name), download: group.page.name, key: "p", "aria-label": "Download the report page of " + group.when },
          icon("reports", 16, 2),
          "Page"
        )
      : null,
    group.table
      ? h(
          "a",
          { class: "rep-link", href: reportHref(group.table.name), download: group.table.name, key: "t", "aria-label": "Download the devices table of " + group.when },
          icon("devices", 16, 2),
          "Table"
        )
      : null,
  ];
}

function reportsNote(list) {
  if (list === null) return "Asking the Cardputer…";
  return "No reports saved yet. Download report saves one of what the Cardputer knows now.";
}

function ReportsPhone({ list }) {
  const groups = reportGroups(list);
  return [
    h("div", { class: "sec", key: "h" }, h("span", null, "Saved reports"), groups.length ? h("span", { class: "sec-note" }, String(groups.length)) : null),
    h(
      "div",
      { class: "group", key: "g" },
      groups.length
        ? groups.map((group) =>
            h(
              "div",
              { class: "row rep-row", key: group.stamp },
              h(
                "span",
                { class: "row-text" },
                h("b", { class: "one-line" }, group.when),
                h("span", { class: "mono" }, group.page ? size(group.page.bytes) : "table only")
              ),
              h(ReportLinks, { group })
            )
          )
        : h("div", { class: "row" }, h("span", { class: "row-key muted rep-none" }, reportsNote(list)))
    ),
    h("p", { class: "note", key: "n" }, "The page opens in any browser. The table opens in a spreadsheet."),
  ];
}

function ReportsDesk({ list, span }) {
  const groups = reportGroups(list);
  return h(
    "section",
    { class: "card col " + (span || "span-12"), "aria-labelledby": "reports-d" },
    h(
      "div",
      { class: "card-head" },
      h("h2", { id: "reports-d" }, "Saved reports"),
      h("span", { class: "t" }, String(groups.length)),
      h("span", { class: "r rep-hint" }, "A page for any browser and a table for a spreadsheet")
    ),
    groups.length
      ? h(
          "div",
          { class: "rep-list" },
          groups.map((group) =>
            h(
              "div",
              { class: "rep-row-d", key: group.stamp },
              h("span", { class: "rep-when" }, group.when),
              h("span", { class: "t" }, group.page ? size(group.page.bytes) : ""),
              h("span", { class: "rep-links" }, h(ReportLinks, { group }))
            )
          )
        )
      : h("p", { class: "att-none" }, reportsNote(list))
  );
}

// Follows the shared reports: a page that shows them extends this.
class ReportsFollower extends Component {
  constructor() {
    super();
    this.update = () => this.setState({});
  }

  componentDidMount() {
    reports.subs.add(this.update);
    loadReports();
  }

  componentWillUnmount() {
    reports.subs.delete(this.update);
  }
}

function SaveMessage({ desk }) {
  if (!reports.message) return null;
  return h("p", { class: desk ? "save-msg-d" : "note save-msg", role: "status" }, reports.message);
}

// ---------- The health check ----------

// The confirmation, then the progress with Stop, or how the last request
// went. `desk`: the computer's card, else the phone's.
function CheckPanel({ check, flow, desk, onStart, onCancel, onStop }) {
  const cls = desk ? "card check-card span-12" : "check-card-p";
  const big = desk ? "btn lg " : "bp ";
  const id = desk ? "check-d" : "check-p";
  if (flow.confirm) {
    const range = rangeWords(check.asks);
    return h(
      "section",
      { class: cls, "aria-labelledby": id },
      h("h2", { id, tabIndex: -1 }, "Check your network?"),
      h(
        "p",
        null,
        range
          ? "LokiCTL asks every address from " + range + " to find your devices, then tries " + check.max_ports +
              " common ports on up to " + check.max_devices + " of them."
          : "LokiCTL looks for the devices on your network, then tries common ports on each."
      ),
      h("p", { class: "check-own" }, icon("alert", 18, 2), h("span", null, "Only check networks you own or may test.")),
      !check.can && check.why ? h("p", { class: "check-why", role: "status" }, check.why + ".") : null,
      flow.message ? h("p", { class: "check-msg", role: "status" }, flow.message) : null,
      h(
        "div",
        { class: "check-acts" },
        h(
          "button",
          { type: "button", class: big + "or", onClick: onStart, disabled: !check.can || flow.sending },
          flow.sending ? "Starting…" : "Check my network"
        ),
        h("button", { type: "button", class: big + "gh", onClick: onCancel }, "Cancel")
      )
    );
  }
  const running = check.state === "running";
  if (running || flow.want) {
    const step = running ? progress(check) : { words: flow.want === "stop" ? "Stopping…" : "Waiting for the Cardputer…", part: 0 };
    const percent = Math.round(step.part * 100);
    const title = running ? "Checking your network" : flow.want === "start" ? "Starting the check…" : "Stopping the check…";
    return h(
      "section",
      { class: cls, "aria-labelledby": id },
      h(
        "div",
        { class: "check-top" },
        h("h2", { id, tabIndex: -1 }, title),
        running ? h("span", { class: "mono check-time" }, duration(check.elapsed_s)) : null
      ),
      h("p", { class: "check-step", role: "status" }, step.words),
      h(
        "div",
        { class: "check-bar", role: "progressbar", "aria-label": "How far the check is", "aria-valuemin": 0, "aria-valuemax": 100, "aria-valuenow": percent },
        h("i", { style: { width: percent + "%" } })
      ),
      flow.message ? h("p", { class: "check-msg", role: "status" }, flow.message) : null,
      h(
        "div",
        { class: "check-acts" },
        h(
          "button",
          { type: "button", class: big + "gh", onClick: onStop, disabled: !running || flow.want === "stop" || flow.sending },
          flow.want === "stop" ? "Stopping…" : "Stop"
        )
      )
    );
  }
  if (flow.message) {
    return h("p", { class: desk ? "check-note-d span-12" : "note check-note", role: "status" }, flow.message);
  }
  return null;
}

// ---------- The findings ----------

// The detail of one finding: what it means, how to fix it, the holes.
function DetailDesk({ thing, ov, primary, busy, onCheck }) {
  const device = findDevice(ov, thing.device);
  return h(
    "section",
    { class: "card detail span-8", "aria-label": "Selected finding" },
    h("h2", { class: "detail-title" }, thing.title),
    h(
      "div",
      { class: "tagrow" },
      h("span", { class: "pill " + TIER_PILL[thing.tier] }, TIER_WORD[thing.tier]),
      thing.where ? h("span", { class: whereClass(thing) }, thing.where) : null
    ),
    thing.about ? h("p", { class: "detail-about" }, thing.about) : null,
    thing.steps && thing.steps.length ? [h("h3", { class: "h3" }, "How to fix it"), h(Steps, { steps: thing.steps })] : null,
    h(
      "div",
      { class: "acts-d" },
      h(
        "button",
        { type: "button", class: "btn lg " + (primary ? "or" : "gh"), onClick: onCheck, disabled: busy },
        thing.tier === "know" ? "Check again" : "I've fixed it, check again"
      ),
      thing.device ? h("a", { class: "btn gh lg", href: deviceHref(thing.device) }, "Open device") : null
    ),
    thing.also && thing.also.length
      ? [
          h("h3", { class: "h3" }, "Also found here"),
          thing.also.map((title, i) => h("div", { class: "other", key: i }, h("span", { class: "sq" }), h("span", { class: "rt" }, h("b", null, title)))),
        ]
      : null,
    device && device.holes.length
      ? [
          h("h3", { class: "h3 holes-h" }, "The holes"),
          device.holes.map((hole) => h(Hole, { hole, key: hole.cve })),
          h("p", { class: "detail-note" }, holesNote(device, ov)),
        ]
      : null
  );
}

function FindingsDesk({ ov, selected }) {
  const list = ov.things || [];
  return h(
    "section",
    { class: "card find-list span-4", "aria-label": "Findings" },
    GROUPS.map(([tier, word]) => {
      const count = ov[tier] || 0;
      if (!count) return null;
      const shown = list.filter((thing) => thing.tier === tier);
      return h(
        "div",
        { key: tier },
        h("h2", { class: "gh2" }, word, h("span", { class: "pill " + COUNT_PILL[tier] }, String(count))),
        shown.map((thing) => {
          const on = selected && thing.id === selected.id;
          return h(
            "a",
            { class: on ? "item on" : "item", href: securityHref(thing.id), key: thing.id, "aria-current": on ? "true" : undefined },
            h("span", { class: "sq " + (TIER_TILE[tier] || "") }),
            h("span", { class: "rt" }, h("b", null, thing.title), h("span", { class: "one-line" }, secondLine(thing)))
          );
        }),
        count > shown.length ? h("p", { class: "item-more" }, count - shown.length + " more not listed here") : null
      );
    })
  );
}

// No findings to show: nothing found, a check that runs, or none yet.
function NoFindings({ ov, check, desk, primary, busy, asking, onCheck }) {
  let title = "Not checked yet";
  let text =
    "A health check finds the devices on your network and looks for known security holes, old software, open logins and a weak Wi-Fi lock. It tells you what to fix, in plain steps.";
  let tile = "security";
  if (busy) {
    // The check's own card above already says "Checking your network".
    title = "Nothing found yet";
    text = "What the check finds shows up here as it goes.";
  } else if (ov.checked) {
    title = "Nothing to fix";
    text = "The last check found nothing to fix or look at on your network.";
    tile = "check";
  }
  const showButton = desk && !busy && !asking && !ov.checked;
  return h(
    "section",
    { class: "empty" + (desk ? " span-12 empty-d" : "") },
    h("span", { class: "tile" }, icon(tile, 22, 1.8)),
    h("h2", null, title),
    h("p", null, text),
    showButton
      ? h(
          "button",
          { type: "button", class: "btn lg empty-act " + (primary ? "or" : "gh"), onClick: onCheck, disabled: !check.can },
          "Check my network"
        )
      : null,
    showButton && !check.can && check.why ? h("p", { class: "check-why" }, check.why + ".") : null
  );
}

function FindingRowsPhone({ ov }) {
  const list = ov.things || [];
  return GROUPS.map(([tier, word]) => {
    const count = ov[tier] || 0;
    if (!count) return null;
    const shown = list.filter((thing) => thing.tier === tier);
    return [
      h("div", { class: "sec", key: tier + "-h" }, h("span", null, word), h("span", { class: "pill " + COUNT_PILL[tier] }, String(count))),
      h(
        "div",
        { class: "group", key: tier + "-g" },
        shown.map((thing) =>
          h(
            "a",
            { class: "row icon-row", href: thing.device ? deviceHref(thing.device) : securityHref(thing.id), key: thing.id },
            h("span", { class: "tile " + (TIER_TILE[tier] || "know") }, icon(THING_ICON[thing.icon] || "alert", 18, 2)),
            h("span", { class: "row-text" }, h("b", null, thing.title), h("span", { class: "one-line" }, secondLine(thing))),
            h("span", { class: "chev" }, icon("chevron", 16, 2.4))
          )
        ),
        count > shown.length
          ? h("div", { class: "row" }, h("span", { class: "row-key muted" }, count - shown.length + " more not listed here"))
          : null
      ),
    ];
  });
}

// The phone's page of one finding.
function ThingPhone({ thing, ov, check, flow, primary, busy, handlers }) {
  const device = findDevice(ov, thing.device);
  const problem = thing.tier === "fix" || thing.tier === "look";
  return h(
    "div",
    { class: "phone-only dev-page" },
    h("a", { class: "back", href: "#/security" }, icon("back", 22, 2.4), "Security"),
    h(
      "div",
      { class: "dev-head" },
      h("span", { class: "dev-big" }, icon(THING_ICON[thing.icon] || "alert", 28, 1.6)),
      h(
        "div",
        { class: "dev-head-text" },
        h("h1", { class: "dev-title thing-title" }, thing.title),
        thing.where ? h("div", { class: "dev-meta" }, h("span", { class: whereClass(thing) }, thing.where)) : null
      )
    ),
    h(
      "section",
      { class: "alert " + (problem ? thing.tier : "calm"), "aria-label": TIER_WORD[thing.tier] },
      h("h2", null, TIER_WORD[thing.tier]),
      thing.about ? h("p", null, thing.about) : null
    ),
    h(CheckPanel, { check, flow, desk: false, ...handlers }),
    thing.steps && thing.steps.length
      ? [h("div", { class: "sec", key: "s" }, h("span", null, "How to fix it")), h("div", { class: "group steps-box", key: "b" }, h(Steps, { steps: thing.steps }))]
      : null,
    h(
      "div",
      { class: "acts-p" },
      h(
        "button",
        { type: "button", class: "bp " + (primary ? "or" : "gh") + (thing.device ? "" : " wide"), onClick: handlers.onCheck, disabled: busy },
        problem ? "I've fixed it, check again" : "Check again"
      ),
      thing.device ? h("a", { class: "bp gh", href: deviceHref(thing.device) }, "Open device") : null
    ),
    thing.also && thing.also.length
      ? [
          h("div", { class: "sec", key: "a" }, h("span", null, "Also found here")),
          h(
            "div",
            { class: "group", key: "ag" },
            thing.also.map((title, i) => h("div", { class: "row", key: i }, h("span", { class: "row-key" }, title)))
          ),
        ]
      : null,
    device && device.holes.length
      ? [
          h("div", { class: "sec", key: "h" }, h("span", null, "Security holes")),
          h("div", { class: "group", key: "hg" }, device.holes.map((hole) => h(Hole, { hole, key: hole.cve }))),
          h("p", { class: "note", key: "hn" }, holesNote(device, ov)),
        ]
      : null
  );
}

function SampleTag({ status }) {
  return status && status.sample ? h("span", { class: "tag" }, "Sample data") : null;
}

function Verdict({ ov, check, cls }) {
  const [em, rest] = verdict(ov, check);
  return h("p", { class: cls }, em ? h("b", null, em) : null, rest);
}

// ---------- The page ----------

// A request (start or stop) waits here until the status shows it happened,
// or 10 s pass.
const WAIT_MS = 10000;

export class SecurityPage extends ReportsFollower {
  constructor() {
    super();
    this.state = { confirm: false, want: null, since: 0, from: "", sending: false, message: "" };
    this.timer = 0;
    this.pendingAsk = false;
    this.onAsk = () => {
      if (takeCheckAsk()) this.openConfirm();
    };
    this.handlers = {
      onCheck: () => this.openConfirm(),
      onStart: () => this.send(true),
      onStop: () => this.send(false),
      onCancel: () => this.setState({ confirm: false, message: "" }),
    };
    this.onSave = () => saveReport();
  }

  componentDidMount() {
    super.componentDidMount();
    window.addEventListener(CHECK_EVENT, this.onAsk);
    if (takeCheckAsk()) this.openConfirm();
  }

  componentWillUnmount() {
    super.componentWillUnmount();
    clearTimeout(this.timer);
    window.removeEventListener(CHECK_EVENT, this.onAsk);
  }

  check() {
    return (this.props.status && this.props.status.check) || null;
  }

  openConfirm() {
    const check = this.check();
    if (!check) {
      this.pendingAsk = true; // shown once the first status arrives
      return;
    }
    if (check.state === "running" || this.state.want) return;
    this.setState({ confirm: true, message: "" }, () => {
      // The confirmation's heading takes the focus, on whichever layout shows.
      for (const id of ["check-d", "check-p"]) {
        const heading = document.getElementById(id);
        if (heading && heading.offsetParent !== null) {
          heading.focus();
          if (heading.scrollIntoView) heading.scrollIntoView({ block: "nearest" });
          break;
        }
      }
    });
  }

  async send(start) {
    if (this.state.sending) return;
    const check = this.check();
    this.setState({ sending: true, message: "" });
    let code = 0;
    try {
      code = await post("/api/v1/check", start ? { run: "1", own: "1" } : { run: "0" });
    } catch (err) {
      code = 0;
    }
    if (code !== 204) {
      this.setState({ sending: false, message: REFUSED[code] || NO_ANSWER });
      return;
    }
    this.setState({
      sending: false,
      confirm: false,
      want: start ? "start" : "stop",
      since: Date.now(),
      from: check ? check.state + "/" + check.elapsed_s : "",
    });
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.setState({}), WAIT_MS + 100);
    refreshSoon();
  }

  // What the status says about the last request, worked out on every new status.
  componentDidUpdate() {
    const check = this.check();
    if (this.pendingAsk && check) {
      this.pendingAsk = false;
      this.openConfirm();
    }
    const { want, since, from } = this.state;
    if (!want || !check) return;
    // While a request waits, ask for each next status soon, not in 2 s.
    if (this.props.status !== this.seenStatus) {
      this.seenStatus = this.props.status;
      refreshSoon();
    }
    const now = check.state + "/" + check.elapsed_s;
    if (want === "start" && (check.state === "running" || (check.state !== "idle" && now !== from))) {
      this.setState({ want: null });
    } else if (want === "stop" && check.state !== "running") {
      this.setState({ want: null });
    } else if (Date.now() - since > WAIT_MS) {
      const message =
        want === "start"
          ? "The check didn't start." + (check.why ? " " + check.why + "." : " Try again.")
          : "The check didn't stop yet. Try again.";
      this.setState({ want: null, message });
    }
  }

  render({ status, sub }, flow) {
    const ov = (status && status.overview) || null;
    const check = (status && status.check) || null;
    if (!ov || !check) {
      return [
        h("h1", { class: "large-title phone-only", key: "p" }, "Security"),
        h("h1", { class: "page-title desk-only", key: "d" }, "Security"),
      ];
    }
    const list = ov.things || [];
    const busy = check.state === "running" || !!flow.want;
    const picked = list.find((thing) => thing.id === sub) || null;
    const selected = picked || list[0] || null;
    const line = checkedLine(ov, check);

    // The one orange action of each view.
    const deskPrimary = flow.confirm ? "confirm" : busy ? "" : selected ? (selected.tier !== "know" ? "detail" : "") : !ov.checked ? "empty" : "";
    const phonePrimary = flow.confirm ? "confirm" : busy ? "" : picked ? (picked.tier !== "know" ? "detail" : "") : !ov.checked ? "check" : "";

    const panelDesk = h(CheckPanel, { check, flow, desk: true, ...this.handlers });
    const desk = h(
      "div",
      { class: "desk-only" },
      h(
        "div",
        { class: "sec-head" },
        h("div", null, h("h1", { class: "page-title" }, "Security"), h(Verdict, { ov, check, cls: "sec-verdict" })),
        h(
          "div",
          { class: "sec-acts" },
          h(SampleTag, { status }),
          line ? h("span", { class: "mono sec-when" }, line) : null,
          h(
            "button",
            { type: "button", class: "btn gh", onClick: this.onSave, disabled: reports.saving, "aria-busy": reports.saving ? "true" : "false" },
            reports.saving ? "Saving report…" : "Download report"
          )
        )
      ),
      h(SaveMessage, { desk: true }),
      h(
        "div",
        { class: "grid" },
        panelDesk,
        selected
          ? [
              h(FindingsDesk, { ov, selected, key: "l" }),
              h(DetailDesk, { thing: selected, ov, primary: deskPrimary === "detail", busy, onCheck: this.handlers.onCheck, key: "d" }),
            ]
          : h(NoFindings, { ov, check, desk: true, primary: deskPrimary === "empty", busy, asking: flow.confirm, onCheck: this.handlers.onCheck }),
        h(ReportsDesk, { list: reports.list })
      )
    );

    let phone;
    if (picked) {
      phone = h(ThingPhone, { thing: picked, ov, check, flow, primary: phonePrimary === "detail", busy, handlers: this.handlers });
    } else {
      phone = h(
        "div",
        { class: "phone-only" },
        h(
          "header",
          { class: "phone-head" },
          h("h1", { class: "large-title" }, "Security"),
          h(Verdict, { ov, check, cls: "sec-verdict-p" }),
          line || status.sample ? h("div", { class: "meta" }, line ? h("span", { class: "mono sec-when" }, line) : null, h(SampleTag, { status })) : null
        ),
        h(CheckPanel, { check, flow, desk: false, ...this.handlers }),
        flow.confirm || busy
          ? null
          : h(
              "div",
              { class: "acts-p" },
              h(
                "button",
                { type: "button", class: "bp " + (phonePrimary === "check" ? "or" : "gh"), onClick: this.handlers.onCheck, disabled: !check.can },
                ov.checked ? "Check again" : "Check my network"
              ),
              h(
                "button",
                { type: "button", class: "bp gh", onClick: this.onSave, disabled: reports.saving, "aria-busy": reports.saving ? "true" : "false" },
                reports.saving ? "Saving…" : "Download report"
              )
            ),
        !flow.confirm && !busy && !check.can && check.why ? h("p", { class: "note" }, check.why + ".") : null,
        h(SaveMessage, { desk: false }),
        list.length ? h(FindingRowsPhone, { ov }) : h(NoFindings, { ov, check, desk: false, busy }),
        h(ReportsPhone, { list: reports.list })
      );
    }
    return [phone, desk];
  }
}

// The Reports page: the same list, with Download report.
export class ReportsPage extends ReportsFollower {
  constructor() {
    super();
    this.onSave = () => saveReport();
  }

  render() {
    const button = (cls) =>
      h(
        "button",
        { type: "button", class: cls, onClick: this.onSave, disabled: reports.saving, "aria-busy": reports.saving ? "true" : "false" },
        reports.saving ? "Saving report…" : "Download a new report"
      );
    const about = "Saved on the Cardputer, the newest first. A new one holds what the Cardputer knows now.";
    return [
      h(
        "div",
        { class: "phone-only", key: "p" },
        h("header", { class: "phone-head" }, h("h1", { class: "large-title" }, "Reports"), h("p", { class: "sec-verdict-p" }, about)),
        h("div", { class: "acts-p" }, button("bp or wide")),
        h(SaveMessage, { desk: false }),
        h(ReportsPhone, { list: reports.list })
      ),
      h(
        "div",
        { class: "desk-only", key: "d" },
        h(
          "div",
          { class: "sec-head" },
          h("div", null, h("h1", { class: "page-title" }, "Reports"), h("p", { class: "sec-verdict" }, about)),
          h("div", { class: "sec-acts" }, button("btn or"))
        ),
        h(SaveMessage, { desk: true }),
        h("div", { class: "grid" }, h(ReportsDesk, { list: reports.list }))
      ),
    ];
  }
}

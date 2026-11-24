// SPDX-FileCopyrightText: 2026 LokiCTL contributors
// SPDX-License-Identifier: MIT
//
// Makes docs/media/dashboard-phone.gif: the web dashboard (Remote view) on
// a phone-sized screen, from the desktop simulator's made-up sample data.
// It starts the simulator with Remote view on, pairs a headless Google
// Chrome with the code, taps through Home, Devices, one device with known
// security holes and its fix, and Security, and turns the pictures into a
// GIF.
//
//   node scripts/demo-dashboard.mjs
//
// Needs:
//   - the desktop simulator (see docs/development.md):
//       cmake --preset darwin-arm64
//       cmake --build --preset darwin-arm64-dbg
//     Another binary can be named with LOKICTL_BIN=/path/to/lokictl.
//   - Node.js 22 or newer (it uses the built-in fetch and WebSocket)
//   - Google Chrome; another one can be named with CHROME_BIN=/path/to/chrome
//   - ffmpeg (macOS: brew install ffmpeg)
//   - port 8780 free on 127.0.0.1 (no other LokiCTL with Remote view on)
//
// The simulator runs without a window and with a throwaway home folder and
// settings file, so no setting of yours changes, and in the desktop build
// Remote view only listens on 127.0.0.1. Chrome runs headless with a
// throwaway profile. Both are stopped and the throwaway folders removed at
// the end, also after an error or Ctrl+C.

import { spawn, spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const simulator = process.env.LOKICTL_BIN || join(repoRoot, "build/darwin-arm64/Debug/lokictl");
const chromeBinary = process.env.CHROME_BIN || "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
const outFile = join(repoRoot, "docs/media/dashboard-phone.gif");
const remotePort = 8780;

// The phone: an iPhone-sized screen, drawn at twice the size and scaled
// down in the GIF so the text stays sharp.
const phone = { width: 390, height: 844, scale: 2 };
const warnBytes = 1500000;

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

function fail(message) {
  throw new Error(message);
}

let work = "";
let sim = null;
let chrome = null;
let socket = null;
let cleaning = null;

// Waits for a child process to end, and kills it hard after `ms`.
function stopProcess(child, signal, ms) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return Promise.resolve();
  return new Promise((done) => {
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
    }, ms);
    child.once("exit", () => {
      clearTimeout(timer);
      done();
    });
    child.kill(signal);
  });
}

async function cleanUp() {
  if (cleaning) return cleaning;
  cleaning = (async () => {
    if (socket && socket.readyState === WebSocket.OPEN) {
      try {
        socket.send(JSON.stringify({ id: 999999, method: "Browser.close" }));
      } catch {
        // Chrome is stopped below anyway.
      }
    }
    await stopProcess(chrome, "SIGTERM", 5000);
    // The app quits on SIGINT the same way as on a forced quit.
    await stopProcess(sim, "SIGINT", 5000);
    if (work) rmSync(work, { recursive: true, force: true });
  })();
  return cleaning;
}

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    cleanUp().finally(() => process.exit(130));
  });
}

function checkTools() {
  if (!existsSync(simulator)) {
    fail(`No simulator at ${simulator}. Build it first: cmake --preset darwin-arm64 && cmake --build --preset darwin-arm64-dbg`);
  }
  if (!existsSync(chromeBinary)) fail(`No Chrome at ${chromeBinary}. Name one with CHROME_BIN=/path/to/chrome`);
  if (spawnSync("ffmpeg", ["-version"], { stdio: "ignore" }).status !== 0) fail("ffmpeg is needed (macOS: brew install ffmpeg)");
  if (typeof WebSocket !== "function") fail("Node.js 22 or newer is needed (for the built-in WebSocket)");
}

function checkPortFree() {
  return new Promise((done, reject) => {
    const server = createServer();
    server.once("error", () => reject(new Error(`Port ${remotePort} is busy. Is another LokiCTL running with Remote view on?`)));
    server.listen(remotePort, "127.0.0.1", () => server.close(done));
  });
}

// Starts the simulator with Remote view on and returns its address and code.
async function startSimulator() {
  const home = join(work, "home");
  mkdirSync(home);
  const config = join(work, "lokictl.conf");
  copyFileSync(join(repoRoot, "config/lokictl.conf"), config);
  const env = {};
  for (const [name, value] of Object.entries(process.env)) {
    if (!name.startsWith("LOKICTL_")) env[name] = value;
  }
  Object.assign(env, {
    HOME: home,
    XDG_CONFIG_HOME: join(home, ".config"),
    LOKICTL_CONFIG_FILE: config,
    LOKICTL_REMOTE: "on",
    SDL_VIDEODRIVER: "dummy",
  });
  sim = spawn(simulator, ["--data", "sample"], { cwd: work, env, stdio: ["ignore", "ignore", "pipe"] });
  let text = "";
  sim.stderr.setEncoding("utf8");
  sim.stderr.on("data", (chunk) => {
    text += chunk;
  });
  for (let waited = 0; waited < 10000; waited += 100) {
    const found = /remote view: code (\d{6}) at (http:\/\/127\.0\.0\.1:\d+)/.exec(text);
    if (found) return { code: found[1], base: found[2] };
    if (sim.exitCode !== null) break;
    await sleep(100);
  }
  fail(`The simulator gave no Remote view code. Is port ${remotePort} busy?\n${text.trim()}`);
}

// Starts headless Chrome and opens the DevTools connection to its page.
async function startChrome() {
  const profile = join(work, "chrome");
  mkdirSync(profile);
  chrome = spawn(
    chromeBinary,
    [
      "--headless=new",
      "--disable-gpu",
      "--hide-scrollbars",
      "--no-first-run",
      "--no-default-browser-check",
      "--disable-background-networking",
      "--disable-component-update",
      "--disable-sync",
      "--lang=en-US",
      "--remote-debugging-port=0",
      `--user-data-dir=${profile}`,
      "about:blank",
    ],
    { stdio: "ignore" }
  );
  const portFile = join(profile, "DevToolsActivePort");
  let port = "";
  for (let waited = 0; waited < 15000 && !port; waited += 100) {
    await sleep(100);
    if (existsSync(portFile)) port = readFileSync(portFile, "utf8").split("\n")[0].trim();
    if (chrome.exitCode !== null) fail("Chrome quit at start");
  }
  if (!port) fail("Chrome did not open its DevTools port");
  let page = null;
  for (let tries = 0; tries < 50 && !page; tries++) {
    try {
      const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      page = targets.find((target) => target.type === "page");
    } catch {
      // Not ready yet.
    }
    if (!page) await sleep(100);
  }
  if (!page) fail("Chrome has no page to drive");
  socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((done, reject) => {
    socket.addEventListener("open", done, { once: true });
    socket.addEventListener("error", () => reject(new Error("Could not reach Chrome")), { once: true });
  });
}

let nextId = 0;
const waiting = new Map();

function send(method, params = {}) {
  return new Promise((done, reject) => {
    const id = ++nextId;
    waiting.set(id, { done, reject, method });
    socket.send(JSON.stringify({ id, method, params }));
  });
}

function listen() {
  socket.addEventListener("message", (event) => {
    const message = JSON.parse(event.data);
    const entry = message.id && waiting.get(message.id);
    if (!entry) return;
    waiting.delete(message.id);
    if (message.error) entry.reject(new Error(`${entry.method}: ${message.error.message}`));
    else entry.done(message.result);
  });
}

async function evaluate(expression) {
  const result = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
  if (result.exceptionDetails) fail(`Page script failed: ${result.exceptionDetails.text} in ${expression}`);
  return result.result.value;
}

// Waits until the page shows `text`, then for its fonts and two paints.
async function waitForText(text, ms = 8000) {
  for (let waited = 0; waited < ms; waited += 100) {
    if (await evaluate(`!!document.body && document.body.innerText.includes(${JSON.stringify(text)})`)) {
      await evaluate("document.fonts.ready.then(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))))");
      await sleep(250);
      return;
    }
    await sleep(100);
  }
  fail(`The page never showed "${text}"`);
}

// Taps the middle of the first element that matches `selector` (and holds
// `text`, when given) the way a finger does.
async function tap(selector, text = "") {
  const box = await evaluate(`(() => {
    const element = [...document.querySelectorAll(${JSON.stringify(selector)})]
      .find((e) => e.innerText.includes(${JSON.stringify(text)}) && e.getClientRects().length);
    if (!element) return null;
    const r = element.getBoundingClientRect();
    return { x: r.left + r.width / 2, y: r.top + r.height / 2 };
  })()`);
  if (!box) fail(`Nothing to tap for ${selector} ${text}`);
  const point = [{ x: box.x, y: box.y }];
  await send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: point });
  await sleep(60);
  await send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
}

const frames = [];

// Takes one picture of the page, shown for `ms` milliseconds in the GIF.
async function shot(ms) {
  const result = await send("Page.captureScreenshot", { format: "png" });
  const file = join(work, "frames", `frame-${String(frames.length + 1).padStart(3, "0")}.png`);
  writeFileSync(file, Buffer.from(result.data, "base64"));
  frames.push({ file, ms });
}

// Scrolls so the heading `text` sits near the top, in short steps with a
// picture after each; the last one is held for `holdMs`.
async function scrollTo(text, stepMs, holdMs) {
  const target = await evaluate(`(() => {
    const heading = [...document.querySelectorAll("h2, h3, .h3, .sec-title, div, span")]
      .find((e) => e.children.length === 0 && e.textContent.trim() === ${JSON.stringify(text)});
    if (!heading) return -1;
    const top = heading.getBoundingClientRect().top + window.scrollY - 76;
    const end = document.documentElement.scrollHeight - window.innerHeight;
    return Math.max(0, Math.min(Math.round(top), end));
  })()`);
  if (target < 0) fail(`No "${text}" on the page`);
  const steps = [0.2, 0.5, 0.78, 0.94, 1];
  for (let i = 0; i < steps.length; i++) {
    await evaluate(`window.scrollTo(0, ${Math.round(target * steps[i])}); new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)))`);
    await shot(i === steps.length - 1 ? holdMs : stepMs);
  }
}

async function record({ code, base }) {
  mkdirSync(join(work, "frames"));
  listen();
  await send("Page.enable");
  await send("Runtime.enable");
  await send("Emulation.setDeviceMetricsOverride", {
    width: phone.width,
    height: phone.height,
    deviceScaleFactor: phone.scale,
    mobile: true,
  });
  await send("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  // Still pictures: the red dot's pulse would only freeze at a random step.
  await send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-reduced-motion", value: "reduce" }] });

  // Pairing: type the code from the simulator, as on the Cardputer's panel.
  await send("Page.navigate", { url: `${base}/` });
  await waitForText("Continue");
  await evaluate(`document.getElementById("code").focus()`);
  await send("Input.insertText", { text: code });
  await tap(".btn-continue");
  await waitForText("thing to fix");

  // Home: the verdict and the devices around the Cardputer.
  await shot(1700);
  // Devices: everything the Cardputer knows, the ones to fix first.
  await tap("nav.tabs a", "Devices");
  await waitForText("Looks fine");
  await shot(1200);
  // The device with known security holes, its worst problem and the fix.
  await tap("a", "Smart TV");
  await waitForText("Open services");
  await shot(1200);
  await tap("button", "Fix it step by step");
  await waitForText("Show less");
  await shot(1700);
  // Down to the holes themselves: how serious, and the official number.
  await scrollTo("Security holes", 150, 1500);
  // Security: one sentence for the whole network, the worst first.
  await tap("nav.tabs a", "Security");
  await waitForText("Fix soon");
  await shot(1900);
}

function makeGif() {
  const list = join(work, "frames.txt");
  let text = "";
  // "framerate 100" reads each picture in hundredths of a second, the GIF's
  // own unit, so every frame keeps its exact time.
  for (const frame of frames) {
    text += `file '${frame.file}'\noption framerate 100\nduration ${(frame.ms / 1000).toFixed(3)}\n`;
  }
  // The concat reader ignores the last duration unless the file comes again.
  text += `file '${frames[frames.length - 1].file}'\noption framerate 100\n`;
  writeFileSync(list, text);
  mkdirSync(dirname(outFile), { recursive: true });
  const run = spawnSync(
    "ffmpeg",
    [
      "-v", "error", "-y",
      "-f", "concat", "-safe", "0", "-i", list,
      "-filter_complex",
      // A light ordered dither keeps the soft glows on Home and the red
      // card free of bands; the flat colours stay flat.
      `scale=${phone.width}:-2:flags=lanczos,split[a][b];` +
        "[a]palettegen=max_colors=256:stats_mode=full[p];" +
        "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
      "-fps_mode", "passthrough", "-loop", "0", outFile,
    ],
    { stdio: ["ignore", "inherit", "inherit"] }
  );
  if (run.status !== 0) fail("ffmpeg could not make the GIF");
}

async function main() {
  checkTools();
  await checkPortFree();
  work = mkdtempSync(join(tmpdir(), "lokictl-dashboard-"));
  const remote = await startSimulator();
  await startChrome();
  await record(remote);
  makeGif();
  const bytes = statSync(outFile).size;
  const seconds = frames.reduce((sum, frame) => sum + frame.ms, 0) / 1000;
  console.log(`==> ${relative(repoRoot, outFile)}: ${frames.length} frames, ${seconds} s, ${bytes} bytes`);
  if (bytes > warnBytes) console.log(`==> warning: the GIF is over ${warnBytes} bytes; GitHub pages load it slowly`);
}

try {
  await main();
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
} finally {
  await cleanUp();
}

<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# Development

This page is for building LokiCTL, trying it on a Mac and running it on a
CardputerZero from your computer. Using the app is described in the
[README](../README.md).

## Build and run the simulator (macOS)

You need the Xcode command-line tools (`xcode-select --install`), CMake 3.31 or
newer and a few Homebrew packages:

```sh
brew install cmake ninja libpng jpeg fmt sdl2 freetype
```

Then, from the repository root:

```sh
cmake --preset darwin-arm64
cmake --build --preset darwin-arm64-dbg
./build/darwin-arm64/Debug/lokictl
```

Notes:

- The first configure downloads LVGL 9.5 into `.cache/`, so it needs a network
  connection.
- The preset expects Homebrew in `/opt/homebrew`. If yours lives elsewhere, add
  `-DCMAKE_PREFIX_PATH="$(brew --prefix jpeg)"` to the configure command.
- Intel Macs use the `darwin-x86-64` presets instead. The presets for Linux and
  Windows desktops are carried over but untested.
- Keys: type on your Mac's keyboard or click the keys of the keyboard drawn
  below the screen. `4` to `8` are the five soft keys under the screen.
  `F`, `X`, `Z` and `C` work as up, down, left and right, like the arrow
  keys. A short `ESC` goes back one page; on the home page, hold `ESC` (or
  close the window) to quit. For the help panel (fn+H), click `fn` and then
  `H` on the drawn keyboard. [navigation.md](navigation.md) explains every
  key.
- `--theme <name>` starts the simulator in one theme for this run only, for
  example `./build/darwin-arm64/Debug/lokictl --theme constellation`. The
  device frame drawn around the screen follows the theme: white for
  Instrument, black for the other two. [themes.md](themes.md) explains how
  the theme is chosen at startup.
- The simulator's home page shows moving demo values: the clock runs one
  minute per second and the battery counts down, to show that the page
  follows its data. `--data <name>` picks other values for the run:
  `sample` (fixed values), `demo` or `live` (the Mac's own clock; the Mac
  has no Linux network files, so it shows no network). The
  made-up examples `--data wifi`, `--data cable` and `--data offline` show
  the home page joined to a Wi-Fi network, on a cable and with no network,
  and `--data tour` walks through all of them, one every 2 s. See
  [Live data](#live-data) and [home.md](home.md).
- The simulator reads its settings from `config/lokictl.conf`. Picking a
  theme on the Settings page (opened with `6` on the home page) saves the
  choice and rewrites that file. To keep the repository's file as it is,
  give the app another file in the environment variable
  `LOKICTL_CONFIG_FILE`, for example
  `LOKICTL_CONFIG_FILE=build/simulator.conf ./build/darwin-arm64/Debug/lokictl`.
  The app then reads and writes that file instead; it is created the first
  time a setting is saved.
- On the Mac, mode switches, scans and probes only pretend, so every page
  can be tried without touching your network.

## Checks

The same build makes `build/darwin-arm64/Debug/lokictl_tests` (Mac only;
`ctest --test-dir build/darwin-arm64 -C Debug` runs it too). It checks how LokiCTL
reads what a port says and the check against the shipped list of known
security holes (`tests/vuln_checks.cpp`), and exits with 1 when a check
fails. Given a text, it prints what the list says about it:
`lokictl_tests "nginx/1.18.0 (Ubuntu)"`. The list itself is rebuilt with
`python3 scripts/vulndb/build.py`; see [checks.md](checks.md).
`build/darwin-arm64/Debug/lokictl_watch_tests` (also run by `ctest`)
replays made-up frames through Watch's alert detector
(`tests/watch_alerts.cpp`): one of each alert kind, and ordinary traffic
that must raise none. `build/darwin-arm64/Debug/lokictl_health_tests`
(also run by `ctest`) gives each health check rule a made-up result that
should raise it and one that should not, and checks that every finding's
text fits the screen (`tests/health_rules.cpp`).
`build/darwin-arm64/Debug/lokictl_trap_tests` (also run by `ctest`)
checks the trap's limits, how what a visitor sent is shown, the reading of
the listening ports and the fixed port map (`tests/trap_checks.cpp`).
`build/darwin-arm64/Debug/lokictl_topology_tests` (also run by `ctest`,
built with the address and undefined-behaviour checkers) reads made-up
LLDP and CDP frames: whole, cut short at every byte, with fields running
past the end, hostile texts and random bytes, plus the Map's router, port
and network words (`tests/topology_frames.cpp`).
`build/darwin-arm64/Debug/lokictl_report_tests` (also run by `ctest`,
with the same checkers) builds a report from made-up values with hostile
texts in every field that comes from the network (`<script>`, quotes,
broken UTF-8, a spreadsheet formula) and checks that the page and the
devices table only ever show them as text, that the page loads and runs
nothing and links only to https pages, and that saving, listing and
reading keep to the reports folder with the right modes
(`tests/report_checks.cpp`). Given a folder, it also leaves the page made
from those values there (`report-hostile.html`) to open in a browser. The
simulator and snapshots save reports only when `LOKICTL_REPORT_DIR` names a
folder, for example
`LOKICTL_REPORT_DIR=$PWD/build/reports lokictl --snapshot out.png --page
health --keys 6,6,data,data,r --settle-ms 9000` saves one from the made-up
health check.

## Screenshots without a window

The same program can draw one page straight into a PNG file without opening
the simulator window. The picture is always 320x170, the size of the device
screen and the size the app store asks for. From the repository root, after
building:

```sh
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home.png
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-instrument.png --theme instrument
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-next.png --page home --keys c
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-switched.png --theme instrument --switch-to terminal
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/settings.png --page settings --theme terminal
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/counter-terminal.png --page demo-counter --theme terminal
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/keys.png --page demo-counter --keys 8,x
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-demo.png --data demo --data-steps 181
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-wifi.png --data wifi --theme terminal
./build/darwin-arm64/Debug/lokictl --list-pages
```

`scripts/store-screenshots.sh` makes the four store screenshots in
`screenshot/store/` this way, from made-up sample data only.

Options:

- `--snapshot <file>`: where to save the PNG. Missing folders are created.
- `--page <name>`: which page to draw. The default is `home`, the first
  page, and `--list-pages` prints all page names (see the list below).
- `--theme <name>`: `instrument`, `constellation` or `terminal`. The default
  is `constellation`, the app's default theme. Any other name is refused
  with a message that lists the three.
- `--switch-to <theme>[,<theme>...]`: after drawing the page, switch through
  these themes in order while it is shown, the same way a pick on the
  Settings page does, and take the picture after the last switch. After the
  first draw and after every switch it prints `fonts loaded: <n> (<theme>)`
  to standard error. The picture must be identical to one drawn directly in
  the last theme;
  [themes.md](themes.md#checking-live-switching) shows how to
  check that and how to look for leaking fonts.
- `--keys <key>[,<key>...]`: after drawing the page, press these keys one
  after another, the way the keyboard does, and take the picture afterwards
  (before any `--switch-to`). Names: `0` to `9`, `a` to `z`, `esc`,
  `enter`, `space`, `dot` (`.`), `colon` (`:`), `tab`, `backspace`, `up`,
  `down`, `left`, `right` and `help`. Add `:long` to hold a key for one
  second, for example `esc:long`. The name `data` is not a key: it takes
  one further reading of the values at that point (it has no `:long`). The
  page from `--page` is the first page, so start at `home` to test going
  back from Settings, and at `demo-counter` to test going back from the key
  test page. If the keys made the app ask to quit, `quit requested` goes to
  standard error. See
  [Testing keys](navigation.md#testing-keys-with---keys) in
  navigation.md for the details.
- `--data <name>`: where the values come from: `sample` (fixed made-up
  values, the default for a snapshot), `demo` (the made-up values, moving
  on a fixed script) or `live` (the real clock, battery and network link).
  `wifi`, `cable` and `offline` are fixed made-up examples of the other
  situations the device can be in (joined to a Wi-Fi network, on a cable,
  no network), and `tour` walks through all of them, one stop per reading;
  see [home.md](home.md). Any other name is refused with a message
  that lists all seven.
- `--data-steps <n>`: takes `n` further readings, from 0 to 100000, right
  after the page is built and before `--keys`. With `--data demo` each
  reading is one step of the demo script, so
  `--data demo --data-steps 181` shows `10:47` and `100%+`; with
  `--data tour` each reading is one stop, so `--data tour --data-steps 15`
  shows the widest texts. See [data.md](data.md).
- `--settle-ms <n>`: how long LVGL's own clock runs before the picture is
  taken, from 0 to 60000. The default is 300. With `--keys`, the app also
  runs this long after every key. The program does not really wait that
  long; it finishes in a fraction of a second.
- `--help`: shows a short summary of all options.

The app has 28 pages. `--list-pages` prints them in this order:

- `home`, the home page, where the app starts (see [home.md](home.md));
- `settings`, the Settings page with the theme picker and the "Dim when
  idle", "Remote view" and "Keep mode after quitting" rows, which the home
  page opens;
- `status`, the Status page with every detail (ports, VPN, USB Wi-Fi
  adapters, DNS, battery, device), which the home page's key 8 opens (see
  [navigation.md](navigation.md#the-status-page));
- `modes`, `hotspot` and `devices`: the Modes page (home key 4), the
  Hotspot page with the join code (key 7 there) and the Devices page (home
  `enter`);
- `tools`, `ping`, `dns`, `trace`, `ports`, `speed` and `wifi`: the Tools
  page (home key 7) and its Ping, DNS lookup, Trace, Ports, Speed and Wi-Fi
  pages (see
  [navigation.md](navigation.md#the-ping-dns-and-trace-pages),
  [the Ports page](navigation.md#the-ports-page),
  [the Speed page](navigation.md#the-speed-page) and
  [the Wi-Fi page](navigation.md#the-wi-fi-page));
- `discover`, the Discover page (home key 5), which lists the devices it
  hears on the network while sending nothing, and those a probe finds (see
  [navigation.md](navigation.md#the-discover-page));
- `map`, the Map page (Discover key 5), which draws the network as a map:
  the internet, the router, this device, the devices Discover found and
  the hotspot's clients (see
  [navigation.md](navigation.md#the-map-page));
- `demo-counter` and `demo-keys`, two demo pages kept for navigation and
  key tests: a counter and a key test page. Nothing in the app opens them
  any more, so they are drawn by name only, with `--page`; the counter
  page's key 8 still opens the key test page (see
  [navigation.md](navigation.md#the-demo-pages));
- eleven developer pages whose names start with `dev-` (see
  [Developer pages](#developer-pages)).

Notes:

- Snapshot mode does not read or change the settings file (not even the one
  named by `LOKICTL_CONFIG_FILE`), so the theme you picked in Settings does
  not affect the picture. Without `--theme` it always uses the default
  theme.
- Only the page itself is captured. Pop-up messages, the help overlay and
  the command bar are not included, also not when `--keys` opens them. The
  developer pages `dev-help`, `dev-toast` and `dev-command` show them on a
  page of their own, so their look can still be checked.
- A snapshot shows the fixed sample values unless `--data` says otherwise,
  on the home page and in the title bar of the other pages, so two pictures
  of the same page are always the same file.

## Developer pages

Pages whose names start with `dev-` are for developers. They can only be
drawn with `--snapshot` and cannot be opened from the app itself:

- `dev-fonts` and the five `dev-fonts-<family>` pages show the raw fonts
  (see [fonts.md](fonts.md));
- `dev-theme` shows a theme's colours, font roles, a card, a list and a pane;
- `dev-help` and `dev-toast` show the help panel and the pop-up messages;
- `dev-softkeys` shows the soft-key bar in every state;
- `dev-command` shows the command bar (`:`) with an error and with choices.

```sh
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/fonts.png --page dev-fonts
```

## Live data

The pages get their values from a small data layer. A background reader
collects them beside the screen, so reading never holds up drawing.
`--data <name>` picks the source: `sample`, `demo`, `live`, `wifi`,
`cable`, `offline` or `tour`. Without it, a snapshot uses `sample`, and a
normal start uses `live` on the device and `demo` in the simulator.
[home.md](home.md) shows what the home page looks like in each, and
[data.md](data.md) explains the sources, the reader, how a page updates in
place, how to add a value and every test option.

`--print-data` prints the values once as `name=value` lines, without a
window and without reading the settings file:

```sh
./build/darwin-arm64/Debug/lokictl --print-data --data sample
```

It starts like this:

```text
source=sample
sample=yes
clock=07:46
battery_percent=91
battery_state=discharging
mode=router
link=cable
```

and goes on with the network's lines (`interface=`, `address=`, `router=`,
`speed_mbps=` and more, down to `state=`), then the Status page's details
(one `port=` line per port, `dns=`, the battery, processor, memory and
uptime lines), then `usb_wifi=` with one `usb_wifi_adapter=` line per USB
Wi-Fi adapter. A value that is not known prints `unknown`.

It takes `--data` and `--data-steps` and nothing else. The environment
variable `LOKICTL_DATA_ROOT=<folder>` makes the `live` source read
`<folder>/sys/...` and `<folder>/proc/...` instead of the system's battery
and network files (and `<folder>/addresses` for the addresses), so every
battery state and kind of link can be tested on the Mac. On a device,
`scripts/device.sh data` prints the device's own values.

## Build the device package

The CardputerZero runs Raspberry Pi OS, a version of Debian Linux, on a 64-bit
ARM ("arm64") processor. To put the app on it, the app is compiled for that
system and packed into a Debian package, a `.deb` file the device can install.

What you need is a running Docker engine. Docker runs a small, separate Linux
system (a "container") on your computer, and the whole build happens inside
it, so you don't install compilers or libraries yourself. On macOS you can use
colima, for example; start it with `colima start`.

Then, from the repository root:

```sh
./scripts/build-deb.sh
```

What the script does:

- The first time, it creates a build image called `lokictl-build:trixie`:
  Debian 13 ("trixie") for arm64, the Debian release the CardputerZero's
  system is based on, with the compiler and libraries added. This takes about
  half a minute.
- It compiles the app inside a container, the same way it would be compiled
  on a CardputerZero, and writes
  `dist/lokictl_<version>-<revision>_arm64.deb`. The list of libraries the
  package needs is read from the finished program.
- The first build also downloads LVGL into `.cache/` and compiles all of it,
  which takes about a minute. Later builds only redo what changed and take a
  few seconds.
- At the end it prints the full path of the package.

The container needs an arm64 system. An Apple silicon Mac is one, so the build
runs at full speed there. On other computers Docker would have to emulate
arm64, which this project has not tested.

The easiest way to install the package on a device and start it there is
`scripts/device.sh`, described in [Run on a device](#run-on-a-device). If you
only want to copy the package, add the device's user name and address:

```sh
REMOTE_USER=<user> REMOTE_HOST=<device-ip> ./deploy.sh
```

This runs the build script and copies the package to the user's home folder
on the device with `scp`. Then, on the device, install it with:

```sh
sudo apt install ./lokictl_<version>-<revision>_arm64.deb
```

`apt` also installs the libraries the package needs.

On the device the system-wide settings file is `/etc/lokictl.conf`, and
per-user changes are saved to `~/.config/lokictl/lokictl.conf`.

### Alternative for Linux computers

On a Debian or Ubuntu computer you can build without Docker by using a cross
compiler, a compiler that runs on your computer but makes programs for the
device. Besides CMake 3.31 or newer and Ninja, install it with
`sudo apt install crossbuild-essential-arm64`, then run:

```sh
cmake --workflow --preset cp0-cross-package
```

The first run downloads M5Stack's BSP sysroot (a copy of the device's headers
and libraries) into `.cache/`. The package made this way uses a hand-written
list of the libraries it needs instead of one read from the program. To build
and copy in one step, run
`WORKFLOW_PRESET=cp0-cross-package REMOTE_USER=<user> REMOTE_HOST=<device-ip> ./deploy.sh`.
Use only one of the two routes in a checkout, because both keep LVGL in
`.cache/cp0-deps`.

## Run on a device

`scripts/device.sh` installs LokiCTL on a CardputerZero and runs it there.
It also lets you check the result from your computer: it saves what the
device's screen shows as a PNG file, presses keys on the device's keyboard and
reports how much memory and processor time LokiCTL uses.

What you need:

- The CardputerZero switched on, on the same network as your computer, and
  reachable with SSH using a key. The device's user needs `sudo` without a
  password (to install the package) and must be in the `video` and `input`
  groups (to read the screen and press keys). The device's standard user
  already has all of this.
- A running Docker engine to build the package, as described above.

Tell the script which device to use. These two settings live only in your
shell; they are not saved anywhere in the project:

```sh
export LOKICTL_DEVICE=<user>@<device-ip>
export LOKICTL_SSH_OPTS="-i <path-to-key>"   # optional: extra ssh options
```

A typical session, from the repository root:

```sh
scripts/device.sh deploy                         # build if needed, copy, install
scripts/device.sh data                           # its clock and battery
scripts/device.sh run --theme constellation      # start LokiCTL in one theme
scripts/device.sh screen build/shots/device.png  # save the device's screen
scripts/device.sh key c                          # select the next client
scripts/device.sh key 6                          # press 6 (opens Settings)
scripts/device.sh key x                          # move the highlight down
scripts/device.sh key enter                      # switch to Terminal, saved
scripts/device.sh key esc                        # back to the home page
scripts/device.sh screen build/shots/device-terminal.png
scripts/device.sh stats                          # memory, CPU and frame time
scripts/device.sh stop                           # stop it, the launcher returns
```

The commands:

- `deploy [--no-build] [--with-deps] [<file.deb>]`: builds the package with
  `scripts/build-deb.sh` (only what changed is rebuilt), copies it to the
  user's home folder on the device (as `lokictl_upload.deb`) and installs it
  with `apt`. It first checks
  that `apt` would install nothing but LokiCTL; `--with-deps` also allows
  missing libraries. `--no-build` takes the newest package in `dist/`, or you
  can name a package file. Afterwards it checks that the installed program
  matches the package and that the launcher entry and icon are in place, and
  deletes the copied file.
- `run [--theme T] [--data D]`: starts LokiCTL on the device's screen (see
  below). `--theme` picks `instrument`, `constellation` or `terminal` for
  this run only and does not save it; without it, the theme saved on the
  device decides, or else the default theme. A theme picked on the
  Settings page during the run is still saved. `--data` picks `sample`,
  `demo`, `live`, `wifi`, `cable`, `offline` or `tour` for this run only;
  without it the device reads live values. For example
  `scripts/device.sh run --data wifi` shows the made-up Wi-Fi example on
  the real screen, and `--data tour` changes the situation every 2 s (see
  [home.md](home.md)). If LokiCTL does not know a name, it exits
  at once: `run` then shows LokiCTL's message, brings the launcher back and
  exits with code 1.
- `stop`: stops LokiCTL, however it was started, and brings the launcher back
  on HOME. Running it when nothing runs does no harm.
- `status`: shows the installed version, whether LokiCTL runs and which
  program has the screen.
- `screen <file.png> [--no-wait]`: saves the device's screen, 320x170, with
  exactly the pixels that were written to it. It waits until the picture stops
  changing, unless you add `--no-wait`.
- `key <name>... [--long | --hold-ms N] [--launcher]`: presses keys, one after
  another (see below).
- `stats [--seconds N]`: measures LokiCTL for N seconds (3 by default, up to
  60) and prints the numbers explained below.
- `snapshot <file.png> [--page P] [--theme T] [--data D] [--data-steps N]
  [--keys K[,K...]] [--switch-to T[,T...]] [--settle-ms N]`: runs
  `lokictl --snapshot` on the device (see
  [Screenshots without a window](#screenshots-without-a-window)) and saves
  the picture. It works while LokiCTL is running and does not touch the
  screen. Without `--data` the picture shows the fixed sample values;
  `--data` takes any of the seven sources, for example `--data offline`,
  and `--data-steps` takes further readings before the keys. `--keys` presses
  keys inside that snapshot run, not on the device's keyboard, so LokiCTL
  does not need to be running; for example `scripts/device.sh snapshot
  build/shots/keys.png --page demo-counter --keys 8,x`. The name `data` in
  `--keys` takes one more reading at that point. The `fonts loaded` lines
  of `--switch-to` and the `quit requested` line of `--keys` appear on your
  computer's standard error.
- `data [--data D]`: runs `lokictl --print-data` on the device and prints
  its `name=value` lines: the source, whether the values are sample data,
  the clock, the battery's charge and state, and the network's lines (see
  [Live data](#live-data)). Without `--data` it reads the device's live
  values; `--data wifi`, `cable`, `offline` or `tour` print the made-up
  examples, the same lines as on the computer. It works while the
  launcher is on screen and leaves the screen and a running LokiCTL alone.
- `logs [-n N]`: shows the last N log lines (50 by default) of the latest
  `run` only, including systemd's lines about starting and stopping it.
  Earlier runs are left out. It also works after LokiCTL has stopped.
- `remove`: stops LokiCTL and uninstalls it.

`scripts/device.sh --help` lists the same. The exit code is 0 when the
command worked, 1 when it failed or refused (the message says why), 2 for a
usage error or a missing `LOKICTL_DEVICE`, and 3 when the device cannot be
reached. The script connects once per command and never retries, so a device
that is switched off gives a quick, clear error.

### How `run` shares the screen with the launcher

The device's own launcher, the HOME screen with the app carousel, normally
draws on the screen and reads the keyboard. When you open an app on the
device, the launcher stops drawing until the app ends. An app started over
SSH gets no such handover, so `run` pauses the launcher while LokiCTL is on
screen. The launcher comes back by itself as soon as LokiCTL ends: after
`stop`, if LokiCTL crashes, or when you quit LokiCTL on the device. `run`
refuses to start when LokiCTL already runs or when another app is open on the
device.

The launcher switches the screen's backlight off after a short time without
key presses. If that has happened before `run`, the panel stays dark while
LokiCTL runs (`screen` still captures what it draws, and `status` shows the
backlight). Press a key on the device to wake the screen first.

After `deploy`, LokiCTL also appears on the launcher's HOME screen, and you can
open it there like any other app. Holding ESC for 3 seconds makes the launcher
close it again.

### Pressing keys

Key names: `0` to `9`, `a` to `z`, `esc`, `enter`, `tab`, `space`,
`backspace`, `up`, `down`, `left`, `right`, `help` and `screenshot`. The fn
combinations `fn+h`, `fn+j`, `fn+f`, `fn+x`, `fn+z` and `fn+c` work too. The
keyboard handles fn by itself and reports these combinations as other keys
(help, screenshot and the four arrows), so the script sends those keys.

A press lasts 60 ms. `--long` holds each key for 1 second, and `--hold-ms`
holds it for 20 to 2500 ms. Keys are only sent while LokiCTL runs, so they
cannot open something on the launcher by accident; `--launcher` turns that
check off.

After 60 s without a key LokiCTL dims the screen, and the first key after
that only brightens it again (see
[Battery care](data.md#battery-care)). After a wait, send one extra
key first (any key: it only wakes the screen), or switch "Dim when idle" off
on the Settings page.

What each key does on each page is listed in
[navigation.md](navigation.md).

### What the stats mean

- `pid`: LokiCTL's process number, how it was started and how long it has
  been running.
- `memory`: RSS is the memory LokiCTL holds, including the parts of system
  libraries it uses. PSS shares those libraries out between all programs that
  use them, so it is the fairer number.
- `cpu`: how busy LokiCTL kept one of the four processor cores during the
  measurement; 100 % means one core was fully busy.
- `system memory`: how much memory the whole device still has available, of
  the total, and how much has been moved to compressed swap.
- `load`: the device's load averages over 1, 5 and 15 minutes.
- `frame time`: how long LokiCTL took to draw a frame and send it to the
  screen (average, slowest and latest), and how many frames it drew in total
  and during the measurement. A screen that does not change draws almost
  nothing, so few frames are normal.

Frame time is only measured when LokiCTL was started with `run`. `run` sets
the environment variable `LOKICTL_FRAME_STATS_FILE` to a file path, and
LokiCTL then writes its frame counts to that file about once a second while it
draws. Without the variable LokiCTL measures nothing.

### What the kit changes on the device

Nothing is installed besides the LokiCTL package. The device side of the
script, `scripts/device-agent.py`, is sent to the device's `python3` with each
command and is not stored there, and the copied package file is deleted after
installing. What LokiCTL itself writes and changes on the device is listed in
[permissions.md](permissions.md#files-lokictl-writes).

A saved picture shows exactly the pixels written to the screen, but how
readable they are on the small 1.9-inch panel is something only your own
eyes can judge.

## Project layout

```text
assets/
  audio/              sound effects
  fonts/              text and icon fonts and their licenses
  images/             app icon (lokictl.svg and the PNGs made from it) and
                      the simulator's device frame
cmake/
  cm0-package.cmake   install layout and Debian packaging
  templates/          inputs for the launcher entry, systemd unit and conffiles
  toolchains/         cross-compiler setup for the device
config/               default settings file and the polkit rule
docs/                 notes on the themes, navigation, the home page, the
                      data layer, remote view, permissions and fonts
screenshot/
  store/              the store screenshots (320 x 170), made by
                      scripts/store-screenshots.sh
scripts/
  build-deb.sh        builds the device package in a Debian arm64 container
  build-deb.Dockerfile
                      the build image used by build-deb.sh
  device.sh           installs, runs and tests LokiCTL on a device over SSH
  device-agent.py     the device side of device.sh
  make-icons.sh       draws the app icon PNGs from the SVG in a Debian
                      container
  store-screenshots.sh
                      draws the store screenshots with the simulator's
                      snapshot mode from made-up sample data
src/
  app/                startup, command line, snapshot mode, main loop, page
                      switching, key handling, command bar, assets,
                      simulator window
  config/             LVGL configuration for desktop and device
  helper/             the two small helper programs (lokictl-helper and
                      lokictl-capture)
  logger/             logging
  model/              application state
  platform/           keyboard input, screenshots, frame statistics, the data
                      layer (data sources, battery reader, background
                      reader), the network tools and other device services
  reactive/           helpers around LVGL observer subjects
  view/               themes (colour and font tables, theme engine), shared
                      style helpers and UI constants
    screens/          full-screen pages
    widgets/          reusable UI parts
  viewmodel/          state and actions exposed to the views
  main.cpp            program entry point
CMakeLists.txt        build definition and app identity
CMakePresets.json     configure and build presets
app-builder.json      app store metadata
deploy.sh             builds the device package and copies it to a device
                      (scripts/device.sh also installs and runs it)
```

## Renaming the app

The name is defined in the identity block at the top of `CMakeLists.txt` (the
`project()` line and `APP_DISPLAY_NAME`). When changing it, also rename
`config/<name>.conf` and `assets/images/<name>.svg` and `<name>*.png`, and
update `app-builder.json`, `icon_name` in `scripts/make-icons.sh` and the
README. Configure stops with a clear message if the file names and the
identity block disagree. The build image tag
`lokictl-build:trixie` at the top of `scripts/build-deb.sh` carries the name
too; renaming it is optional. The device scripts carry it as well: the
constants at the top of `scripts/device-agent.py` (program path, unit and file
names), the `dist/lokictl_*_arm64.deb` pattern in `scripts/device.sh`, the
environment variable `LOKICTL_FRAME_STATS_FILE` (set in
`scripts/device-agent.py`, read in `src/app/app.cpp`) and the environment
variables `LOKICTL_CONFIG_FILE`, `LOKICTL_DATA_ROOT`, `LOKICTL_IDLE_SECONDS`
and `LOKICTL_BACKLIGHT_FILE` (read in `src/app/app.cpp`).
The connection variables `LOKICTL_DEVICE` and `LOKICTL_SSH_OPTS` and the
upload file name (`upload_name`) in `scripts/device.sh` contain it too, and
so do the copyright holder in `LICENSE`, the SPDX headers of the project's own
files and the mentions in `docs/`.

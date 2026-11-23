<!--
SPDX-FileCopyrightText: 2026 LokiCTL contributors
SPDX-License-Identifier: MIT
-->

# LokiCTL

LokiCTL is a pocket network multitool for the M5Stack CardputerZero, a small
Linux handheld with a 320x170 screen and a built-in keyboard. It shows the
state of your network at a glance, switches the device between router,
hotspot-sharing and VPN-gateway modes, finds the devices on a network and
offers everyday network tools, all with three switchable visual themes:
Instrument, Constellation and Terminal.

## Status

**Early development.** The app builds and runs in the desktop simulator. It
opens on a home page drawn in each theme's own layout, and it has a
Settings page where you pick the theme. On the device the home page shows
the real time, battery charge, network link and traffic (download and
upload speed with a moving graph). Of the network features above, the
Modes page with Client and Router (sharing the cable's internet through a
hotspot) works; the others do not exist yet. The made-up example data used
in the simulator and in screenshots is marked `sample data`.

The device package also builds, and it installs and runs on a real
CardputerZero. From your computer, `scripts/device.sh` installs and starts it
on the device, saves what the device's screen shows, presses keys on the
device and reports memory and CPU use (see [Run on a device](#run-on-a-device)).

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
  `H` on the drawn keyboard. [docs/navigation.md](docs/navigation.md)
  explains every key.
- `--theme <name>` starts the simulator in one theme for this run only, for
  example `./build/darwin-arm64/Debug/lokictl --theme constellation`. The
  device frame drawn around the screen follows the theme: white for
  Instrument, black for the other two. See [Themes](#themes).
- The simulator's home page shows moving demo values: the clock runs one
  minute per second and the battery counts down, to show that the page
  follows its data. `--data <name>` picks other values for the run:
  `sample` (fixed values), `demo` or `live` (the Mac's own clock; the Mac
  has no Linux network files, so it shows no network). The
  made-up examples `--data wifi`, `--data cable` and `--data offline` show
  the home page joined to a Wi-Fi network, on a cable and with no network,
  and `--data tour` walks through all of them, one every 2 s. See
  [Live data](#live-data) and [docs/home.md](docs/home.md).
- The simulator reads its settings from `config/lokictl.conf`. Picking a
  theme on the Settings page (opened with `6` on the home page) saves the
  choice and rewrites that file. To keep the repository's file as it is,
  give the app another file in the environment variable
  `LOKICTL_CONFIG_FILE`, for example
  `LOKICTL_CONFIG_FILE=build/simulator.conf ./build/darwin-arm64/Debug/lokictl`.
  The app then reads and writes that file instead; it is created the first
  time a theme is picked.

## Themes

LokiCTL has three themes: Instrument, Constellation and Terminal. Each one
sets the colours, fonts, spacing and borders of every screen. The home page
goes further and has a layout of its own in each theme: a big-type gauge in
Instrument, a network map in Constellation and boxed panes in Terminal (see
[docs/home.md](docs/home.md)).

The theme is picked on the Settings page, which `6` (SETUP) on the home page
opens. It lists the three themes, each with its name, four small squares in
that theme's colours and a tick on the theme that is active. Move the
highlight with `f` and `x` (or the up and down arrows), then press `enter`
or `6` (USE). The app switches at once: the page is drawn again in the new
theme, and a short message names it. The choice is saved as `theme=<name>`
in the `[application]` section of the settings file, and every other line of
that file stays as it is. Picking the theme that is already active does
nothing.

At startup the app picks the theme like this:

1. `--theme instrument`, `--theme constellation` or `--theme terminal` on the
   command line. On a normal start it counts for that run only and is never
   written to the settings file, but a theme picked on the Settings page
   during the run is saved. Picking the theme that is already running does
   nothing, so that one is not saved.
2. Otherwise the saved `theme=` line. A name the app does not know is
   reported in the log; the app then skips to step 4 and leaves the file
   as it is.
3. Otherwise an old `dark_mode=` line from an earlier version: `yes` means
   Terminal, `no` means Instrument. The app replaces it with the matching
   `theme=` line the first time it starts with that file.
4. Otherwise the default theme, Constellation. It is set in one place,
   `view::kDefaultThemeId` in `src/view/theme.h`.

[docs/themes.md](docs/themes.md) lists every colour, font and spacing value
of the three themes and explains how screens use them, how a live switch
works and how to check it. [docs/navigation.md](docs/navigation.md) explains
how pages open on top of each other, what every key does and how a page
handles keys.

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
  [docs/themes.md](docs/themes.md#checking-live-switching) shows how to
  check that and how to look for leaking fonts.
- `--keys <key>[,<key>...]`: after drawing the page, press these keys one
  after another, the way the keyboard does, and take the picture afterwards
  (before any `--switch-to`). Names: `0` to `9`, `a` to `z`, `esc`,
  `enter`, `space`, `backspace`, `up`, `down`, `left`, `right` and `help`.
  Add `:long` to hold a key for one second, for example `esc:long`. The
  name `data` is not a key: it takes one further reading of the values at
  that point (it has no `:long`). The page from `--page` is the first
  page, so start at `home` to test going back from Settings, and at
  `demo-counter` to test going back from the key test page. If the keys
  made the app ask to quit, `quit requested` goes to standard error. See
  [Testing keys](docs/navigation.md#testing-keys-with---keys) in
  docs/navigation.md for the details.
- `--data <name>`: where the values come from: `sample` (fixed made-up
  values, the default for a snapshot), `demo` (the made-up values, moving
  on a fixed script) or `live` (the real clock, battery and network link).
  `wifi`, `cable` and `offline` are fixed made-up examples of the other
  situations the device can be in (joined to a Wi-Fi network, on a cable,
  no network), and `tour` walks through all of them, one stop per reading;
  see
  [docs/home.md](docs/home.md). Any other name is refused with a message
  that lists all seven.
- `--data-steps <n>`: takes `n` further readings, from 0 to 100000, right
  after the page is built and before `--keys`. With `--data demo` each
  reading is one step of the demo script, so
  `--data demo --data-steps 181` shows `10:47` and `100%+`; with
  `--data tour` each reading is one stop, so `--data tour --data-steps 15`
  shows the widest texts. See [docs/data.md](docs/data.md).
- `--settle-ms <n>`: how long LVGL's own clock runs before the picture is
  taken, from 0 to 60000. The default is 300. With `--keys`, the app also
  runs this long after every key. The program does not really wait that
  long; it finishes in a fraction of a second.
- `--help`: shows a short summary of all options.

The app has 24 pages. `--list-pages` prints them in this order:

- `home`, the home page, where the app starts (see
  [docs/home.md](docs/home.md));
- `settings`, the Settings page with the theme picker and the "Dim when
  idle" switch, which the home page opens;
- `status`, the Status page with every detail (ports, DNS, battery,
  device), which the home page's key 8 opens (see
  [docs/navigation.md](docs/navigation.md#the-status-page));
- `modes`, `hotspot` and `devices`: the Modes page (home key 4), the
  Hotspot page with the join code (key 7 there) and the Devices page (home
  `enter`);
- `tools`, `ping`, `dns`, `trace` and `wifi`: the Tools page (home key 7)
  and its Ping, DNS lookup, Trace and Wi-Fi pages (see
  [docs/navigation.md](docs/navigation.md#the-ping-dns-and-trace-pages)
  and [the Wi-Fi page](docs/navigation.md#the-wi-fi-page));
- `discover`, the Discover page (home key 5), which lists the devices it
  hears on the network while sending nothing (see
  [docs/navigation.md](docs/navigation.md#the-discover-page));
- `demo-counter` and `demo-keys`, two demo pages kept for navigation and
  key tests: a counter and a key test page. Nothing in the app opens them
  any more, so they are drawn by name only, with `--page`; the counter
  page's key 8 still opens the key test page (see
  [Pressing keys](#pressing-keys));
- ten developer pages whose names start with `dev-` (see [Fonts](#fonts)).

Notes:

- Snapshot mode does not read or change the settings file (not even the one
  named by `LOKICTL_CONFIG_FILE`), so the theme you picked in Settings does
  not affect the picture. Without `--theme` it always uses the default
  theme.
- Only the page itself is captured. Pop-up messages and the help overlay are
  not included, also not when `--keys` opens them. The developer pages
  `dev-help` and `dev-toast` show the same help panel and messages on a page
  of their own, so their look can still be checked.
- A snapshot shows the fixed sample values unless `--data` says otherwise,
  on the home page and in the title bar of the other pages, so two pictures
  of the same page are always the same file.

## Live data

The home page gets its values from a small data layer. A background reader
collects them beside the screen, so reading never holds up drawing. When a
value changes, the page changes only that text; when the situation changes
(for example from a Wi-Fi network to a cable), the page's content is built
again, once. The clock, the battery and the network link (Wi-Fi or cable,
address, router, signal, cable speed) are real; the traffic is still made
up, so the page keeps its `sample data` mark. The title bar of the other
pages shows the same clock, battery and link.

`--data <name>` picks the source: `sample`, `demo`, `live`, `wifi`,
`cable`, `offline` or `tour`. Without it, a snapshot uses `sample`, and a
normal start uses `live` on the device and `demo` in the simulator. `live`
shows the device's real mode (Client, or Router while LokiCTL's hotspot
runs) with its real link, and on the device the Wi-Fi network's name and
state from NetworkManager. While Router runs it also lists the devices on
the hotspot (see [docs/data.md](docs/data.md#devices-on-the-hotspot)).
`--data wifi`, `--data cable` and `--data offline` are fixed made-up
examples of the device joined to a Wi-Fi network, on a cable and with no
network, and `--data tour` walks through every situation, one stop every
2 s in a window. [docs/home.md](docs/home.md) shows what the page looks
like in each.

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
uptime lines). A value that is not known prints `unknown`.

It takes `--data` and `--data-steps` and nothing else. The environment
variable `LOKICTL_DATA_ROOT=<folder>` makes the `live` source read
`<folder>/sys/...` and `<folder>/proc/...` instead of the system's battery
and network files (and `<folder>/addresses` for the addresses), so every
battery state and kind of link can be tested on the Mac. On a device,
`scripts/device.sh data` prints the device's own values.
[docs/data.md](docs/data.md) explains the sources, the reader, how a page
updates in place, how to add a value and every test option.

## Fonts

LokiCTL bundles five text font families in `assets/fonts/`: Space Grotesk,
Inter and IBM Plex Mono for the Instrument theme, Geist Mono for
Constellation and JetBrains Mono for Terminal. JetBrains Mono also fills in
the symbols the other fonts lack. They are the unmodified files from each
project's official release and are licensed under the SIL Open Font License.
[docs/fonts.md](docs/fonts.md) lists their versions and which special
characters (arrows, dots, line pieces, braille) each font can draw.

To see the fonts as the app draws them:

```sh
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/fonts.png --page dev-fonts
```

Pages whose names start with `dev-` are for developers. They can only be
drawn this way and cannot be opened from the app itself:

- `dev-fonts` and the `dev-fonts-<family>` pages show the raw fonts;
- `dev-theme` shows a theme's colours, font roles, a card, a list and a pane;
- `dev-help` and `dev-toast` show the help panel and the pop-up messages;
- `dev-softkeys` shows the soft-key bar in every state.

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
  [docs/home.md](docs/home.md)). If LokiCTL does not know a name, it exits
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
[Battery care](docs/data.md#battery-care)). After a wait, send one extra
key first (any key: it only wakes the screen), or switch "Dim when idle" off
on the Settings page.

The bar along the bottom of the screen names what the keys `4` to `8` do on
the current page. Pages open on top of each other, and a short `esc` goes
back one page. On the home page, which shows the real time and battery
but sample network data for now (see [docs/home.md](docs/home.md)):

- `f`, `z`, `up` or `left` selects the previous client and `x`, `c`,
  `down` or `right` the next one; the selection stops at the first and the
  last client;
- `6` SETUP opens the Settings page;
- `8` INFO opens the Status page, a list of every detail (every network
  port, DNS, battery, processor, memory) that scrolls with `f`/`x` and
  jumps between its parts with `z`/`c` or `5`/`6`;
- `4` MODES opens the Modes page (below);
- `enter` opens the Devices page (below), on the selected client;
- `7` TOOLS opens the Tools page: Ping, DNS lookup, Trace (the route to
  a host, hop by hop) and Wi-Fi (the networks nearby), with a count of
  every packet LokiCTL sent;
- `5` SCAN opens the Discover page: `6` START listens on the network the
  Cardputer is on for 5 minutes and lists every device it hears (hardware
  address, IPv4 address, the name it gave over DHCP, how long ago it was
  heard), while sending nothing (`Sent` does not move); `6` STOP ends it
  early, `f`/`x` scroll;
- a short `esc` shows the hint `Hold ESC to exit`, and holding `esc` quits.

The Settings page lists the three themes; a tick marks the active one:

- `f` or `up` moves the highlight up, `x` or `down` moves it down; it stops
  at the first and the last row;
- `enter` or `6` USE switches to the highlighted theme at once and saves it
  on the device in `~/.config/lokictl/lokictl.conf` (a message names the
  theme). USE is dimmed while the highlight is on the active theme, and
  picking that theme does nothing;
- `4` BACK or a short `esc` goes back to the home page.

The Modes page switches the device between its modes. A tick marks the
mode that is on:

- **Client** joins a Wi-Fi network, like a phone. This is how the device
  normally runs.
- **Router** shares the cable's internet through a hotspot of its own on
  the built-in Wi-Fi. It needs a network cable that leads to the internet;
  without one the page says `Plug in a cable first` and nothing changes.
  The hotspot is called `LokiCTL-` and four characters of the Wi-Fi's
  hardware address until you rename it. Its password is made once, at the
  first switch to Router, and saved in `~/.config/lokictl/lokictl.conf`
  (readable only by you). The page shows the name and the password while
  Router runs.
- **Share** stays on the Wi-Fi network the device has joined and runs the
  same hotspot next to it, so phones and laptops on the hotspot get the
  internet the device has (over the cable when one is plugged in,
  otherwise over that Wi-Fi). It needs the built-in Wi-Fi joined to a
  network; without one the page says `Join a Wi-Fi network first`. The
  chip sends on one channel only, so the hotspot uses the channel of the
  joined network. **The joined Wi-Fi drops for a few seconds whenever the
  hotspot starts or stops (usually 2 to 3), and up to about 15 seconds
  after a new name or password**, then comes back by itself with the same
  address. To go between Router and Share, go back to
  Client first; the page says `Back to Client first`.
- **VPN** comes later and is dimmed.

`f` or `up` and `x` or `down` move the highlight; `enter` or `6` USE
switches to the highlighted mode, and a line under the list says
`Switching…`, then shows the hotspot or what went wrong. USE is
dimmed on the mode that is on and while a switch runs. `4` BACK or a short
`esc` goes back. **Whenever LokiCTL quits, the device goes back to
Client**: the hotspot is removed and the Wi-Fi network it was on before
comes back (in Share it never left). The hotspot's profile is only ever
kept in memory, so a restart of the device always ends in Client too. If
LokiCTL is killed instead of quitting, the hotspot stays on until LokiCTL
starts again; the Modes page then shows Router or Share and can switch
back. On the Mac the switch
only pretends, so the pages can be tried.

`7` QR on the Modes page opens the Hotspot page. While Router or Share runs it shows
a QR code at the left: point a phone's camera at it to join the hotspot
without typing the password. At the right are the hotspot's name, its
password, the Wi-Fi channel and how long it has run; while Router is off it
shows the name and password Router will use.

- `5` NAME renames the hotspot: type the new name (1 to 32 characters;
  the keyboard gives lowercase letters, digits and space for now), `enter`
  saves it, a short `esc` cancels. `enter` on an empty name puts the
  default name back.
- `6` PASS makes a new random password; press it twice, so it never
  happens by accident.
- Both are saved in `~/.config/lokictl/lokictl.conf`. While Router or
  Share runs the hotspot starts again at once with the new name or password (the page
  says `Applying…`), so phones and laptops on it must join again.
- `4` BACK or a short `esc` goes back.

`enter` on the home page opens the Devices page: every phone, laptop or
other device on the hotspot, two at a time. Each shows its name (the one it
gave when it joined, or `unnamed`), `new` for the first five minutes, its
address, its traffic, its hardware address and how long ago it joined. The
home page's router view shows the same devices (at most three). `f`/`x`,
`z`/`c` or `5`/`6` move the highlight, `7` QR opens the Hotspot page and
`4` BACK or a short `esc` goes back. While the hotspot is off the page says so.
The built-in Wi-Fi does not report each device's signal while it runs a
hotspot, so the page leaves the signal out there.

`8` BLOCK blocks the highlighted device: it stays joined to the hotspot and
listed (with `blocked`), but gets no address, no internet and no answer
from the Cardputer. `8` ALLOW lets it through again. The list of blocked
devices is saved and applies again whenever Router or Share runs; it goes by the
device's hardware address, so a device that changes its address (many
phones pick a new one per network) is a new device. If blocking fails, a
message says why (`Block failed: helper not installed`).

The package installs a polkit rule, `/usr/share/polkit-1/rules.d/60-lokictl.rules`,
because apps started by the launcher may not change NetworkManager's
settings on their own. It lets members of the `netdev` group control the
network, change system-wide connection profiles and share the network
through a protected (WPA) hotspot. polkit cannot tell programs apart, so
**every program of a `netdev` member gets these rights, not only
LokiCTL**. Removing the package removes the rule. Router also needs
`dnsmasq-base` and `nftables`, which the package depends on.

Blocking, and Share's second Wi-Fi interface, need one right the app
itself does not have: network administration. The package installs a
small helper program for that,
`/usr/libexec/lokictl/lokictl-helper` (source: `src/helper/lokictl_helper.c`).
It is not setuid; the package's install script gives it the single Linux
capability `CAP_NET_ADMIN` (with `setcap`, from `libcap2-bin`) and lets only
root and the `netdev` group run it. It does one of six fixed commands
(`block` and `unblock` with device addresses, `list`, `clear`, `ap-add`,
`ap-del`) and exits. It only ever changes its own nftables table, `inet
lokictl`: a set of blocked addresses and two rules that drop what they send
in through the hotspot's interface (`wlan0` in Router, `lokiap0` in Share).
`ap-add` and `ap-del` add and remove Share's interface `lokiap0` with `iw`
(which the package depends on) and fixed arguments. LokiCTL removes the
table when the hotspot stops, and `lokiap0` when Share stops, and both when
it quits; removing the package removes them too. Like the polkit rule,
**every program of a `netdev` member may run the helper**, so it could
block or unblock devices on the hotspot and add or remove `lokiap0`, and
nothing else.

The Discover page needs a second right: hearing the network's raw traffic.
A second, separate helper does that,
`/usr/libexec/lokictl/lokictl-capture` (source: `src/helper/lokictl_capture.c`),
with the single capability `CAP_NET_RAW`, again not setuid and only for
root and the `netdev` group. It does one fixed command (`discover`, an
interface and a number of seconds, at most 30 minutes). It opens a packet
socket with a fixed filter that lets only ARP and DHCP through, never in
promiscuous mode, then drops every capability and locks itself (seccomp)
so that it can only receive, never send. It writes what it hears to the
app as a PCAP stream on a pipe (no file) and stops at its time limit,
after 16 MiB, or when the app goes away; the app reads the stream without
any rights. **Every program of a `netdev` member may run it too**, so it
could listen to the ARP and DHCP traffic that reaches the Cardputer, and
nothing else. The store may refuse either helper; both are optional for
the rest of the app.

The two demo pages, the counter and the key test page, are not opened from
the home page. They are reached with `--page` in snapshot runs (see
[Screenshots without a window](#screenshots-without-a-window)), where the
keys are pressed with `--keys`. On the counter page, `5` (or `z` or `left`)
counts down and `7` (or `c` or `right`) counts up. `8` KEYS opens the key
test page. `6` does nothing there. Started with `--page demo-counter`, the
counter page is the first page of the run, so its `4` BACK has nothing to
go back to, and `esc` works as on the home page.

The key test page shows what a page gets from the keyboard. It has a list
of four rows and a box that names the last key the page got:

- `f` or `up` moves the selection up, `x` or `down` moves it down; it stops
  at the first and the last row;
- `z`, `c`, `left` and `right` only show their name (`LEFT`, `RIGHT`);
- `enter` picks the selected row and marks it;
- `5` TYPE turns typing mode on: letters and the keys `4` to `8` then
  arrive as characters (the box shows `f` instead of `UP`), the soft keys
  are dimmed, and the arrow keys and `enter` still work. A short `esc` ends
  typing mode;
- `4` BACK or a short `esc` goes back to the counter page.

The pages keep what they show when you leave them and come back, and when
the theme changes. The one exception is the Settings page: each time it is
opened, the highlight starts on the active theme again. `help` (fn+H) shows
the keys of the page that is open; `esc` or `help` closes it again. A long
`esc` (`key esc --long`) only quits on the first page, the home page; on the
other pages it does nothing, because holding ESC for 3 seconds is how the
launcher closes an app.

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

### What it changes on the device

Nothing is installed besides the LokiCTL package. The device side of the
script, `scripts/device-agent.py`, is sent to the device's `python3` with each
command and is not stored there, and the copied package file is deleted after
installing. LokiCTL keeps one file of its own there: picking a theme or
switching "Dim when idle" on the Settings page saves it in
`~/.config/lokictl/lokictl.conf`, and so do the first switch to Router
(the hotspot's password), a new name or password on the Hotspot page and
blocking a device (the `blocked_devices=` list). The package also installs
the polkit rule and the two helpers described above. Switching to Router or Share changes the network (see the Modes
page above); quitting LokiCTL switches back to Client. While it runs, LokiCTL dims the screen
through `/sys/class/backlight/backlight/brightness` and puts the old value
back when it quits (see [docs/data.md](docs/data.md#battery-care)). A saved picture
shows exactly the pixels written to the screen, but how readable they are on
the small 1.9-inch panel is something only your own eyes can judge.

## Project layout

```text
assets/
  audio/              sound effects
  fonts/              text and icon fonts and their licenses
  images/             app icons and the simulator's device frame
cmake/
  cm0-package.cmake   install layout and Debian packaging
  templates/          inputs for the launcher entry, systemd unit and conffiles
  toolchains/         cross-compiler setup for the device
config/               default settings file
docs/                 notes on the bundled fonts, the themes, navigation,
                      the home page and the data layer
screenshot/           simulator screenshots
scripts/
  build-deb.sh        builds the device package in a Debian arm64 container
  build-deb.Dockerfile
                      the build image used by build-deb.sh
  device.sh           installs, runs and tests LokiCTL on a device over SSH
  device-agent.py     the device side of device.sh
src/
  app/                startup, command line, snapshot mode, main loop, page
                      switching, key handling, assets, simulator window
  config/             LVGL configuration for desktop and device
  logger/             logging
  model/              application state
  platform/           keyboard input, screenshots, frame statistics, the data
                      layer (data sources, battery reader, background
                      reader) and other device services
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
`config/<name>.conf` and `assets/images/<name>*.png`, and update
`app-builder.json` and this README. Configure stops with a clear message if the
file names and the identity block disagree. The build image tag
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

## License

MIT, see [LICENSE](LICENSE).
The project started from M5Stack's CardputerZero application template (MIT,
https://github.com/CardputerZero/Template); files derived from it keep their
original copyright headers.
The bundled fonts and icons have their own licenses (SIL Open Font License
1.1 and MIT), listed in `assets/fonts/LICENSE.txt`. LokiCTL is an
independent project and is not affiliated with or endorsed by M5Stack or
Flipper Devices.

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

**Early development.** The project has just been set up. It builds and runs in
the desktop simulator. It still shows mostly the starter demo pages. The
first real page is Settings, where you pick the theme; none of the network
features above exist yet.

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
  keys. A short `ESC` goes back one page; on the first page, hold `ESC` (or
  close the window) to quit. For the help panel (fn+H), click `fn` and then
  `H` on the drawn keyboard. [docs/navigation.md](docs/navigation.md)
  explains every key.
- `--theme <name>` starts the simulator in one theme for this run only, for
  example `./build/darwin-arm64/Debug/lokictl --theme constellation`. The
  device frame drawn around the screen follows the theme: white for
  Instrument, black for the other two. See [Themes](#themes).
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
sets the colours, fonts, spacing and borders of every screen.

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
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/counter-terminal.png --page demo-counter --theme terminal
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-instrument.png --theme instrument
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/home-switched.png --theme instrument --switch-to terminal
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/keys.png --page demo-home --keys 8,8,x
./build/darwin-arm64/Debug/lokictl --snapshot build/shots/settings.png --page settings --theme terminal
./build/darwin-arm64/Debug/lokictl --list-pages
```

Options:

- `--snapshot <file>`: where to save the PNG. Missing folders are created.
- `--page <name>`: which page to draw. The default is the first page, and
  `--list-pages` prints all page names (see the list below).
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
  page from `--page` is the first page, so start at `demo-home` to test
  going back. If the keys made the app ask to quit, `quit requested` goes
  to standard error. See
  [Testing keys](docs/navigation.md#testing-keys-with---keys) in
  docs/navigation.md for the details.
- `--settle-ms <n>`: how long LVGL's own clock runs before the picture is
  taken, from 0 to 60000. The default is 300. With `--keys`, the app also
  runs this long after every key. The program does not really wait that
  long; it finishes in a fraction of a second.
- `--help`: shows a short summary of all options.

The app has 14 pages. `--list-pages` prints them in this order:

- `demo-home`, `demo-counter` and `demo-keys`, the three demo pages: the
  home page, a counter and a key test page. The app opens them itself (see
  [Pressing keys](#pressing-keys));
- `settings`, the Settings page with the theme picker, which the home page
  opens;
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
- The clock in the title bar shows the current time, so two pictures taken a
  minute apart are not identical.

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
scripts/device.sh run --theme constellation      # start LokiCTL in one theme
scripts/device.sh screen build/shots/device.png  # save the device's screen
scripts/device.sh key 6                          # press 6 (opens Settings)
scripts/device.sh key x                          # move the highlight down
scripts/device.sh key enter                      # switch to Terminal, saved
scripts/device.sh key esc                        # back to the home page
scripts/device.sh key 8                          # press 8 (counter page)
scripts/device.sh screen build/shots/device-counter.png
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
- `run [--theme T]`: starts LokiCTL on the device's screen (see below).
  `--theme` picks `instrument`, `constellation` or `terminal` for this run
  only and does not save it; without it, the theme saved on the device
  decides, or else the default theme. A theme picked on the Settings page
  during the run is still saved. If LokiCTL does not know the name, it
  exits at once: `run` then shows LokiCTL's message, brings the launcher
  back and exits with code 1.
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
  30) and prints the numbers explained below.
- `snapshot <file.png> [--page P] [--theme T] [--keys K[,K...]]
  [--switch-to T[,T...]] [--settle-ms N]`: runs `lokictl --snapshot` on the
  device (see [Screenshots without a window](#screenshots-without-a-window))
  and saves the picture. It works while LokiCTL is running and does not
  touch the screen. `--keys` presses keys inside that snapshot run, not on
  the device's keyboard, so LokiCTL does not need to be running; for
  example `scripts/device.sh snapshot build/shots/keys.png --page demo-home
  --keys 8,8,x`. The `fonts loaded` lines of `--switch-to` and the
  `quit requested` line of `--keys` appear on your computer's standard
  error.
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

The bar along the bottom of the screen names what the keys `4` to `8` do on
the current page. Pages open on top of each other, and a short `esc` goes
back one page. On the home page of the current demo:

- `4` EXIT: pressing `4` or `esc` shows the hint `Hold ESC to exit`, and
  holding either one quits;
- `5` SIZE switches the greeting between two text sizes;
- `6` SETUP opens the Settings page;
- `7` INFO shows the version line;
- `8` COUNT opens the counter page.

The Settings page lists the three themes; a tick marks the active one:

- `f` or `up` moves the highlight up, `x` or `down` moves it down; it stops
  at the first and the last row;
- `enter` or `6` USE switches to the highlighted theme at once and saves it
  on the device in `~/.config/lokictl/lokictl.conf` (a message names the
  theme). USE is dimmed while the highlight is on the active theme, and
  picking that theme does nothing;
- `4` BACK or a short `esc` goes back to the home page.

On the counter page, `4` or a short `esc` goes back to the home page, `5`
(or `z` or `left`) counts down and `7` (or `c` or `right`) counts up. `8`
KEYS opens the key test page. `6` does nothing there.

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
installing. LokiCTL keeps one file of its own there: picking a theme on the
Settings page saves it in `~/.config/lokictl/lokictl.conf`. A saved picture
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
docs/                 notes on the bundled fonts, the themes and navigation
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
  platform/           keyboard input, screenshots, frame statistics and other
                      device services
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
variable `LOKICTL_CONFIG_FILE` (read in `src/app/app.cpp`).
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
1.1, MIT and CC0), listed in `assets/fonts/LICENSE.txt`. LokiCTL is an
independent project and is not affiliated with or endorsed by M5Stack or
Flipper Devices.

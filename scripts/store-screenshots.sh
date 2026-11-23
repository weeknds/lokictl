#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
#
# Makes the store screenshots in screenshot/store/, each 320 x 170 pixels:
#   1-home.png    the home page in the Constellation theme
#   2-modes.png   the Modes page in the Instrument theme, Router and VPN on
#   3-map.png     the network map in the Terminal theme, while listening
#   4-speed.png   a finished speed test in the Constellation theme
#
# They are drawn by the desktop simulator's snapshot mode, without a window,
# from made-up sample data only (invented names, private example addresses,
# made-up hardware addresses). No real network values are read, no settings
# file is read or written, and the made-up tools send nothing. The same
# simulator build gives the same files byte for byte.
#
# Build the simulator first (see docs/development.md):
#   cmake --preset darwin-arm64
#   cmake --build --preset darwin-arm64-dbg
# Another simulator binary can be named with LOKICTL_BIN=/path/to/lokictl.

set -euo pipefail

script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(CDPATH='' cd -- "${script_dir}/.." && pwd -P)"
binary="${LOKICTL_BIN:-${repo_root}/build/darwin-arm64/Debug/lokictl}"
out_dir="${repo_root}/screenshot/store"

log() {
  printf '==> %s\n' "$*" >&2
}

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

[ -x "${binary}" ] || fail "No simulator at ${binary}. Build it first: cmake --preset darwin-arm64 && cmake --build --preset darwin-arm64-dbg"

mkdir -p "${out_dir}"
cd "${repo_root}"

# shot <file> <snapshot options...>
shot() {
  local name="$1"
  shift
  "${binary}" --snapshot "${out_dir}/${name}" --data sample "$@" >/dev/null 2>&1 \
    || fail "Could not draw ${name}. Run the simulator by hand with the same options to see why."
  log "screenshot/store/${name}"
}

# The home page: the router with its access point and three clients.
shot 1-home.png --page home --theme constellation

# Modes: Router is on in the sample. Down twice to VPN, 6 turns it on (the
# pretend switch), and three further readings let the switch finish and the
# tunnel's first reply arrive.
shot 2-modes.png --page modes --theme instrument --keys down,down,6,data,data,data --settle-ms 3000

# The map: 6 starts the pretend listen, which hears its made-up devices
# over the next seconds.
shot 3-map.png --page map --theme terminal --keys 6 --settle-ms 12000

# Speed test: 6 starts the pretend run, which is done after 20 seconds.
shot 4-speed.png --page speed --theme constellation --keys 6 --settle-ms 20000

log "Store screenshots ready in screenshot/store"

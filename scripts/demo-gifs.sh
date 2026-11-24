#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
#
# Makes the short animated GIFs in docs/media/ that show LokiCTL's features
# in the README. Every frame is drawn by the desktop simulator's snapshot
# mode from made-up data only, then placed inside a picture of the
# CardputerZero.
#
#   scripts/demo-gifs.sh              makes every clip in scripts/demo-gifs/
#   scripts/demo-gifs.sh home ports   makes only these clips
#
# Needs the desktop simulator (see docs/development.md) and ffmpeg:
#   cmake --preset darwin-arm64
#   cmake --build --preset darwin-arm64-dbg
# Another simulator binary can be named with LOKICTL_BIN=/path/to/lokictl.
#
# A clip is a text file scripts/demo-gifs/<name>.clip and becomes
# docs/media/<name>.gif. Each line is one frame:
#
#   <milliseconds> <snapshot options>
#
# for example "250 --page home --data demo --data-steps 60". The frame is
# shown for that many milliseconds. A line with {A..B} in it stands for one
# frame per number from A to B, in order. Empty lines and lines starting
# with # are skipped. "lokictl --help" lists the snapshot options, and
# --keys takes the key names it lists (including "data", one more reading).
#
# A clip that needs a made-up test folder (see docs/data.md) has a
# scripts/demo-gifs/<name>.setup next to it. The script runs it with an empty
# folder to fill and points LOKICTL_DATA_ROOT at that folder for that clip.

set -euo pipefail

script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(CDPATH='' cd -- "${script_dir}/.." && pwd -P)"
binary="${LOKICTL_BIN:-${repo_root}/build/darwin-arm64/Debug/lokictl}"
clip_dir="${script_dir}/demo-gifs"
out_dir="${repo_root}/docs/media"

# The screen sits at (155, 21) in the 630 x 405 picture the simulator draws.
# Here the picture is drawn twice as large and cut to the top part, so the
# screen is 640 x 340 at (80, 42) inside an 800 x 400 GIF.
scale_w=1260
scale_h=810
crop="800:400:230:0"
screen_x=80
screen_y=42
warn_bytes=1500000

log() {
  printf '==> %s\n' "$*" >&2
}

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

[ -x "${binary}" ] || fail "No simulator at ${binary}. Build it first: cmake --preset darwin-arm64 && cmake --build --preset darwin-arm64-dbg"
command -v ffmpeg >/dev/null || fail "ffmpeg is needed (macOS: brew install ffmpeg)"

unset LOKICTL_DATA_ROOT
clip_root=""
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT
mkdir -p "${out_dir}"

shell="${work}/shell.png"
ffmpeg -v error -y -i "${repo_root}/assets/images/cp0-dark.png" \
  -vf "scale=${scale_w}:${scale_h}:flags=lanczos,crop=${crop}" "${shell}"

# One frame: snapshot, then put it into the picture of the device.
# $1 frame number, $2 milliseconds, $3 snapshot options (split on spaces).
add_frame() {
  local number="$1" ms="$2" options="$3"
  local raw="${work}/raw.png"
  local frame
  frame="$(printf '%s/frame-%04d.png' "${work}" "${number}")"
  # shellcheck disable=SC2086 # the options are split on purpose
  if [ -n "${clip_root}" ]; then
    LOKICTL_DATA_ROOT="${clip_root}" "${binary}" --snapshot "${raw}" ${options} >/dev/null 2>"${work}/snapshot.err" \
      || fail "Snapshot failed for '${options}': $(cat "${work}/snapshot.err")"
  else
    "${binary}" --snapshot "${raw}" ${options} >/dev/null 2>"${work}/snapshot.err" \
      || fail "Snapshot failed for '${options}': $(cat "${work}/snapshot.err")"
  fi
  ffmpeg -v error -y -i "${shell}" -i "${raw}" -filter_complex \
    "[1:v]scale=640:340:flags=neighbor[s];[0:v][s]overlay=${screen_x}:${screen_y}" \
    -frames:v 1 "${frame}"
  printf "file '%s'\nduration %s\n" "${frame}" "$(awk -v ms="${ms}" 'BEGIN { printf "%.3f", ms / 1000 }')" \
    >> "${work}/list.txt"
  last_frame="${frame}"
}

make_clip() {
  local name="$1"
  local clip="${clip_dir}/${name}.clip"
  [ -f "${clip}" ] || fail "No clip ${clip}"
  clip_root=""
  if [ -f "${clip_dir}/${name}.setup" ]; then
    clip_root="${work}/root-${name}"
    mkdir -p "${clip_root}"
    bash "${clip_dir}/${name}.setup" "${clip_root}" || fail "${name}.setup failed"
  fi
  : > "${work}/list.txt"
  rm -f "${work}"/frame-*.png
  local count=0 line ms options first last
  while IFS= read -r line || [ -n "${line}" ]; do
    case "${line}" in ''|'#'*) continue ;; esac
    read -r ms options <<< "${line}"
    [[ "${ms}" =~ ^[0-9]+$ ]] || fail "${clip}: a line must start with milliseconds: ${line}"
    if [[ "${options}" =~ \{([0-9]+)\.\.([0-9]+)\} ]]; then
      first="${BASH_REMATCH[1]}"
      last="${BASH_REMATCH[2]}"
      for ((n = first; n <= last; n++)); do
        count=$((count + 1))
        add_frame "${count}" "${ms}" "${options/\{${first}..${last}\}/${n}}"
      done
    else
      count=$((count + 1))
      add_frame "${count}" "${ms}" "${options}"
    fi
  done < "${clip}"
  [ "${count}" -gt 0 ] || fail "${clip} has no frames"
  # The concat reader ignores the last duration unless the file comes again.
  printf "file '%s'\n" "${last_frame}" >> "${work}/list.txt"

  local gif="${out_dir}/${name}.gif"
  ffmpeg -v error -y -f concat -safe 0 -i "${work}/list.txt" -filter_complex \
    "split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" \
    -fps_mode passthrough -loop 0 "${gif}"
  local bytes
  bytes="$(wc -c < "${gif}" | tr -d ' ')"
  log "${gif#"${repo_root}/"}: ${count} frames, ${bytes} bytes"
  [ "${bytes}" -le "${warn_bytes}" ] || log "warning: ${name}.gif is over ${warn_bytes} bytes; GitHub pages load it slowly"
}

if [ "$#" -gt 0 ]; then
  for name in "$@"; do make_clip "${name}"; done
else
  found=0
  for clip in "${clip_dir}"/*.clip; do
    [ -e "${clip}" ] || continue
    found=1
    name="$(basename "${clip}" .clip)"
    make_clip "${name}"
  done
  [ "${found}" -eq 1 ] || fail "No clips in ${clip_dir}"
fi

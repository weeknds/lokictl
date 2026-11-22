#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
#
# Installs, runs and tests LokiCTL on a real CardputerZero over SSH.
#
#   scripts/device.sh deploy        build the package if needed, copy it, install it
#   scripts/device.sh run           start LokiCTL on the device's screen
#   scripts/device.sh run --theme constellation
#                                   the same, in that theme for this run only
#   scripts/device.sh screen x.png  save what the screen shows
#   scripts/device.sh key 6         press keys on the device (6: next theme)
#   scripts/device.sh snapshot x.png --theme instrument --switch-to terminal
#                                   draw a page off screen, after a live switch
#   scripts/device.sh stats         memory, CPU and frame time
#   scripts/device.sh stop          stop LokiCTL and bring the launcher back
#
# "scripts/device.sh --help" lists every command and option.
#
# The device is chosen with two environment variables, never stored in the
# repository:
#   LOKICTL_DEVICE     <user>@<device-ip> (required)
#   LOKICTL_SSH_OPTS   extra ssh options, for example "-i <path-to-key>" (optional)
#
# Everything runs over plain ssh. The device side is scripts/device-agent.py,
# which is streamed to the device's python3 on each call; nothing else is
# installed there.

set -euo pipefail

script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(CDPATH='' cd -- "${script_dir}/.." && pwd -P)"
agent_script="${script_dir}/device-agent.py"
package_name_pattern='^[A-Za-z0-9._+-]+\.deb$'
# The package is copied to this name in the device user's home folder, so it
# never replaces a file of the user's; the device side deletes it afterwards.
upload_name="lokictl_upload.deb"
png_signature="89504e470d0a1a0a"

device=""
ssh_opts=()
cleanup_file=""

usage() {
  cat <<'EOF'
Usage: scripts/device.sh <command> [options]

Install, run and test LokiCTL on a CardputerZero over SSH.

Commands:
  deploy [--no-build] [--with-deps] [<file.deb>]
                  Build the package if needed (scripts/build-deb.sh), copy it
                  to the device and install it. --no-build uses the newest
                  dist/lokictl_*_arm64.deb. --with-deps allows apt to install
                  missing libraries as well (by default it must install
                  nothing but LokiCTL).
  remove          Stop LokiCTL and uninstall it.
  run [--theme T] Start LokiCTL on the device's screen. The launcher is
                  paused while it runs and comes back when LokiCTL stops.
                  --theme uses theme T (instrument, constellation or
                  terminal) for this run only and does not save it; without
                  it, LokiCTL's saved theme or its default decides. An
                  unknown name makes LokiCTL exit at once with a message.
                  Pressing 6 on the home page switches the theme and saves
                  the new choice, also after a --theme start.
  stop            Stop LokiCTL (however it was started) and bring the
                  launcher back on HOME. Safe to repeat.
  status          Show the installed version and what is running.
  screen <out.png> [--no-wait]
                  Save the device's screen (320x170) as a PNG. It waits for
                  the picture to stop changing, unless --no-wait.
  key <name>... [--long | --hold-ms N] [--launcher]
                  Press keys, one after another. --long holds each for 1 s,
                  --hold-ms holds for 20 to 2500 ms (default 60). Keys are
                  only sent while LokiCTL runs, unless --launcher.
                  Names: 0-9, a-z, esc, enter, tab, space, backspace, up,
                  down, left, right, help, screenshot, and fn+h (help),
                  fn+j (screenshot), fn+f (up), fn+x (down), fn+z (left),
                  fn+c (right).
  stats [--seconds N]
                  LokiCTL's memory and CPU use over N seconds (default 3,
                  1 to 30), free memory and frame time.
  snapshot <out.png> [--page P] [--theme T] [--switch-to T[,T...]]
           [--settle-ms N]
                  Run "lokictl --snapshot" on the device and save the PNG.
                  Without --theme the picture uses the default theme; the
                  settings file is not read or written. --switch-to
                  switches live through the listed themes, the way key 6
                  does, before the picture is taken; the "fonts loaded"
                  lines it prints appear on standard error.
  logs [-n N]     Show the last N log lines (default 50) of the latest
                  'run' only, also after LokiCTL has stopped.

Environment:
  LOKICTL_DEVICE    <user>@<device-ip> of the device (required).
  LOKICTL_SSH_OPTS  Extra ssh options, split on spaces, for example
                    "-i <path-to-key>" (optional).

Exit codes:
  0  done
  1  failed or refused (the message says why)
  2  usage error or LOKICTL_DEVICE missing
  3  device unreachable
EOF
}

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

usage_error() {
  printf '%s\n' "$*" >&2
  printf "Run 'scripts/device.sh --help' to see the commands.\n" >&2
  exit 2
}

log() {
  printf '==> %s\n' "$*" >&2
}

on_exit() {
  if [ -n "${cleanup_file}" ]; then
    rm -f "${cleanup_file}"
  fi
}
trap on_exit EXIT

require_device() {
  device="${LOKICTL_DEVICE:-}"
  if [ -z "${device}" ]; then
    printf 'Set LOKICTL_DEVICE to the device, for example:\n' >&2
    printf '  export LOKICTL_DEVICE=<user>@<device-ip>\n' >&2
    printf '  export LOKICTL_SSH_OPTS="-i <path-to-key>"   # optional\n' >&2
    exit 2
  fi
  case "${device}" in
    -* | *[[:space:]]*)
      printf 'LOKICTL_DEVICE must look like <user>@<device-ip>.\n' >&2
      exit 2
      ;;
  esac
  ssh_opts=()
  if [ -n "${LOKICTL_SSH_OPTS:-}" ]; then
    read -r -a ssh_opts <<< "${LOKICTL_SSH_OPTS}"
  fi
}

# remote <command>: runs one shell command on the device, exactly one attempt.
# The caller decides what ssh reads from stdin.
remote() {
  local status=0
  # The user's options come first: ssh keeps the first value of each option.
  ssh ${ssh_opts[@]+"${ssh_opts[@]}"} -o BatchMode=yes -o ConnectTimeout=10 \
    "${device}" "$1" || status=$?
  if [ "${status}" -eq 255 ]; then
    printf 'Device unreachable: is it switched on, and are LOKICTL_DEVICE / LOKICTL_SSH_OPTS right?\n' >&2
    exit 3
  fi
  return "${status}"
}

# agent <args...>: runs device-agent.py on the device with these arguments.
agent() {
  local command="python3 -" arg
  for arg in "$@"; do
    command="${command} $(printf '%q' "${arg}")"
  done
  remote "${command}" < "${agent_script}"
}

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum < "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 < "$1" | awk '{print $1}'
  fi
}

wants_help() {
  local arg
  for arg in "$@"; do
    case "${arg}" in
      -h | --help) return 0 ;;
    esac
  done
  return 1
}

cmd_deploy() {
  local build=1 package="" with_deps=()
  while [ $# -gt 0 ]; do
    case "$1" in
      --no-build) build=0 ;;
      --with-deps) with_deps=(--with-deps) ;;
      -*) usage_error "deploy: unknown option $1" ;;
      *)
        [ -z "${package}" ] || usage_error "deploy: give at most one package file"
        package="$1"
        ;;
    esac
    shift
  done
  require_device

  local started="${SECONDS}"
  if [ -n "${package}" ]; then
    [ -f "${package}" ] || fail "Package not found: ${package}"
  elif [ "${build}" -eq 0 ]; then
    package="$(ls -t "${repo_root}"/dist/lokictl_*_arm64.deb 2>/dev/null | head -n 1 || true)"
    [ -n "${package}" ] || fail "No dist/lokictl_*_arm64.deb found. Run without --no-build to build one."
  else
    log "Building the package (scripts/build-deb.sh)"
    package="$("${repo_root}/scripts/build-deb.sh")" || fail "Building the package failed (see above)."
  fi

  local name size checksum
  name="$(basename "${package}")"
  [[ "${name}" =~ ${package_name_pattern} ]] || fail "Unexpected package file name: ${name}"
  size="$(wc -c < "${package}" | tr -d ' ')"
  checksum="$(sha256_of "${package}")"

  log "Copying ${name} (${size} bytes) to the device"
  remote "cat > $(printf '%q' "${upload_name}")" < "${package}" || fail "Copying the package to the device failed."

  log "Installing on the device"
  local status=0
  if [ -n "${checksum}" ]; then
    agent install "${upload_name}" --label "${name}" --sha256 "${checksum}" \
      ${with_deps[@]+"${with_deps[@]}"} || status=$?
  else
    agent install "${upload_name}" --label "${name}" ${with_deps[@]+"${with_deps[@]}"} || status=$?
  fi
  if [ "${status}" -eq 0 ]; then
    log "Deployed in $((SECONDS - started)) s"
  fi
  return "${status}"
}

# capture <out.png> <agent args...>: saves the agent's PNG output safely.
capture() {
  local out="$1"
  shift
  local dir base
  dir="$(dirname -- "${out}")"
  base="$(basename -- "${out}")"
  mkdir -p -- "${dir}" || fail "Cannot create the folder ${dir}."
  cleanup_file="$(mktemp "${dir}/.${base}.XXXXXX")" || fail "Cannot write in ${dir}."

  local status=0
  agent "$@" > "${cleanup_file}" || status=$?
  if [ "${status}" -ne 0 ]; then
    return "${status}"
  fi
  local signature
  signature="$(head -c 8 "${cleanup_file}" | od -An -tx1 | tr -d ' \n')"
  if [ "${signature}" != "${png_signature}" ]; then
    printf 'The device did not send a PNG picture.\n' >&2
    return 1
  fi
  # mktemp makes the file private (0600); give it the usual permissions.
  chmod "$(printf '%o' $((0666 & ~0$(umask))))" "${cleanup_file}" || true
  mv -f -- "${cleanup_file}" "${out}" || fail "Cannot write ${out}."
  cleanup_file=""
  printf 'Saved %s\n' "${out}" >&2
}

cmd_capture() {
  local command="$1"
  shift
  if [ $# -eq 0 ] || [ -z "$1" ]; then
    usage_error "${command}: give the PNG file to write, for example: scripts/device.sh ${command} build/shots/screen.png"
  fi
  case "$1" in
    -*) usage_error "${command}: the first argument must be the PNG file to write" ;;
  esac
  local out="$1"
  shift
  require_device
  capture "${out}" "${command}" "$@"
}

main() {
  if [ $# -eq 0 ]; then
    usage >&2
    exit 2
  fi
  local command="$1"
  shift
  case "${command}" in
    -h | --help | help)
      usage
      exit 0
      ;;
  esac
  if wants_help "$@"; then
    usage
    exit 0
  fi

  # Called without "||" on purpose, so "set -e" stays active inside them. A
  # failing step ends the script with that step's exit code.
  case "${command}" in
    deploy)
      cmd_deploy "$@"
      ;;
    screen | snapshot)
      cmd_capture "${command}" "$@"
      ;;
    remove | run | stop | status | key | stats | logs)
      require_device
      agent "${command}" "$@"
      ;;
    *)
      printf 'Unknown command: %s\n\n' "${command}" >&2
      usage >&2
      exit 2
      ;;
  esac
  exit 0
}

main "$@"

#!/usr/bin/env bash
# Builds the device package and copies it to a CardputerZero.
#
# Two ways to build the package:
#   ./deploy.sh
#       Default. Runs scripts/build-deb.sh, which builds the package in a
#       Debian arm64 container. Needs a running Docker engine (for example
#       colima on macOS).
#   WORKFLOW_PRESET=cp0-cross-package ./deploy.sh
#       Runs that CMake workflow on this computer instead, for Linux computers
#       with an arm64 cross compiler.
#
# Set REMOTE_USER and REMOTE_HOST to copy the package to the device:
#   REMOTE_USER=<user> REMOTE_HOST=<device-ip> ./deploy.sh
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

WORKFLOW_PRESET="${WORKFLOW_PRESET:-}"
REMOTE_USER="${REMOTE_USER:-}"
REMOTE_HOST="${REMOTE_HOST:-}"
REMOTE_DIR="${REMOTE_DIR:-/home/${REMOTE_USER}}"
PACKAGE_GLOB="${PACKAGE_GLOB:-dist/*_arm64.deb}"

log() {
  printf '\n==> %s\n' "$*"
}

if [[ -n "${WORKFLOW_PRESET}" ]]; then
  log "Workflow: cmake --workflow --preset ${WORKFLOW_PRESET}"
  cmake --workflow --preset "${WORKFLOW_PRESET}"

  shopt -s nullglob
  packages=( ${PACKAGE_GLOB} )
  shopt -u nullglob

  if (( ${#packages[@]} == 0 )); then
    printf 'No package matched: %s\n' "${PACKAGE_GLOB}" >&2
    exit 1
  fi

  latest_package="$(ls -t "${packages[@]}" | head -n 1)"
else
  log "Build: scripts/build-deb.sh"
  latest_package="$(./scripts/build-deb.sh)"
fi

if [[ -z "${REMOTE_USER}" || -z "${REMOTE_HOST}" ]]; then
  printf 'Package ready: %s\n' "${latest_package}" >&2
  printf 'Set REMOTE_USER and REMOTE_HOST to deploy, for example:\n' >&2
  printf '  REMOTE_USER=<user> REMOTE_HOST=<device-ip> ./deploy.sh\n' >&2
  exit 1
fi

remote_target="${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_DIR}"

log "Deploy: scp ${latest_package} ${remote_target}"
scp "${latest_package}" "${remote_target}"

log "Done: ${latest_package} -> ${remote_target}"

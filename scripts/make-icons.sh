#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
#
# Makes the app icons from the drawing assets/images/lokictl.svg:
#   assets/images/lokictl.png       512 px, the store icon (app-builder.json)
#   assets/images/lokictl_100.png   100 px, the launcher icon (the .desktop entry)
#   assets/images/lokictl_80.png     80 px, the small launcher size
# Each size is drawn straight from the SVG, not shrunk from a bigger picture,
# so the small ones stay sharp.
#
# The drawing runs with rsvg-convert in a throwaway Debian 13 (trixie) arm64
# container, so nothing is installed on this computer. All you need is a
# running Docker engine (on macOS for example colima: colima start). The same
# drawing tool version gives the same files byte for byte.

set -euo pipefail

icon_name="lokictl"
image="debian:trixie"

script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(CDPATH='' cd -- "${script_dir}/.." && pwd -P)"
images_dir="${repo_root}/assets/images"

log() {
  printf '==> %s\n' "$*" >&2
}

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

[ -f "${images_dir}/${icon_name}.svg" ] || fail "Missing assets/images/${icon_name}.svg."

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  fail "Docker is not reachable. Start it first (with colima: colima start), then run this script again."
fi

log "Drawing the icons in a throwaway ${image} container"
docker run --rm --init --platform linux/arm64 \
  --env ICON_NAME="${icon_name}" --env OWNER="$(id -u):$(id -g)" \
  --volume "${images_dir}:/images" --workdir /images \
  "${image}" bash -ec '
    if [ ! -f "${ICON_NAME}.svg" ]; then
      echo "The images folder is not visible inside the container. With colima, keep the repository inside your home folder." >&2
      exit 1
    fi
    apt-get update -qq >/dev/null
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends librsvg2-bin >/dev/null
    rsvg-convert --version >&2
    render() {
      rsvg-convert --width "$1" --height "$1" --output "$2" "${ICON_NAME}.svg"
      chown "${OWNER}" "$2"
      echo "    $2 ($1 x $1)" >&2
    }
    render 512 "${ICON_NAME}.png"
    render 100 "${ICON_NAME}_100.png"
    render 80 "${ICON_NAME}_80.png"
  '

log "Icons ready in assets/images"

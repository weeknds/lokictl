#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 LokiCTL contributors
# SPDX-License-Identifier: MIT
#
# Builds the CardputerZero Debian package (dist/<name>_<version>-<revision>_arm64.deb)
# inside a Debian 13 (trixie) arm64 container, the same system the device runs.
# All you need is a running Docker engine (on macOS for example colima; the
# README explains how to start it). The build image is created on the first run.
#
# Progress goes to stderr. The only line on stdout is the absolute path of the
# finished package, so other scripts can use: package="$(scripts/build-deb.sh)"

set -euo pipefail

image_tag="lokictl-build:trixie"
# Docker label holding the hash of the Dockerfile the image was built from.
image_label="${image_tag%%:*}.dockerfile.sha256"
# The repository is always mounted at this fixed path. Paths from the build
# end up inside the program, so the host path (with your home folder name)
# must never be used here.
container_dir="/work"

script_dir="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(CDPATH='' cd -- "${script_dir}/.." && pwd -P)"
dockerfile="${script_dir}/build-deb.Dockerfile"
build_dir_name="build/cp0-native"
deps_dir_name=".cache/cp0-deps"

log() {
  printf '==> %s\n' "$*" >&2
}

fail() {
  printf '%s\n' "$*" >&2
  exit 1
}

# 1. Docker must already be running. This script never starts a VM.
if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  fail "Docker is not reachable. Start it first (with colima: colima start), then run this script again."
fi

# 2. Build folders made outside the container cannot be reused: CMake refuses a
#    build folder that was configured from another path, and the LVGL folders
#    in .cache/cp0-deps are shared by every device build of this checkout.
check_build_folder() {
  local folder="$1" file="$2" pattern="$3" expected="$4"
  local found
  if [ -f "${repo_root}/${folder}/${file}" ]; then
    found="$(sed -n "s|${pattern}||p" "${repo_root}/${folder}/${file}" | head -n 1)"
    case "${found}" in
      ${expected}) ;;
      *) fail "${folder} was created by a build outside the container. Delete that folder and run this script again." ;;
    esac
  fi
}
check_build_folder "${build_dir_name}" "CMakeCache.txt" \
  '^CMAKE_HOME_DIRECTORY:INTERNAL=' "${container_dir}"
check_build_folder "${deps_dir_name}" "lvgl-build/cmake_install.cmake" \
  '^# Install script for directory: ' "${container_dir}/${deps_dir_name}/*"

# 3. Build image: create it when it is missing or the Dockerfile changed.
if command -v sha256sum >/dev/null 2>&1; then
  wanted_hash="$(sha256sum < "${dockerfile}" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  wanted_hash="$(shasum -a 256 < "${dockerfile}" | awk '{print $1}')"
else
  fail "Neither sha256sum nor shasum was found; install one of them (coreutils or perl)."
fi
current_hash="$(docker image inspect --format "{{ index .Config.Labels \"${image_label}\" }}" "${image_tag}" 2>/dev/null || true)"

if [ "${current_hash}" != "${wanted_hash}" ]; then
  log "Creating the build image ${image_tag} (Debian 13 arm64, about half a minute)"
  # The Dockerfile is read from stdin, so no other files are sent to Docker.
  docker build --platform linux/arm64 \
    --label "${image_label}=${wanted_hash}" \
    --tag "${image_tag}" \
    - < "${dockerfile}" >&2
else
  log "Using the build image ${image_tag}"
fi

# 4. Configure, build and package inside the container, as the current user so
#    every file it writes in the repository belongs to you.
log "Building the package in the container (cmake --workflow --preset cp0-native-package)"
docker run --rm --init --platform linux/arm64 \
  --user "$(id -u):$(id -g)" --env HOME=/tmp \
  --volume "${repo_root}:${container_dir}" --workdir "${container_dir}" \
  "${image_tag}" bash -ec '
    if [ ! -f CMakePresets.json ]; then
      echo "The repository is not visible inside the container. With colima, keep it inside your home folder." >&2
      exit 1
    fi
    arch="$(dpkg --print-architecture)"
    if [ "${arch}" != "arm64" ]; then
      echo "The build container runs on ${arch}, but the device package needs arm64." >&2
      exit 1
    fi
    exec cmake --workflow --preset cp0-native-package
  ' >&2

# 5. Report where the package is.
cpack_config="${repo_root}/${build_dir_name}/CPackConfig.cmake"
package_name=""
if [ -f "${cpack_config}" ]; then
  package_name="$(sed -n 's/^set(CPACK_PACKAGE_FILE_NAME "\(.*\)")$/\1/p' "${cpack_config}" | head -n 1)"
fi
if [ -z "${package_name}" ]; then
  fail "Could not read the package name from ${build_dir_name}/CPackConfig.cmake."
fi
package_path="${repo_root}/dist/${package_name}.deb"
if [ ! -f "${package_path}" ]; then
  fail "The build finished, but dist/${package_name}.deb was not found."
fi

log "Package ready"
printf '%s\n' "${package_path}"

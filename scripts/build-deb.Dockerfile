# SPDX-FileCopyrightText: 2026 Netdeck contributors
# SPDX-License-Identifier: MIT
#
# Build environment for the device package, used by scripts/build-deb.sh.
# It uses the same Debian release (13, trixie) and architecture (arm64) as the
# CardputerZero system image, so the package links against matching libraries.

FROM debian:trixie

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        dpkg-dev \
        file \
        git \
        ninja-build \
        pkg-config \
        libfmt-dev \
        libfreetype-dev \
        libjpeg-dev \
        libpng-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

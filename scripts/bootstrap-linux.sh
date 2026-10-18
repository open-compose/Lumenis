#!/usr/bin/env bash
# Install build dependencies on Debian/Ubuntu (24.04+) or Fedora (40+).
# The ffmpeg command (not only its libraries) is for the decode corpus and
# the playback measurement (BUILDING.md, Phase 2 gates).
set -euo pipefail

sudo_cmd=""
if [[ $(id -u) -ne 0 ]]; then sudo_cmd="sudo"; fi

if command -v apt-get >/dev/null; then
    $sudo_cmd apt-get update
    $sudo_cmd apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config curl file \
        qt6-base-dev qt6-base-dev-tools qt6-wayland libgl-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
        ffmpeg dpkg-dev rpm
elif command -v dnf >/dev/null; then
    $sudo_cmd dnf install -y \
        gcc-c++ cmake ninja-build pkgconf curl file \
        qt6-qtbase-devel qt6-qtwayland mesa-libGL-devel \
        ffmpeg-free-devel ffmpeg-free \
        rpm-build
else
    echo "Unsupported distribution: install a C++20 compiler, CMake 3.24+, Ninja and Qt 6.4+ manually." >&2
    exit 1
fi

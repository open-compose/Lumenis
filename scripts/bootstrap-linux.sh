#!/usr/bin/env bash
# Install build dependencies on Debian/Ubuntu (24.04+) or Fedora (40+).
# The ffmpeg command (not only its libraries) is for the decode corpus and
# the playback measurement (BUILDING.md, Phase 2 gates); venv and Python's
# headers for the interchange checks' packages (scripts/check-python.py); Qt's Linguist tools (qt6-l10n-tools,
# qt6-tools-dev) for the interface's translations (BUILDING.md, Translations).
set -euo pipefail

sudo_cmd=""
if [[ $(id -u) -ne 0 ]]; then sudo_cmd="sudo"; fi

if command -v apt-get >/dev/null; then
    $sudo_cmd apt-get update
    $sudo_cmd apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config curl file \
        qt6-base-dev qt6-base-dev-tools qt6-wayland qt6-l10n-tools qt6-tools-dev libgl-dev \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
        ffmpeg dpkg-dev rpm python3-venv python3-dev
elif command -v dnf >/dev/null; then
    $sudo_cmd dnf install -y \
        gcc-c++ cmake ninja-build pkgconf curl file \
        qt6-qtbase-devel qt6-qtwayland qt6-qttools-devel qt6-linguist mesa-libGL-devel \
        ffmpeg-free-devel ffmpeg-free \
        rpm-build python3-devel
else
    echo "Unsupported distribution: install a C++20 compiler, CMake 3.24+, Ninja and Qt 6.4+ manually." >&2
    exit 1
fi

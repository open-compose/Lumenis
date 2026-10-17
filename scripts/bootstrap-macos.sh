#!/usr/bin/env bash
# Install build dependencies on macOS (Apple Silicon) with Homebrew, and Qt
# 6.8 LTS with aqtinstall into ~/Qt. Xcode or the Command Line Tools must be
# installed first: xcode-select --install
set -euo pipefail

qt_version="${QT_VERSION:-6.8.3}"

if ! command -v brew >/dev/null; then
    echo "Homebrew is required: https://brew.sh" >&2
    exit 1
fi
brew install cmake ninja python@3.12 pkg-config ffmpeg

python3 -m pip install --user --upgrade aqtinstall
python3 -m aqt install-qt mac desktop "$qt_version" clang_64 -O "$HOME/Qt"

echo
echo "Qt installed. Before building:"
echo "  export QT_ROOT_DIR=\"$HOME/Qt/$qt_version/macos\""

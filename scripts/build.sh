#!/usr/bin/env bash
# Build, test and package on macOS or Linux.
#
#   scripts/build.sh [--debug] [--qt <qt-prefix>] [--no-tests] [--package] [--clean]
#
# Qt is found from, in order: --qt, $QT_ROOT_DIR, then the system default.
# The build goes to build/<preset>.
set -euo pipefail

cd "$(dirname "$0")/.."

config=release
run_tests=1
package=0
clean=0
qt_dir="${QT_ROOT_DIR:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) config=debug ;;
        --release) config=release ;;
        --qt) qt_dir="$2"; shift ;;
        --no-tests) run_tests=0 ;;
        --package) package=1 ;;
        --clean) clean=1 ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

preset="$config"
if [[ "$(uname -s)" == "Darwin" ]]; then
    preset="macos-$config"
fi

export QT_ROOT_DIR="$qt_dir"

if [[ $clean -eq 1 ]]; then
    rm -rf "build/$preset"
fi

cmake --preset "$preset"
cmake --build --preset "$preset"

if [[ $run_tests -eq 1 ]]; then
    ctest --preset "$preset"
fi

if [[ $package -eq 1 ]]; then
    (cd "build/$preset" && cpack)
    if [[ "$(uname -s)" == "Linux" ]]; then
        packaging/linux/make-appimage.sh "build/$preset"
    fi
    echo "Packages are in build/$preset"
fi

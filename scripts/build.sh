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

# A damaged or half-updated Xcode / Command Line Tools install is the most
# common macOS build failure ("'cstdint' file not found"); catch it up front.
if [[ "$(uname -s)" == "Darwin" ]]; then
    if ! printf '#include <cstdint>\nint main() { return 0; }\n' \
            | xcrun clang++ -std=c++20 -x c++ - -o /dev/null 2>/dev/null; then
        cat >&2 <<'MSG'
error: the C++ compiler cannot find the standard library headers.
Your Xcode or Command Line Tools installation is incomplete. Fix it with ONE of:
  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer   (Xcode installed)
  sudo rm -rf /Library/Developer/CommandLineTools && xcode-select --install
then delete build/ and run this script again. See BUILDING.md, "Troubleshooting".
MSG
        exit 1
    fi
fi

if [[ $clean -eq 1 ]]; then
    rm -rf "build/$preset"
fi

cmake --preset "$preset"
cmake --build --preset "$preset"

if [[ $run_tests -eq 1 ]]; then
    ctest --preset "$preset"
fi

echo
exe_name="$(sed -n 's/^PRODUCT_EXE_NAME:STRING=//p' "build/$preset/CMakeCache.txt")"
echo "Program: $(ls -d "build/$preset"/bin/*.app 2>/dev/null || echo "build/$preset/bin/$exe_name")"

if [[ $package -eq 1 ]]; then
    (cd "build/$preset" && cpack)
    if [[ "$(uname -s)" == "Linux" ]]; then
        packaging/linux/make-appimage.sh "build/$preset"
    fi
    echo "Packages:"
    ls -1 "build/$preset"/*.{dmg,deb,rpm,tar.gz,AppImage} 2>/dev/null | sed 's/^/  /'
else
    echo "Installers were not built; add --package to make them."
fi

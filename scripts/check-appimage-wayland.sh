#!/usr/bin/env bash
# Run an AppImage on a headless Wayland compositor (weston) and check that it
# opens its window natively, through its own xdg-shell plugin, with no X11 to
# fall back to. Used by CI; see BUILDING.md ("No shell integration named
# xdg-shell found").
#
#   scripts/check-appimage-wayland.sh <AppImage>
set -euo pipefail

appimage="$(readlink -f "$1")"
runtime="$(mktemp -d)"
chmod 700 "$runtime"
export XDG_RUNTIME_DIR="$runtime"
weston --backend=headless --socket=cutthulhu-check --width=1280 --height=800 >"$runtime/weston.log" 2>&1 &
weston_pid=$!
trap 'kill "$weston_pid" 2>/dev/null || true; rm -rf "$runtime"' EXIT
for _ in $(seq 50); do
    [[ -S "$runtime/cutthulhu-check" ]] && break
    sleep 0.2
done
[[ -S "$runtime/cutthulhu-check" ]] || { cat "$runtime/weston.log"; echo "weston did not start" >&2; exit 1; }

log="$runtime/app.log"
env -u DISPLAY -u QT_QPA_PLATFORM WAYLAND_DISPLAY=cutthulhu-check APPIMAGE_EXTRACT_AND_RUN=1 \
    QT_LOGGING_RULES="qt.qpa.wayland=true" "$appimage" --smoke-test >"$log" 2>&1 || true
cat "$log"
grep -q "smoke-test: ok" "$log" || { echo "check-appimage-wayland: the smoke test failed" >&2; exit 1; }
grep -q "Using the 'xdg-shell' shell integration" "$log" \
    || { echo "check-appimage-wayland: not running on Wayland through xdg-shell" >&2; exit 1; }
echo "check-appimage-wayland: ok"

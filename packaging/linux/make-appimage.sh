#!/usr/bin/env bash
# Build a self-contained AppImage from a configured and built tree.
#
#   packaging/linux/make-appimage.sh <build-dir>
#
# Downloads pinned linuxdeploy tools on first use into a per-user cache
# ($LUMENIS_APPIMAGE_TOOLS, else ${XDG_CACHE_HOME:-~/.cache}/cutthulhu/
# appimage-tools/<tags>), so fresh build folders and CI runs don't depend on
# GitHub's release downloads each time (a 504 there once failed CI;
# BUILDING.md). Qt is taken from $QT_ROOT_DIR if set (qmake6 on PATH otherwise).
set -euo pipefail

build_dir="$(cd "$1" && pwd)"
appdir="$build_dir/AppDir"

linuxdeploy_tag="1-alpha-20250213-2"
qt_plugin_tag="1-alpha-20250213-1"
arch="x86_64"
tools="${LUMENIS_APPIMAGE_TOOLS:-${XDG_CACHE_HOME:-$HOME/.cache}/cutthulhu/appimage-tools/$linuxdeploy_tag-$qt_plugin_tag}"

mkdir -p "$tools"
fetch() {
    local url="$1" out="$2"
    if [[ ! -x "$out" ]]; then
        # Into a temporary file first, so a broken download is never taken for
        # the tool; retry server errors (5xx) too, with a growing delay.
        curl -fsSL --retry 6 --retry-all-errors --retry-delay 5 -o "$out.part" "$url"
        chmod +x "$out.part"
        mv -f "$out.part" "$out"
    fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/$linuxdeploy_tag/linuxdeploy-$arch.AppImage" \
      "$tools/linuxdeploy"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/$qt_plugin_tag/linuxdeploy-plugin-qt-$arch.AppImage" \
      "$tools/linuxdeploy-plugin-qt"

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr

if [[ -n "${QT_ROOT_DIR:-}" ]]; then
    export QMAKE="$QT_ROOT_DIR/bin/qmake"
    export LD_LIBRARY_PATH="$QT_ROOT_DIR/lib:${LD_LIBRARY_PATH:-}"
elif command -v qmake6 >/dev/null; then
    export QMAKE="$(command -v qmake6)"
fi

# xcb is always bundled. Add Wayland for modern desktops and offscreen for
# headless smoke tests, when this Qt has them.
plugin_dir="$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms"
extra=()
# Qt 6.10 replaced libqwayland-egl/-generic with one libqwayland.so.
for p in libqwayland.so libqwayland-egl.so libqwayland-generic.so libqoffscreen.so; do
    [[ -f "$plugin_dir/$p" ]] && extra+=("$p")
done
export EXTRA_PLATFORM_PLUGINS="$(IFS=';'; echo "${extra[*]}")"

desktop="$(ls "$appdir"/usr/share/applications/*.desktop | head -n1)"
# The tools are AppImages themselves; CI containers often lack FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1
(cd "$build_dir" && PATH="$tools:$PATH" "$tools/linuxdeploy" \
    --appdir "$appdir" --desktop-file "$desktop" --plugin qt)

# The Qt plugin bundles the Wayland platform plugin but not the plugins its
# windows need: the shell integrations (xdg-shell), window decorations and
# graphics integrations. Without them Qt finds the platform plugin and then
# fails ("No shell integration named xdg-shell found"; BUILDING.md). Bundle
# them, with what they link to.
qt_plugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
deps_only=()
if compgen -G "$appdir/usr/plugins/platforms/libqwayland*.so" >/dev/null; then
    for d in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
        if [[ -d "$qt_plugins/$d" ]]; then
            mkdir -p "$appdir/usr/plugins/$d"
            cp -a "$qt_plugins/$d/." "$appdir/usr/plugins/$d/"
            deps_only+=(--deploy-deps-only "$appdir/usr/plugins/$d")
        fi
    done
    if [[ ! -f "$appdir/usr/plugins/wayland-shell-integration/libxdg-shell.so" ]]; then
        echo "make-appimage: Qt's xdg-shell plugin is missing ($qt_plugins/wayland-shell-integration);" \
             "install qt6-wayland" >&2
        exit 1
    fi
fi

# The update check's HTTPS (docs/spec/update-check.md) goes through Qt's TLS backends, which Qt loads as plugins
# (the OpenSSL one opens the system's libssl at run time). Bundle them, and stop if there are none: the check would
# only ever say "No secure connection is available".
if [[ -d "$qt_plugins/tls" ]]; then
    mkdir -p "$appdir/usr/plugins/tls"
    cp -a "$qt_plugins/tls/." "$appdir/usr/plugins/tls/"
    deps_only+=(--deploy-deps-only "$appdir/usr/plugins/tls")
fi
if ! compgen -G "$appdir/usr/plugins/tls/*.so" >/dev/null; then
    echo "make-appimage: no Qt TLS plugin in $qt_plugins/tls (BUILDING.md \"Update check\")" >&2
    exit 1
fi

(cd "$build_dir" && PATH="$tools:$PATH" "$tools/linuxdeploy" \
    --appdir "$appdir" "${deps_only[@]}" --output appimage)
echo "AppImage written to $build_dir"

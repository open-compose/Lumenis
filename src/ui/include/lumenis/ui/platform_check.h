#pragma once

#include <QString>

namespace lumenis::ui {

// Linux: Qt's Wayland support comes in parts that distributions package
// separately. The platform plugin (platforms/libqwayland*.so) is not enough:
// windows also need a shell integration (wayland-shell-integration/
// libxdg-shell.so). With the plugin but no shell, Qt prints a page of
// warnings before falling back to X11 (see BUILDING.md).
struct WaylandSupport {
    bool platformPlugin = false;
    bool shellIntegration = false;
    QString loadError; // a part that is there but does not load
    bool bundled = false; // the plugins came with the program (an AppImage)
    bool complete() const { return platformPlugin && shellIntegration; }
};

// What is installed in a Qt plugins folder. With `tryLoad`, the parts are
// also loaded (not started): a plugin from a different build of Qt than the
// libraries it needs is there but unusable, as happens when the package
// manager holds back part of an update.
WaylandSupport waylandSupport(const QString& pluginsDir, bool tryLoad = false);

// The platform to ask Qt for before the application starts: "xcb" when this
// is a Wayland session whose Wayland support is incomplete and X11
// (XWayland) is there; otherwise empty, leaving Qt's own choice. A platform
// the user set (QT_QPA_PLATFORM) is never overridden. `why` says what is
// wrong and what to do whenever Wayland support is incomplete in a Wayland session, also
// when there is no X11 to fall back to.
struct PlatformEnvironment {
    bool platformSet = false; // QT_QPA_PLATFORM
    bool wayland = false;     // WAYLAND_DISPLAY, or XDG_SESSION_TYPE=wayland
    bool x11 = false;         // DISPLAY
};
QString fallbackPlatform(const PlatformEnvironment& env, const WaylandSupport& support, QString* why = nullptr);

// The Qt plugins folder this process will use. Before the application object
// exists Qt has not read the qt.conf next to the executable, so this reads it
// as Qt will: an AppImage carries its own plugins (usr/plugins), not the
// system's. `executable` is this program's path.
QString qtPluginsDir(const QString& executable);

// The two together, from this process's environment and Qt's plugins folder.
// Call before creating the QApplication.
QString fallbackPlatformForThisSystem(QString* why = nullptr);

} // namespace lumenis::ui

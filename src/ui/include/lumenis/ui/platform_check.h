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
    bool complete() const { return platformPlugin && shellIntegration; }
};

// What is installed in a Qt plugins folder.
WaylandSupport waylandSupport(const QString& pluginsDir);

// The platform to ask Qt for before the application starts: "xcb" when this
// is a Wayland session whose Wayland support is incomplete and X11
// (XWayland) is there; otherwise empty, leaving Qt's own choice. A platform
// the user set (QT_QPA_PLATFORM) is never overridden. `why` says what is
// missing whenever Wayland support is incomplete in a Wayland session, also
// when there is no X11 to fall back to.
struct PlatformEnvironment {
    bool platformSet = false; // QT_QPA_PLATFORM
    bool wayland = false;     // WAYLAND_DISPLAY, or XDG_SESSION_TYPE=wayland
    bool x11 = false;         // DISPLAY
};
QString fallbackPlatform(const PlatformEnvironment& env, const WaylandSupport& support, QString* why = nullptr);

// The two together, from this process's environment and Qt's plugins folder.
// Call before creating the QApplication.
QString fallbackPlatformForThisSystem(QString* why = nullptr);

} // namespace lumenis::ui

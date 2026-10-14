#include "lumenis/ui/platform_check.h"

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>

namespace lumenis::ui {

WaylandSupport waylandSupport(const QString& pluginsDir) {
    WaylandSupport s;
    // libqwayland-egl/-generic.so up to Qt 6.9, libqwayland.so from 6.10.
    s.platformPlugin = !QDir(pluginsDir + QStringLiteral("/platforms"))
                            .entryList({QStringLiteral("libqwayland*.so")}, QDir::Files)
                            .isEmpty();
    s.shellIntegration =
        QFileInfo::exists(pluginsDir + QStringLiteral("/wayland-shell-integration/libxdg-shell.so"));
    return s;
}

QString fallbackPlatform(const PlatformEnvironment& env, const WaylandSupport& support, QString* why) {
    if (env.platformSet || !env.wayland || !env.x11 || support.complete()) return {};
    if (why) {
        *why = support.platformPlugin ? QStringLiteral("Qt's Wayland shell integration (xdg-shell) is missing")
                                      : QStringLiteral("Qt's Wayland platform plugin is missing");
    }
    return QStringLiteral("xcb");
}

QString fallbackPlatformForThisSystem(QString* why) {
#if defined(Q_OS_LINUX)
    PlatformEnvironment env;
    env.platformSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    env.wayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
                  qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
    env.x11 = qEnvironmentVariableIsSet("DISPLAY");
    if (env.platformSet || !env.wayland || !env.x11) return {};
    return fallbackPlatform(env, waylandSupport(QLibraryInfo::path(QLibraryInfo::PluginsPath)), why);
#else
    Q_UNUSED(why);
    return {};
#endif
}

} // namespace lumenis::ui

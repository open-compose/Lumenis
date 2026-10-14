#include "lumenis/ui/platform_check.h"

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPluginLoader>

namespace lumenis::ui {

WaylandSupport waylandSupport(const QString& pluginsDir, bool tryLoad) {
    WaylandSupport s;
    // libqwayland-egl/-generic.so up to Qt 6.9, libqwayland.so from 6.10.
    const QStringList platform = QDir(pluginsDir + QStringLiteral("/platforms"))
                                     .entryList({QStringLiteral("libqwayland*.so")}, QDir::Files);
    const QString shell = pluginsDir + QStringLiteral("/wayland-shell-integration/libxdg-shell.so");
    s.platformPlugin = !platform.isEmpty();
    s.shellIntegration = QFileInfo::exists(shell);
    if (tryLoad && s.complete()) {
        auto loads = [&s](const QString& path) {
            QPluginLoader loader(path);
            if (loader.load()) return true; // loaded, not instantiated; Qt reuses it
            s.loadError = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), loader.errorString());
            return false;
        };
        s.platformPlugin = loads(pluginsDir + QStringLiteral("/platforms/") + platform.first());
        s.shellIntegration = s.platformPlugin && loads(shell);
    }
    return s;
}

QString fallbackPlatform(const PlatformEnvironment& env, const WaylandSupport& support, QString* why) {
    if (env.platformSet || !env.wayland || support.complete()) return {};
    if (why) {
        // What is wrong, and what to do about it.
        if (!support.loadError.isEmpty()) {
            *why = QStringLiteral("Qt's Wayland support is installed but does not load (%1). Its packages are "
                                  "probably from different updates: sudo apt update && sudo apt full-upgrade")
                       .arg(support.loadError);
        } else {
            *why = QStringLiteral("%1 is missing. Install qt6-wayland (Fedora: qt6-qtwayland)")
                       .arg(support.platformPlugin ? QStringLiteral("Qt's Wayland shell integration (xdg-shell)")
                                                   : QStringLiteral("Qt's Wayland platform plugin"));
        }
    }
    // Without X11 there is nothing to fall back to; `why` still says what to
    // install.
    return env.x11 ? QStringLiteral("xcb") : QString();
}

QString fallbackPlatformForThisSystem(QString* why) {
#if defined(Q_OS_LINUX)
    PlatformEnvironment env;
    env.platformSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    env.wayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
                  qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
    env.x11 = qEnvironmentVariableIsSet("DISPLAY");
    if (env.platformSet || !env.wayland) return {};
    return fallbackPlatform(env, waylandSupport(QLibraryInfo::path(QLibraryInfo::PluginsPath), true), why);
#else
    Q_UNUSED(why);
    return {};
#endif
}

} // namespace lumenis::ui

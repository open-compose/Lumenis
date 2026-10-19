#include "lumenis/ui/platform_check.h"

#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <QSettings>

// Picks the Qt platform on Linux before the QApplication exists (platform_check.h). When a Wayland
// session lacks part of Qt's Wayland support, the program asks for xcb (XWayland) instead and says what
// to install, rather than letting Qt print a page of warnings. app/main.cpp calls
// fallbackPlatformForThisSystem(); the parts are separate functions so tests can give any environment and
// plugins folder. BUILDING.md has the troubleshooting entry.

namespace lumenis::ui {

// Checks for the files first; loading is tried only when both are there.
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
            const QString part = support.platformPlugin ? QStringLiteral("Qt's Wayland shell integration (xdg-shell)")
                                                        : QStringLiteral("Qt's Wayland platform plugin");
            *why = support.bundled
                       // The system's packages cannot help: the program's own copy of Qt lacks it.
                       ? QStringLiteral("%1 is missing from this package's own Qt. Make the AppImage again with the "
                                        "current packaging/linux/make-appimage.sh, or run the built bin/cutthulhu")
                             .arg(part)
                       : QStringLiteral("%1 is missing. Install qt6-wayland (Fedora: qt6-qtwayland)").arg(part);
        }
    }
    // Without X11 there is nothing to fall back to; `why` still says what to
    // install.
    return env.x11 ? QStringLiteral("xcb") : QString();
}

// qt.conf's [Paths] Prefix is relative to the executable's folder and Plugins to the prefix, with Qt's
// defaults "." and "plugins". Without a qt.conf, Qt's built-in plugins path.
QString qtPluginsDir(const QString& executable) {
    const QDir bin = QFileInfo(executable).absoluteDir();
    const QString conf = bin.filePath(QStringLiteral("qt.conf"));
    if (!executable.isEmpty() && QFileInfo::exists(conf)) {
        QSettings settings(conf, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("Paths"));
        const QString prefix = settings.value(QStringLiteral("Prefix"), QStringLiteral(".")).toString();
        const QString plugins = settings.value(QStringLiteral("Plugins"), QStringLiteral("plugins")).toString();
        const QDir base(QDir::isAbsolutePath(prefix) ? prefix : bin.filePath(prefix));
        return QDir::cleanPath(QDir::isAbsolutePath(plugins) ? plugins : base.filePath(plugins));
    }
    return QLibraryInfo::path(QLibraryInfo::PluginsPath);
}

// `bundled` is set when the plugins folder is not Qt's built-in one, i.e. it came from a qt.conf (an
// AppImage). Other platforms always return empty.
QString fallbackPlatformForThisSystem(QString* why) {
#if defined(Q_OS_LINUX)
    PlatformEnvironment env;
    env.platformSet = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    env.wayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
                  qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland");
    env.x11 = qEnvironmentVariableIsSet("DISPLAY");
    if (env.platformSet || !env.wayland) return {};
    const QString executable = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    const QString plugins = qtPluginsDir(executable);
    WaylandSupport support = waylandSupport(plugins, true);
    support.bundled = QDir(plugins) != QDir(QLibraryInfo::path(QLibraryInfo::PluginsPath));
    return fallbackPlatform(env, support, why);
#else
    Q_UNUSED(why);
    return {};
#endif
}

} // namespace lumenis::ui

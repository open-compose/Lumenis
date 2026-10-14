#include "lumenis/ui/platform_check.h"

#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis::ui;

namespace {

void touch(const QString& path) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
}

} // namespace

class TestPlatformCheck : public QObject {
    Q_OBJECT

private slots:
    void findsTheWaylandParts() {
        QTemporaryDir plugins;
        QVERIFY(!waylandSupport(plugins.path()).platformPlugin);
        // Ubuntu 26.04 with only the Qt development packages: the platform
        // plugin, no shell integration (BUILDING.md).
        touch(plugins.filePath(QStringLiteral("platforms/libqwayland.so")));
        WaylandSupport s = waylandSupport(plugins.path());
        QVERIFY(s.platformPlugin);
        QVERIFY(!s.shellIntegration);
        QVERIFY(!s.complete());
        touch(plugins.filePath(QStringLiteral("wayland-shell-integration/libxdg-shell.so")));
        QVERIFY(waylandSupport(plugins.path()).complete());

        // Files that are not plugins are there but do not load.
        const WaylandSupport broken = waylandSupport(plugins.path(), true);
        QVERIFY(!broken.complete());
        QVERIFY(broken.loadError.contains(QStringLiteral("libqwayland.so")));
        QString why;
        QCOMPARE(fallbackPlatform({false, true, true}, broken, &why), QStringLiteral("xcb"));
        QVERIFY(why.contains(QStringLiteral("does not load")));

        // Qt 6.9 and earlier name the plugin differently.
        QTemporaryDir older;
        touch(older.filePath(QStringLiteral("platforms/libqwayland-generic.so")));
        QVERIFY(waylandSupport(older.path()).platformPlugin);
    }

    void loadsTheRealPlugins() {
        // This machine's Qt, when it has Wayland support: it loads.
        const QString dir = QLibraryInfo::path(QLibraryInfo::PluginsPath);
        if (!waylandSupport(dir).complete()) QSKIP("no Wayland support installed here");
        const WaylandSupport s = waylandSupport(dir, true);
        QVERIFY2(s.complete(), qPrintable(s.loadError));
    }

    void readsQtConfLikeQt() {
        // An AppImage: usr/bin/qt.conf points at usr/plugins.
        QTemporaryDir app;
        touch(app.filePath(QStringLiteral("usr/bin/cutthulhu")));
        QFile conf(app.filePath(QStringLiteral("usr/bin/qt.conf")));
        QVERIFY(conf.open(QIODevice::WriteOnly));
        conf.write("[Paths]\nPrefix = ../\nPlugins = plugins\n");
        conf.close();
        QCOMPARE(qtPluginsDir(app.filePath(QStringLiteral("usr/bin/cutthulhu"))),
                 QDir::cleanPath(app.filePath(QStringLiteral("usr/plugins"))));
        // Without one, Qt's own folder.
        QTemporaryDir plain;
        touch(plain.filePath(QStringLiteral("cutthulhu")));
        QCOMPARE(qtPluginsDir(plain.filePath(QStringLiteral("cutthulhu"))),
                 QLibraryInfo::path(QLibraryInfo::PluginsPath));
    }

    void fallsBackToX11OnlyWhenItShould() {
        const WaylandSupport incomplete{true, false, {}};
        const WaylandSupport complete{true, true, {}};
        PlatformEnvironment session{false, true, true}; // Wayland session with XWayland
        QString why;
        QCOMPARE(fallbackPlatform(session, incomplete, &why), QStringLiteral("xcb"));
        QVERIFY(why.contains(QStringLiteral("xdg-shell")));
        QCOMPARE(fallbackPlatform(session, WaylandSupport{false, false, {}}, &why), QStringLiteral("xcb"));
        QVERIFY(why.contains(QStringLiteral("platform plugin")));
        // Complete support: Qt chooses.
        QVERIFY(fallbackPlatform(session, complete).isEmpty());
        // In an AppImage, the system's packages are no help.
        WaylandSupport bundled = incomplete;
        bundled.bundled = true;
        QCOMPARE(fallbackPlatform(session, bundled, &why), QStringLiteral("xcb"));
        QVERIFY(why.contains(QStringLiteral("make-appimage.sh")));
        QVERIFY(!why.contains(QStringLiteral("Install qt6-wayland")));
        // The user's own choice stays.
        QVERIFY(fallbackPlatform({true, true, true}, incomplete).isEmpty());
        // Not a Wayland session, or no X11 to fall back to.
        QVERIFY(fallbackPlatform({false, false, true}, incomplete).isEmpty());
        why.clear();
        QVERIFY(fallbackPlatform({false, true, false}, incomplete, &why).isEmpty());
        // ... but it still says what is missing.
        QVERIFY(why.contains(QStringLiteral("xdg-shell")));
    }
};

QTEST_GUILESS_MAIN(TestPlatformCheck)
#include "tst_platform_check.moc"

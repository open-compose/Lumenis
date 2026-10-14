#include "lumenis/ui/platform_check.h"

#include <QDir>
#include <QFile>
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

        // Qt 6.9 and earlier name the plugin differently.
        QTemporaryDir older;
        touch(older.filePath(QStringLiteral("platforms/libqwayland-generic.so")));
        QVERIFY(waylandSupport(older.path()).platformPlugin);
    }

    void fallsBackToX11OnlyWhenItShould() {
        const WaylandSupport incomplete{true, false};
        const WaylandSupport complete{true, true};
        PlatformEnvironment session{false, true, true}; // Wayland session with XWayland
        QString why;
        QCOMPARE(fallbackPlatform(session, incomplete, &why), QStringLiteral("xcb"));
        QVERIFY(why.contains(QStringLiteral("xdg-shell")));
        QCOMPARE(fallbackPlatform(session, WaylandSupport{false, false}, &why), QStringLiteral("xcb"));
        QVERIFY(why.contains(QStringLiteral("platform plugin")));
        // Complete support: Qt chooses.
        QVERIFY(fallbackPlatform(session, complete).isEmpty());
        // The user's own choice stays.
        QVERIFY(fallbackPlatform({true, true, true}, incomplete).isEmpty());
        // Not a Wayland session, or no X11 to fall back to.
        QVERIFY(fallbackPlatform({false, false, true}, incomplete).isEmpty());
        QVERIFY(fallbackPlatform({false, true, false}, incomplete).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPlatformCheck)
#include "tst_platform_check.moc"

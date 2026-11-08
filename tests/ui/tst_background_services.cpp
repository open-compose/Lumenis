// The background services (8.4a; Editing Guide p.375-377): the Background Queue starting jobs only while the services
// are active, pausing (explicit, during playback, extended while play goes on), the error report, the Timeline's progress
// indicator and Tools > Background Services with its options kept in the user's settings.
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/background_services.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;
using State = BackgroundQueue::ServiceState;

namespace {

BackgroundQueue::Work quick(bool fail = false) {
    return [fail](const std::function<bool(double)>& progress) {
        progress(1.0);
        media::ConsolidateResult r;
        if (fail) r.error = QStringLiteral("disk full");
        return r;
    };
}

} // namespace

class TestBackgroundServices : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void queueFollowsTheServices() {
        BackgroundQueue q;
        QCOMPARE(q.serviceState(), State::Active);
        // Stopped: jobs wait (p.376: "These services are off by default. You must turn the Avid Services On").
        q.stopServices();
        const int id = q.add(QStringLiteral("a"), {}, quick());
        q.waitForIdle();
        QCOMPARE(q.jobs().first().state, BackgroundQueue::State::Waiting);
        QVERIFY(q.isBusy());
        q.startServices();
        q.waitForIdle();
        QCOMPARE(q.jobs().first().state, BackgroundQueue::State::Completed);
        Q_UNUSED(id);

        // Pause for a time; it ends by itself.
        q.pauseServices(50);
        QCOMPARE(q.serviceState(), State::Paused);
        q.add(QStringLiteral("b"), {}, quick());
        q.waitForIdle();
        QCOMPARE(q.jobs().last().state, BackgroundQueue::State::Waiting);
        QTRY_COMPARE(q.serviceState(), State::Active);
        q.waitForIdle();
        QCOMPARE(q.jobs().last().state, BackgroundQueue::State::Completed);

        // Playback pauses; a pause ending during play is extended (p.377).
        q.setAutoPause(true);
        QCOMPARE(q.serviceState(), State::Paused);
        q.pauseServices(20);
        QTest::qWait(120);
        QCOMPARE(q.serviceState(), State::Paused);
        q.setAutoPause(false);
        QTRY_COMPARE(q.serviceState(), State::Active);
        // Without Enable Pausing nothing pauses.
        q.setPausingEnabled(false);
        q.setAutoPause(true);
        q.pauseServices(1000);
        QCOMPARE(q.serviceState(), State::Active);
        q.setAutoPause(false);
        q.setPausingEnabled(true);

        // A failure leaves the error triangle until the report is taken.
        q.add(QStringLiteral("clip 3"), {}, quick(true));
        q.waitForIdle();
        QVERIFY(q.hasErrors());
        QCOMPARE(q.takeErrorReport(), QStringLiteral("clip 3: disk full"));
        QVERIFY(!q.hasErrors());
    }

    void indicatorStates() {
        BackgroundIndicator i;
        QCOMPARE(i.description(), QStringLiteral("idle"));
        i.setState({State::Active, true, false, false});
        QCOMPARE(i.description(), QStringLiteral("busy"));
        i.setState({State::Active, false, true, true});
        QCOMPARE(i.description(), QStringLiteral("assets error"));
        i.setState({State::Stopped, false, false, false});
        QCOMPARE(i.description(), QStringLiteral("stopped"));
        i.setState({State::Paused, true, false, false});
        QCOMPARE(i.description(), QStringLiteral("paused"));
    }

    void inTheApplication() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        BackgroundQueue& q = window.backgroundQueue();
        BackgroundIndicator* indicator = window.timeline().backgroundIndicator();
        QVERIFY(indicator);
        // Started at launch (ours: Always Start ... at Launch is on to start with).
        QCOMPARE(q.serviceState(), State::Active);
        QCOMPARE(indicator->description(), QStringLiteral("idle"));

        // Tools > Background Services: Stop at once; the options kept on OK.
        window.setBackgroundServicesHook([](BackgroundServicesDialog& d) {
            d.stop();
            return true;
        });
        commands.action(QStringLiteral("tools.background_services"))->trigger();
        QCOMPARE(q.serviceState(), State::Stopped);
        QCOMPARE(indicator->description(), QStringLiteral("stopped"));
        QString status;
        window.setBackgroundServicesHook([&status](BackgroundServicesDialog& d) {
            status = d.statusText();
            d.findChild<QCheckBox*>(QStringLiteral("startAtLaunch"))->setChecked(false);
            d.findChild<QCheckBox*>(QStringLiteral("stopOnExit"))->setChecked(true);
            return true;
        });
        QVERIFY(window.backgroundServices());
        QCOMPARE(status, QStringLiteral("Inactive"));
        QVERIFY(!window.backgroundServiceOptions().startAtLaunch);
        QVERIFY(window.backgroundServiceOptions().stopOnExit);
        // The next user's settings start as theirs say: ed's stay stopped at "launch".
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        QCOMPARE(q.serviceState(), State::Stopped);
        window.setBackgroundServicesHook([](BackgroundServicesDialog& d) {
            d.start();
            d.pause(5);
            return false; // Cancel: the buttons acted, the options stay
        });
        QVERIFY(!window.backgroundServices());
        QCOMPARE(q.serviceState(), State::Paused);
        QCOMPARE(indicator->description(), QStringLiteral("paused"));
        q.startServices();

        // An error: the triangle, and the report into the Console.
        q.add(QStringLiteral("clip 9"), {}, quick(true));
        q.waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(indicator->description(), QStringLiteral("idle error"));
        ConsoleLog::instance().clear();
        emit indicator->errorReportRequested();
        QVERIFY(ConsoleLog::instance().lines().join(QLatin1Char('\n')).contains(QStringLiteral("clip 9: disk full")));
        QCOMPARE(indicator->description(), QStringLiteral("idle"));
    }
};

QTEST_MAIN(TestBackgroundServices)
#include "tst_background_services.moc"

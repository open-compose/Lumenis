// The background services (8.4a; Editing Guide p.375-377): the Background Queue starting jobs only while the services
// are active, pausing (explicit, during playback, extended while play goes on), the error report, the Timeline's progress
// indicator and Tools > Background Services with its options kept in the user's settings.
#include "lumenis/media/dynamic_media_folders.h"
#include "lumenis/media/media.h"
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
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <vector>

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

    // Dynamic Media Folders (8.4b, p.371-376): an enabled folder's new file is linked and copied on the Background
    // Queue once it has looked the same twice; the asset waits with the green dot; the folders are kept for the user.
    void dynamicMediaFolders() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        BackgroundIndicator* indicator = window.timeline().backgroundIndicator();
        const QString watched = dir.filePath(QStringLiteral("card"));
        QVERIFY(QDir().mkpath(watched));
        {
            media::WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = QStringLiteral("pcm_s16le");
            s.channels = 1;
            s.sampleRate = 48000;
            media::MediaWriter w;
            QVERIFY(w.open(watched + QStringLiteral("/VO_1.wav"), s));
            std::vector<float> v(4800, 0.1f);
            QVERIFY(w.writeAudio(v.data(), 4800) && w.finish());
        }

        media::DynamicMediaFolders folders;
        media::DmfProfile profile;
        profile.name = QStringLiteral("Copy");
        media::DmfAction copy;
        copy.copyTo = dir.filePath(QStringLiteral("share"));
        profile.actions = {copy};
        folders.storeProfile(profile);
        folders.addFolder(watched);
        folders.folders[0].profile = profile.name;
        // Not enabled: nothing happens.
        window.setDynamicMediaFolders(folders);
        window.scanDynamicMediaFolders();
        QVERIFY(window.dmfAssets().isEmpty());

        folders.folders[0].enabled = true;
        window.setDynamicMediaFolders(folders); // the first look
        QVERIFY(window.dmfAssets().isEmpty());
        window.scanDynamicMediaFolders(); // the same: queued
        QCOMPARE(window.dmfAssets().size(), 1);
        window.scanDynamicMediaFolders(); // not twice
        QCOMPARE(window.dmfAssets().size(), 1);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        const MainWindow::DmfAsset& asset = window.dmfAssets()[0];
        QVERIFY(asset.ready);
        QVERIFY2(asset.error.isEmpty(), qPrintable(asset.error));
        QCOMPARE(asset.clips.size(), 1);
        QCOMPARE(media::linkOf(asset.clips[0])->path, dir.filePath(QStringLiteral("share/VO_1.wav")));
        QCOMPARE(indicator->description(), QStringLiteral("assets"));
        QCOMPARE(window.dynamicMediaFolders().folders[0].done, QStringList{QStringLiteral("VO_1.wav")});

        // Kept beside the user's settings, the done entry with them.
        const QString file = window.dynamicMediaFoldersFile();
        QVERIFY(file.endsWith(QLatin1String("Dynamic_Media_Folders.json")));
        const media::DynamicMediaFolders back = media::DynamicMediaFolders::load(file);
        QCOMPARE(back.folders.size(), 1);
        QCOMPARE(back.folders[0].done, QStringList{QStringLiteral("VO_1.wav")});
        QCOMPARE(back.profiles.size(), 1);

        // A file that is not media fails: the error triangle, no green dot from it.
        QFile notes(watched + QStringLiteral("/notes.txt"));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("not media");
        notes.close();
        window.scanDynamicMediaFolders();
        window.scanDynamicMediaFolders();
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(window.dmfAssets().size(), 2);
        QVERIFY(!window.dmfAssets()[1].error.isEmpty());
        QCOMPARE(indicator->description(), QStringLiteral("assets error"));
    }
};

QTEST_MAIN(TestBackgroundServices)
#include "tst_background_services.moc"

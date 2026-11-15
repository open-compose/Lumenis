// The background services (8.4a; Editing Guide p.375-377): the Background Queue starting jobs only while the services
// are active, pausing (explicit, during playback, extended while play goes on), the error report, the Timeline's progress
// indicator and Tools > Background Services with its options kept in the user's settings.
#include "lumenis/media/dynamic_media_folders.h"
#include "lumenis/media/media.h"
#include "lumenis/media/movie_export.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/background_services.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/dynamic_media_folders_window.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
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

// Work that runs until told to stop (or a few seconds), counting its runs.
BackgroundQueue::Work slow(std::shared_ptr<std::atomic<int>> runs) {
    return [runs](const std::function<bool(double)>& progress) {
        ++*runs;
        media::ConsolidateResult r;
        for (int i = 0; i < 300; ++i) {
            if (!progress(i / 300.0)) {
                r.error = QStringLiteral("stopped");
                return r;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        r.added << model::BinItem{};
        return r;
    };
}

} // namespace

class TestBackgroundServices : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Stop Services suspends the running job; Resume (in place of its Cancel button, p.357) runs it again from the
    // start. Show Job Groups hides and shows each group's jobs, listed under their headings.
    void suspendResumeAndGroups() {
        BackgroundQueue q;
        auto runs = std::make_shared<std::atomic<int>>(0);
        int finished = 0;
        connect(&q, &BackgroundQueue::finished, this, [&](const QString&, const media::ConsolidateResult&) { ++finished; });
        const int id = q.add(QStringLiteral("Interview"), {}, slow(runs), BackgroundQueue::Priority::Normal, {},
                             QStringLiteral("Transcode"));
        q.add(QStringLiteral("Render: Story"), {}, quick(), BackgroundQueue::Priority::Low, {}, QStringLiteral("Render"));
        QTRY_VERIFY(q.jobs().first().state == BackgroundQueue::State::Running && runs->load() == 1);
        q.stopServices();
        QTRY_COMPARE(q.jobs().first().state, BackgroundQueue::State::Suspended);
        QCOMPARE(finished, 0); // nothing handed on
        QCOMPARE(q.jobs().last().state, BackgroundQueue::State::Waiting);
        QVERIFY(!q.isBusy() || q.jobs().last().state == BackgroundQueue::State::Waiting);
        // Clear Inactive Jobs keeps it.
        q.clearInactive();
        QCOMPARE(q.jobs().size(), 2);

        BackgroundQueueWindow window(q);
        window.show();
        QPushButton* resume = window.findChild<QPushButton*>(QStringLiteral("resumeJob"));
        QVERIFY(resume);
        QStringList headings;
        for (QLabel* l : window.findChildren<QLabel*>(QStringLiteral("jobGroup"))) headings << l->text();
        QCOMPARE(headings, (QStringList{"Transcode", "Render"}));
        // Show Job Groups lists the groups; hiding Render leaves its job out.
        QMenu* groups = window.findChild<QMenu*>(QStringLiteral("showJobGroups"));
        QVERIFY(groups);
        emit groups->aboutToShow();
        QCOMPARE(groups->actions().size(), 2);
        QVERIFY(groups->actions().at(1)->isChecked());
        groups->actions().at(1)->toggle();
        QVERIFY(!window.groupShown(QStringLiteral("Render")));
        QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("backgroundJob")).size(), 1);
        window.setGroupShown(QStringLiteral("Render"), true);
        QCOMPARE(window.findChildren<QWidget*>(QStringLiteral("backgroundJob")).size(), 2);

        // Resume: it waits, and runs again from the start once the services start.
        window.findChild<QPushButton*>(QStringLiteral("resumeJob"))->click();
        QCOMPARE(q.jobs().first().state, BackgroundQueue::State::Waiting);
        q.startServices();
        QTRY_COMPARE(runs->load(), 2);
        q.cancel(id);
        q.waitForIdle();
        QCOMPARE(q.jobs().first().state, BackgroundQueue::State::Cancelled);
        QCOMPARE(q.jobs().last().state, BackgroundQueue::State::Completed);
    }

    // The Link setting's AIS Metadata tab (p.1204-1205): linked DPX files get the Labroll, Camroll and DPX columns it
    // says, as Dynamic Media Folders' profiles fill them.
    void linkSettingAisColumns() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(24);
        QDir().mkpath(project.folder);
        window.setProject(project);
        const QString reel = dir.filePath(QStringLiteral("Roll_C"));
        QVERIFY(QDir().mkpath(reel));
        QImage picture(64, 36, QImage::Format_RGB32);
        picture.fill(Qt::blue);
        media::StillOptions o;
        o.format = media::StillFormat::Dpx;
        o.depth = 10;
        o.dpxInputDevice = QStringLiteral("Scanner 3");
        const QString file = reel + QStringLiteral("/C001_0000042.dpx");
        QVERIFY(media::writeStill(file, picture, o));
        window.settings().setValue(QStringLiteral("Link"), QStringLiteral("reelName"), QStringLiteral("folderName"));
        window.settings().setValue(QStringLiteral("Link"), QStringLiteral("frameCount"), QStringLiteral("fileName"));
        const QString binId = window.binContainer().newBin();
        const QStringList ids = window.linkFiles(binId, {file});
        QCOMPARE(ids.size(), 1);
        const model::BinItem* clip = window.binContainer().document(binId)->bin().item(ids.first());
        QVERIFY(clip);
        QCOMPARE(clip->fields.value(QStringLiteral("Labroll")), QStringLiteral("Roll_C"));
        QCOMPARE(clip->fields.value(QStringLiteral("Camroll")), QStringLiteral("Scanner 3"));
        QCOMPARE(clip->fields.value(QStringLiteral("DPX")), QStringLiteral("Roll_C0000042"));
    }

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
        // The failure is in the error report; it leaves nothing to acquire.
        QCOMPARE(window.dmfAssets().size(), 1);
        QCOMPARE(window.dynamicMediaFolders().folders[0].done.size(), 2);
        QCOMPARE(indicator->description(), QStringLiteral("assets error"));
    }

    // Tools > Dynamic Media Folders (8.4c, p.372-376): + adds a folder, the Profile Editor saves a named profile for it,
    // Enable starts the watching, Acquire puts the clips in a new bin named by the profile; an entry still being
    // processed goes in offline and is replaced when it is acquired again.
    void dmfWindowAndAcquire() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();

        commands.action(QStringLiteral("tools.dynamic_media_folders"))->trigger();
        DynamicMediaFoldersWindow* w = window.dynamicMediaFoldersWindow();
        QVERIFY(w && !w->isHidden());
        const QString watched = dir.filePath(QStringLiteral("card"));
        QVERIFY(QDir().mkpath(watched));
        w->setFolderChooser([&] { return watched; });
        w->findChild<QToolButton*>(QStringLiteral("addFolder"))->click();
        QCOMPARE(window.dynamicMediaFolders().folders.size(), 1);
        QVERIFY(!window.dynamicMediaFolders().folders[0].enabled);

        // The Profile Editor: the menu bars open and close; Actions + with the Menu button's choices, a row dragged
        // above another; Save names the profile, which the folder then has.
        window.setDmfProfileEditorHook([](DmfProfileEditor& e) {
            e.openSection(QStringLiteral("link"), false);
            if (e.isSectionOpen(QStringLiteral("link"))) return false;
            e.openSection(QStringLiteral("link"), true);
            e.findChild<QComboBox*>(QStringLiteral("naming"))->setCurrentIndex(2);
            e.findChild<QLineEdit*>(QStringLiteral("binName"))->setText(QStringLiteral("Card A"));
            e.addAction();
            e.setActionKind(0, media::DmfAction::Kind::Transcode);
            e.addAction(); // Copy to Folder, selected
            e.findChild<QLineEdit*>(QStringLiteral("copyTo"))->setText(QStringLiteral("/tmp/nowhere"));
            e.moveAction(1, 0);
            const media::DmfProfile p = e.profile();
            if (p.actions.size() != 2 || p.actions[0].kind != media::DmfAction::Kind::CopyToFolder ||
                p.actions[0].copyTo != QLatin1String("/tmp/nowhere") || p.actions[1].kind != media::DmfAction::Kind::Transcode)
                return false;
            // Only the Transcode is kept, so that nothing is copied in this test.
            e.removeAction(0);
            e.setNameChooser([](const QString&) { return QStringLiteral("Card"); });
            return e.save();
        });
        w->selectFolder(0);
        w->findChild<QPushButton*>(QStringLiteral("profileEditor"))->click();
        const media::DmfProfile* saved = window.dynamicMediaFolders().profile(QStringLiteral("Card"));
        QVERIFY(saved);
        QCOMPARE(saved->link.naming, media::DmfLinkSettings::Naming::Specify);
        QCOMPARE(saved->link.binName, QStringLiteral("Card A"));
        QCOMPARE(saved->actions.size(), 1);
        QCOMPARE(window.dynamicMediaFolders().folders[0].profile, QStringLiteral("Card"));
        // Without actions, so the job only links.
        media::DynamicMediaFolders folders = window.dynamicMediaFolders();
        folders.profiles[0].actions.clear();
        window.setDynamicMediaFolders(folders);

        // A file; Enable; two looks queue it. Stopped services keep it waiting, so it is acquired while processing.
        auto writeWav = [](const QString& path) {
            media::WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = QStringLiteral("pcm_s16le");
            s.channels = 1;
            s.sampleRate = 48000;
            media::MediaWriter m;
            std::vector<float> v(4800, 0.1f);
            return m.open(path, s) && m.writeAudio(v.data(), 4800) && m.finish();
        };
        QVERIFY(writeWav(watched + QStringLiteral("/Take1.wav")));
        window.backgroundQueue().stopServices();
        w->setEnabled(0, true);
        QVERIFY(window.dynamicMediaFolders().folders[0].enabled);
        window.scanDynamicMediaFolders();
        QCOMPARE(window.dmfAssets().size(), 1);
        QVERIFY(!window.dmfAssets()[0].ready);
        QVERIFY(w->acquireButton(0)->isEnabled());
        QCOMPARE(w->acquireButton(0)->text(), QStringLiteral("○"));

        const QString binId = window.acquireDmfAssets(watched);
        QVERIFY(!binId.isEmpty());
        BinDocument* doc = window.binContainer().document(binId);
        QVERIFY(doc);
        QCOMPARE(doc->name(), QStringLiteral("Card A"));
        QCOMPARE(doc->bin().items.size(), 1);
        QCOMPARE(doc->bin().items[0].name, QStringLiteral("Take1"));
        QVERIFY(!media::isLinked(doc->bin().items[0])); // offline
        QVERIFY(!w->acquireButton(0)->isEnabled());

        // Processed: the green icon; acquired again, the clip replaces the offline one.
        window.backgroundQueue().startServices();
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QVERIFY(window.dmfAssets()[0].ready);
        QCOMPARE(w->acquireButton(0)->text(), QStringLiteral("● 1"));
        QCOMPARE(window.timeline().backgroundIndicator()->description(), QStringLiteral("assets"));
        QCOMPARE(window.acquireDmfAssets(watched), binId);
        QCOMPARE(doc->bin().items.size(), 1);
        QVERIFY(media::isLinked(doc->bin().items[0]));
        QVERIFY(window.dmfAssets().isEmpty());
        QCOMPARE(window.timeline().backgroundIndicator()->description(), QStringLiteral("idle"));

        // Use active bin: the next file goes into the active bin.
        folders = window.dynamicMediaFolders();
        folders.profiles[0].link.bin = media::DmfLinkSettings::Bin::Active;
        window.setDynamicMediaFolders(folders);
        QVERIFY(writeWav(watched + QStringLiteral("/Take2.wav")));
        window.scanDynamicMediaFolders();
        window.scanDynamicMediaFolders();
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        window.binContainer().openBin(binId);
        QVERIFY(window.binContainer().activeDocument());
        const QString active = window.binContainer().activeDocument()->nodeId();
        QCOMPARE(window.acquireDmfAssets(watched), active);
        QVERIFY(window.binContainer().document(active)->bin().items.size() >= 1);

        // - removes the folder.
        w->selectFolder(0);
        w->findChild<QPushButton*>(QStringLiteral("removeFolder"))->click();
        QVERIFY(window.dynamicMediaFolders().folders.isEmpty());
    }
};

QTEST_MAIN(TestBackgroundServices)
#include "tst_background_services.moc"

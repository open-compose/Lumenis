#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/relink_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

QColor colourOf(int n) {
    return QColor(40 + (n * 23) % 176, 40 + (175 - n * 11 % 176), 40 + (n * 57) % 176);
}

// A 25 fps QuickTime file of flat colours.
bool writeMovie(const QString& path, int frames, const QString& timecode = {}) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    s.startTimecode = timecode;
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < frames; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(colourOf(i));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

bool writeTone(const QString& path) {
    media::WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.channels = 2;
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    const std::vector<float> silence(2 * 4800, 0.0f);
    return w.writeAudio(silence.data(), 4800) && w.finish();
}

bool near(const QImage& image, const QColor& want) {
    if (image.isNull()) return false;
    const QColor got = image.pixelColor(image.width() / 2, image.height() / 2);
    return std::abs(got.red() - want.red()) <= 2 && std::abs(got.green() - want.green()) <= 2 &&
           std::abs(got.blue() - want.blue()) <= 2;
}

// The folder of test media: two clips, a sound file, a text file, and a
// clip in a folder below.
struct MediaFolder {
    QTemporaryDir dir;
    QString root;
    MediaFolder() {
        root = QDir(dir.path()).filePath(QStringLiteral("Card A"));
        QDir().mkpath(root + QStringLiteral("/Day 2"));
        QVERIFY(writeMovie(root + QStringLiteral("/A001.mov"), 10, QStringLiteral("01:00:00:00")));
        QVERIFY(writeMovie(root + QStringLiteral("/A002.mov"), 6));
        QVERIFY(writeTone(root + QStringLiteral("/Room Tone.wav")));
        QFile notes(root + QStringLiteral("/notes.txt"));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("not media");
        notes.close();
        QVERIFY(writeMovie(root + QStringLiteral("/Day 2/B001.mov"), 4));
    }
    QString path(const char* name) const { return root + QLatin1Char('/') + QString::fromUtf8(name); }
};

QStringList shownNames(const SourceBrowser& browser) {
    QStringList names;
    const QAbstractItemModel* m = browser.textView()->model();
    for (int r = 0; r < m->rowCount(); ++r) names << m->index(r, 0).data().toString();
    return names;
}

QString cell(const SourceBrowser& browser, const QString& path, const QString& heading) {
    const SourceMediaModel* m = browser.media();
    return m->index(m->rowOf(path), static_cast<int>(SourceMediaModel::columns().indexOf(heading))).data().toString();
}

} // namespace

class TestSourceBrowser : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void listsAndProbesAFolder() {
        // The display area (p.314): folders, then files with what probing
        // found; files no plug-in reads say "Not detected" (p.322).
        MediaFolder f;
        MediaFrames frames;
        SourceBrowser browser;
        media::LinkOptions o;
        o.editRate = Rational(25);
        browser.setLinkOptions(o);
        browser.setFolder(f.root);
        browser.waitForProbes();
        QCOMPARE(shownNames(browser),
                 (QStringList{"Day 2", "A001.mov", "A002.mov", "notes.txt", "Room Tone.wav"}));
        QCOMPARE(browser.media()->pending(), 0);
        QCOMPARE(cell(browser, f.path("A001.mov"), "Duration"), QStringLiteral("0:10"));
        QCOMPARE(cell(browser, f.path("A001.mov"), "Start"), QStringLiteral("01:00:00:00"));
        QCOMPARE(cell(browser, f.path("A001.mov"), "Video"), QStringLiteral("FFV1"));
        QCOMPARE(cell(browser, f.path("A001.mov"), "Plug-In"), media::linkPluginName());
        QCOMPARE(cell(browser, f.path("Room Tone.wav"), "Tracks"), QStringLiteral("A1-2"));
        QCOMPARE(cell(browser, f.path("notes.txt"), "Plug-In"), QStringLiteral("Not detected"));

        // Search (p.314).
        browser.setSearch(QStringLiteral("a00"));
        QCOMPARE(shownNames(browser), (QStringList{"A001.mov", "A002.mov"}));
        browser.setSearch({});

        // Media Folder View: the media below, as clips, and nothing else
        // (p.313).
        browser.setMediaFolderView(true);
        browser.waitForProbes();
        QCOMPARE(shownNames(browser), (QStringList{"A001.mov", "A002.mov", "B001.mov", "Room Tone.wav"}));
        browser.setMediaFolderView(false);
        browser.waitForProbes();

        // Link (p.317): the selection, or with nothing selected everything
        // that can be linked.
        browser.setBins({{QStringLiteral("b1"), QStringLiteral("Dailies")}, {QStringLiteral("b2"), QStringLiteral("Sound")}},
                        QStringLiteral("b2"));
        QCOMPARE(browser.targetBin(), QStringLiteral("b2"));
        QSignalSpy linked(&browser, &SourceBrowser::linkRequested);
        browser.linkSelection();
        QCOMPARE(linked.count(), 1);
        QCOMPARE(linked.at(0).at(0).toString(), QStringLiteral("b2"));
        QCOMPARE(linked.at(0).at(1).toStringList(),
                 (QStringList{f.path("A001.mov"), f.path("A002.mov"), f.path("Room Tone.wav")}));
        browser.selectPaths({f.path("A002.mov")});
        QCOMPARE(browser.pathsToLink(), (QStringList{f.path("A002.mov")}));

        // Dragging files carries their paths (p.316-317).
        const QModelIndex i = browser.media()->index(browser.media()->rowOf(f.path("A002.mov")), 0);
        std::unique_ptr<QMimeData> mime(browser.media()->mimeData({i}));
        QCOMPARE(QString::fromUtf8(mime->data(mediaFilesMime())), f.path("A002.mov"));
        QVERIFY(!(browser.media()->flags(browser.media()->index(0, 0)) & Qt::ItemIsDragEnabled)); // a folder
    }

    void navigation() {
        // Back, forward, up, home, breadcrumbs, favorites and recent folders
        // (p.313-314).
        MediaFolder f;
        SourceBrowser browser;
        QVERIFY(!browser.canGoBack());
        browser.setFolder(f.root);
        browser.setFolder(f.path("Day 2"));
        QCOMPARE(browser.breadcrumbs().last(), QStringLiteral("Day 2"));
        QCOMPARE(browser.breadcrumbs().at(browser.breadcrumbs().size() - 2), QStringLiteral("Card A"));
        QVERIFY(browser.canGoBack());
        browser.back();
        QCOMPARE(browser.folder(), f.root);
        QVERIFY(browser.canGoForward());
        browser.forward();
        QCOMPARE(browser.folder(), f.path("Day 2"));
        browser.up();
        QCOMPARE(browser.folder(), f.root);
        QVERIFY(!browser.canGoForward());
        browser.toggleFavorite();
        QCOMPARE(browser.favorites(), (QStringList{f.root}));
        QVERIFY(browser.button(QStringLiteral("favorite"))->isChecked());
        QCOMPARE(browser.recent().first(), f.root);
        QVERIFY(browser.recent().contains(f.path("Day 2")));
        browser.home();
        QCOMPARE(browser.folder(), QDir::cleanPath(QDir::homePath()));
        QVERIFY(!browser.button(QStringLiteral("favorite"))->isChecked());

        // Settings and view are kept between sessions.
        browser.setFrameView(true);
        browser.setThumbnailStep(3);
        browser.setDoubleClick(SourceBrowser::DoubleClick::LoadInSourceMonitor);
        browser.setCloseAfterLink(true);
        browser.setSettingsCollapsed(true);
        browser.setFolder(f.root);
        QTemporaryDir store;
        QSettings settings(store.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        browser.save(settings);
        SourceBrowser again;
        again.restore(settings);
        QCOMPARE(again.folder(), f.root);
        QCOMPARE(again.favorites(), (QStringList{f.root}));
        QVERIFY(again.isFrameView());
        QCOMPARE(again.thumbnailStep(), 3);
        QCOMPARE(again.doubleClick(), SourceBrowser::DoubleClick::LoadInSourceMonitor);
        QVERIFY(again.closesAfterLink());
        QVERIFY(again.isSettingsCollapsed());
    }

    void frameViewThumbnails() {
        // Frame view shows the first frame; hovering moves through the clip
        // and the frame shown stays (p.315-316). Ctrl+L enlarges.
        MediaFolder f;
        MediaFrames frames;
        SourceBrowser browser;
        media::LinkOptions o;
        o.editRate = Rational(25);
        browser.setLinkOptions(o);
        browser.setFolder(f.root);
        browser.waitForProbes();
        browser.setFrameView(true);
        const int row = browser.media()->rowOf(f.path("A001.mov"));
        const SourceMediaModel::Entry* e = browser.media()->entry(row);
        QVERIFY(e && e->linkable);
        QCOMPARE(e->frame, 0);
        QVERIFY(frames.thumbnail(e->clip, 0, browser.thumbnailSize()).isNull()); // decoding
        frames.waitForThumbnails();
        QTRY_VERIFY(near(frames.thumbnail(e->clip, 0, browser.thumbnailSize()), colourOf(0)));
        browser.scrubTo(f.path("A001.mov"), 7);
        QCOMPARE(browser.media()->entry(row)->frame, 7);
        browser.scrubTo(f.path("A001.mov"), 99); // the last frame at most
        QCOMPARE(browser.media()->entry(row)->frame, 9);
        const int step = browser.thumbnailStep();
        browser.show();
        browser.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&browser));
        browser.frameView()->setFocus();
        QTest::keyClick(browser.frameView(), Qt::Key_L, Qt::ControlModifier);
        QCOMPARE(browser.thumbnailStep(), step + 1);
        QTest::keyClick(browser.frameView(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(browser.thumbnailStep(), step);
    }

    void linkingInTheApplication() {
        // File > Input > Source Browser; Link puts master clips in the
        // target bin (p.317); the monitors show their frames, and
        // "Media Offline" once the file is gone (p.347).
        MediaFolder f;
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        QVERIFY(window.binContainer().openBin(binId));

        commands.action(QStringLiteral("file.input.source_browser"))->trigger();
        Panel* panel = window.panels().panel(QStringLiteral("SourceBrowser"));
        QVERIFY(window.panels().isOpen(panel));
        SourceBrowser& browser = window.sourceBrowser();
        BinDocument* bin = window.binContainer().activeDocument();
        QVERIFY(bin);
        QCOMPARE(browser.targetBin(), bin->nodeId());

        browser.setFolder(f.root);
        browser.waitForProbes();
        browser.selectPaths({f.path("A001.mov"), f.path("Room Tone.wav"), f.path("notes.txt")});
        browser.linkSelection();
        QCOMPARE(bin->bin().items.size(), 2);
        const model::BinItem clip = bin->bin().items.at(0);
        QCOMPARE(clip.name, QStringLiteral("A001"));
        QCOMPARE(clip.kind, model::BinItemKind::MasterClip);
        QCOMPARE(clip.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
        QCOMPARE(bin->bin().items.at(1).kind, model::BinItemKind::AudioClip);

        // The Source monitor shows the clip's frames.
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(6);
        bool offline = true;
        QVERIFY(near(composer.picture(MonitorId::Source, QSize(64, 36), &offline), colourOf(6)));
        QVERIFY(!offline);

        // Cut into a sequence, the Record monitor shows them too.
        composer.state(MonitorId::Source).setPosition(0);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        QVERIFY(!composer.state(MonitorId::Record).isEmpty());
        composer.state(MonitorId::Record).setPosition(3);
        QVERIFY(near(composer.picture(MonitorId::Record, QSize(64, 36)), colourOf(3)));

        // A clip loaded straight from the Source Browser is in no bin until
        // it is cut in (p.316).
        window.loadFiles({f.path("A002.mov")});
        const model::LoadedClip* loaded = composer.state(MonitorId::Source).current();
        QVERIFY(loaded && loaded->binId.isEmpty());
        QCOMPARE(bin->bin().items.size(), 3); // the sequence, nothing more
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        QCOMPARE(composer.state(MonitorId::Source).current()->binId, browser.targetBin());
        QCOMPARE(bin->bin().items.size(), 4);

        // Clear Source Monitor Upon Closing Source Browser (p.315).
        window.loadFiles({f.path("A002.mov")});
        const int loadedCount = composer.state(MonitorId::Source).loadedCount();
        browser.setClearSourceOnClose(true);
        window.panels().closePanel(panel);
        QCoreApplication::processEvents();
        QCOMPARE(composer.state(MonitorId::Source).loadedCount(), loadedCount - 1);

        // Add Volume Bin (p.318): a bin named after the folder with all its
        // media.
        const QString volume = window.linkVolume(f.root);
        BinDocument* volumeBin = window.binContainer().document(volume);
        QVERIFY(volumeBin);
        QCOMPARE(volumeBin->name(), QStringLiteral("Card A"));
        QCOMPARE(volumeBin->bin().items.size(), 4);

        // Dropped on a bin from the Source Browser (p.317).
        const QStringList before = {f.path("A002.mov")};
        QCOMPARE(window.linkFiles(bin->nodeId(), before).size(), 1);

        // Gone from disk: "Media Offline" (p.347).
        MediaFrames::instance()->clear();
        QVERIFY(QFile::remove(f.path("A001.mov")));
        composer.loadDefault(bin->nodeId(), clip);
        QVERIFY(composer.picture(MonitorId::Source, QSize(64, 36), &offline).isNull());
        QVERIFY(offline);
    }
    void relinkUnlinkAndClipFrames() {
        // Clip > Modify > Unlink Media (p.368), Clip > Relink > Linked
        // Media (p.364-365) and the Timeline's Clip Frames (p.592).
        MediaFolder f;
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {f.path("A001.mov"), f.path("A002.mov")});
        QCOMPARE(ids.size(), 2);
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(ids.at(0)));
        bool offline = true;
        QVERIFY(near(composer.picture(MonitorId::Source, QSize(64, 36), &offline), colourOf(0)));

        // Unlink: Media Offline.
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({ids.at(0)});
        commands.action(QStringLiteral("clip.modify.unlink_media"))->trigger();
        QVERIFY(media::isUnlinked(*bin->bin().item(ids.at(0))));
        QVERIFY(composer.picture(MonitorId::Source, QSize(64, 36), &offline).isNull());
        QVERIFY(offline);

        // The files move; A002 is offline too now. The decoders let go of
        // them first: Windows does not move open files.
        MediaFrames::instance()->clear();
        QTemporaryDir moved;
        QDir().mkpath(moved.filePath(QStringLiteral("Card A copy")));
        for (const char* name : {"A001.mov", "A002.mov"}) {
            QVERIFY(QFile::rename(f.path(name), moved.filePath(QStringLiteral("Card A copy/")) + QString::fromLatin1(name)));
        }
        QStringList shown;
        QString summary;
        window.setLinkedMediaHook([&](LinkedMediaDialog& dialog) {
            shown = dialog.shownNames();
            // Offline only by default (picture on p.365); both are.
            dialog.setOfflineOnly(false);
            dialog.setSearch(QStringLiteral("A00"));
            QCOMPARE(dialog.list()->topLevelItemCount(), 2);
            dialog.setSearch({});
            QCOMPARE(dialog.locateMedia(moved.path()), 2);
            summary = dialog.summary();
            dialog.setOfflineOnly(true);
            QCOMPARE(dialog.shownNames(), QStringList());
        });
        view->selectIds({});
        commands.action(QStringLiteral("clip.relink.linked_media"))->trigger();
        QCOMPARE(shown, (QStringList{"A001", "A002"}));
        QVERIFY(summary.startsWith(QStringLiteral("2 relinked")));
        const model::BinItem relinked = *bin->bin().item(ids.at(0));
        QVERIFY(!media::isUnlinked(relinked));
        QCOMPARE(relinked.fields.value(QStringLiteral("Source Path")),
                 QFileInfo(moved.filePath(QStringLiteral("Card A copy"))).absoluteFilePath());
        QVERIFY(near(composer.picture(MonitorId::Source, QSize(64, 36), &offline), colourOf(0)));

        // Clip Frames: each segment's start frame in the Timeline.
        composer.state(MonitorId::Source).setPosition(2);
        composer.state(MonitorId::Source).markIn();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        QAction* clipFrames = nullptr;
        QAction* defaultSetup = nullptr;
        for (QAction* a : window.timeline().fastMenu()->actions()) {
            if (a->text() == QStringLiteral("Clip Frames")) clipFrames = a;
            if (a->text() == QStringLiteral("Default Setup")) defaultSetup = a;
        }
        // Zoomed in, the segment is wide enough for its frame.
        QVERIFY(defaultSetup);
        defaultSetup->trigger();
        window.timeline().setScale(20, 0);
        QVERIFY(clipFrames && clipFrames->isEnabled() && clipFrames->isCheckable());
        clipFrames->setChecked(true);
        QVERIFY(window.timeline().display().clipFrames);
        window.timeline().view()->grab();
        MediaFrames::instance()->waitForThumbnails();
        QCoreApplication::processEvents();
        // The segment's start is frame 2 of the clip, at the row's size.
        QVERIFY(near(MediaFrames::instance()->thumbnail(relinked, 2, QSize(46, 26)), colourOf(2)));
    }
};

QTEST_MAIN(TestSourceBrowser)
#include "tst_source_browser.moc"

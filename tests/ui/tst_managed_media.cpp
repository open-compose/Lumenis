#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/consolidate_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/relink_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/source_browser.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

QColor colourOf(int n) {
    return QColor(40 + (n * 23) % 176, 40 + (175 - n * 11 % 176), 40 + (n * 57) % 176);
}

// A 25 fps QuickTime file of flat colours with stereo sound.
bool writeMovie(const QString& path, int frames) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.channels = 2;
    s.startTimecode = QStringLiteral("01:00:00:00");
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    const std::vector<float> sound(2 * 1920, 0.25f);
    for (int i = 0; i < frames; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(colourOf(i));
        if (!w.writeVideo(image) || !w.writeAudio(sound.data(), 1920)) return false;
    }
    return w.finish();
}

bool near(const QImage& image, const QColor& want) {
    if (image.isNull()) return false;
    const QColor got = image.pixelColor(image.width() / 2, image.height() / 2);
    const bool ok = std::abs(got.red() - want.red()) <= 14 && std::abs(got.green() - want.green()) <= 14 &&
                    std::abs(got.blue() - want.blue()) <= 14;
    if (!ok) qWarning() << "got" << got << "want" << want;
    return ok;
}

const model::BinItem* named(const BinDocument& doc, const QString& name) {
    for (const model::BinItem& item : doc.bin().items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

} // namespace

class TestManagedMedia : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void importConsolidateAndTheMediaDatabase() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Card"));
        QVERIFY(QDir().mkpath(source));
        QVERIFY(writeMovie(source + QStringLiteral("/A001.mov"), 12));
        QImage still(64, 36, QImage::Format_RGB32);
        still.fill(colourOf(5));
        QVERIFY(still.save(source + QStringLiteral("/Title.png")));
        // A folder standing in for a media drive.
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});

        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Harbor"));
        project.settings.name = QStringLiteral("Harbor");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("frameImportDuration"), 2);
        const media::MediaCreation creation = window.mediaCreation(QStringLiteral("import"));
        QCOMPARE(creation.videoDrive, QDir::cleanPath(drive));
        QCOMPARE(creation.raster, QSize(256, 144));
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // File > Input > Import Media: the Source Browser, importing
        // (p.213-214).
        commands.action(QStringLiteral("file.input.import_media"))->trigger();
        SourceBrowser& browser = window.sourceBrowser();
        QVERIFY(browser.isImportMode());
        QCOMPARE(browser.findChild<QPushButton*>(QStringLiteral("linkButton"))->text(), QStringLiteral("Import"));
        browser.setImportResolution(QStringLiteral("dnx_lb"));
        browser.setFolder(source);
        browser.waitForProbes();
        browser.selectPaths({source + QStringLiteral("/A001.mov"), source + QStringLiteral("/Title.png")});
        browser.linkSelection();
        QCOMPARE(bin->bin().items.size(), 2);
        const model::BinItem clip = *named(*bin, QStringLiteral("A001"));
        const auto link = media::linkOf(clip);
        QVERIFY(link && link->isManaged());
        QCOMPARE(clip.fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx LB"));
        QCOMPARE(clip.fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));
        QCOMPARE(clip.durationFrames, std::int64_t{12});
        QVERIFY(link->path.startsWith(QDir(drive).filePath(QStringLiteral("Avid MediaFiles/MXF/1"))));
        // A picture lasts the Frame Import Duration (p.1196).
        const model::BinItem title = *named(*bin, QStringLiteral("Title"));
        QCOMPARE(title.durationFrames, std::int64_t{50});
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(7);
        QVERIFY(near(composer.picture(MonitorId::Source, QSize(256, 144)), colourOf(7)));

        // Cut into a sequence, then Clip > Consolidate/Transcode on it
        // (p.353): a new master clip of the part used, with handles.
        composer.state(MonitorId::Source).setPosition(4);
        composer.state(MonitorId::Source).markIn();
        composer.state(MonitorId::Source).setPosition(7);
        composer.state(MonitorId::Source).markOut();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        const QString sequenceId = record->item.id;
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({sequenceId});
        window.setConsolidateHook([](ConsolidateDialog& dialog) {
            dialog.setTranscode(true);
            // Our drive is chosen, from the Media Creation setting.
            const QListWidgetItem* target = dialog.drives()->currentItem();
            if (!target || target->data(Qt::UserRole).toString().isEmpty()) return false;
            dialog.findChild<QSpinBox*>(QStringLiteral("handleLength"))->setValue(2);
            dialog.setResolution(QStringLiteral("dnx_sq"));
            return true;
        });
        commands.action(QStringLiteral("clip.consolidate_transcode"))->trigger();
        QVERIFY(named(*bin, QStringLiteral("A001.new.01")));
        const model::BinItem madeItem = *named(*bin, QStringLiteral("A001.new.01"));
        const model::BinItem* made = &madeItem;
        // Frames 4-7 used, 2 either side.
        QCOMPARE(made->durationFrames, std::int64_t{8});
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx SQ"));
        const model::Sequence after = model::Sequence::fromItem(*bin->bin().item(sequenceId));
        QCOMPARE(after.track(QStringLiteral("V1"))->segments.first().clipId, made->id);
        QCOMPARE(after.track(QStringLiteral("V1"))->segments.first().sourceIn, std::int64_t{2});

        // Consolidating a master clip asks how it links (p.352).
        view->selectIds({clip.id});
        bool asked = false;
        window.setConsolidateHook([](ConsolidateDialog& dialog) {
            dialog.setTranscode(false);
            return true;
        });
        window.setCopyingMediaHook([&asked]() -> std::optional<bool> {
            asked = true;
            return true;
        });
        commands.action(QStringLiteral("clip.consolidate_transcode"))->trigger();
        QVERIFY(asked);
        QVERIFY(named(*bin, QStringLiteral("A001.new.02")));

        // The media database (p.358): moved media is found when the
        // project's database is loaded again.
        const QString moved = QDir(drive).filePath(QStringLiteral("Avid MediaFiles/MXF/7"));
        QVERIFY(QDir().mkpath(moved));
        MediaFrames::instance()->clear();
        for (const QString& f : link->files()) QVERIFY(QFile::rename(f, QDir(moved).filePath(QFileInfo(f).fileName())));
        QVERIFY(MediaFrames::instance()->isOffline(*bin->bin().item(clip.id)));
        commands.action(QStringLiteral("file.media.load_media_database"))->trigger();
        QVERIFY(window.mediaDatabase().fileCount() >= 3);
        QVERIFY(!MediaFrames::instance()->isOffline(*bin->bin().item(clip.id)));
        QVERIFY(media::linkOf(*bin->bin().item(clip.id))->path.startsWith(moved));

        // The Media Creation setting's drives are menus of the drives.
        const model::SettingsFile& f = window.settings().file(model::SettingsScope::Project);
        const int i = f.activeIndex(QStringLiteral("Media Creation"));
        QVERIFY(i >= 0);
        SettingsWindow* settingsWindow = window.showSettings();
        QVERIFY(settingsWindow);
        SettingsList* list = settingsWindow->list(model::SettingsScope::Project);
        bool checked = false;
        list->dialogHook = [&](SettingsDialog& d) {
            auto* menu = qobject_cast<QComboBox*>(d.editor(QStringLiteral("importVideoDrive")));
            checked = menu && menu->currentData().toString() == QDir::cleanPath(drive);
            QTimer::singleShot(0, &d, &QDialog::reject);
        };
        list->openEntry(i);
        QVERIFY(checked);
        media::setExtraMediaDrives({});
    }

    void relinkDeleteAndTheBackgroundQueue() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A001.mov"));
        QVERIFY(writeMovie(movie, 12));
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Harbor"));
        project.settings.name = QStringLiteral("Harbor");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.importFiles(binId, {movie}, QStringLiteral("dnx_lb")).size(), 1);
        const QString clipId = bin->bin().items.first().id;
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);

        // Background Process (p.356): the transcode runs as a job in the
        // Background Queue Window; its clip comes when it is done.
        view->selectIds({clipId});
        window.setConsolidateHook([](ConsolidateDialog& dialog) {
            dialog.setTranscode(true);
            dialog.setResolution(QStringLiteral("dnx_sq"));
            dialog.setBackground(true);
            return true;
        });
        commands.action(QStringLiteral("clip.consolidate_transcode"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("BackgroundQueue"))));
        window.backgroundQueue().waitForIdle();
        QCOMPARE(window.backgroundQueue().jobs().size(), 1);
        QCOMPARE(window.backgroundQueue().jobs().first().state, BackgroundQueue::State::Completed);
        QVERIFY(named(*bin, QStringLiteral("A001.new.01")));
        // A copy: the bin's list changes below.
        const model::BinItem sqItem = *named(*bin, QStringLiteral("A001.new.01"));
        const model::BinItem* sq = &sqItem;
        QCOMPARE(sq->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx SQ"));
        window.backgroundQueue().clearInactive();
        QVERIFY(window.backgroundQueue().jobs().isEmpty());

        // Relink > Managed Media, Highest Quality: the LB clip takes the SQ
        // media (p.361-363).
        view->selectIds({clipId});
        window.setManagedRelinkHook([](ManagedRelinkDialog& dialog) {
            return dialog.allDrives()->isChecked() && dialog.check(QStringLiteral("newSequences"))->isChecked();
        });
        commands.action(QStringLiteral("clip.relink.managed_media"))->trigger();
        const model::BinItem relinked = *bin->bin().item(clipId);
        QCOMPARE(relinked.fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx SQ"));
        QCOMPARE(media::linkOf(relinked)->mediaId, media::linkOf(*sq)->mediaId);

        // The Media Tool shows the media files; Delete Media deletes the
        // tracks chosen (p.347-348).
        window.setMediaToolDisplayHook([](MediaToolDisplayDialog& d) {
            d.option(QStringLiteral("mediaFiles"))->setChecked(true);
            return true;
        });
        commands.action(QStringLiteral("tools.media_tool"))->trigger();
        MediaTool& tool = window.mediaTool();
        QVERIFY(tool.names().contains(QStringLiteral("A001.new.01")));
        QCOMPARE(tool.names().count(QStringLiteral("A001 V1")), 2);
        tool.selectNames({QStringLiteral("A001.new.01")});
        QStringList offered;
        tool.deleteMediaHook = [&offered](DeleteMediaDialog& d) {
            offered = QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")};
            for (const QString& t : offered) {
                if (!d.track(t)) offered.removeAll(t);
            }
            d.track(QStringLiteral("A1"))->setChecked(false);
            d.track(QStringLiteral("A2"))->setChecked(false);
            return true;
        };
        int confirmed = 0;
        tool.confirmHook = [&confirmed](int count) {
            confirmed = count;
            return true;
        };
        QVERIFY(tool.deleteSelected());
        QCOMPARE(offered, (QStringList{"V1", "A1", "A2"}));
        QCOMPARE(confirmed, 1);
        QCOMPARE(tool.names().count(QStringLiteral("A001 V1")), 1);
        QVERIFY(MediaFrames::instance()->isOffline(*bin->bin().item(clipId)));

        // Deleting the clip from the bin can delete its media files too
        // (p.359).
        view->selectIds({sq->id});
        int files = 0;
        window.setBinDeleteHook([&files](int, int count) -> std::optional<MainWindow::BinDeleteChoice> {
            files = count;
            return MainWindow::BinDeleteChoice{true, true};
        });
        const QStringList left = media::linkOf(*sq)->audioPaths;
        QCOMPARE(window.binContainer().deleteSelection(), 1);
        QCOMPARE(files, 3);
        for (const QString& f : left) QVERIFY(!QFile::exists(f));
        QVERIFY(!named(*bin, QStringLiteral("A001.new.01")));
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestManagedMedia)
#include "tst_managed_media.moc"

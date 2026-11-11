// Batch Import and Batch Re-import (9.2a; docs/spec/batch-import.md; Editing Guide p.233-237, p.1097-1098): which clips
// can be imported again, Set File Location's search beside the first file and at the same relative place, mixed-rate
// sequences refused, Offline only and All clips, the clips a sequence uses, Skip This Clip, the new media under the
// clip's MobID with the sequence playing them, and the clip's own Import settings or the current ones.
#include "lumenis/ext/provider.h"
#include "lumenis/media/batch_import.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/batch_import_dialog.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

bool writeMovie(const QString& path, QColor color, int frames = 10) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < frames; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(color);
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

bool touch(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write("x") == 1;
}

} // namespace

class TestBatchImport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // The rules without the application.
    void rules() {
        QTemporaryDir dir;
        // Set File Location: beside the first file, then at the same place relative to its folder (p.235).
        const QString first = dir.filePath(QStringLiteral("New/Day1/a.mov"));
        QVERIFY(touch(first));
        QVERIFY(touch(dir.filePath(QStringLiteral("New/Day1/b.mov"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("New/Day2/c.mov"))));
        const QString oldFirst = QStringLiteral("/Old/Shoot/Day1/a.mov");
        QCOMPARE(media::relocatedSource(oldFirst, first, QStringLiteral("/Elsewhere/b.mov")),
                 dir.filePath(QStringLiteral("New/Day1/b.mov")));
        QCOMPARE(media::relocatedSource(oldFirst, first, QStringLiteral("/Old/Shoot/Day2/c.mov")),
                 dir.filePath(QStringLiteral("New/Day2/c.mov")));
        QVERIFY(media::relocatedSource(oldFirst, first, QStringLiteral("/Old/Shoot/Day3/d.mov")).isEmpty());

        // Imported clips can be imported again; linked ones and those that name no file cannot.
        model::BinItem clip;
        clip.kind = model::BinItemKind::MasterClip;
        QVERIFY(!media::isReimportable(clip));
        clip.fields.insert(QStringLiteral("Source File"), QStringLiteral("a.mov"));
        clip.fields.insert(QStringLiteral("Source Path"), QStringLiteral("/Old/Shoot/Day1"));
        QVERIFY(media::isReimportable(clip)); // offline: no media at all
        QCOMPARE(media::lastImportPath(clip), QStringLiteral("/Old/Shoot/Day1/a.mov"));
        media::Link linked;
        linked.path = QStringLiteral("/Old/Shoot/Day1/a.mov");
        clip.link = linked.toJson();
        QVERIFY(!media::isReimportable(clip));
        linked.mediaId = QStringLiteral("managed");
        clip.link = linked.toJson();
        QVERIFY(media::isReimportable(clip));
        model::BinItem sequence;
        sequence.kind = model::BinItemKind::Sequence;
        QVERIFY(!media::isReimportable(sequence));

        // A sequence at another rate than the project's is mixed rate (p.234).
        sequence.editRate = Rational(24);
        QVERIFY(media::isMixedRateSequence(sequence, Rational(25)));
        sequence.editRate = Rational(25);
        QVERIFY(!media::isMixedRateSequence(sequence, Rational(25)));
    }

    void reimport() {
        QTemporaryDir dir;
        const QString shoot = dir.filePath(QStringLiteral("Shoot"));
        QVERIFY(QDir().mkpath(shoot));
        const QString harbor = QDir(shoot).filePath(QStringLiteral("Harbor.mov"));
        const QString bridge = QDir(shoot).filePath(QStringLiteral("Bridge.mov"));
        QVERIFY(writeMovie(harbor, Qt::darkBlue));
        QVERIFY(writeMovie(bridge, Qt::darkRed));
        const QString still = QDir(shoot).filePath(QStringLiteral("Logo.png"));
        QImage logo(64, 36, QImage::Format_RGB32);
        logo.fill(Qt::yellow);
        QVERIFY(logo.save(still));
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
        project.folder = dir.filePath(QStringLiteral("Reimport"));
        project.settings.name = QStringLiteral("Reimport");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        // A still lasts 2 seconds (p.1196).
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("frameImportDuration"), 2);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.importFiles(binId, {harbor, bridge, still});
        QCOMPARE(ids.size(), 3);
        const model::BinItem harborClip = *bin->bin().item(ids.at(0));
        QVERIFY(media::isReimportable(harborClip));
        QCOMPARE(media::lastImportPath(harborClip), harbor);
        QCOMPARE(bin->bin().item(ids.at(2))->durationFrames, std::int64_t(50));
        // Harbor edited into a sequence.
        Composer& composer = window.composer();
        composer.loadDefault(binId, harborClip);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        const QString sequenceId = record->item.id;
        BinView* view = window.binContainer().activeView();
        QVERIFY(view);

        // Harbor's media deleted: it is offline.
        const QString harborMedia = media::linkOf(harborClip)->path;
        QVERIFY(QFile::remove(harborMedia));
        QVERIFY(!QFileInfo::exists(harborMedia));

        // Offline only, from the sequence and Bridge: the clips missing their media, Harbor through the sequence.
        window.setBatchImportChoiceHook([] { return MainWindow::BatchImportChoice::OfflineOnly; });
        view->selectIds({sequenceId, ids.at(1)});
        QList<BatchImportDialog::Entry> listed = window.batchImportClips(true);
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().itemId, ids.at(0));
        QCOMPARE(listed.first().path, harbor);
        // All clips: Bridge too.
        listed = window.batchImportClips(false);
        QCOMPARE(listed.size(), 2);

        // The dialog: found clips, the caption, Skip This Clip.
        int dialogs = 0;
        window.setBatchImportHook([&](BatchImportDialog& dialog) {
            ++dialogs;
            if (dialogs == 1) {
                // Cancel the first time.
                return false;
            }
            if (dialogs == 2) {
                // Offline only: Harbor.
                if (dialog.caption() != QStringLiteral("1 of 1 clips found")) return false;
                return true;
            }
            return false;
        });
        QAction* reimport = commands.action(QStringLiteral("clip.batch_re_import"));
        QVERIFY(reimport);
        reimport->trigger();
        QCOMPARE(dialogs, 1);
        QVERIFY(!QFileInfo::exists(harborMedia));
        view->selectIds({sequenceId, ids.at(1)});
        QCOMPARE(window.batchImport(), 1);
        QCOMPARE(dialogs, 2);
        // Harbor has new media under its own MobID, and the sequence plays them.
        const model::BinItem again = *bin->bin().item(ids.at(0));
        QCOMPARE(again.mobId, harborClip.mobId);
        QCOMPARE(again.name, harborClip.name);
        const auto link = media::linkOf(again);
        QVERIFY(link && link->isManaged());
        QVERIFY(QFileInfo::exists(link->path));
        QVERIFY(link->path != harborMedia);
        QCOMPARE(again.durationFrames, std::int64_t(10));
        QVERIFY(window.composer().resolve(binId, ids.at(0)).has_value());
        {
            const model::Sequence seq = model::Sequence::fromItem(*bin->bin().item(sequenceId));
            const model::Track* v1 = seq.track(QStringLiteral("V1"));
            QVERIFY(v1 && !v1->segments.isEmpty());
            QCOMPARE(v1->segments.first().clipId, ids.at(0));
        }
        const QImage frame = composer.pictureAt(MonitorId::Record, 3, QSize(64, 36));
        QVERIFY(!frame.isNull());
        QVERIFY(QColor(frame.pixel(32, 18)).blue() > 100);

        // The sources moved: not found (red) until Set File Location finds the first, and the second beside it.
        const QString moved = dir.filePath(QStringLiteral("Moved"));
        QVERIFY(QDir().rename(shoot, moved));
        view->selectIds({ids.at(0), ids.at(1)});
        listed = window.batchImportClips(false);
        QCOMPARE(listed.size(), 2);
        QVERIFY(listed.at(0).path.isEmpty() && listed.at(1).path.isEmpty());
        {
            BatchImportDialog dialog(listed);
            QCOMPARE(dialog.caption(), QStringLiteral("0 of 2 clips found"));
            dialog.list()->topLevelItem(0)->setSelected(true);
            dialog.list()->topLevelItem(1)->setSelected(true);
            dialog.setLocateHook([&](const QString& last) {
                return last == harbor ? QDir(moved).filePath(QStringLiteral("Harbor.mov")) : QString();
            });
            auto* setLocation = dialog.findChild<QPushButton*>(QStringLiteral("setFileLocation"));
            QVERIFY(setLocation);
            setLocation->click();
            QCOMPARE(dialog.foundCount(), 2);
            QCOMPARE(dialog.entries().at(1).path, QDir(moved).filePath(QStringLiteral("Bridge.mov")));
            // Skip This Clip takes Bridge out.
            dialog.list()->clearSelection();
            dialog.list()->topLevelItem(1)->setSelected(true);
            dialog.skipSelected();
            QCOMPARE(dialog.entries().size(), 1);
            QCOMPARE(dialog.caption(), QStringLiteral("1 of 1 clips found"));
            // Imported again from where it is now; its columns say so.
            QCOMPARE(window.runBatchImport(dialog), 1);
            QCOMPARE(bin->bin().item(ids.at(0))->fields.value(QStringLiteral("Source Path")), moved);
        }

        // The still: its own Import settings (2 seconds) unless overridden by the current ones (4 seconds).
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("frameImportDuration"), 4);
        view->selectIds({ids.at(2)});
        listed = window.batchImportClips(false);
        QCOMPARE(listed.size(), 1);
        listed.first().path = QDir(moved).filePath(QStringLiteral("Logo.png"));
        {
            BatchImportDialog dialog(listed);
            QCOMPARE(window.runBatchImport(dialog), 1);
            QCOMPARE(bin->bin().item(ids.at(2))->durationFrames, std::int64_t(50));
            dialog.setOverrideSettings(true);
            QCOMPARE(window.runBatchImport(dialog), 1);
            QCOMPARE(bin->bin().item(ids.at(2))->durationFrames, std::int64_t(100));
        }
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestBatchImport)
#include "tst_batch_import.moc"

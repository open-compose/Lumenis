#include "lumenis/ext/provider.h"
#include "lumenis/media/consolidate.h"
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
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/mixdown_dialog.h"
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
#include <QLineEdit>
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

    // Timeline > Mixdown > Audio > To Sequence (p.731-732): A1 and A2 of a cut, mixed down Mono into
    // a new track, with the premix sequence saved first; one undo step.
    void audioMixdownIntoTheSequence() {
        QTemporaryDir dir;
        QVERIFY(writeMovie(dir.filePath(QStringLiteral("Tone.mov")), 12));
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
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {dir.filePath(QStringLiteral("Tone.mov"))});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        const QString sequenceId = record->item.id;
        // Frames 2 to 9 marked.
        model::MonitorState& marks = composer.state(MonitorId::Record);
        marks.setPosition(2);
        marks.markIn();
        marks.setPosition(9);
        marks.markOut();
        const int before = window.timeline().history().undoCount();

        window.setMixdownHook([&](AudioMixdownDialog& d) {
            d.setFormat(media::MixdownFormat::Mono);
            // A1 and A2 are mixed, and no other audio track is empty: a new track.
            if (d.targetTracks() != QStringList{}) return false;
            d.setSavePremixSequence(true);
            return true;
        });
        commands.action(QStringLiteral("timeline.mixdown.audio.to_sequence"))->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(window.timeline().history().undoCount(), before + 1);
        // The premix copy and the mixdown clip are in the bin.
        QCOMPARE(bin->bin().items.size(), 4);
        const model::BinItem* mix = named(*bin, QStringLiteral("Untitled Sequence.01 Mixdown"));
        QVERIFY(mix);
        QCOMPARE(mix->kind, model::BinItemKind::AudioClip);
        QCOMPARE(mix->durationFrames, std::int64_t{8});
        // A3 holds it from frame 2.
        const model::Sequence seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        const model::Track* a3 = seq.track(QStringLiteral("A3"));
        QVERIFY(a3);
        std::int64_t start = 0;
        const int i = a3->segmentAt(2, &start);
        QVERIFY(i >= 0);
        QCOMPARE(start, std::int64_t{2});
        QCOMPARE(a3->segments.at(i).clipId, mix->id);
        QCOMPARE(a3->segments.at(i).length, std::int64_t{8});
        // Mono: both channels at their clip gain (0 dB), 0.25 each.
        const auto link = media::linkOf(*mix);
        QVERIFY(link && link->isManaged());
        media::AudioReader reader;
        QVERIFY(reader.open(link->audioPaths.first()));
        const std::vector<float> got = reader.samples(100, 100);
        QCOMPARE(got.size(), std::size_t{100});
        for (float v : got) QVERIFY(std::abs(v - 0.5f) < 1e-3f);
        // One undo takes it out of the sequence again.
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        QVERIFY(!model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("A3")));
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, sequenceId);
        media::setExtraMediaDrives({});
    }

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

    // Consolidate with the master clip taking the copy (p.349): the clip
    // keeps its MobID, and the copy's files name it as their material
    // package; ".old.01" is a new clip with the media the clip had.
    void consolidateRelinksTheClip() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A001.mov"));
        QVERIFY(writeMovie(movie, 6));
        const QString drive = dir.filePath(QStringLiteral("Media"));
        const QString drive2 = dir.filePath(QStringLiteral("Media 2"));
        QVERIFY(QDir().mkpath(drive) && QDir().mkpath(drive2));
        media::LinkOptions o;
        o.editRate = Rational(25);
        const auto linked = media::linkClip(*media::probe(movie), o);
        QVERIFY(linked);
        media::MediaCreation c;
        c.raster = QSize(256, 144);
        c.videoDrive = drive;
        c.resolution = *media::findResolution(QStringLiteral("dnx_lb"));
        QString error;
        const auto managed = media::transcodeClip(*linked, 0, -1, c, &error);
        QVERIFY2(managed, qPrintable(error));

        media::ConsolidateOptions options;
        options.keepMastersLinked = false;
        options.creation = c;
        options.creation.videoDrive = drive2;
        const media::ConsolidateResult result = media::consolidate({*managed}, QStringLiteral("b"), {}, {}, options);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.changed.size(), 1);
        QCOMPARE(result.added.size(), 1);
        const model::BinItem& clip = result.changed.first();
        QCOMPARE(clip.id, managed->id);
        QCOMPARE(clip.mobId, managed->mobId);
        const auto link = media::linkOf(clip);
        QVERIFY(link && link->path.startsWith(QDir(drive2).absolutePath()));
        int files = 0;
        for (const model::ClipSource& s : clip.sources) {
            if (s.kind != model::ClipSource::Kind::File) continue;
            ++files;
            QVERIFY(link->files().contains(s.path));
            const auto info = media::readMediaFile(s.path);
            QVERIFY(info);
            QCOMPARE(info->material, clip.mobId);
        }
        QCOMPARE(files, 3);
        const model::BinItem& old = result.added.first();
        QCOMPARE(old.name, QStringLiteral("A001.old.01"));
        QVERIFY(old.mobId != clip.mobId);
        QCOMPARE(old.sources, managed->sources);
        QCOMPARE(media::linkOf(old)->path, media::linkOf(*managed)->path);
        // The media database finds the clip's new media by its MobID.
        media::MediaDatabase db;
        db.scan({drive2});
        const auto set = db.setOf(clip.mobId);
        QVERIFY(set);
        QCOMPARE(set->mediaId, link->mediaId);
    }

    // File > Output > Export to File as AAF (p.868-869, p.1175), and the
    // AAF imported into another bin (p.547): the clips come back with
    // their media online, found by MobID, and the sequence names them.
    void exportAndImportAaf() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A002.mov"));
        QVERIFY(writeMovie(movie, 10));
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
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.importFiles(binId, {movie}, QStringLiteral("dnx_lb")).size(), 1);
        const model::BinItem clip = bin->bin().items.first();
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(2);
        composer.state(MonitorId::Source).markIn();
        composer.state(MonitorId::Source).setPosition(6);
        composer.state(MonitorId::Source).markOut();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        const QString sequenceId = record->item.id;
        window.binContainer().saveAll();

        // Only the sequence selected: its clip goes with it.
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({sequenceId});
        const QString aaf = dir.filePath(QStringLiteral("Cut.aaf"));
        QString proposed;
        window.setExportHook([&](ExportAsDialog& dialog) -> bool {
            proposed = dialog.pathEdit()->text();
            dialog.setPath(aaf);
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY2(proposed.endsWith(QLatin1String(".aaf")), qPrintable(proposed));
        QVERIFY(QFile::exists(aaf));

        // Imported into a new bin, through the Source Browser's Import.
        const QString otherId = window.binContainer().newBin();
        BinDocument* other = window.binContainer().openBin(otherId);
        QVERIFY(other);
        const QStringList ids = window.importFiles(otherId, {aaf});
        QCOMPARE(ids.size(), 2);
        const model::BinItem* back = named(*other, clip.name);
        QVERIFY(back);
        QCOMPARE(back->mobId, model::mobIdOf(clip));
        const auto link = media::linkOf(*back);
        QVERIFY2(link && link->isManaged(), "the imported clip finds its managed media by MobID");
        const model::BinItem* sequence = nullptr;
        for (const model::BinItem& item : other->bin().items) {
            if (item.kind == model::BinItemKind::Sequence) sequence = &item;
        }
        QVERIFY(sequence);
        const model::Sequence cut = model::Sequence::fromItem(*sequence);
        const model::Track* v1 = cut.track(QStringLiteral("V1"));
        QVERIFY(v1 && !v1->segments.isEmpty());
        QCOMPARE(v1->segments.first().clipId, back->id);
        QCOMPARE(v1->segments.first().binId, otherId);
        QCOMPARE(v1->segments.first().sourceIn, std::int64_t{2});
        QCOMPARE(v1->segments.first().length, std::int64_t{5}); // OUT is the last frame used
        QVERIFY(cut.track(QStringLiteral("A1")));

        // Include All Audio Tracks in Sequence off (p.1175): no audio.
        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("includeAudio"), false);
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        const QString third = window.binContainer().newBin();
        QVERIFY(window.binContainer().openBin(third));
        window.importFiles(third, {aaf});
        for (const model::BinItem& item : window.binContainer().document(third)->bin().items) {
            if (item.kind == model::BinItemKind::Sequence) QVERIFY(!model::Sequence::fromItem(item).track(QStringLiteral("A1")));
        }
        media::setExtraMediaDrives({});
    }

    // File > Output > Export to File with the Export setting's Export As at
    // Media Composer Bin (ours, docs/spec/avb.md "Export"): the sequence is
    // written as a .avb with its clip as a reference clip, and File > Open
    // Bin of that file (p.296) lists it under Other Bins with the clip online
    // (its managed media found by MobID) and the sequence naming it.
    void exportAsMediaComposerBin() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A003.mov"));
        QVERIFY(writeMovie(movie, 10));
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
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.importFiles(binId, {movie}, QStringLiteral("dnx_lb")).size(), 1);
        const model::BinItem clip = bin->bin().items.first();
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(1);
        composer.state(MonitorId::Source).markIn();
        composer.state(MonitorId::Source).setPosition(7);
        composer.state(MonitorId::Source).markOut();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        const QString sequenceId = record->item.id;
        const QString sequenceName = record->item.name;
        window.binContainer().saveAll();

        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("avb"));
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({sequenceId});
        const QString avb = dir.filePath(QStringLiteral("Selects.avb"));
        QString proposed;
        window.setExportHook([&](ExportAsDialog& dialog) -> bool {
            proposed = dialog.pathEdit()->text();
            dialog.setPath(dir.filePath(QStringLiteral("Selects"))); // the extension is added
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY2(proposed.endsWith(QLatin1String(".avb")), qPrintable(proposed));
        QVERIFY(QFile::exists(avb));

        QString error;
        const QString otherId = window.openBinFile(avb, &error);
        QVERIFY2(!otherId.isEmpty(), qPrintable(error));
        BinDocument* other = window.binContainer().document(otherId);
        QVERIFY(other);
        QCOMPARE(other->bin().items.size(), 2);
        const model::BinItem* back = named(*other, clip.name);
        QVERIFY(back);
        QCOMPARE(back->mobId, model::mobIdOf(clip));
        QVERIFY(!back->userPlaced); // a reference clip: only the sequence was selected
        QCOMPARE(back->sources.size(), clip.sources.size());
        const auto link = media::linkOf(*back);
        QVERIFY2(link && link->isManaged(), "the clip read back finds its managed media by MobID");
        const model::BinItem* sequence = named(*other, sequenceName);
        QVERIFY(sequence);
        QCOMPARE(sequence->kind, model::BinItemKind::Sequence);
        const model::Sequence cut = model::Sequence::fromItem(*sequence);
        const model::Track* v1 = cut.track(QStringLiteral("V1"));
        QVERIFY(v1 && !v1->segments.isEmpty());
        QCOMPARE(v1->segments.first().clipId, back->id);
        QCOMPARE(v1->segments.first().sourceIn, std::int64_t{1});
        QCOMPARE(v1->segments.first().length, std::int64_t{7});
        QVERIFY(cut.track(QStringLiteral("A1")));
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

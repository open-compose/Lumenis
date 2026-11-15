#include "lumenis/ext/provider.h"
#include "lumenis/interop/aaf_essence.h"
#include "lumenis/interop/aaf_import.h"
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
#include "lumenis/ui/multi_mix_dialog.h"
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
#include <QStandardItemModel>
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

    // Timeline > Mixdown > Audio > Multiple Mixes (p.732-734): a stereo mix of A1 and A2 and a mono
    // mix of A1, between the marks, into a new sequence with the video copied; the setting saved.
    void multipleMixes() {
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
        model::MonitorState& marks = composer.state(MonitorId::Record);
        marks.setPosition(2);
        marks.markIn();
        marks.setPosition(9);
        marks.markOut();
        const QString sequenceName = composer.state(MonitorId::Record).current()->item.name;
        const int items = static_cast<int>(bin->bin().items.size());
        window.setMultiMixHook([&](MultiMixDialog& d) {
            d.nameAsk = [] { return std::optional(QStringLiteral("Stems")); };
            media::MultiMix m = d.mix(); // one stereo output of A1 and A2 to start
            if (m.outputs.size() != 1 || m.outputs.first().inputs != QStringList{"A1", "A2"}) return false;
            d.addOutput();
            d.setFormat(1, media::MixdownFormat::Mono);
            d.toggleInput(1, QStringLiteral("A1"));
            d.addOutput(); // track 3: Dual Mono, two tracks
            d.setFormat(2, media::MixdownFormat::DualMono);
            d.toggleInput(2, QStringLiteral("A2"));
            if (!d.saveSettingAs()) return false;
            return d.mixButton()->isEnabled();
        });
        commands.action(QStringLiteral("timeline.mixdown.audio.multiple_mixes"))->trigger();
        QCoreApplication::processEvents();
        // Three clips and the new sequence.
        QCOMPARE(static_cast<int>(bin->bin().items.size()), items + 4);
        const model::BinItem* stereo = named(*bin, sequenceName + QStringLiteral(" Mix 1"));
        const model::BinItem* mono = named(*bin, sequenceName + QStringLiteral(" Mix 2"));
        QVERIFY(stereo && mono);
        QCOMPARE(stereo->durationFrames, std::int64_t{8});
        QCOMPARE(stereo->fields.value(QStringLiteral("Track Formats")), QStringLiteral("Stereo A1A2"));
        // The mono mix is A1 alone at its clip gain: 0.25.
        const auto link = media::linkOf(*mono);
        QVERIFY(link);
        media::AudioReader reader;
        QVERIFY(reader.open(link->audioPaths.first()));
        const std::vector<float> got = reader.samples(100, 50);
        QCOMPARE(got.size(), std::size_t{50});
        for (float v : got) QVERIFY(std::abs(v - 0.25f) < 1e-3f);
        const model::BinItem* made = named(*bin, sequenceName + QStringLiteral(".MultiMix"));
        QVERIFY(made);
        const model::Sequence seq = model::Sequence::fromItem(*made);
        QVERIFY(seq.track(QStringLiteral("V1")));
        const model::Track* a1 = seq.track(QStringLiteral("A1"));
        const model::Track* a2 = seq.track(QStringLiteral("A2"));
        QVERIFY(a1 && a2);
        QCOMPARE(a1->format, model::AudioFormat::Stereo);
        QCOMPARE(a2->format, model::AudioFormat::Mono);
        std::int64_t start = 0;
        const int i = a2->segmentAt(2, &start);
        QVERIFY(i >= 0);
        QCOMPARE(start, std::int64_t{2});
        QCOMPARE(a2->segments.at(i).clipId, mono->id);
        // Dual Mono: two mono tracks, panned left and right (p.734).
        const model::BinItem* dual = named(*bin, sequenceName + QStringLiteral(" Mix 3"));
        QVERIFY(dual);
        const model::Track* a3 = seq.track(QStringLiteral("A3"));
        const model::Track* a4 = seq.track(QStringLiteral("A4"));
        QVERIFY(a3 && a4);
        QCOMPARE(a3->format, model::AudioFormat::Mono);
        QCOMPARE(a4->format, model::AudioFormat::Mono);
        const int l = a3->segmentAt(2);
        const int r = a4->segmentAt(2);
        QVERIFY(l >= 0 && r >= 0);
        QCOMPARE(a3->segments.at(l).clipId, dual->id);
        QCOMPARE(a3->segments.at(l).sourceTrack, QStringLiteral("A1"));
        QCOMPARE(a4->segments.at(r).sourceTrack, QStringLiteral("A2"));
        QVERIFY(a3->segments.at(l).hasPan && a3->segments.at(l).pan < 0);
        QVERIFY(a4->segments.at(r).hasPan && a4->segments.at(r).pan > 0);
        // The setting is a Project setting now.
        QVERIFY(window.savedMultiMixes().contains(QStringLiteral("Stems")));
        QCOMPARE(window.savedMultiMixes().value(QStringLiteral("Stems")).outputs.size(), 3);
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

    // AAF export's Export Methods (7.1c1, p.870-872, p.1175-1177): Copy All Media copies the clip's files next to the
    // AAF and the AAF names the copies; Consolidate Media makes new media of the used part with its handles; Video
    // Mixdown replaces the video tracks with one "Video Mixdown" track of a new clip.
    // Media embedded in AAF (docs/spec/aaf.md, "Embedded media"): Media Destinations' Embedded in AAF puts the media the
    // export methods copy into the AAF file itself (p.871), nothing beside it; imported where that media is not, the
    // clips get managed media of their own, at the OMFI/AAF tab's Resolution (p.1197), and come online with the same
    // pictures and sound.
    // The Consolidate/Transcode dialog's options of 11.4a1 (p.351-356) map onto media::ConsolidateOptions; Relink before
    // skipping appears with Skip media files, and the sample rate menu follows Convert Audio Sample Rate.
    // The Relink dialog's options of 11.4a3 (p.362-364): every Timecode entry, the Source Name columns but the two not
    // made, Relink to's formats, Allow relinking to offline items and Tracks to Relink map onto media::RelinkOptions.
    void relinkDialogOptions() {
        ManagedRelinkDialog dialog(QStringLiteral("Harbor"));
        dialog.setProjectFormat(QSize(1920, 1080), Rational(25));
        auto enabled = [](QComboBox* menu, int i) {
            auto* model = qobject_cast<QStandardItemModel*>(menu->model());
            return model && model->item(i)->isEnabled();
        };
        QComboBox* timecode = dialog.menu(QStringLiteral("relinkTimecode"));
        QComboBox* source = dialog.menu(QStringLiteral("relinkSourceName"));
        QComboBox* to = dialog.menu(QStringLiteral("relinkTo"));
        QVERIFY(timecode && source && to);
        for (int i = 0; i < timecode->count(); ++i) QVERIFY(enabled(timecode, i));
        for (int i = 0; i < to->count(); ++i) QVERIFY(enabled(to, i));
        QVERIFY(!enabled(source, 2) && !enabled(source, 3)); // Source File ID and Keynumber
        QVERIFY(enabled(source, 4) && enabled(source, 6));
        QVERIFY(dialog.check(QStringLiteral("offlineItems"))->isEnabled());

        timecode->setCurrentIndex(3);
        source->setCurrentIndex(5);
        to->setCurrentIndex(1);
        dialog.check(QStringLiteral("offlineItems"))->setChecked(true);
        dialog.check(QStringLiteral("relinkAudio"))->setChecked(false);
        media::RelinkOptions o = dialog.options();
        QCOMPARE(o.timecode, QStringLiteral("Auxiliary TC3"));
        QCOMPARE(o.by, media::RelinkOptions::By::Column);
        QCOMPARE(o.column, QStringLiteral("Labroll"));
        QCOMPARE(o.format, media::RelinkOptions::Format::Project);
        QCOMPARE(o.projectRaster, QSize(1920, 1080));
        QVERIFY(o.offlineItems && o.relinkVideo && !o.relinkAudio);
        timecode->setCurrentIndex(6);
        source->setCurrentIndex(1);
        o = dialog.options();
        QCOMPARE(o.timecode, QStringLiteral("Sound TC"));
        QCOMPARE(o.by, media::RelinkOptions::By::Name);
    }

    void consolidateDialogOptions() {
        ConsolidateDialog dialog(Rational(25), true);
        dialog.show();
        auto* deleteOriginals = dialog.findChild<QCheckBox*>(QStringLiteral("deleteOriginals"));
        auto* skip = dialog.findChild<QCheckBox*>(QStringLiteral("skipOnTarget"));
        auto* relinkFirst = dialog.findChild<QCheckBox*>(QStringLiteral("relinkBeforeSkipping"));
        QVERIFY(deleteOriginals && skip && relinkFirst);
        QVERIFY(deleteOriginals->isEnabled());
        QVERIFY(!relinkFirst->isVisible());
        skip->setChecked(true);
        QVERIFY(relinkFirst->isVisible());
        relinkFirst->setChecked(true);
        deleteOriginals->setChecked(true);
        auto* groups = dialog.findChild<QCheckBox*>(QStringLiteral("groupEdits"));
        QVERIFY(groups && groups->isEnabled());
        groups->setChecked(true);
        media::ConsolidateOptions o = dialog.options();
        QVERIFY(o.skipOnTarget && o.relinkBeforeSkipping && o.deleteOriginals && o.groupEdits);
        QVERIFY(!o.convertToProjectRate && !o.applyFraming);

        dialog.setTranscode(true);
        dialog.setSampleRate(44100);
        auto* convert = dialog.findChild<QRadioButton*>(QStringLiteral("convertProjectRate"));
        auto* keep = dialog.findChild<QRadioButton*>(QStringLiteral("keepSourceRate"));
        auto* framing = dialog.findChild<QCheckBox*>(QStringLiteral("applyFraming"));
        auto* convertRate = dialog.findChild<QCheckBox*>(QStringLiteral("convertSampleRate"));
        auto* rate = dialog.findChild<QComboBox*>(QStringLiteral("targetSampleRate"));
        QVERIFY(convert && keep && framing && convertRate && rate);
        QVERIFY(keep->isChecked());
        convert->setChecked(true);
        framing->setChecked(true);
        o = dialog.options();
        QVERIFY(o.convertToProjectRate && o.applyFraming && o.convertSampleRate);
        QCOMPARE(o.creation.sampleRate, 44100);
        // Consolidate's own options do not reach a transcode.
        QVERIFY(!o.deleteOriginals && !o.skipOnTarget && !o.groupEdits);
        convertRate->setChecked(false);
        QVERIFY(!rate->isEnabled());
        QVERIFY(!dialog.options().convertSampleRate);
    }

    void aafEmbeddedMedia() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A005.mov"));
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
        Settings& settings = window.settings();
        settings.setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        settings.setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.importFiles(binId, {movie}, QStringLiteral("dnx_lb")).size(), 1);
        const model::BinItem clip = bin->bin().items.first();
        window.binContainer().saveAll();
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds({clip.id});

        // Exported with Copy All Media, embedded: the AAF holds the picture's DNx frames and each channel as PCM.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("aaf"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExportMethod"), QStringLiteral("copy"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExportMethod"), QStringLiteral("copy"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("mediaDestination"), QStringLiteral("embedded"));
        const QString folder = dir.filePath(QStringLiteral("Out"));
        QDir().mkpath(folder);
        const QString aaf = QDir(folder).filePath(QStringLiteral("A005.aaf"));
        window.setExportHook([aaf](ExportAsDialog& dialog) {
            dialog.setPath(aaf);
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY(QFileInfo::exists(aaf));
        QCOMPARE(QDir(folder).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot), QStringList{QStringLiteral("A005.aaf")});
        {
            interop::aaf::File file;
            QVERIFY(file.open(aaf));
            const QList<interop::aaf::Essence> essence = interop::aaf::embeddedEssence(file);
            QCOMPARE(essence.size(), 3);
            int dnx = 0, pcm = 0;
            for (const interop::aaf::Essence& e : essence) {
                if (e.kind == interop::aaf::Essence::Kind::Dnx) {
                    ++dnx;
                    QCOMPARE(e.length, std::int64_t{10});
                    QCOMPARE(e.rate, Rational(25));
                } else if (e.kind == interop::aaf::Essence::Kind::Pcm) {
                    ++pcm;
                    QCOMPARE(e.channels, 1);
                    QCOMPARE(e.sampleRate, 48000);
                    QCOMPARE(e.length, std::int64_t{19200});
                }
            }
            QCOMPARE(dnx, 1);
            QCOMPARE(pcm, 2);
        }

        // Imported where its media is not (another drive): the clip's media read out, the source's resolution kept,
        // its own MobID in the files.
        auto importOn = [&](const QString& name) -> std::optional<model::BinItem> {
            const QString other = dir.filePath(name);
            QDir().mkpath(other);
            media::setExtraMediaDrives({other});
            settings.setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), other);
            settings.setValue(QStringLiteral("Media Creation"), QStringLiteral("importAudioDrive"), other);
            const QString into = window.binContainer().newBin();
            BinDocument* doc = window.binContainer().openBin(into);
            if (!doc) return std::nullopt;
            window.importAaf(into, {aaf});
            for (const model::BinItem& i : doc->bin().items) {
                if (i.kind == model::BinItemKind::MasterClip) return i;
            }
            return std::nullopt;
        };
        auto check = [&](const model::BinItem& got, const QString& drive2) {
            const auto link = media::linkOf(got);
            QVERIFY(link && link->isManaged());
            QVERIFY(QFileInfo(link->path).absoluteFilePath().startsWith(QDir(drive2).absolutePath()));
            QCOMPARE(got.mobId, clip.mobId);
            QCOMPARE(got.durationFrames, std::int64_t{10});
            media::VideoReader video;
            QVERIFY(video.open(link->path));
            for (int f : {0, 4, 9}) QVERIFY(near(video.frame(f), colourOf(f)));
            QCOMPARE(link->audioPaths.size(), 2);
            media::AudioReader sound;
            QVERIFY(sound.open(link->audioFile(1)));
            const std::vector<float> s = sound.samples(1000, 10);
            QVERIFY(!s.empty());
            QVERIFY(std::abs(s.front() - 0.25f) < 1e-3f);
        };
        settings.setValue(QStringLiteral("Import"), QStringLiteral("omfResolution"), QStringLiteral("source"));
        auto kept = importOn(QStringLiteral("Media2"));
        QVERIFY(kept);
        check(*kept, dir.filePath(QStringLiteral("Media2")));
        QCOMPARE(kept->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx LB"));

        // "Use the current import resolution": made anew at the Media Creation setting's Import resolution.
        settings.setValue(QStringLiteral("Media Creation"), QStringLiteral("importResolution"), QStringLiteral("dnx_sq"));
        settings.setValue(QStringLiteral("Import"), QStringLiteral("omfResolution"), QStringLiteral("current"));
        auto current = importOn(QStringLiteral("Media3"));
        QVERIFY(current);
        check(*current, dir.filePath(QStringLiteral("Media3")));
        QCOMPARE(current->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx SQ"));

        // "Ask me ..." asks for a file whose resolution differs, naming both; the answer decides.
        settings.setValue(QStringLiteral("Import"), QStringLiteral("omfResolution"), QStringLiteral("ask"));
        QStringList asked;
        window.setEmbeddedResolutionHook([&](const model::BinItem& c, const QString& source, const QString& now) {
            asked << c.name << source << now;
            return true;
        });
        auto answered = importOn(QStringLiteral("Media4"));
        QVERIFY(answered);
        QCOMPARE(asked, (QStringList{clip.name, QStringLiteral("Avid DNx LB"), QStringLiteral("Avid DNx SQ")}));
        QCOMPARE(answered->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx LB"));

        // Imported again where its media already is, nothing is read out: the clip finds it by MobID.
        asked.clear();
        auto again = importOn(QStringLiteral("Media4"));
        QVERIFY(again);
        QVERIFY(asked.isEmpty());
        QVERIFY(media::linkOf(*again));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("mediaDestination"), QStringLiteral("folder"));
        media::setExtraMediaDrives({});
    }

    void aafExportMethods() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A004.mov"));
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
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
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
        const QString sequenceId = composer.state(MonitorId::Record).current()->item.id;
        const QString sequenceName = composer.state(MonitorId::Record).current()->item.name;
        window.binContainer().saveAll();
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds({sequenceId});
        Settings& settings = window.settings();
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("aaf"));
        auto exportTo = [&](const QString& aaf) {
            QDir().mkpath(QFileInfo(aaf).absolutePath());
            window.setExportHook([aaf](ExportAsDialog& dialog) {
                dialog.setPath(aaf);
                return true;
            });
            commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
            return interop::aaf::importFile(aaf);
        };
        auto masterIn = [](const interop::aaf::ImportResult& r) -> const model::BinItem* {
            for (const model::BinItem& i : r.items) {
                if (i.kind == model::BinItemKind::MasterClip) return &i;
            }
            return nullptr;
        };
        auto filesUnder = [](const model::BinItem& item, const QString& folder) {
            int n = 0;
            for (const model::ClipSource& src : item.sources) {
                if (src.kind != model::ClipSource::Kind::File) continue;
                if (!QFileInfo(src.path).absoluteFilePath().startsWith(QDir(folder).absolutePath()) || !QFileInfo::exists(src.path)) return -1;
                ++n;
            }
            return n;
        };

        // Copy All Media into the AAF's folder: the clip's video and audio files.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExportMethod"), QStringLiteral("copy"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExportMethod"), QStringLiteral("copy"));
        const QString copyFolder = dir.filePath(QStringLiteral("Copy"));
        auto copied = exportTo(QDir(copyFolder).filePath(QStringLiteral("Cut.aaf")));
        QVERIFY(copied);
        const model::BinItem* m = masterIn(*copied);
        QVERIFY(m);
        QVERIFY2(filesUnder(*m, copyFolder) >= 2, "the clip's files are the copies");

        // Consolidate Media with handles of 1: the five frames used and a frame either side.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExportMethod"), QStringLiteral("consolidate"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExportMethod"), QStringLiteral("consolidate"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoHandles"), 1);
        const QString consolidated = dir.filePath(QStringLiteral("Consolidated"));
        auto made = exportTo(QDir(consolidated).filePath(QStringLiteral("Cut.aaf")));
        QVERIFY(made);
        m = masterIn(*made);
        QVERIFY(m);
        QCOMPARE(m->name, clip.name);
        QCOMPARE(m->durationFrames, std::int64_t{7});
        QVERIFY(filesUnder(*m, consolidated) >= 2);

        // Video Mixdown: one video track, "Video Mixdown", of a new clip in the folder; the audio linked.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExportMethod"), QStringLiteral("mixdown"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExportMethod"), QStringLiteral("link"));
        const QString mixed = dir.filePath(QStringLiteral("Mixdown"));
        auto mix = exportTo(QDir(mixed).filePath(QStringLiteral("Cut.aaf")));
        QVERIFY(mix);
        const model::BinItem* mixClip = nullptr;
        const model::BinItem* seqItem = nullptr;
        for (const model::BinItem& i : mix->items) {
            if (i.name == sequenceName + QStringLiteral(" Video Mixdown")) mixClip = &i;
            if (i.kind == model::BinItemKind::Sequence) seqItem = &i;
        }
        QVERIFY(mixClip && seqItem);
        QVERIFY(filesUnder(*mixClip, mixed) >= 1);
        const model::Sequence cut = model::Sequence::fromItem(*seqItem);
        int videoTracks = 0;
        for (const model::Track& t : cut.tracks()) videoTracks += t.kind == model::TrackKind::Video ? 1 : 0;
        QCOMPARE(videoTracks, 1);
        QCOMPARE(cut.track(QStringLiteral("V1"))->segments.first().length, std::int64_t{5});
        QVERIFY(cut.track(QStringLiteral("A1")));

        // Export Media Difference Between Sequences (p.885-886): Copy All Media, excluding the media a copy of the
        // sequence uses, copies nothing; the clip keeps its own files.
        model::BinItem previous = model::duplicateItem(bin->bin(), composer.state(MonitorId::Record).current()->item,
                                                       QDateTime::currentDateTimeUtc());
        previous.name = QStringLiteral("Previous Cut");
        bin->addItem(previous);
        view->selectIds({sequenceId});
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExportMethod"), QStringLiteral("copy"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExportMethod"), QStringLiteral("copy"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExcludePrevious"), true);
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExcludePrevious"), true);
        QStringList offered;
        window.setExcludeSequenceHook([&](const QList<model::BinItem>& sequences) {
            for (const model::BinItem& i : sequences) offered << i.name;
            return static_cast<int>(offered.indexOf(QStringLiteral("Previous Cut")));
        });
        const QString difference = dir.filePath(QStringLiteral("Difference"));
        auto diff = exportTo(QDir(difference).filePath(QStringLiteral("Cut.aaf")));
        QVERIFY(diff);
        QVERIFY(offered.contains(QStringLiteral("Previous Cut")));
        QVERIFY(!offered.contains(sequenceName)); // not the one exported
        m = masterIn(*diff);
        QVERIFY(m);
        QCOMPARE(filesUnder(*m, difference), -1); // still the original files
        QVERIFY(QDir(difference).entryList(QDir::Files).filter(QStringLiteral(".mxf"), Qt::CaseInsensitive).isEmpty());
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExcludePrevious"), false);
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExcludePrevious"), false);

        // The Simplified AAF (p.884-885): the shipped Link To Effects Mixdown version turns both on; an effect on V1
        // becomes a mixdown clip and A1 with clip gain one mixed-down track, both new clips in the bin.
        const SettingKind* exportKind = SettingsSchema::builtIn().find(QStringLiteral("Export"));
        QVERIFY(exportKind && exportKind->versions.size() == 1);
        QCOMPARE(exportKind->versions.first().name, QStringLiteral("Link To Effects Mixdown"));
        model::SettingsFile fresh;
        QCOMPARE(SettingsSchema::builtIn().addShippedVersions(fresh, model::SettingsScope::User), 1);
        QCOMPARE(SettingsSchema::builtIn().addShippedVersions(fresh, model::SettingsScope::User), 0);
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("effects"), [](model::Sequence& seq) {
            seq.track(QStringLiteral("V1"))->segments.first().effect = model::Effect{"Image", "Flip-Flop", {}};
            seq.track(QStringLiteral("A1"))->segments.first().gain = -6.0;
            return true;
        }));
        window.binContainer().saveAll();
        view->selectIds({sequenceId});
        settings.setValue(QStringLiteral("Export"), QStringLiteral("videoExportMethod"), QStringLiteral("link"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioExportMethod"), QStringLiteral("link"));
        for (const auto& [key, value] : exportKind->versions.first().values.toVariantMap().toStdMap())
            settings.setValue(QStringLiteral("Export"), key, QJsonValue::fromVariant(value));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("audioTrackCount"), QStringLiteral("2"));
        const int before = static_cast<int>(bin->bin().items.size());
        const QString simple = dir.filePath(QStringLiteral("Simple"));
        auto simplifiedAaf = exportTo(QDir(simple).filePath(QStringLiteral("Cut.aaf")));
        QVERIFY(simplifiedAaf);
        QCOMPARE(static_cast<int>(bin->bin().items.size()), before + 2);
        const model::BinItem* flat = nullptr;
        for (const model::BinItem& i : simplifiedAaf->items) {
            if (i.kind == model::BinItemKind::Sequence) flat = &i;
        }
        QVERIFY(flat);
        const model::Sequence simplifiedCut = model::Sequence::fromItem(*flat);
        const model::Segment& v1 = simplifiedCut.track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(v1.name, sequenceName + QStringLiteral(" Effects Mixdown 1"));
        QVERIFY(!v1.effect);
        const model::Segment& a1 = simplifiedCut.track(QStringLiteral("A1"))->segments.first();
        QCOMPARE(a1.name, sequenceName + QStringLiteral(" A1 Mixdown"));
        QCOMPARE(a1.gain, 0.0);
        int audioTracks = 0;
        for (const model::Track& t : simplifiedCut.tracks()) audioTracks += t.kind == model::TrackKind::Audio ? 1 : 0;
        QVERIFY(audioTracks <= 2);
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

// Nests in the application (FX Guide p.219-228; docs/spec/nesting.md): Step In and Step Out with the
// nesting level in the Timecode display, edits inside a nest stored back into its segment and the bin,
// and Timeline > Mixdown > Video making a master clip of what the monitor shows.
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/mixdown_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps file whose frame n is grey level 10n.
bool writeMovie(const QString& path, int frames) {
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
        image.fill(QColor(10 * i, 10 * i, 10 * i));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

model::Segment firstV1(const model::BinItem& item) {
    return model::Sequence::fromItem(item).track(QStringLiteral("V1"))->segments.at(0);
}

} // namespace

class TestNestUi : public QObject {
    Q_OBJECT

private slots:
    void stepInAndOut() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("grey.mov"));
        QVERIFY(writeMovie(movie, 20));
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
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 320;
        project.settings.format.height = 180;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        Timeline& timeline = window.timeline();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        const QString sequenceId = composer.state(MonitorId::Record).current()->item.id;
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Flop"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect = model::Effect{QStringLiteral("Image"), QStringLiteral("Flop"), {}};
            return true;
        }));
        settle();

        // Step In: the nest is made, the Timeline shows its tracks and "N1" (p.220).
        composer.moveTo(MonitorId::Record, 5);
        QVERIFY(timeline.stepIn());
        settle();
        QCOMPARE(timeline.nestLevel(), 1);
        const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
        QVERIFY(rec->item.id != sequenceId);
        QCOMPARE(composer.state(MonitorId::Record).position(), 5);
        QVERIFY(timeline.findChild<QLabel*>(QStringLiteral("timelineTimecode"))->text().startsWith(QStringLiteral("N1")));
        QVERIFY(!firstV1(rec->item).effect); // the clip inside, without the effect
        QVERIFY(firstV1(*bin->bin().item(sequenceId)).hasNest());
        // An edit inside goes back into the segment, and the sequence in the bin.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Mosaic"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect = model::Effect{QStringLiteral("Image"), QStringLiteral("Mosaic Effect"), {}};
            return true;
        }));
        settle();
        const model::Segment outer = firstV1(*bin->bin().item(sequenceId));
        QCOMPARE(outer.effect->name, QStringLiteral("Flop"));
        QCOMPARE(model::nestOf(outer).track(QStringLiteral("V1"))->segments.at(0).effect->name, QStringLiteral("Mosaic Effect"));
        // Undo inside the nest goes back too.
        commands.action(QStringLiteral("edit.undo"))->trigger();
        settle();
        QVERIFY(!model::nestOf(firstV1(*bin->bin().item(sequenceId))).track(QStringLiteral("V1"))->segments.at(0).effect);
        // Step Out: the sequence again, on the same frame.
        QVERIFY(timeline.stepOut());
        settle();
        QCOMPARE(timeline.nestLevel(), 0);
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, sequenceId);
        QCOMPARE(composer.state(MonitorId::Record).position(), 5);
        QVERIFY(!timeline.findChild<QLabel*>(QStringLiteral("timelineTimecode"))->text().startsWith(QStringLiteral("N")));
        QVERIFY(!timeline.stepOut());

        // Timeline > Mixdown > Video (p.227-228): a master clip of what the monitor shows, the sequence left.
        window.setVideoMixdownHook([](VideoMixdownDialog&) { return true; });
        composer.state(MonitorId::Record).clearBoth();
        const auto mixed = window.videoMixdown();
        QVERIFY(mixed);
        QCOMPARE(mixed->kind, model::BinItemKind::MasterClip);
        QCOMPARE(mixed->durationFrames, 20);
        QVERIFY(bin->bin().item(mixed->id));
        QVERIFY(media::isLinked(*mixed));
        QVERIFY(firstV1(*bin->bin().item(sequenceId)).hasNest());
    }
};

QTEST_MAIN(TestNestUi)
#include "tst_nest_ui.moc"

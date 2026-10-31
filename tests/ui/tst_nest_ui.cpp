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
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/mixdown_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QToolButton>
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
        // Redo after Step Out still goes into the segment, not into the bin as an item of its own.
        const qsizetype items = bin->bin().items.size();
        commands.action(QStringLiteral("edit.redo"))->trigger();
        settle();
        QCOMPARE(bin->bin().items.size(), items);
        QCOMPARE(model::nestOf(firstV1(*bin->bin().item(sequenceId))).track(QStringLiteral("V1"))->segments.at(0).effect->name,
                 QStringLiteral("Mosaic Effect"));
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, sequenceId);
        commands.action(QStringLiteral("edit.undo"))->trigger();
        settle();
        QCOMPARE(bin->bin().items.size(), items);
        QVERIFY(!model::nestOf(firstV1(*bin->bin().item(sequenceId))).track(QStringLiteral("V1"))->segments.at(0).effect);

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

    void nestedEffectsInTheEditor() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("grey.mov"));
        QVERIFY(writeMovie(movie, 20));
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
        // A Flop nested inside a Picture-in-Picture inside a Mosaic, as Alt+drag makes them (p.216).
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Nest"), [](model::Sequence& s) {
            model::Segment& g = s.track(QStringLiteral("V1"))->segments[0];
            g.effect = model::Effect{QStringLiteral("Image"), QStringLiteral("Flop"), {}};
            model::nestEffect(g, model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}});
            model::nestEffect(g, model::Effect{QStringLiteral("Image"), QStringLiteral("Mosaic Effect"), {}});
            return true;
        }));
        settle();
        auto chain = [&] {
            QStringList out;
            for (const model::NestedEffect& n : model::nestedEffects(firstV1(*bin->bin().item(sequenceId)), 5)) out << n.effect.name;
            return out;
        };

        // "you can see both effects and their parameters in the Effect Editor" (p.221).
        EffectEditor& editor = window.effectEditor();
        composer.moveTo(MonitorId::Record, 5);
        editor.refresh();
        QCOMPARE(editor.nestedEffects(), (QStringList{QStringLiteral("Mosaic Effect"), QStringLiteral("Picture-in-Picture"), QStringLiteral("Flop")}));
        QCOMPARE(editor.nestedLevel(), 0);
        QCOMPARE(editor.effectName(), QStringLiteral("Mosaic Effect"));
        QVERIFY(editor.findChild<QToolButton*>(QStringLiteral("nested:2")));
        // Opening the Picture-in-Picture shows its groups; a change goes to it, inside the nest.
        editor.findChild<QToolButton*>(QStringLiteral("nested:1"))->click();
        settle();
        QCOMPARE(editor.nestedLevel(), 1);
        QCOMPARE(editor.effectName(), QStringLiteral("Picture-in-Picture"));
        QVERIFY(editor.categories().contains(QStringLiteral("Position")));
        QVERIFY(editor.slider(QStringLiteral("Position"), QStringLiteral("X")));
        editor.slider(QStringLiteral("Position"), QStringLiteral("X"))->setValue(300);
        settle();
        const QList<model::NestedEffect> nested = model::nestedEffects(firstV1(*bin->bin().item(sequenceId)), 5);
        QCOMPARE(nested.at(1).effect.parameters.value(QStringLiteral("Position")).toObject().value(QStringLiteral("X")).toInt(), 300);
        QVERIFY(nested.at(0).effect.parameters.isEmpty());
        // Undo takes it back.
        commands.action(QStringLiteral("edit.undo"))->trigger();
        settle();
        QVERIFY(model::nestedEffects(firstV1(*bin->bin().item(sequenceId)), 5).at(1).effect.parameters.isEmpty());

        // "Drag the effect icon for the effect you want to move to the position you want it to occupy" (p.222).
        QVERIFY(editor.moveNestedEffect(2, 0));
        settle();
        QCOMPARE(chain(), (QStringList{QStringLiteral("Flop"), QStringLiteral("Mosaic Effect"), QStringLiteral("Picture-in-Picture")}));
        QCOMPARE(editor.nestedEffects(), chain());
        QCOMPARE(firstV1(*bin->bin().item(sequenceId)).effect->name, QStringLiteral("Flop")); // the Timeline's too
        commands.action(QStringLiteral("edit.undo"))->trigger();
        settle();
        QCOMPARE(chain().first(), QStringLiteral("Mosaic Effect"));

        // "If you attempt a change in order that is not possible, a message box opens that explains why" (p.222).
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Timewarp"), [](model::Sequence& s) {
            model::Segment& g = s.track(QStringLiteral("V1"))->segments[0];
            return model::setNestedEffect(g, 5, 2, model::Effect{QStringLiteral("Timewarp"), QStringLiteral("Timewarp"), {}});
        }));
        settle();
        editor.refresh();
        QVERIFY(!editor.moveNestedEffect(2, 1));
        settle();
        auto* box = editor.findChild<QMessageBox*>(QStringLiteral("nestedOrderRefused"));
        QVERIFY(box);
        QVERIFY(box->text().contains(QStringLiteral("innermost")));
        box->close();
        settle();
    }

    void expandingInTheTimeline() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("grey.mov"));
        QVERIFY(writeMovie(movie, 20));
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
        composer.moveTo(MonitorId::Record, 6);

        // "In Source/Record mode, click one of the segment tools ... and then double-click the nested effect" (p.220).
        QVERIFY(!timeline.doubleClickTrack(QStringLiteral("V1"), 6)); // no segment tool
        timeline.setSegmentTool(model::EditKind::Overwrite, true);
        QVERIFY(timeline.doubleClickTrack(QStringLiteral("V1"), 6));
        settle();
        QVERIFY(timeline.isNestExpanded());
        QCOMPARE(timeline.expandedTrack(), QStringLiteral("V1"));
        QCOMPARE(composer.state(MonitorId::Record).position(), 6);
        QVERIFY(composer.state(MonitorId::Record).current()->item.id != sequenceId);
        // "The nested tracks appear in the Timeline above the track you double-clicked" (p.220), as "1.1".
        QCOMPARE(timeline.shown().track(QStringLiteral("V2"))->customName, QStringLiteral("1.1"));
        QVERIFY(timeline.isNestedRow(QStringLiteral("V2")));
        // "You cannot monitor individual tracks when you expand nested tracks" (p.220).
        QVERIFY(!timeline.isMonitored(QStringLiteral("V2")));
        timeline.toggleMonitor(QStringLiteral("V2"));
        QVERIFY(!timeline.isMonitored(QStringLiteral("V2")));
        QCOMPARE(timeline.expandedFrames(), (std::pair<std::int64_t, std::int64_t>{0, 20}));

        // An edit on the nested track goes into the nest, and the sequence in the bin.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Lift"), [](model::Sequence& s) {
            s.lift(QStringLiteral("V2"), 5, 3);
            return true;
        }));
        settle();
        QVERIFY(timeline.isNestExpanded());
        {
            const model::Sequence nest = model::nestOf(firstV1(*bin->bin().item(sequenceId)));
            const model::Track* v1 = nest.track(QStringLiteral("V1"));
            QVERIFY(v1->segments.at(v1->segmentAt(6)).isFiller());
        }
        QCOMPARE(firstV1(*bin->bin().item(sequenceId)).effect->name, QStringLiteral("Flop"));
        // Undo puts it back, still expanded.
        commands.action(QStringLiteral("edit.undo"))->trigger();
        settle();
        QVERIFY(timeline.isNestExpanded());
        {
            const model::Sequence nest = model::nestOf(firstV1(*bin->bin().item(sequenceId)));
            const model::Track* v1 = nest.track(QStringLiteral("V1"));
            QVERIFY(!v1->segments.at(v1->segmentAt(6)).isFiller());
        }

        // "Double-click the main track for the nested effect ... to close the nested tracks again" (p.221).
        QVERIFY(timeline.doubleClickTrack(QStringLiteral("V1"), 6));
        settle();
        QVERIFY(!timeline.isNestExpanded());
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, sequenceId);
        QCOMPARE(composer.state(MonitorId::Record).position(), 6);
        QVERIFY(!timeline.shown().track(QStringLiteral("V2")));

        // "In Effect mode, double-click the nested effect"; Step In closes it first.
        timeline.setSegmentTool(model::EditKind::Overwrite, false);
        timeline.setEffectMode(true);
        QVERIFY(timeline.doubleClickTrack(QStringLiteral("V1"), 6));
        settle();
        QVERIFY(timeline.isNestExpanded());
        QVERIFY(timeline.stepIn());
        settle();
        QVERIFY(!timeline.isNestExpanded());
        QCOMPARE(timeline.nestLevel(), 1);
        QVERIFY(timeline.stepOut());
        settle();
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, sequenceId);
        timeline.setEffectMode(false);
    }
};

QTEST_MAIN(TestNestUi)
#include "tst_nest_ui.moc"

// The Composer settings' Edit tab (Editing Guide p.1159-1160; docs/spec/composer-edit.md): Sync Point
// editing and its orange mark, Single Mark Editing, Phantom Marks, Auto-create New Tracks, Auto-enable
// Source Tracks, Copy Source Markers, and Composer > Load Filler.
#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/button_mapping.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QMenu>
#include <QTest>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

Segment seg(const QString& clipId, const QString& track, std::int64_t length) {
    Segment s;
    s.binId = QStringLiteral("bin");
    s.clipId = clipId;
    s.name = clipId;
    s.sourceTrack = track;
    s.length = length;
    return s;
}

BinItem clip(const QString& name, std::int64_t frames) {
    BinItem c;
    c.id = name;
    c.name = name;
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = frames;
    return c;
}

// A Composer and Timeline over a sequence of A (100) on V1, A1 and A2, with clip C (200) in the Source
// monitor.
struct Fixture {
    Composer composer;
    Timeline timeline{composer};

    Fixture() {
        timeline.resize(900, 300);
        timeline.show();
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        for (const QString& t : seq.trackNames()) seq.splice(t, 0, {seg(QStringLiteral("A"), t, 100)});
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("C"), 200));
        settle();
    }
    Sequence recorded() const { return Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    void set(const std::function<void(Timeline::Options&)>& change) {
        Timeline::Options o = timeline.options();
        change(o);
        timeline.setOptions(o);
    }
    void mark(MonitorId m, std::optional<std::int64_t> in, std::optional<std::int64_t> out, std::int64_t at) {
        MonitorState& s = composer.state(m);
        s.clearBoth();
        if (in) {
            s.setPosition(*in);
            s.markIn();
        }
        if (out) {
            s.setPosition(*out);
            s.markOut();
        }
        s.setPosition(at);
    }
};

} // namespace

class TestComposerEdit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void planning() {
        // Single Mark Editing (p.468): a source IN alone up to the position; a source OUT alone from it.
        const EditMarks none{std::nullopt, std::nullopt, 50, 100};
        EditPlan p = planEdit({10, std::nullopt, 30, 200}, none);
        QCOMPARE(p.sourceStart, 10);
        QCOMPARE(p.length, 21);
        p = planEdit({10, std::nullopt, 30, 200}, none, false);
        QCOMPARE(p.length, 190);
        p = planEdit({std::nullopt, 40, 30, 200}, none);
        QCOMPARE(p.sourceStart, 30);
        QCOMPARE(p.length, 11);
        p = planEdit({std::nullopt, 40, 30, 200}, none, false);
        QCOMPARE(p.sourceStart, 0);
        QCOMPARE(p.length, 41);
        // With the position at the IN, to the clip's end as before.
        QCOMPARE(planEdit({10, std::nullopt, 10, 200}, none).length, 190);
        // Sync Point (p.496): every pair of marks.
        p = planSyncPoint({std::nullopt, std::nullopt, 20, 200}, {40, 59, 50, 100});
        QCOMPARE(p.sourceStart, 10);
        QCOMPARE(p.recordStart, 40);
        QCOMPARE(p.length, 20);
        p = planSyncPoint({std::nullopt, 29, 20, 200}, {40, std::nullopt, 50, 100});
        QCOMPARE(p.sourceStart, 10);
        QCOMPARE(p.recordStart, 40);
        QCOMPARE(p.length, 20);
        QVERIFY(!planSyncPoint({1, std::nullopt, 20, 200}, {std::nullopt, std::nullopt, 50, 100}).ok());
        QVERIFY(!planSyncPoint({std::nullopt, std::nullopt, 0, 200}, {40, 59, 50, 100}).ok()); // before the clip
    }

    // Composer > Sync Point Overwrite toggles the mode and the Overwrite button's orange mark (p.496).
    void syncPointMark() {
        Fixture f;
        QVERIFY(!ButtonMapper::instance().isMarked(QStringLiteral("edit.overwrite")));
        QVERIFY(f.timeline.perform(QStringLiteral("composer.sync_point_overwrite")));
        QVERIFY(f.timeline.options().syncPointEditing);
        QVERIFY(ButtonMapper::instance().isMarked(QStringLiteral("edit.overwrite")));
        // A splice is not a sync point edit.
        f.mark(MonitorId::Source, 10, 19, 15);
        f.mark(MonitorId::Record, std::nullopt, std::nullopt, 50);
        QVERIFY(f.timeline.perform(QStringLiteral("edit.splice_in")));
        QCOMPARE(f.recorded().track(QStringLiteral("V1"))->segments.at(1).sourceIn, 10);
        QCOMPARE(f.recorded().duration(), 110);
        QVERIFY(f.timeline.perform(QStringLiteral("composer.sync_point_overwrite")));
        QVERIFY(!ButtonMapper::instance().isMarked(QStringLiteral("edit.overwrite")));
    }

    // Phantom Marks (p.501): the marks the edit would use, where none is set.
    void phantomMarks() {
        Fixture f;
        f.mark(MonitorId::Source, 20, std::nullopt, 20);
        f.mark(MonitorId::Record, std::nullopt, std::nullopt, 30);
        QCOMPARE(f.composer.phantomMarks(MonitorId::Source), (std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{}));
        f.set([](Timeline::Options& o) { o.phantomMarks = true; });
        // One mark IN: the record IN at the position, both OUTs from the clip's length.
        auto source = f.composer.phantomMarks(MonitorId::Source);
        auto record = f.composer.phantomMarks(MonitorId::Record);
        QCOMPARE(source.first, std::nullopt);
        QCOMPARE(source.second, std::optional<std::int64_t>(199));
        QCOMPARE(record.first, std::optional<std::int64_t>(30));
        QCOMPARE(record.second, std::optional<std::int64_t>(209));
        // A second mark recalculates them (p.501).
        f.mark(MonitorId::Source, 20, 59, 20);
        record = f.composer.phantomMarks(MonitorId::Record);
        QCOMPARE(record.second, std::optional<std::int64_t>(69));
        QCOMPARE(f.composer.phantomMarks(MonitorId::Source), (std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{}));
    }

    // Auto-enable Source Tracks (p.1160): a new source clip has all its tracks on; off, they stay as
    // they were.
    void autoEnableSourceTracks() {
        Fixture f;
        f.timeline.toggleSourceTrack(QStringLiteral("A2"));
        QVERIFY(!f.timeline.patch().isSourceSelected(QStringLiteral("A2")));
        f.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("D"), 50));
        settle();
        QVERIFY(f.timeline.patch().isSourceSelected(QStringLiteral("A2")));
        f.set([](Timeline::Options& o) { o.autoEnableSourceTracks = false; });
        f.timeline.toggleSourceTrack(QStringLiteral("A2"));
        f.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("E"), 50));
        settle();
        QVERIFY(!f.timeline.patch().isSourceSelected(QStringLiteral("A2")));
    }

    // Auto-create New Tracks off (p.1159-1160): a source track patched to a missing record track is
    // not edited.
    void autoCreateTracks() {
        Fixture f;
        QVERIFY(f.timeline.patchTracks(QStringLiteral("V1"), QStringLiteral("V2")));
        f.set([](Timeline::Options& o) { o.autoCreateTracks = false; });
        f.mark(MonitorId::Source, 0, 9, 0);
        f.mark(MonitorId::Record, std::nullopt, std::nullopt, 0);
        QVERIFY(f.timeline.perform(QStringLiteral("edit.overwrite")));
        QVERIFY(!f.recorded().track(QStringLiteral("V2")));
        QCOMPARE(f.recorded().track(QStringLiteral("A1"))->segments.first().name, QStringLiteral("C"));
        f.set([](Timeline::Options& o) { o.autoCreateTracks = true; });
        QVERIFY(f.timeline.perform(QStringLiteral("edit.overwrite")));
        QVERIFY(f.recorded().track(QStringLiteral("V2")));
    }

    // Copy Source Markers (p.1160).
    void copySourceMarkers() {
        Fixture f;
        BinItem c = clip(QStringLiteral("M"), 100);
        Marker in;
        in.id = model::newItemId();
        in.color = QStringLiteral("Red");
        in.track = QStringLiteral("V1");
        in.frame = 15;
        Marker out = in;
        out.id = model::newItemId();
        out.frame = 60; // outside the marked material
        setMarkers(c, {in, out});
        f.composer.load(MonitorId::Source, QStringLiteral("bin"), c);
        settle();
        f.mark(MonitorId::Source, 10, 29, 10);
        f.mark(MonitorId::Record, std::nullopt, std::nullopt, 40);
        QVERIFY(f.timeline.perform(QStringLiteral("edit.overwrite")));
        const QList<Marker> copied = markersOf(f.composer.state(MonitorId::Record).current()->item);
        QCOMPARE(copied.size(), 1);
        QCOMPARE(copied.first().frame, 45);
        QCOMPARE(copied.first().track, QStringLiteral("V1"));
        f.set([](Timeline::Options& o) { o.copySourceMarkers = false; });
        f.mark(MonitorId::Source, 10, 29, 10);
        f.mark(MonitorId::Record, std::nullopt, std::nullopt, 70);
        QVERIFY(f.timeline.perform(QStringLiteral("edit.overwrite")));
        QCOMPARE(markersOf(f.composer.state(MonitorId::Record).current()->item).size(), 1);
    }

    // Load Filler from the Source monitor's Clip Name menu (p.462): two minutes of filler on the
    // sequence's tracks.
    void loadFiller() {
        Fixture f;
        QMenu* menu = f.composer.clipNameMenu(MonitorId::Source);
        QAction* load = nullptr;
        for (QAction* a : menu->actions()) {
            if (a->text() == QLatin1String("Load Filler")) load = a;
        }
        QVERIFY(load);
        load->trigger();
        settle();
        const LoadedClip* loaded = f.composer.state(MonitorId::Source).current();
        QVERIFY(loaded && loaded->binId.isEmpty());
        QCOMPARE(loaded->item.name, QStringLiteral("Filler"));
        QCOMPARE(f.composer.state(MonitorId::Source).duration(), 120 * 25);
        const Sequence filler = Sequence::fromItem(loaded->item);
        QCOMPARE(filler.trackNames(), f.recorded().trackNames());
        QVERIFY(filler.track(QStringLiteral("A2"))->segments.first().isFiller());
    }
};

QTEST_MAIN(TestComposerEdit)
#include "tst_composer_edit.moc"

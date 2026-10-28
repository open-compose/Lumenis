// The Timeline settings' Edit tab (Editing Guide p.1220-1221; docs/spec/timeline-settings.md):
// Auto-Patching, Auto-Monitoring, Segment Drag Sync Locks, Position Bar Snap, Default Sync Locks On,
// Select Filler with Segment Tools, Applying Effects opens Effect Editor, Clicking the TC Track or
// ruler Disables Smart Tools and Lassoing transitions enters Segment Mode.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QDir>
#include <QPoint>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

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

QStringList names(const Sequence& seq, const QString& track) {
    QStringList out;
    for (const Segment& s : seq.track(track)->segments) out << (s.isFiller() ? QStringLiteral("-%1").arg(s.length) : s.name);
    return out;
}

// A Timeline over a sequence of V1 a (50) - (25) b (50), V2 empty, A1 x (125), A2 empty.
struct Fixture {
    Composer composer;
    Timeline timeline{composer};
    BinItem item;

    Fixture() {
        timeline.resize(1000, 360);
        timeline.show();
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.addTrack(TrackKind::Video);
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 50), Segment::filler(25),
                                              seg(QStringLiteral("b"), QStringLiteral("V1"), 50)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("x"), QStringLiteral("A1"), 125)});
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        timeline.showEntireSequence();
        settle();
    }
    Sequence recorded() const { return Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    void set(const std::function<void(Timeline::Options&)>& change) {
        Timeline::Options o = timeline.options();
        change(o);
        timeline.setOptions(o);
    }
    // A point in a row, at a frame.
    QPoint at(const QString& track, std::int64_t frame) const {
        for (const TimelineView::Row& r : timeline.view()->rows()) {
            if (r.track == track) return QPoint(static_cast<int>(std::ceil(timeline.view()->xFor(frame))), r.top + r.height / 2);
        }
        return {};
    }
};

} // namespace

class TestTimelineSettings : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Segment Drag Sync Locks (p.618): filler where the segment was and on the other sync-locked tracks.
    void segmentDragSyncLocks() {
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 10), seg(QStringLiteral("b"), QStringLiteral("V1"), 10)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("x"), QStringLiteral("A1"), 20)});
        seq.track(QStringLiteral("V1"))->syncLocked = true;
        seq.track(QStringLiteral("A1"))->syncLocked = true;
        Sequence kept = seq;
        QCOMPARE(moveSegments(kept, {{QStringLiteral("V1"), 0}}, 20, 0, EditKind::SpliceIn, false, nullptr, true).size(), 1);
        QCOMPARE(names(kept, QStringLiteral("V1")), (QStringList{"-10", "b", "a"}));
        QCOMPARE(names(kept, QStringLiteral("A1")), (QStringList{"x"}));
        QCOMPARE(kept.track(QStringLiteral("A1"))->length(), 20);
        // Into the middle of A1's clip: A1 gets filler there.
        Sequence middle = seq;
        moveSegments(middle, {{QStringLiteral("V1"), 10}}, -5, 0, EditKind::SpliceIn, false, nullptr, true);
        QCOMPARE(names(middle, QStringLiteral("V1")), (QStringList{"a", "b", "a", "-10"}));
        QCOMPARE(middle.track(QStringLiteral("A1"))->length(), 30);
        // Without the option the gap closes.
        Sequence closed = seq;
        moveSegments(closed, {{QStringLiteral("V1"), 0}}, 20, 0, EditKind::SpliceIn, false);
        QCOMPARE(names(closed, QStringLiteral("V1")), (QStringList{"b", "a"}));
    }

    // Auto-Patching and Auto-Monitoring (p.629, p.1220).
    void autoPatching() {
        Fixture f;
        BinItem clip;
        clip.id = QStringLiteral("c");
        clip.name = QStringLiteral("c");
        clip.kind = BinItemKind::MasterClip;
        clip.editRate = Rational(25);
        clip.durationFrames = 100;
        f.set([](Timeline::Options& o) { o.autoPatching = true; });
        f.composer.load(MonitorId::Source, QStringLiteral("bin"), clip);
        settle();
        QCOMPARE(f.timeline.patch().recordFor(QStringLiteral("V1")), QStringLiteral("V1"));
        // V1 off: the source's V1 goes to the next enabled video track, and its monitor with it.
        QVERIFY(f.timeline.isMonitored(QStringLiteral("V1")));
        f.timeline.toggleRecordTrack(QStringLiteral("V1"));
        QCOMPARE(f.timeline.patch().recordFor(QStringLiteral("V1")), QStringLiteral("V2"));
        QVERIFY(!f.timeline.isMonitored(QStringLiteral("V1")));
        QVERIFY(f.timeline.isMonitored(QStringLiteral("V2")));
        // Audio follows its enabled tracks too.
        f.timeline.toggleRecordTrack(QStringLiteral("A1"));
        QCOMPARE(f.timeline.patch().recordFor(QStringLiteral("A1")), QStringLiteral("A2"));
        // Without Auto-Monitoring a patch leaves the monitors.
        f.set([](Timeline::Options& o) {
            o.autoPatching = false;
            o.autoMonitoring = false;
        });
        QVERIFY(f.timeline.patchTracks(QStringLiteral("V1"), QStringLiteral("V1")));
        QVERIFY(f.timeline.isMonitored(QStringLiteral("V2")) && !f.timeline.isMonitored(QStringLiteral("V1")));
    }

    // Position Bar Snap (p.397, p.1220): a click near an event goes to it.
    void positionBarSnap() {
        Fixture f;
        QWidget* view = f.timeline.view();
        const QPoint near = f.at(QStringLiteral("V1"), 50) + QPoint(7, 0);
        QTest::mouseClick(view, Qt::LeftButton, {}, near);
        QVERIFY(f.composer.state(MonitorId::Record).position() != 50);
        f.set([](Timeline::Options& o) { o.positionBarSnap = true; });
        QTest::mouseClick(view, Qt::LeftButton, {}, near);
        QCOMPARE(f.composer.state(MonitorId::Record).position(), 50);
        // Far from every event the click stays where it is.
        QTest::mouseClick(view, Qt::LeftButton, {}, f.at(QStringLiteral("V1"), 100));
        QCOMPARE(f.composer.state(MonitorId::Record).position(), 100);
    }

    // Default Sync Locks On (p.1220): new sequences and new tracks.
    void defaultSyncLocks() {
        BinItem item;
        item.kind = BinItemKind::Sequence;
        syncLockAllTracks(item);
        const Sequence seq = Sequence::fromItem(item);
        for (const QString& name : seq.trackNames()) QVERIFY2(seq.track(name)->syncLocked, qPrintable(name));
        Fixture f;
        f.set([](Timeline::Options& o) { o.defaultSyncLocks = true; });
        QVERIFY(f.timeline.perform(QStringLiteral("timeline.new.audio_track.mono")));
        QVERIFY(f.recorded().track(QStringLiteral("A3"))->syncLocked);
    }

    // Select Filler with Segment Tools (p.616): off, a click on filler selects nothing.
    void selectFiller() {
        Fixture f;
        f.timeline.setSegmentTool(EditKind::Overwrite, true);
        QTest::mouseClick(f.timeline.view(), Qt::LeftButton, {}, f.at(QStringLiteral("V1"), 60));
        QCOMPARE(f.timeline.selection(), (QList<SegmentRef>{SegmentRef{QStringLiteral("V1"), 50}}));
        f.set([](Timeline::Options& o) { o.selectFiller = false; });
        QTest::mouseClick(f.timeline.view(), Qt::LeftButton, {}, f.at(QStringLiteral("V1"), 60));
        QVERIFY(f.timeline.selection().isEmpty());
        QTest::mouseClick(f.timeline.view(), Qt::LeftButton, {}, f.at(QStringLiteral("V1"), 10));
        QCOMPARE(f.timeline.selection(), (QList<SegmentRef>{SegmentRef{QStringLiteral("V1"), 0}}));
    }

    // Clicking the TC Track or ruler Disables Smart Tools (p.1220).
    void rulerDisablesSmartTools() {
        Fixture f;
        f.timeline.setSegmentTool(EditKind::SpliceIn, true);
        f.timeline.setTrimTool(TrimKind::Ripple, true);
        const QPoint ruler(f.timeline.view()->xFor(20) > 0 ? static_cast<int>(f.timeline.view()->xFor(20)) : 200, 4);
        QTest::mouseClick(f.timeline.view(), Qt::LeftButton, {}, ruler);
        QVERIFY(f.timeline.isSegmentTool(EditKind::SpliceIn));
        f.set([](Timeline::Options& o) { o.tcDisablesSmartTools = true; });
        QTest::mouseClick(f.timeline.view(), Qt::LeftButton, {}, ruler);
        QVERIFY(!f.timeline.isSegmentTool(EditKind::SpliceIn));
        QVERIFY(!f.timeline.isTrimTool(TrimKind::Ripple));
    }

    // Lassoing transitions enters: Segment Mode (p.1220-1221), else Trim Mode.
    void lassoTransitions() {
        Fixture f;
        QWidget* view = f.timeline.view();
        const QPoint from = f.at(QStringLiteral("V1"), 45) - QPoint(0, 8);
        const QPoint to = f.at(QStringLiteral("V1"), 55) + QPoint(0, 8);
        QTest::mousePress(view, Qt::LeftButton, Qt::AltModifier, from);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::AltModifier, to);
        QVERIFY(f.timeline.isTrimMode());
        f.timeline.exitTrimMode();
        f.set([](Timeline::Options& o) { o.lassoSegmentMode = true; });
        QTest::mousePress(view, Qt::LeftButton, Qt::AltModifier, from);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::AltModifier, to);
        QVERIFY(!f.timeline.isTrimMode());
        QCOMPARE(f.timeline.selection(), (QList<SegmentRef>{SegmentRef{QStringLiteral("V1"), 0}, SegmentRef{QStringLiteral("V1"), 50}}));
        QVERIFY(f.timeline.isSegmentTool(f.timeline.options().defaultSegmentTool));
    }

    // Applying Effects opens Effect Editor (p.1220).
    void effectEditorOpens() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 50)});
        seq.storeIn(item);
        const QString binId = window.binContainer().newBin();
        QVERIFY(window.binContainer().openBin(binId));
        window.binContainer().document(binId)->addItem(item);
        window.composer().load(MonitorId::Record, binId, item);
        settle();
        Panel* editor = window.panels().panel(QStringLiteral("EffectEditor"));
        QVERIFY(editor);
        if (editor->isVisible()) editor->close();
        EffectInfo flop;
        flop.category = QStringLiteral("Image");
        flop.name = QStringLiteral("Flop");
        flop.segment = true;
        Timeline& timeline = window.timeline();
        QSignalSpy applied(&timeline, &Timeline::effectApplied);
        timeline.setSelection({SegmentRef{QStringLiteral("V1"), 0}});
        QVERIFY(timeline.applyEffectToSelection(flop));
        QCOMPARE(applied.count(), 1);
        QVERIFY(!editor->isVisible());
        window.settings().setValue(QStringLiteral("Timeline"), QStringLiteral("openEffectEditor"), true);
        settle();
        QVERIFY(timeline.options().openEffectEditor);
        timeline.setSelection({SegmentRef{QStringLiteral("V1"), 0}});
        QVERIFY(timeline.applyEffectToSelection(flop));
        settle();
        QVERIFY(editor->isVisible());
    }
};

QTEST_MAIN(TestTimelineSettings)
#include "tst_timeline_settings.moc"

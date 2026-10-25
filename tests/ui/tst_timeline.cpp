#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/marker_dialogs.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

BinItem clip(const QString& name, std::int64_t frames, const QString& tracks = QStringLiteral("V1 A1-2")) {
    BinItem i;
    i.id = model::newItemId();
    i.name = name;
    i.kind = BinItemKind::MasterClip;
    i.durationFrames = frames;
    i.editRate = Rational(25);
    i.fields.insert(QStringLiteral("Tracks"), tracks);
    return i;
}

// A sequence with a clip on V1, A1 and A2.
BinItem sequence(const QString& name, std::int64_t frames) {
    BinItem s;
    s.id = model::newItemId();
    s.name = name;
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("A");
        a.sourceTrack = t;
        a.length = frames / 2;
        model::Segment b = a;
        b.clipId = QStringLiteral("b");
        b.name = QStringLiteral("B");
        b.length = frames - frames / 2;
        seq.splice(t, 0, {a, b});
    }
    seq.storeIn(s);
    return s;
}

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

// A Composer and Timeline on their own, with a store that keeps items.
struct Rig {
    Composer composer;
    Timeline timeline{composer};
    QHash<QString, BinItem> stored;
    Rig() {
        timeline.setSequenceStore([this](const QString&, const BinItem& item, bool) {
            stored.insert(item.id, item);
            return true;
        });
        timeline.setSequenceMaker([]() -> std::optional<std::pair<QString, BinItem>> {
            BinItem s;
            s.id = model::newItemId();
            s.name = QStringLiteral("Untitled Sequence.01");
            s.kind = BinItemKind::Sequence;
            s.editRate = Rational(25);
            return std::make_pair(QStringLiteral("bin"), s);
        });
        timeline.resize(900, 300);
        timeline.show();
        settle();
    }
    model::Sequence recordSequence() const {
        return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
    }
    QStringList rowNames() const {
        QStringList out;
        for (const auto& r : timeline.view()->rows()) out << r.track;
        return out;
    }
};

} // namespace

class TestTimeline : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void rowsInTimelineOrder() {
        Rig rig;
        QVERIFY(rig.rowNames().isEmpty()); // nothing loaded
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        // V1 above TC1, audio below (p.607).
        QCOMPARE(rig.rowNames(), (QStringList{"V1", "TC1", "A1", "A2"}));
        QVERIFY(rig.timeline.perform(QStringLiteral("timeline.new.video_track")));
        QVERIFY(rig.timeline.perform(QStringLiteral("timeline.new.data_track")));
        QCOMPARE(rig.rowNames(), (QStringList{"D1", "V2", "V1", "TC1", "A1", "A2"}));
        // Show Track > TC1 hides the timecode track (p.598).
        QMenu* fast = rig.timeline.fastMenu();
        QMenu* showTrack = nullptr;
        for (QAction* a : fast->actions()) {
            if (a->text() == QLatin1String("Show Track")) showTrack = a->menu();
        }
        QVERIFY(showTrack);
        showTrack->actions().first()->trigger();
        QCOMPARE(rig.rowNames(), (QStringList{"D1", "V2", "V1", "A1", "A2"}));
        // A source track patched to a missing track shows where it goes.
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("Wide"), 50, QStringLiteral("V1 A1-3")));
        QVERIFY(rig.rowNames().contains(QStringLiteral("A3")));
    }

    void firstEditMakesTheSequence() {
        // Splice-in with nothing in the Record monitor makes a sequence (p.463).
        Rig rig;
        const BinItem a = clip(QStringLiteral("Alpha"), 100);
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), a);
        rig.composer.state(MonitorId::Source).setPosition(10);
        rig.composer.state(MonitorId::Source).markIn();
        rig.composer.state(MonitorId::Source).setPosition(59);
        rig.composer.state(MonitorId::Source).markOut();
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.splice_in")));
        QVERIFY(rig.timeline.lastMessage().isEmpty());
        const model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        QVERIFY(!rec.isEmpty());
        QCOMPARE(rec.duration(), 50);
        // The end of the new edit (p.464).
        QCOMPARE(rec.position(), 50);
        const model::Sequence seq = rig.recordSequence();
        QCOMPARE(seq.trackNames(), (QStringList{"V1", "A1", "A2"}));
        QCOMPARE(seq.track(QStringLiteral("A2"))->segments.first().clipId, a.id);
        QCOMPARE(seq.track(QStringLiteral("A2"))->segments.first().sourceIn, 10);
        QCOMPARE(rig.stored.value(rec.current()->item.id).durationFrames, 50);
        // Another splice at the end; then an overwrite over the join.
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.splice_in")));
        QCOMPARE(rec.duration(), 100);
        rig.composer.state(MonitorId::Source).clearBoth();
        rig.composer.state(MonitorId::Source).setPosition(0);
        rig.composer.state(MonitorId::Record).setPosition(40);
        rig.composer.state(MonitorId::Record).markIn();
        rig.composer.state(MonitorId::Record).setPosition(59);
        rig.composer.state(MonitorId::Record).markOut();
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.overwrite")));
        QCOMPARE(rec.duration(), 100);
        QCOMPARE(rig.recordSequence().editPoints({QStringLiteral("V1")}), (QList<std::int64_t>{0, 40, 60, 100}));
        QVERIFY(!rec.in()); // marks used up
        // Undo and redo, step by step (p.466).
        QCOMPARE(rig.timeline.history().descriptions(), (QStringList{"Overwrite", "Splice-in", "Splice-in"}));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(rig.recordSequence().editPoints({QStringLiteral("V1")}), (QList<std::int64_t>{0, 50, 100}));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(rec.duration(), 50);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.redo")));
        QCOMPARE(rec.duration(), 100);
        QCOMPARE(rig.stored.value(rec.current()->item.id).durationFrames, 100);
        // The Undo/Redo List.
        QMenu* list = rig.timeline.undoRedoMenu();
        QCOMPARE(list->actions().size(), 3);
        QCOMPARE(list->actions().first()->text(), QStringLiteral("Redo Overwrite"));
        list->actions().last()->trigger(); // undo back to the first edit
        QCOMPARE(rec.duration(), 0);
        delete list;
    }

    void editsNeedSourceAndTracks() {
        Rig rig;
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.splice_in")));
        QVERIFY(!rig.timeline.lastMessage().isEmpty()); // no source clip
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("Alpha"), 100));
        // Edited only when selected on both sides (p.626).
        for (const QString& t : {QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}) {
            rig.timeline.toggleRecordTrack(t);
        }
        rig.timeline.perform(QStringLiteral("edit.overwrite"));
        QVERIFY(!rig.timeline.lastMessage().isEmpty());
        QCOMPARE(rig.timeline.history().undoCount(), 0);
        // Select All Tracks (p.627), then only video.
        rig.timeline.perform(QStringLiteral("edit.select_all_tracks"));
        rig.timeline.toggleSourceTrack(QStringLiteral("A1"));
        rig.timeline.toggleSourceTrack(QStringLiteral("A2"));
        rig.composer.state(MonitorId::Record).setPosition(0);
        rig.composer.state(MonitorId::Source).setPosition(90);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.splice_in")));
        QVERIFY(rig.timeline.lastMessage().isEmpty());
        const model::Sequence seq = rig.recordSequence();
        QCOMPARE(seq.track(QStringLiteral("V1"))->length(), 110);
        QCOMPARE(seq.track(QStringLiteral("A1"))->length(), 100);
    }

    void patchesByDragging() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.timeline.perform(QStringLiteral("timeline.new.audio_track.mono"));
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("Alpha"), 100));
        TimelineView* view = rig.timeline.view();
        auto rowOf = [&](const QString& track) {
            for (const auto& r : view->rows()) {
                if (r.track == track) return r;
            }
            return TimelineView::Row{};
        };
        // Drag the A1 source button to the A3 record button (p.629).
        const QPoint from = view->sourceButton(rowOf(QStringLiteral("A1"))).center();
        const QPoint to = view->recordButton(rowOf(QStringLiteral("A3"))).center();
        QTest::mousePress(view, Qt::LeftButton, {}, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), view->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(view, &move);
        QTest::mouseRelease(view, Qt::LeftButton, {}, to);
        QCOMPARE(rig.timeline.patch().recordFor(QStringLiteral("A1")), QStringLiteral("A3"));
        // Audio cannot go to video.
        QVERIFY(!rig.timeline.patchTracks(QStringLiteral("A2"), QStringLiteral("V1")));
        // A click toggles the source track.
        const QPoint a2 = view->sourceButton(rowOf(QStringLiteral("A2"))).center();
        QTest::mouseClick(view, Qt::LeftButton, {}, a2);
        QVERIFY(!rig.timeline.patch().isSourceSelected(QStringLiteral("A2")));
        // A click on a record button toggles it.
        QTest::mouseClick(view, Qt::LeftButton, {}, view->recordButton(rowOf(QStringLiteral("V1"))).center());
        QVERIFY(!rig.timeline.patch().isRecordSelected(QStringLiteral("V1")));
        // Timeline > Restore Default Patch (p.630).
        rig.timeline.perform(QStringLiteral("timeline.restore_default_patch"));
        QCOMPARE(rig.timeline.patch().recordFor(QStringLiteral("A1")), QStringLiteral("A1"));
    }

    void liftAndExtract() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        rig.timeline.perform(QStringLiteral("edit.lift"));
        QVERIFY(!rig.timeline.lastMessage().isEmpty()); // no marks
        rec.setPosition(40);
        rec.markIn();
        rec.setPosition(59);
        rec.markOut();
        // Lift leaves filler (p.478).
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.lift")));
        QCOMPARE(rec.duration(), 100);
        QVERIFY(rig.recordSequence().track(QStringLiteral("V1"))->segments.at(1).isFiller());
        // Extract on V1 only: it gets shorter, the others do not (p.479).
        rig.timeline.toggleRecordTrack(QStringLiteral("A1"));
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.extract")));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->length(), 80);
        QCOMPARE(rig.recordSequence().track(QStringLiteral("A1"))->length(), 100);
        QCOMPARE(rec.position(), 40);
        QVERIFY(rec.in() && !rec.out());
    }

    void positionAndEvents() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        TimelineView* view = rig.timeline.view();
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        // Clicking in the ruler moves the position.
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(static_cast<int>(view->xFor(30)) + 1, 5));
        QCOMPARE(rec.position(), 30);
        // Ctrl snaps to the nearest edit (p.609); Ctrl+Alt to the frame before.
        QTest::mouseClick(view, Qt::LeftButton, Qt::ControlModifier, QPoint(static_cast<int>(view->xFor(47)) + 1, 5));
        QCOMPARE(rec.position(), 50);
        QTest::mouseClick(view, Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier,
                          QPoint(static_cast<int>(view->xFor(47)) + 1, 5));
        QCOMPARE(rec.position(), 49);
        // Go to Next/Previous Event uses the edits, and Mark Clip marks the
        // segment (p.398, p.407).
        rig.composer.setActiveMonitor(MonitorId::Record);
        rec.setPosition(10);
        rig.composer.perform(QStringLiteral("move.go_to_next_event"));
        QCOMPARE(rec.position(), 50);
        rig.composer.perform(QStringLiteral("move.go_to_next_event"));
        QCOMPARE(rec.position(), 100);
        rig.composer.perform(QStringLiteral("move.go_to_previous_event"));
        QCOMPARE(rec.position(), 50);
        rec.setPosition(70);
        rig.composer.perform(QStringLiteral("edit.mark_clip"));
        QCOMPARE(rec.in(), 50);
        QCOMPARE(rec.out(), 99);
        // The end of a sequence is just after its last frame.
        rig.composer.perform(QStringLiteral("move.go_to_end"));
        QCOMPARE(rec.position(), 100);
    }

    void zoomAndFocus() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 25 * 600));
        Timeline& t = rig.timeline;
        const double fit = t.fitScale();
        QCOMPARE(t.scale(), fit);
        QVERIFY(!t.scrollBar()->isEnabled());
        rig.composer.state(MonitorId::Record).setPosition(25 * 300);
        // Focus: 90 pixels per second around the position, then back (p.608).
        t.toolButton(QStringLiteral("focus"))->click();
        QCOMPARE(t.scale(), 90.0 / 25);
        QVERIFY(t.isFocused());
        const double x = t.view()->xFor(25 * 300);
        QVERIFY(std::abs(x - (t.view()->headerWidth() + t.view()->width()) / 2.0) < 4);
        t.toolButton(QStringLiteral("focus"))->click();
        QCOMPARE(t.scale(), fit);
        // The scale bar zooms around the position indicator.
        const double before = t.view()->xFor(25 * 300);
        t.scaleBar()->setValue(500);
        QVERIFY(t.scale() > fit);
        QVERIFY(std::abs(t.view()->xFor(25 * 300) - before) < 2);
        QVERIFY(t.scrollBar()->isEnabled());
        // Zoom In on an area, then Zoom Back.
        t.zoomTo(1000, 1100);
        QVERIFY(std::abs(t.firstFrame() - 1000) < 1);
        t.zoomBack();
        QCOMPARE(t.scaleBar()->value(), 500);
        t.showEntireSequence();
        QCOMPARE(t.scale(), fit);
        // The Zoom In and Zoom Out buttons.
        t.toolButton(QStringLiteral("zoomIn"))->click();
        QCOMPARE(t.scale(), fit * 2);
        t.toolButton(QStringLiteral("zoomOut"))->click();
        QCOMPARE(t.scale(), fit);
        // Track Panel off hides the Track Selector (p.592).
        QMenu* fast = t.fastMenu();
        for (QAction* a : fast->actions()) {
            if (a->text() == QLatin1String("Track Panel")) a->trigger();
        }
        QCOMPARE(t.view()->headerWidth(), 0);
    }

    void showsTheSource() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("Alpha"), 80, QStringLiteral("V1 A1")));
        // Toggle Source/Record in Timeline (p.607).
        rig.timeline.toolButton(QStringLiteral("toggleSourceRecord"))->click();
        QVERIFY(rig.timeline.showsSource());
        QCOMPARE(rig.timeline.shown().duration(), 80);
        QCOMPARE(rig.rowNames(), (QStringList{"V1", "TC1", "A1"}));
        TimelineView* view = rig.timeline.view();
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(static_cast<int>(view->xFor(20)) + 1, 5));
        QCOMPARE(rig.composer.state(MonitorId::Source).position(), 20);
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 0);
        rig.timeline.setShowSource(false);
        QCOMPARE(rig.timeline.shown().duration(), 100);
    }

    void keyboardEdits() {
        Rig rig;
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap keys;
        keys.loadBuiltin(commands);
        rig.timeline.setKeyResolver([&](const QKeySequence& k) { return keys.commandFor(k); });
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.composer.setActiveMonitor(MonitorId::Record);
        rig.composer.state(MonitorId::Record).setPosition(70);
        // T-R-X: Mark Clip, Mark Out, Extract (p.639): the Top command.
        rig.timeline.view()->setFocus();
        QTest::keyClick(rig.timeline.view(), Qt::Key_T);
        QCOMPARE(rig.composer.state(MonitorId::Record).in(), 50);
        QTest::keyClick(rig.timeline.view(), Qt::Key_R);
        QCOMPARE(rig.composer.state(MonitorId::Record).out(), 70);
        QTest::keyClick(rig.timeline.view(), Qt::Key_X);
        QCOMPARE(rig.composer.state(MonitorId::Record).duration(), 79);
    }

    void segmentToolsSelectAndDrag() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        Timeline& t = rig.timeline;
        TimelineView* view = t.view();
        auto rowOf = [&](const QString& track) {
            for (const auto& r : view->rows()) {
                if (r.track == track) return r;
            }
            return TimelineView::Row{};
        };
        const auto v1 = rowOf(QStringLiteral("V1"));
        const QPoint onB(static_cast<int>(view->xFor(70)), v1.top + v1.height / 2);
        // Without a segment tool a click moves the position.
        QTest::mouseClick(view, Qt::LeftButton, {}, onB);
        QVERIFY(t.selection().isEmpty());
        // The red Lift/Overwrite tool (p.611).
        t.toolButton(QStringLiteral("smart.lift_overwrite"))->click();
        QVERIFY(t.isSegmentTool(model::EditKind::Overwrite));
        QTest::mouseClick(view, Qt::LeftButton, {}, onB);
        QCOMPARE(t.selection(), (QList<model::SegmentRef>{{"V1", 50}}));
        // Shift-click adds (p.611).
        const auto a1 = rowOf(QStringLiteral("A1"));
        QTest::mouseClick(view, Qt::LeftButton, Qt::ShiftModifier, QPoint(onB.x(), a1.top + a1.height / 2));
        QCOMPARE(t.selection().size(), 2);
        // Drag both 20 frames earlier: they overwrite and leave filler.
        const QPoint to(static_cast<int>(view->xFor(50)), onB.y());
        QTest::mousePress(view, Qt::LeftButton, {}, onB);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), view->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(view, &move);
        QTest::mouseRelease(view, Qt::LeftButton, {}, to);
        const model::Sequence seq = rig.recordSequence();
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.at(1).clipId, QStringLiteral("b"));
        QVERIFY(seq.track(QStringLiteral("V1"))->segments.last().isFiller());
        QCOMPARE(seq.duration(), 100);
        QCOMPARE(t.history().descriptions().first(), QStringLiteral("Lift/Overwrite"));
        QCOMPARE(t.selection().size(), 2); // still selected (p.620)
        // Delete with the Extract/Splice-in tool closes the gap (p.621).
        t.setSegmentTool(model::EditKind::Overwrite, false);
        t.setSegmentTool(model::EditKind::SpliceIn, true);
        t.setSelection({{"V1", 0}});
        view->setFocus();
        QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.first().clipId, QStringLiteral("b"));
        // Alt-drag draws a lasso; all segments inside are selected.
        t.setSegmentTool(model::EditKind::SpliceIn, false);
        QTest::mousePress(view, Qt::LeftButton, Qt::AltModifier, QPoint(static_cast<int>(view->xFor(0)) + 1, v1.top + 2));
        const QPoint end(static_cast<int>(view->xFor(100)) + 2, a1.top + a1.height - 2);
        QMouseEvent lasso(QEvent::MouseMove, QPointF(end), view->mapToGlobal(QPointF(end)), Qt::NoButton, Qt::LeftButton,
                          Qt::AltModifier);
        QApplication::sendEvent(view, &lasso);
        QTest::mouseRelease(view, Qt::LeftButton, Qt::AltModifier, end);
        QVERIFY(t.selection().size() >= 3);
        QVERIFY(t.isSegmentTool(model::EditKind::Overwrite)); // a lasso turns it on (p.612)
    }

    void moveClipUpAndDown() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.timeline.setSegmentTool(model::EditKind::Overwrite, true);
        rig.timeline.setSelection({{"V1", 0}});
        // Up a track makes V2 (p.613).
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.move_clip_up")));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V2"))->segments.first().clipId, QStringLiteral("a"));
        QCOMPARE(rig.timeline.selection(), (QList<model::SegmentRef>{{"V2", 0}}));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.move_clip_down")));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.first().clipId, QStringLiteral("a"));
    }

    void locksAndSyncLocks() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        TimelineView* view = rig.timeline.view();
        TimelineView::Row a2;
        TimelineView::Row tc;
        for (const auto& r : view->rows()) {
            if (r.track == QLatin1String("A2")) a2 = r;
            if (r.track == QLatin1String("TC1")) tc = r;
        }
        // The Sync Lock button, and Sync Lock All next to TC1 (p.493, p.633).
        QTest::mouseClick(view, Qt::LeftButton, {}, view->syncButton(a2).center());
        QVERIFY(rig.recordSequence().track(QStringLiteral("A2"))->syncLocked);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->syncButton(tc).center());
        const model::Sequence allLocked = rig.recordSequence();
        for (const auto& t : allLocked.tracks()) QVERIFY(t.syncLocked);
        rig.timeline.perform(QStringLiteral("edit.sync_lock_all_tracks"));
        const model::Sequence noneLocked = rig.recordSequence();
        for (const auto& t : noneLocked.tracks()) QVERIFY(!t.syncLocked);
        // Lock Track from the track button's menu; a locked track is not
        // edited (p.633).
        QMenu* menu = rig.timeline.contextMenu(QStringLiteral("A2"));
        QCOMPARE(menu->actions().first()->text(), QStringLiteral("Lock Track"));
        menu->actions().first()->trigger();
        delete menu;
        QVERIFY(rig.recordSequence().track(QStringLiteral("A2"))->locked);
        rig.composer.state(MonitorId::Record).setPosition(10);
        rig.composer.state(MonitorId::Record).markIn();
        rig.composer.state(MonitorId::Record).setPosition(19);
        rig.composer.state(MonitorId::Record).markOut();
        rig.timeline.perform(QStringLiteral("edit.extract"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("A2"))->length(), 100);
        QCOMPARE(rig.recordSequence().track(QStringLiteral("A1"))->length(), 90);
        // Clip > Unlock Tracks on the selected tracks.
        rig.timeline.perform(QStringLiteral("clip.unlock_tracks"));
        QVERIFY(!rig.recordSequence().track(QStringLiteral("A2"))->locked);
        // Locks are not undo steps.
        QCOMPARE(rig.timeline.history().descriptions(), (QStringList{"Extract"}));
    }

    void clipboard() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        rig.timeline.perform(QStringLiteral("edit.clipboard_contents"));
        QVERIFY(!rig.timeline.lastMessage().isEmpty()); // empty
        rec.setPosition(40);
        rec.markIn();
        rec.setPosition(59);
        rec.markOut();
        // Copy to Clipboard leaves the sequence alone (p.479).
        rig.timeline.perform(QStringLiteral("edit.copy_to_clipboard"));
        QVERIFY(rig.timeline.clipboard());
        QCOMPARE(rig.timeline.clipboard()->durationFrames, 20);
        QCOMPARE(rec.duration(), 100);
        // Clipboard Contents into the Source monitor (p.480).
        rig.timeline.perform(QStringLiteral("edit.clipboard_contents"));
        const model::MonitorState& src = rig.composer.state(MonitorId::Source);
        QCOMPARE(src.current()->item.name, QStringLiteral("Clipboard Contents.1"));
        // ... and spliced back in at the start.
        rec.clearBoth();
        rec.setPosition(0);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.splice_in")));
        QCOMPARE(rec.duration(), 120);
        const model::Sequence seq = rig.recordSequence();
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.first().clipId, QStringLiteral("a"));
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.at(1).clipId, QStringLiteral("b"));
        // Extract fills the Clipboard too.
        rec.setPosition(0);
        rec.markIn();
        rec.setPosition(4);
        rec.markOut();
        rig.timeline.perform(QStringLiteral("edit.extract"));
        QCOMPARE(rig.timeline.clipboard()->durationFrames, 5);
    }

    void copyAndPasteSegments() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.timeline.setSegmentTool(model::EditKind::SpliceIn, true);
        rig.timeline.setSelection({{"V1", 50}});
        // Ctrl+C, then Ctrl+V at the position (p.622-623).
        rig.timeline.perform(QStringLiteral("edit.copy"));
        rig.composer.state(MonitorId::Record).setPosition(0);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.paste")));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.first().clipId, QStringLiteral("b"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->length(), 150);
        QCOMPARE(rig.recordSequence().track(QStringLiteral("A1"))->length(), 100); // only V1 was copied
        // Ctrl+X cuts with the tool's kind of delete.
        rig.timeline.setSelection({{"V1", 0}});
        rig.timeline.perform(QStringLiteral("edit.cut"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->length(), 100);
    }

    void replaceAddEditTopTail() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("Alpha"), 200));
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        // Replace: the segment under the position, synced on the positions
        // (p.467-468).
        rig.composer.state(MonitorId::Source).setPosition(100);
        rec.setPosition(60);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.replace_edit")));
        QVERIFY2(rig.timeline.lastMessage().isEmpty(), qPrintable(rig.timeline.lastMessage()));
        model::Segment replaced = rig.recordSequence().track(QStringLiteral("V1"))->segments.at(1);
        QCOMPARE(replaced.name, QStringLiteral("Alpha"));
        QCOMPARE(replaced.sourceIn, 90);
        QCOMPARE(replaced.length, 50);
        // Add Edit at the position, then Remove Match Frame Edits (p.639-640).
        rec.setPosition(70);
        rig.timeline.perform(QStringLiteral("edit.add_edit"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.size(), 3);
        QCOMPARE(rig.recordSequence().editPoints({QStringLiteral("A1")}), (QList<std::int64_t>{0, 50, 70, 100}));
        rig.timeline.perform(QStringLiteral("timeline.remove_match_frame_edits"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.size(), 2);
        // Top: from the start of the segment to the position (p.639).
        rec.setPosition(80);
        rig.timeline.perform(QStringLiteral("edit.top"));
        QCOMPARE(rec.duration(), 69);
        QCOMPARE(rec.position(), 50);
        // Tail: from the position to the end of the segment.
        rec.setPosition(10);
        rig.timeline.perform(QStringLiteral("edit.tail"));
        QCOMPARE(rec.duration(), 29);
    }

    void matchFrameLoadsTheSource() {
        Rig rig;
        const BinItem a = clip(QStringLiteral("Alpha"), 200);
        rig.composer.setItemResolver([&](const QString&, const QString& id) -> std::optional<BinItem> {
            if (id == a.id) return a;
            return std::nullopt;
        });
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), a);
        rig.composer.state(MonitorId::Source).setPosition(30);
        rig.timeline.perform(QStringLiteral("edit.splice_in")); // a new sequence
        rig.composer.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("Other"), 50));
        rig.composer.state(MonitorId::Record).setPosition(15);
        rig.composer.setActiveMonitor(MonitorId::Record);
        // The clip, at the matching frame, marked IN (p.432-433).
        QVERIFY(rig.timeline.perform(QStringLiteral("other.match_frame")));
        const model::MonitorState& src = rig.composer.state(MonitorId::Source);
        QCOMPARE(src.current()->item.id, a.id);
        QCOMPARE(src.position(), 45);
        QCOMPARE(src.in(), 45);
    }

    void topToolbarAndMenus() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.composer.state(MonitorId::Record).setPosition(25);
        rig.composer.moveTo(MonitorId::Record, 25);
        // The master timecode above the Track Selector (p.607).
        QCOMPARE(rig.timeline.timecodeDisplay()->text(), rig.composer.state(MonitorId::Record).tracking(model::TrackingFormat::Master));
        QVERIFY(!rig.timeline.timecodeDisplay()->text().isEmpty());
        for (const char* id : {"smart.extract_splice_in", "smart.lift_overwrite", "edit.lift", "edit.extract",
                               "edit.copy_to_clipboard", "edit.add_edit", "edit.replace_edit", "edit.top", "edit.tail",
                               "other.match_frame"}) {
            QVERIFY2(rig.timeline.toolButton(QLatin1String(id)), id);
        }
        // The Timeline's right-click menu (p.613, p.635).
        QMenu* menu = rig.timeline.contextMenu();
        QStringList texts;
        for (QAction* a : menu->actions()) texts << a->text();
        QVERIFY(texts.contains(QStringLiteral("New")));
        QVERIFY(texts.contains(QStringLiteral("Select")));
        QVERIFY(texts.contains(QStringLiteral("Lock Tracks")));
        // New > Sequence is not the Timeline's own.
        QSignalSpy requested(&rig.timeline, &Timeline::commandRequested);
        menu->actions().first()->menu()->actions().first()->trigger();
        QCOMPARE(requested.count(), 1);
        // Select > To the Right of the position.
        rig.composer.state(MonitorId::Record).setPosition(50);
        for (QAction* a : menu->actions()) {
            if (a->text() == QLatin1String("Select")) {
                for (QAction* b : a->menu()->actions()) {
                    if (b->text() == QLatin1String("To the Right")) b->trigger();
                }
            }
        }
        QCOMPARE(rig.timeline.selection().size(), 3);
        delete menu;
    }

    void trimMode() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        Timeline& t = rig.timeline;
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        rec.setPosition(45);
        // The Trim Mode button: dual rollers at the nearest transition on
        // every selected track (p.655).
        t.toolButton(QStringLiteral("trim.trim_mode"))->click();
        QVERIFY(t.isTrimMode());
        QCOMPARE(t.rollers().size(), 3);
        QCOMPARE(t.rollers().first(), (model::TrimRoller{"V1", 50, model::TrimSide::Both}));
        QCOMPARE(rec.position(), 50);
        // Big Trim display (p.648).
        QVERIFY(rig.composer.trimDisplay().on);
        QVERIFY(rig.composer.trimCounter(MonitorId::Source)->isVisibleTo(&rig.composer));
        QVERIFY(rig.composer.trimBar()->isVisibleTo(&rig.composer));
        // Trim buttons (p.656): a dual-roller trim keeps the length.
        rig.composer.trimBar()->findChild<QToolButton*>(QStringLiteral("trim.trim_forward_1_frame"))->click();
        QCOMPARE(rig.recordSequence().editPoints({QStringLiteral("V1")}), (QList<std::int64_t>{0, 51, 100}));
        QCOMPARE(rig.composer.trimDisplay().aFrames, 1);
        QCOMPARE(rig.composer.trimCounter(MonitorId::Source)->text(), QStringLiteral("1"));
        // Cycle Trim Sides: the A-side, a ripple trim (p.651, p.654).
        t.perform(QStringLiteral("trim.cycle_trim_sides"));
        QCOMPARE(t.rollers().first().side, model::TrimSide::A);
        QVERIFY(!rig.composer.trimDisplay().bActive);
        t.perform(QStringLiteral("trim.trim_backward_10_frames"));
        QCOMPARE(rec.duration(), 90); // 25 fps: 10 frames
        QCOMPARE(t.rollers().first().frame, 41);
        // The B counter selects the B-side; Shift both (p.651).
        emit rig.composer.trimSideRequested(model::TrimSide::B);
        QCOMPARE(t.rollers().first().side, model::TrimSide::B);
        // Trims are undo steps.
        QCOMPARE(t.history().descriptions().first(), QStringLiteral("Trim"));
        // A frame step leaves Trim mode (p.651).
        rig.composer.setActiveMonitor(MonitorId::Record);
        rig.composer.perform(QStringLiteral("move.step_forward_one_frame"));
        QVERIFY(!t.isTrimMode());
        QVERIFY(!rig.composer.trimDisplay().on);
        QVERIFY(!rig.composer.trimBar()->isVisibleTo(&rig.composer));
        QVERIFY(rig.composer.positionBar(MonitorId::Source)->isVisibleTo(&rig.composer));
    }

    void trimModeDirections() {
        // Edits at 20, 50 and 80 on every track: the nearest, the nearest before and the nearest
        // after the position (p.655).
        Rig rig;
        BinItem cut = sequence(QStringLiteral("Cut"), 100);
        model::Sequence seq = model::Sequence::fromItem(cut);
        for (const QString& tr : seq.trackNames()) {
            seq.addEdit(tr, 20);
            seq.addEdit(tr, 80);
        }
        seq.storeIn(cut);
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), cut);
        Timeline& t = rig.timeline;
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        auto rollerAfter = [&](std::int64_t at, int direction) -> std::int64_t {
            t.exitTrimMode();
            rec.setPosition(at);
            if (!t.enterTrimMode(direction)) return -1;
            return t.rollers().first().frame;
        };
        QCOMPARE(rollerAfter(45, 0), 50);
        QCOMPARE(rollerAfter(30, 0), 20);
        QCOMPARE(rollerAfter(45, -1), 20);
        QCOMPARE(rollerAfter(70, -1), 50);
        QCOMPARE(rollerAfter(55, 1), 80);
        QCOMPARE(rollerAfter(25, 1), 50);
        // Nothing after the last edit.
        QCOMPARE(rollerAfter(85, 1), -1);
    }

    void verticalScrollStopsAtLastRow() {
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        for (int i = 0; i < 6; ++i) QVERIFY(rig.timeline.perform(QStringLiteral("timeline.new.audio_track.mono")));
        rig.timeline.resize(900, 200);
        settle();
        TimelineView* view = rig.timeline.view();
        const int limit = view->contentHeight() - view->height();
        QVERIFY(limit > 0);
        // Scrolling far down stops with the last row's bottom at the view's bottom.
        view->setTopOffset(100000);
        QCOMPARE(view->topOffset(), limit);
        const auto last = view->rows().constLast();
        QCOMPARE(last.top + last.height, view->height());
        // Asking again from there does not creep further.
        view->setTopOffset(100000);
        QCOMPARE(view->topOffset(), limit);
        // The scroll bar's range is the same.
        QScrollBar* bar = nullptr;
        for (QScrollBar* b : rig.timeline.findChildren<QScrollBar*>()) {
            if (b->orientation() == Qt::Vertical) bar = b;
        }
        QVERIFY(bar);
        rig.timeline.resize(900, 201);
        settle();
        QCOMPARE(bar->maximum(), view->contentHeight() - view->height());
    }

    void transitionKeysAndTools() {
        Rig rig;
        CommandRegistry commands;
        commands.loadBuiltin();
        KeyboardMap keys;
        keys.loadBuiltin(commands);
        rig.timeline.setKeyResolver([&](const QKeySequence& k) { return keys.commandFor(k); });
        BinItem cut = sequence(QStringLiteral("Cut"), 100);
        model::Sequence seq = model::Sequence::fromItem(cut);
        for (const QString& tr : seq.trackNames()) seq.addEdit(tr, 80);
        seq.storeIn(cut);
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), cut);
        rig.composer.state(MonitorId::Record).setPosition(60);
        TimelineView* view = rig.timeline.view();
        view->setFocus();
        // U, A and S: nearest, previous and next transition (p.651).
        QTest::keyClick(view, Qt::Key_S);
        QCOMPARE(rig.timeline.rollers().first().frame, 80);
        QTest::keyClick(view, Qt::Key_A);
        QCOMPARE(rig.timeline.rollers().first().frame, 50);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!rig.timeline.isTrimMode());
        // The Ripple Trim tool: a click just left of an edit is its A-side
        // (p.650-654).
        rig.timeline.toolButton(QStringLiteral("smart.ripple_trim"))->click();
        TimelineView::Row v1;
        for (const auto& r : view->rows()) {
            if (r.track == QLatin1String("V1")) v1 = r;
        }
        const QPoint a(static_cast<int>(view->xFor(50)) - 6, v1.top + v1.height / 2);
        QCOMPARE(view->transitionAt(a)->side, model::TrimSide::A);
        QTest::mousePress(view, Qt::LeftButton, {}, a);
        QVERIFY(rig.timeline.isTrimMode());
        QCOMPARE(rig.timeline.trimKind(), model::TrimKind::Ripple);
        // Dragging it trims.
        const QPoint to(static_cast<int>(view->xFor(55)) - 6, a.y());
        QMouseEvent move(QEvent::MouseMove, QPointF(to), view->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(view, &move);
        QTest::mouseRelease(view, Qt::LeftButton, {}, to);
        QCOMPARE(rig.composer.state(MonitorId::Record).duration(), 105);
    }

    void slipSlideAndExtend() {
        Rig rig;
        BinItem cut = sequence(QStringLiteral("Cut"), 100);
        model::Sequence seq = model::Sequence::fromItem(cut);
        seq.addEdit(QStringLiteral("V1"), 80);
        seq.storeIn(cut);
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), cut);
        Timeline& t = rig.timeline;
        // Slip the middle segment: earlier frames are not there (it starts
        // at frame 0 of its clip), later ones are (p.663).
        t.setSlip({{"V1", 50}}, false);
        QVERIFY(t.isTrimMode());
        t.perform(QStringLiteral("trim.trim_backward_1_frame"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.at(1).sourceIn, 1);
        // Slide it: its neighbours are trimmed.
        t.setSlip({{"V1", 50}}, true);
        t.perform(QStringLiteral("trim.trim_forward_1_frame"));
        QCOMPARE(rig.recordSequence().editPoints({QStringLiteral("V1")}), (QList<std::int64_t>{0, 51, 81, 100}));
        t.exitTrimMode();
        // Slip Left in Source/Record mode (p.665).
        rig.composer.state(MonitorId::Record).setPosition(60);
        t.perform(QStringLiteral("trim.slip_left"));
        QCOMPARE(rig.recordSequence().track(QStringLiteral("V1"))->segments.at(1).sourceIn, 2);
        // Extend: the previous edit forward to just after the OUT (p.661).
        // (Back to an IN would need frames before the clips' starts.)
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        rec.clearBoth();
        rec.setPosition(60);
        rec.markOut();
        t.perform(QStringLiteral("trim.extend"));
        QVERIFY(t.lastMessage().isEmpty());
        QCOMPARE(rig.recordSequence().editPoints({QStringLiteral("A1")}), (QList<std::int64_t>{0, 61, 100}));
        QCOMPARE(t.history().descriptions().first(), QStringLiteral("Extend"));
    }

    // Change Source Track (p.595; 2026.8 guide p.507; What's New 2026.8 p.7): on an audio segment the
    // clip's audio tracks; for a subclip, More lists the master clip's other audio tracks.
    void changeSourceTrack() {
        Rig rig;
        BinItem master = clip(QStringLiteral("Interview"), 200, QStringLiteral("V1 A1-4"));
        BinItem sub = master;
        sub.id = model::newItemId();
        sub.kind = BinItemKind::Subclip;
        sub.name = QStringLiteral("Interview.Sub.01");
        sub.durationFrames = 50;
        sub.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1-2")); // two of the master's four
        model::ClipSource from;
        from.kind = model::ClipSource::Kind::Clip;
        from.mob = model::mobIdOf(master);
        from.offset = 30;
        sub.sources << from;
        const QHash<QString, BinItem> items{{master.id, master}, {sub.id, sub}};
        rig.composer.setItemResolver([&](const QString&, const QString& id) -> std::optional<BinItem> {
            if (items.contains(id)) return items.value(id);
            return std::nullopt;
        });
        rig.composer.setMobResolver([&](const MobId& mob) -> std::optional<std::pair<QString, BinItem>> {
            if (mob == model::mobIdOf(master)) return std::pair{QStringLiteral("masters"), master};
            return std::nullopt;
        });
        // A1 holds 10 frames of the subclip from its frame 5, at -4 dB and panned right.
        BinItem cut = sequence(QStringLiteral("Cut"), 100);
        model::Sequence seq = model::Sequence::fromItem(cut);
        model::Segment s;
        s.clipId = sub.id;
        s.binId = QStringLiteral("bin");
        s.name = sub.name;
        s.sourceTrack = QStringLiteral("A1");
        s.sourceIn = 5;
        s.length = 10;
        s.gain = -4.0;
        s.pan = 60;
        seq.overwrite(QStringLiteral("A1"), 20, {s});
        seq.storeIn(cut);
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), cut);
        const model::SegmentRef ref{QStringLiteral("A1"), 20};

        auto submenu = [](QMenu* menu, const QString& name) -> QMenu* { return menu->findChild<QMenu*>(name); };
        auto texts = [](QMenu* menu) {
            QStringList out;
            for (QAction* a : menu->actions()) {
                if (!a->isSeparator()) out << a->text();
            }
            return out;
        };
        // Not on a video segment, nor over a track button.
        QVERIFY(!submenu(rig.timeline.contextMenu({}, model::SegmentRef{QStringLiteral("V1"), 0}), QStringLiteral("changeSourceTrack")));
        QVERIFY(!submenu(rig.timeline.contextMenu(QStringLiteral("A1")), QStringLiteral("changeSourceTrack")));

        QMenu* menu = rig.timeline.contextMenu({}, ref);
        QMenu* change = submenu(menu, QStringLiteral("changeSourceTrack"));
        QVERIFY(change);
        QCOMPARE(texts(change), (QStringList{"A1", "A2", "More"}));
        QVERIFY(change->actions().first()->isChecked());
        QMenu* more = submenu(change, QStringLiteral("changeSourceTrackMore"));
        QVERIFY(more && more->isEnabled());
        QCOMPARE(texts(more), (QStringList{"A3", "A4"}));

        // A2 of the subclip: only the source track changes.
        change->actions().at(1)->trigger();
        auto now = model::segmentFor(rig.recordSequence(), ref);
        QVERIFY(now);
        QCOMPARE(now->sourceTrack, QStringLiteral("A2"));
        QCOMPARE(now->clipId, sub.id);
        QCOMPARE(now->gain, -4.0);
        QCOMPARE(now->pan, 60);
        QCOMPARE(now->length, std::int64_t{10});

        // More > A4: the master clip's track, at the same frames of the master.
        menu = rig.timeline.contextMenu({}, ref);
        more = submenu(submenu(menu, QStringLiteral("changeSourceTrack")), QStringLiteral("changeSourceTrackMore"));
        more->actions().last()->trigger();
        now = model::segmentFor(rig.recordSequence(), ref);
        QVERIFY(now);
        QCOMPARE(now->clipId, master.id);
        QCOMPARE(now->binId, QStringLiteral("masters"));
        QCOMPARE(now->sourceTrack, QStringLiteral("A4"));
        QCOMPARE(now->sourceIn, std::int64_t{35});
        QCOMPARE(now->gain, -4.0);
        QCOMPARE(now->pan, 60);
        // Each change is an undo step; the subclip itself was not changed.
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(model::segmentFor(rig.recordSequence(), ref)->clipId, sub.id);
        QCOMPARE(items.value(sub.id).fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));

        // A master clip has no More.
        rig.timeline.perform(QStringLiteral("edit.undo"));
        model::Sequence plain = rig.recordSequence();
        QVERIFY(model::changeSourceTrack(plain, ref, master, QStringLiteral("masters"), 30, QStringLiteral("A3")));
        QVERIFY(!model::changeSourceTrack(plain, ref, QStringLiteral("V1"))); // not an audio track
        QVERIFY(!model::changeSourceTrack(plain, model::SegmentRef{QStringLiteral("A1"), 21}, QStringLiteral("A2"))); // no segment starts there
        BinItem withMaster = rig.composer.state(MonitorId::Record).current()->item;
        plain.storeIn(withMaster);
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), withMaster);
        change = submenu(rig.timeline.contextMenu({}, ref), QStringLiteral("changeSourceTrack"));
        QCOMPARE(texts(change), (QStringList{"A1", "A2", "A3", "A4"}));
    }

    void markers() {
        Rig rig;
        rig.timeline.setMarkerDialogEnabled(false);
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(QStringLiteral("Cut"), 100));
        rig.composer.setActiveMonitor(MonitorId::Record);
        model::MonitorState& rec = rig.composer.state(MonitorId::Record);
        // Add Marker on the frame, on the first selected track (p.413).
        rec.setPosition(20);
        QVERIFY(rig.timeline.perform(QStringLiteral("more.add_marker")));
        rec.setPosition(70);
        rig.composer.button(MonitorId::Record, QStringLiteral("more.add_marker"));
        rig.timeline.addMarker(QStringLiteral("Blue"));
        QList<model::Marker> markers = model::markersOf(rec.current()->item);
        QCOMPARE(markers.size(), 2);
        QCOMPARE(markers.first().track, QStringLiteral("V1"));
        QCOMPARE(markers.first().color, QStringLiteral("Red"));
        QCOMPARE(rig.stored.value(rec.current()->item.id).markers.size(), 2);
        // An undo step on a sequence.
        QCOMPARE(rig.timeline.history().descriptions().first(), QStringLiteral("Add Marker"));
        // A spanned marker over IN to OUT, on TC1 (p.414-415).
        rec.setPosition(30);
        rec.markIn();
        rec.setPosition(39);
        rec.markOut();
        QVERIFY(rig.timeline.addMarker(QStringLiteral("Green"), true));
        markers = model::markersOf(rec.current()->item);
        QCOMPARE(markers.at(1).track, QStringLiteral("TC1"));
        QCOMPARE(markers.at(1).length, 10);
        // Go to Previous/Next Marker (p.419).
        rec.setPosition(50);
        rig.timeline.perform(QStringLiteral("move.go_to_next_marker"));
        QCOMPARE(rec.position(), 70);
        rig.timeline.perform(QStringLiteral("move.go_to_previous_marker"));
        QCOMPARE(rec.position(), 30);
        // Mark Markers: between the two markers either side (p.419).
        rec.clearBoth();
        rec.setPosition(50);
        rig.timeline.perform(QStringLiteral("edit.mark_markers"));
        QCOMPARE(rec.in(), 30);
        QCOMPARE(rec.out(), 69);
        // The Markers window (p.419-421).
        MarkersWindow window(rig.timeline);
        QCOMPARE(window.monitor(), MonitorId::Record);
        QCOMPARE(window.table()->rowCount(), 3);
        QCOMPARE(window.table()->item(2, MarkersWindow::TrackColumn)->text(), QStringLiteral("V1"));
        window.jumpTo(0);
        QCOMPARE(rec.position(), 20);
        QMenu* menu = window.contextMenu(0);
        for (QAction* a : menu->actions()) {
            if (a->text() == QLatin1String("Change Color")) {
                for (QAction* c : a->menu()->actions()) {
                    if (c->text() == QLatin1String("Purple")) c->trigger();
                }
            }
        }
        delete menu;
        QCOMPARE(model::markersOf(rec.current()->item).first().color, QStringLiteral("Purple"));
        window.table()->selectRow(0);
        window.deleteSelected();
        QCOMPARE(window.table()->rowCount(), 2);
    }

    void markerDialog() {
        model::Marker m;
        m.frame = 12;
        m.color = QStringLiteral("Blue");
        m.track = QStringLiteral("A1");
        m.user = QStringLiteral("editor");
        MarkerDialog dialog(m, {QStringLiteral("V1"), QStringLiteral("A1")});
        QCOMPARE(dialog.colorBox()->count(), 16);
        QCOMPARE(dialog.colorBox()->currentText(), QStringLiteral("Blue"));
        QCOMPARE(dialog.trackBox()->currentText(), QStringLiteral("A1"));
        dialog.nameEdit()->setText(QStringLiteral("Cutaway"));
        dialog.commentEdit()->setPlainText(QStringLiteral("cover the jump"));
        dialog.trackBox()->setCurrentText(QStringLiteral("V1"));
        const model::Marker out = dialog.marker();
        QCOMPARE(out.name, QStringLiteral("Cutaway"));
        QCOMPARE(out.comment, QStringLiteral("cover the jump"));
        QCOMPARE(out.track, QStringLiteral("V1"));
        QCOMPARE(out.frame, 12);
    }

    void mainWindowEdits() {
        QTemporaryDir dir;
        model::ProjectSettings s;
        s.name = QStringLiteral("Doc");
        s.format = projectFormatPresets().front();
        const auto entry = *model::ProjectStore(dir.path()).create(s);
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.show();
        window.setProject(entry);
        BinContainer& bins = window.binContainer();
        BinDocument* doc = bins.activeDocument();
        QVERIFY(doc);
        BinItem a = clip(QStringLiteral("Alpha"), 100);
        a.editRate = entry.settings.format.editRate;
        doc->addItem(a);
        Composer& composer = window.composer();
        composer.load(MonitorId::Source, doc->nodeId(), a);
        // The Splice-in button under the Source monitor makes and fills a
        // sequence in the active bin.
        composer.button(MonitorId::Source, QStringLiteral("edit.splice_in"))->click();
        QCOMPARE(doc->bin().items.size(), 2);
        const BinItem& made = doc->bin().items.last();
        QCOMPARE(made.kind, BinItemKind::Sequence);
        QCOMPARE(made.durationFrames, 100);
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, made.id);
        // Undo from the Edit menu.
        commands.action(QStringLiteral("edit.undo"))->trigger();
        QCOMPARE(doc->bin().items.last().durationFrames, 0);
        commands.action(QStringLiteral("edit.redo"))->trigger();
        QCOMPARE(doc->bin().items.last().durationFrames, 100);
        // Timeline > New > Video Track (Ctrl+Y).
        commands.action(QStringLiteral("timeline.new.video_track"))->trigger();
        QVERIFY(window.timeline().shown().track(QStringLiteral("V2")));
        // File > New Sequence loads it into the Record monitor (p.457).
        commands.action(QStringLiteral("timeline.new.sequence"))->trigger();
        QCOMPARE(composer.state(MonitorId::Record).current()->item.id, doc->bin().items.last().id);
        QCOMPARE(window.timeline().shown().trackNames(), (QStringList{"V1", "A1", "A2"}));
        window.clearProject();
        QVERIFY(!window.timeline().history().canUndo());
    }
};

QTEST_MAIN(TestTimeline)
#include "tst_timeline.moc"

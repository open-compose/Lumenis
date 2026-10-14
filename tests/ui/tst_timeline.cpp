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
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
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

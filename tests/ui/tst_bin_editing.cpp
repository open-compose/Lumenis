// Bin editing into the Timeline (Editing Guide p.623-624, p.1153, p.1220; docs/spec/bin-editing.md):
// with Enable edit from bin, a clip dragged from a bin is spliced in or overwritten with the segment
// tool's edit, Segment Drag Snap snaps it to edit points, and V and B edit the bin's selected clip.
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QDropEvent>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include <cmath>
#include <memory>

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

BinItem clip(const QString& name, std::int64_t length) {
    BinItem c;
    c.id = name;
    c.name = name;
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = length;
    c.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1"));
    return c;
}

QMimeData* binDrag(const QStringList& ids) {
    auto* mime = new QMimeData;
    mime->setData(binItemsMime(), (QStringList{QStringLiteral("bin")} + ids).join(QLatin1Char('\n')).toUtf8());
    return mime;
}

// A Timeline over V1 a (50) b (50), A1 x (100), and a bin holding the clip "c" (20 frames).
struct Fixture {
    Composer composer;
    Timeline timeline{composer};
    BinItem item;

    Fixture() {
        timeline.resize(1000, 360);
        timeline.show();
        composer.setItemResolver([](const QString&, const QString& id) -> std::optional<BinItem> {
            if (id == QLatin1String("c") || id == QLatin1String("d")) return clip(id, 20);
            return std::nullopt;
        });
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 50), seg(QStringLiteral("b"), QStringLiteral("V1"), 50)});
        seq.splice(QStringLiteral("A1"), 0, {seg(QStringLiteral("x"), QStringLiteral("A1"), 100)});
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
    QPoint at(const QString& track, std::int64_t frame) const {
        for (const TimelineView::Row& r : timeline.view()->rows()) {
            if (r.track == track) return QPoint(static_cast<int>(std::ceil(timeline.view()->xFor(frame))), r.top + r.height / 2);
        }
        return {};
    }
    // Drags `data` to `pos` and drops it there; whether the drop was taken. Takes `data`.
    bool drop(QMimeData* data, const QPoint& pos) {
        const std::unique_ptr<QMimeData> owned(data);
        const QMimeData* mime = owned.get();
        QWidget* view = timeline.view();
        QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &enter);
        if (!enter.isAccepted()) return false;
        QDragMoveEvent move(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &move);
        QDropEvent dropped(QPointF(pos), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &dropped);
        return dropped.isAccepted();
    }
};

} // namespace

class TestBinEditing : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void segmentToolPicksTheEdit() {
        // "If you do not select a segment tool, or if you select both tools, Media Composer defaults to
        // the segment tool specified in the Timeline Settings dialog box" (p.624).
        Fixture f;
        QCOMPARE(f.timeline.binEditKind(), EditKind::Overwrite);
        f.set([](Timeline::Options& o) { o.defaultSegmentTool = EditKind::SpliceIn; });
        QCOMPARE(f.timeline.binEditKind(), EditKind::SpliceIn);
        f.timeline.setSegmentTool(EditKind::Overwrite, true);
        QCOMPARE(f.timeline.binEditKind(), EditKind::Overwrite);
        f.timeline.setSegmentTool(EditKind::SpliceIn, true);
        QCOMPARE(f.timeline.binEditKind(), EditKind::SpliceIn); // both: the default
    }

    void editClipAtAFrame() {
        Fixture f;
        // Overwrite: same length (p.624).
        QVERIFY(f.timeline.editClip(EditKind::Overwrite, QStringLiteral("bin"), clip(QStringLiteral("c"), 20), 10));
        QCOMPARE(names(f.recorded(), QStringLiteral("V1")), (QStringList{"a", "c", "a", "b"}));
        QCOMPARE(f.recorded().duration(), 100);
        // Splice-in: longer.
        QVERIFY(f.timeline.editClip(EditKind::SpliceIn, QStringLiteral("bin"), clip(QStringLiteral("d"), 20), 100));
        QCOMPARE(f.recorded().duration(), 120);
        QCOMPARE(names(f.recorded(), QStringLiteral("A1")).last(), QStringLiteral("d"));
        // "mark In and Out points for each clip ... Otherwise, the entire clip is edited" (p.624): the marks
        // the clip has in the Source monitor are used.
        model::MonitorState& source = f.composer.state(MonitorId::Source);
        source.setPosition(5);
        source.markIn();
        source.setPosition(9);
        source.markOut();
        QVERIFY(f.timeline.editClip(EditKind::SpliceIn, QStringLiteral("bin"), clip(QStringLiteral("d"), 20), 0));
        QCOMPARE(f.recorded().duration(), 125);
    }

    void dragFromABin() {
        Fixture f;
        // Off: a bin clip is not taken.
        QVERIFY(!f.drop(binDrag({QStringLiteral("c")}), f.at(QStringLiteral("V1"), 10)));
        f.set([](Timeline::Options& o) { o.editFromBin = true; });
        // "You can edit only one clip at a time" (p.624).
        QVERIFY(!f.drop(binDrag({QStringLiteral("c"), QStringLiteral("d")}), f.at(QStringLiteral("V1"), 10)));
        f.timeline.setSegmentTool(EditKind::SpliceIn, true);
        QVERIFY(f.drop(binDrag({QStringLiteral("c")}), f.at(QStringLiteral("V1"), 30)));
        Sequence seq = f.recorded();
        QCOMPARE(seq.duration(), 120);
        std::int64_t start = 0;
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.at(seq.track(QStringLiteral("V1"))->segmentAt(30, &start)).name, QStringLiteral("c"));
        QCOMPARE(start, 30);
    }

    void segmentDragSnap() {
        // "clips snap to an existing transition endpoint or to the position bar when you drag them from a
        // bin to the Timeline" (p.1220).
        Fixture f;
        f.set([](Timeline::Options& o) {
            o.editFromBin = true;
            o.segmentDragSnap = true;
        });
        f.timeline.setSegmentTool(EditKind::SpliceIn, true);
        QVERIFY(f.drop(binDrag({QStringLiteral("c")}), f.at(QStringLiteral("V1"), 49)));
        std::int64_t start = 0;
        const Sequence seq = f.recorded();
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.at(seq.track(QStringLiteral("V1"))->segmentAt(55, &start)).name, QStringLiteral("c"));
        QCOMPARE(start, 50);
    }

    void vAndBInABin() {
        // "Select a clip in the bin ... Press the V key to perform a splice-in edit" (p.624).
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 800);
        window.show();
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        window.settings().setValue(QStringLiteral("Bin"), QStringLiteral("editFromBin"), true);
        settle();
        QVERIFY(window.timeline().options().editFromBin);
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        settle();
        BinDocument* doc = bins.document(binId);
        QVERIFY(doc);
        BinItem c = clip(QStringLiteral("c"), 20);
        c.id = model::newItemId();
        doc->addItem(c);
        settle();
        BinView* view = bins.viewFor(doc);
        QVERIFY(view);
        view->selectIds({c.id});
        QCOMPARE(view->selectedIds(), QStringList{c.id});
        window.panels().activate(window.panels().panel(QStringLiteral("BinContainer")));
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        // The first edit makes a sequence (p.463) with the clip in it.
        const model::LoadedClip* rec = window.composer().state(MonitorId::Record).current();
        QVERIFY(rec);
        QCOMPARE(Sequence::fromItem(rec->item).duration(), 20);
        commands.action(QStringLiteral("edit.overwrite"))->trigger();
        settle();
        QCOMPARE(Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item).duration(), 40);
    }
};

QTEST_MAIN(TestBinEditing)
#include "tst_bin_editing.moc"

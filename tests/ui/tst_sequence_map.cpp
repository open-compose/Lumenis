// The Timeline Sequence Map (Editing Guide p.606, Timeline settings p.1219;
// docs/spec/sequence-map.md): Timeline > Show Sequence Map, the white box and dragging it, Show in
// Dedicated Window and Show in Timeline, the Timeline settings' overlay, Clicking Elsewhere Hides and
// Display effects.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/sequence_map.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
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

QAction* menuItem(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

void mouse(QWidget* w, QEvent::Type type, const QPointF& pos, Qt::MouseButtons buttons) {
    QMouseEvent e(type, pos, w->mapToGlobal(pos), Qt::LeftButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &e);
}

int count(const QImage& image, QRgb colour) {
    int n = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) n += image.pixel(x, y) == colour ? 1 : 0;
    }
    return n;
}

} // namespace

class TestSequenceMap : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void map() {
        Composer composer;
        Timeline timeline(composer);
        timeline.resize(900, 320);
        timeline.show();
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        // A long sequence on many tracks: 10 minutes, V1 and A1-A8.
        Sequence seq = Sequence::withDefaultTracks();
        for (int a = 3; a <= 8; ++a) seq.addTrack(TrackKind::Audio);
        Segment withEffect = seg(QStringLiteral("b"), QStringLiteral("V1"), 7500);
        withEffect.effect = Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
        seq.splice(QStringLiteral("V1"), 0, {seg(QStringLiteral("a"), QStringLiteral("V1"), 7500), withEffect});
        for (int a = 1; a <= 8; ++a) {
            const QString name = QStringLiteral("A%1").arg(a);
            seq.splice(name, 0, {seg(QStringLiteral("a"), name, 15000)});
        }
        seq.storeIn(item);
        composer.load(MonitorId::Record, QStringLiteral("bin"), item);
        timeline.showEntireSequence();
        settle();

        // Timeline > Show Sequence Map (p.606): an overlay in the tracks area by default (p.1219).
        QVERIFY(timeline.handles(QStringLiteral("timeline.show_sequence_map")));
        QVERIFY(timeline.perform(QStringLiteral("timeline.show_sequence_map")));
        SequenceMap* map = timeline.sequenceMap();
        QVERIFY(map && map->isVisible() && timeline.sequenceMapShown());
        QCOMPARE(map->parentWidget(), timeline.view());
        QVERIFY(!timeline.sequenceMapInWindow());
        // The whole sequence fits: the white box covers it across.
        QCOMPARE(map->viewBox().width(), map->mapArea().width());

        // Zoomed in, the box is the part in view; dragging it scrolls the Timeline (p.606).
        timeline.setScale(4.0, 0);
        settle();
        QVERIFY(map->viewBox().width() < map->mapArea().width() / 4);
        QVERIFY(map->viewBox().height() < map->mapArea().height());
        const double before = timeline.firstFrame();
        const QPointF centre = map->viewBox().center();
        mouse(map, QEvent::MouseButtonPress, centre, Qt::LeftButton);
        mouse(map, QEvent::MouseMove, centre + QPointF(40, 30), Qt::LeftButton);
        mouse(map, QEvent::MouseButtonRelease, centre + QPointF(40, 30), Qt::NoButton);
        QVERIFY(timeline.firstFrame() > before + 1000);
        QVERIFY(timeline.view()->topOffset() > 0);
        QVERIFY(std::abs(map->viewBox().center().x() - (centre.x() + 40)) < 2);
        QCOMPARE(timeline.scrollBar()->value(), static_cast<int>(timeline.firstFrame()));
        // Ours: a press elsewhere on the map moves the box there.
        mouse(map, QEvent::MouseButtonPress, map->mapArea().topLeft() + QPointF(1, 1), Qt::LeftButton);
        mouse(map, QEvent::MouseButtonRelease, map->mapArea().topLeft() + QPointF(1, 1), Qt::NoButton);
        QCOMPARE(timeline.firstFrame(), 0.0);
        QCOMPARE(timeline.view()->topOffset(), 0);

        // Display effects (p.1219): a mark on the segment with one; none without the option.
        const QRgb mark = 0xffc87dd8;
        QVERIFY(count(map->grab().toImage(), mark) > 0);
        Timeline::Options o = timeline.options();
        o.mapEffects = false;
        timeline.setOptions(o);
        QCOMPARE(count(map->grab().toImage(), mark), 0);

        // Clicking elsewhere hides it (p.1219).
        QTest::mouseClick(timeline.view(), Qt::LeftButton, {}, QPoint(300, 40));
        QVERIFY(!map->isVisible() && !timeline.sequenceMapShown());
        // Without the option it stays until the command hides it.
        o.mapClickHides = false;
        timeline.setOptions(o);
        timeline.perform(QStringLiteral("timeline.show_sequence_map"));
        QTest::mouseClick(timeline.view(), Qt::LeftButton, {}, QPoint(300, 40));
        QVERIFY(map->isVisible());
        timeline.perform(QStringLiteral("timeline.show_sequence_map"));
        QVERIFY(!map->isVisible());

        // Show in Dedicated Window, and back with Show in Timeline (p.606).
        timeline.perform(QStringLiteral("timeline.show_sequence_map"));
        std::unique_ptr<QMenu> menu(map->contextMenu());
        menuItem(menu.get(), QStringLiteral("Show in Dedicated Window"))->trigger();
        QVERIFY(timeline.sequenceMapInWindow());
        QVERIFY(timeline.sequenceMapWindow() && timeline.sequenceMapWindow()->isVisible());
        QCOMPARE(map->window(), timeline.sequenceMapWindow());
        QVERIFY(map->isVisible());
        std::unique_ptr<QMenu> back(map->contextMenu());
        menuItem(back.get(), QStringLiteral("Show in Timeline"))->trigger();
        QCOMPARE(map->parentWidget(), timeline.view());
        QVERIFY(map->isVisible() && !timeline.sequenceMapWindow()->isVisible());
        timeline.showSequenceMap(false);

        // Without "Open Sequence Map as overlay on Timeline" it opens in its window; closing the window
        // hides it.
        o.mapOverlay = false;
        timeline.setOptions(o);
        timeline.perform(QStringLiteral("timeline.show_sequence_map"));
        QVERIFY(timeline.sequenceMapInWindow() && timeline.sequenceMapWindow()->isVisible());
        timeline.sequenceMapWindow()->close();
        QVERIFY(!timeline.sequenceMapShown());
    }
};

QTEST_MAIN(TestSequenceMap)
#include "tst_sequence_map.moc"

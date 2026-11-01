// Titler+ from the Timeline (FX Guide p.303-305; Timeline::applyTitler): Tools > Titler+ and the Timeline's button
// put an empty title on the segment at the position, on the selected segments or between the marks, open the
// Effect Editor, undo in one step, and the "Title Text" Clip Text label shows a title's topmost text.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/titler.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"

#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// V1: A (50 frames), filler (30), B (40).
BinItem sequence() {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    model::Segment a;
    a.clipId = QStringLiteral("a");
    a.name = QStringLiteral("A");
    a.sourceTrack = QStringLiteral("V1");
    a.length = 50;
    model::Segment b = a;
    b.clipId = QStringLiteral("b");
    b.name = QStringLiteral("B");
    b.length = 40;
    seq.splice(QStringLiteral("V1"), 0, {a, model::Segment::filler(30), b});
    seq.storeIn(s);
    return s;
}

} // namespace

class TestTitlerUi : public QObject {
    Q_OBJECT

private slots:
    void apply() {
        Composer composer;
        Timeline timeline{composer};
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        auto v1 = [&] { return *model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1")); };
        QSignalSpy opened(&timeline, &Timeline::titlerApplied);

        // On the empty track segment at the position (p.303).
        composer.moveTo(MonitorId::Record, 60);
        QVERIFY(timeline.perform(QStringLiteral("tools.titler")));
        QCOMPARE(opened.count(), 1);
        QVERIFY(v1().segments.at(1).effect && model::isTitlerEffect(*v1().segments.at(1).effect));
        QVERIFY(!v1().segments.at(0).effect);
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(!v1().segments.at(1).effect);

        // The Timeline's button does the same, here on the clip A.
        auto* button = timeline.findChild<QToolButton*>(QStringLiteral("tools.titler"));
        QVERIFY(button);
        composer.moveTo(MonitorId::Record, 10);
        button->click();
        QCOMPARE(opened.count(), 2);
        QVERIFY(v1().segments.at(0).effect && model::isTitlerEffect(*v1().segments.at(0).effect));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));

        // "set in and out marks in the Timeline to highlight a portion of the track": frames 40-89 become their own
        // segments with titles.
        composer.state(MonitorId::Record).setPosition(40);
        composer.state(MonitorId::Record).markIn();
        composer.state(MonitorId::Record).setPosition(89);
        composer.state(MonitorId::Record).markOut();
        QVERIFY(timeline.perform(QStringLiteral("tools.titler")));
        std::int64_t start = 0;
        int titled = 0;
        for (const model::Segment& s : v1().segments) {
            const bool title = s.effect && model::isTitlerEffect(*s.effect);
            QCOMPARE(title, start >= 40 && start < 90);
            titled += title ? 1 : 0;
            start += s.length;
        }
        QCOMPARE(titled, 3);

        // The "Title Text" label (p.305): on by default, the topmost text.
        model::Track track = v1();
        model::Segment& segment = track.segments[1];
        model::Title title = model::titleOf(*segment.effect);
        title.objects << model::textObject(title, QStringLiteral("Opening\nCredits"), QPointF());
        title.objects << model::shapeObject(title, model::TitleObjectKind::Rectangle, QPointF(), QSizeF(10, 10));
        model::setTitle(*segment.effect, title);
        QVERIFY(timeline.clipText(track, segment).contains(QStringLiteral("Opening")));
        QVERIFY(!timeline.clipText(track, segment).contains(QStringLiteral("Credits")));
        Timeline::Display d = timeline.display();
        QVERIFY(d.titleText);
        d.titleText = false;
        timeline.setDisplay(d);
        QVERIFY(!timeline.clipText(track, segment).contains(QStringLiteral("Opening")));

        // Nowhere to put it: no sequence.
        Composer empty;
        Timeline none{empty};
        QVERIFY(!none.applyTitler());
        QVERIFY(!none.lastMessage().isEmpty());
    }
};

QTEST_MAIN(TestTitlerUi)
#include "tst_titler_ui.moc"

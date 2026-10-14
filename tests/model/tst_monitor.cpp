#include "lumenis/model/monitor.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem clip(const QString& name, std::int64_t frames, BinItemKind kind = BinItemKind::MasterClip) {
    BinItem i;
    i.id = newItemId();
    i.name = name;
    i.kind = kind;
    i.durationFrames = frames;
    i.editRate = Rational(25);
    return i;
}

} // namespace

class TestMonitor : public QObject {
    Q_OBJECT

private slots:
    void loadsAndListsClips() {
        MonitorState m;
        QVERIFY(m.isEmpty());
        QVERIFY(m.tracking(TrackingFormat::Master).isEmpty()); // nothing loaded (p.385)
        const BinItem b = clip(QStringLiteral("beach"), 100);
        const BinItem a = clip(QStringLiteral("Alley"), 50);
        m.load(QStringLiteral("bin"), b);
        m.load(QStringLiteral("bin"), a);
        QCOMPARE(m.current()->item.name, QStringLiteral("Alley"));
        // Alphabetical, or in load order (p.394).
        QCOMPARE(m.menuNames(), (QStringList{"Alley", "beach"}));
        QCOMPARE(m.menuNames(true), (QStringList{"beach", "Alley"}));
        QVERIFY(m.select(1));
        QCOMPARE(m.current()->item.name, QStringLiteral("beach"));
        QVERIFY(m.select(1, true));
        QCOMPARE(m.current()->item.name, QStringLiteral("Alley"));

        // Loading a clip again shows it with its marks.
        m.setPosition(10);
        m.markIn();
        m.load(QStringLiteral("bin"), b);
        m.load(QStringLiteral("bin"), a);
        QCOMPARE(m.loadedCount(), 2);
        QCOMPARE(m.in(), std::optional<std::int64_t>(10));

        // Clear Monitor leaves black but keeps the clips; Clear Menu keeps
        // only the shown one (p.395).
        m.clearMenu();
        QCOMPARE(m.menuNames(), QStringList{"Alley"});
        m.clearMonitor();
        QVERIFY(m.isEmpty());
        QCOMPARE(m.loadedCount(), 1);
        QVERIFY(m.select(0));
        QVERIFY(!m.isEmpty());
    }

    void positionAndMarks() {
        MonitorState m;
        m.load({}, clip(QStringLiteral("c"), 250));
        m.step(-5);
        QCOMPARE(m.position(), 0);
        m.goToEnd();
        QCOMPARE(m.position(), 249);
        m.step(10);
        QCOMPARE(m.position(), 249);
        QCOMPARE(m.inOutDuration(), 250); // no marks: the whole clip

        m.setPosition(50);
        m.markIn();
        m.setPosition(99);
        m.markOut();
        QCOMPARE(m.inOutDuration(), 50);
        m.goToStart();
        QVERIFY(m.goToIn());
        QCOMPARE(m.position(), 50);
        QVERIFY(m.goToOut());
        QCOMPARE(m.position(), 99);
        // An IN after the OUT clears the OUT.
        m.setPosition(120);
        m.markIn();
        QVERIFY(!m.out());
        m.clearBoth();
        QVERIFY(!m.in());
        QVERIFY(!m.goToIn());
    }

    void trackingFormats() {
        MonitorState m;
        BinItem c = clip(QStringLiteral("Interview"), 250);
        c.fields.insert(QStringLiteral("Start"), QStringLiteral("10:00:00:00"));
        m.load({}, c);
        m.setPosition(30);
        QCOMPARE(m.tracking(TrackingFormat::Master), QStringLiteral("10:00:01:05"));
        QCOMPARE(m.tracking(TrackingFormat::Absolute), QStringLiteral("00:00:01:05"));
        QCOMPARE(m.tracking(TrackingFormat::Duration), QStringLiteral("00:00:10:00"));
        QCOMPARE(m.tracking(TrackingFormat::Remain), QStringLiteral("00:00:08:19"));
        QCOMPARE(m.tracking(TrackingFormat::ClipName), QStringLiteral("Interview"));
        QVERIFY(m.tracking(TrackingFormat::None).isEmpty());
        m.markIn();
        QCOMPARE(m.tracking(TrackingFormat::InOut), QStringLiteral("00:00:08:20"));
        QCOMPARE(m.durationText(m.inOutDuration()), QStringLiteral("8:20"));

        // A sequence without a Start column starts at the default hour.
        MonitorState r;
        r.load({}, clip(QStringLiteral("Cut"), 100, BinItemKind::Sequence));
        QCOMPARE(r.tracking(TrackingFormat::Master), QStringLiteral("01:00:00:00"));
    }
};

QTEST_GUILESS_MAIN(TestMonitor)
#include "tst_monitor.moc"

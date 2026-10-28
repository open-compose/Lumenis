// The Composer settings' Window tab (Editing Guide p.380, p.384-385, p.1158-1159;
// docs/spec/composer-display.md): two rows of data with a second tracking display, the Fast Menu
// and the gang icon; Flow Data Dynamically; Tick Marks in Position Bars; the middle Composer button
// panel.
#include "lumenis/model/bin.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::model;
using namespace lumenis::ui;

namespace {

BinItem clip(const QString& name, std::int64_t frames) {
    BinItem i;
    i.id = model::newItemId();
    i.name = name;
    i.kind = BinItemKind::MasterClip;
    i.durationFrames = frames;
    i.editRate = Rational(25);
    return i;
}

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

} // namespace

class TestComposerDisplay : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Always Display Two Rows of Data: "you can display two different types of tracking information
    // for the footage in each monitor" (p.385); the second row has the Fast Menu and, when ganged, the
    // gang icon (p.1158).
    void twoRows() {
        Composer c;
        c.resize(900, 360);
        c.show();
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("a"), 100));
        auto* info2 = c.findChild<QWidget*>(QStringLiteral("sourceInfo2"));
        QVERIFY(info2);
        QVERIFY(!info2->isVisible());
        Composer::Display d = c.display();
        d.twoRows = true;
        c.setDisplay(d);
        QVERIFY(c.showsSecondRow());
        QVERIFY(info2->isVisible());
        c.setTracking(MonitorId::Source, TrackingFormat::Master);
        c.setSecondTracking(MonitorId::Source, TrackingFormat::Duration);
        auto* tracking2 = c.findChild<QToolButton*>(QStringLiteral("sourceTracking2"));
        QVERIFY(tracking2);
        QVERIFY(!tracking2->text().isEmpty());
        QCOMPARE(tracking2->text(), c.secondTrackingText(MonitorId::Source));
        QVERIFY(c.secondTrackingText(MonitorId::Source) != c.trackingText(MonitorId::Source));
        QVERIFY(c.findChild<QToolButton*>(QStringLiteral("recordFastMenu"))->isVisible());
        auto* gang = c.findChild<QWidget*>(QStringLiteral("gangIcon"));
        QVERIFY(gang && !gang->isVisible());
        c.setGanged(true);
        QVERIFY(gang->isVisible());
        // Data Display Off hides both rows.
        d.infoRow = false;
        c.setDisplay(d);
        QVERIFY(!info2->isVisible());
    }

    // Flow Data Dynamically: one or two rows "depending on the size of the Composer window" (p.380).
    void flowData() {
        Composer c;
        c.resize(900, 300);
        c.show();
        Composer::Display d = c.display();
        d.flowData = true;
        c.setDisplay(d);
        settle();
        auto* info2 = c.findChild<QWidget*>(QStringLiteral("recordInfo2"));
        QVERIFY(!info2->isVisible());
        c.resize(900, Composer::kFlowHeight + 40);
        settle();
        QVERIFY(info2->isVisible());
        c.resize(900, Composer::kFlowHeight - 40);
        settle();
        QVERIFY(!info2->isVisible());
    }

    // Tick Marks in Position Bars (p.1159): invisible when off.
    void tickMarks() {
        Composer c;
        c.resize(900, 360);
        c.show();
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("a"), 250));
        c.moveTo(MonitorId::Source, 249);
        settle();
        auto* bar = c.findChild<PositionBar*>(QStringLiteral("sourcePositionBar"));
        QVERIFY(bar);
        // A tick a second (25 frames) at this width.
        const int x = bar->xFor(25);
        auto pixel = [&] { return bar->grab().toImage().pixel(x, bar->height() - 2); };
        const QRgb without = pixel();
        Composer::Display d = c.display();
        d.tickMarks = true;
        c.setDisplay(d);
        settle();
        QVERIFY(pixel() != without);
    }

    // The middle Composer button panel (p.1159): 3 or 6 buttons "depending on the number of button
    // rows", mappable and saved with the button layout.
    void middlePanel() {
        Composer c;
        c.resize(900, 360);
        c.show();
        QVERIFY(!c.middlePanel()->isVisible());
        Composer::Display d = c.display();
        d.middlePanel = true;
        c.setDisplay(d);
        QVERIFY(c.middlePanel()->isVisible());
        auto visibleButtons = [&] {
            int n = 0;
            for (QToolButton* b : c.middlePanel()->findChildren<QToolButton*>()) n += b->isVisible() ? 1 : 0;
            return n;
        };
        QCOMPARE(visibleButtons(), 6);
        d.buttonRows = 1;
        c.setDisplay(d);
        QCOMPARE(visibleButtons(), 3);
        // A command the Composer does not run goes out as commandRequested.
        QSignalSpy requested(&c, &Composer::commandRequested);
        auto* lift = c.middlePanel()->findChild<QToolButton*>(QStringLiteral("edit.lift"));
        QVERIFY(lift);
        lift->click();
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("edit.lift"));
        // Saved and restored with the layout.
        QJsonObject layout = c.buttonLayout();
        QCOMPARE(layout.value(QStringLiteral("middle")).toArray().at(0).toArray().at(0).toString(), QStringLiteral("edit.lift"));
        QJsonArray middle = layout.value(QStringLiteral("middle")).toArray();
        QJsonArray row = middle.at(0).toArray();
        row[0] = QStringLiteral("edit.mark_in");
        middle[0] = row;
        layout.insert(QStringLiteral("middle"), middle);
        c.setButtonLayout(layout);
        QVERIFY(c.middlePanel()->findChild<QToolButton*>(QStringLiteral("edit.mark_in")));
        QCOMPARE(Composer::defaultButtonLayout().value(QStringLiteral("middle")).toArray().size(), 2);
        // Trim mode hides it with the button rows.
        Composer::TrimDisplay t;
        t.on = true;
        c.setTrimDisplay(t);
        QVERIFY(!c.middlePanel()->isVisible());
        c.setTrimDisplay({});
        QVERIFY(c.middlePanel()->isVisible());
    }
};

QTEST_MAIN(TestComposerDisplay)
#include "tst_composer_display.moc"

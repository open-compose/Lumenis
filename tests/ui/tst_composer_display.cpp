// The Composer settings' Window tab (Editing Guide p.380, p.384-385, p.1158-1159;
// docs/spec/composer-display.md): two rows of data with a second tracking display, the Fast Menu
// and the gang icon; Flow Data Dynamically; Tick Marks in Position Bars; the middle Composer button
// panel. Also Dynamic Play (p.401-402), Display During Segment Drag (p.617-618, p.1219) and the monitors' Display Color
// Space (p.544).
#include "lumenis/model/bin.h"
#include "lumenis/ui/composer.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QJsonArray>
#include <QMenu>
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

    // "Right-click in the desired viewer (source /record), select Display Color Space, and choose from the list" (p.544):
    // each monitor's own choice, starting with the project's; the picture is converted as it is drawn.
    void displayColorSpace() {
        Composer c;
        c.resize(900, 360);
        c.setProjectColorSpace(QStringLiteral("YCbCr 709"));
        c.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(200, 40, 40));
            return image;
        });
        c.show();
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("a"), 100));
        c.load(MonitorId::Record, QStringLiteral("bin"), clip(QStringLiteral("b"), 100));
        settle();
        QCOMPARE(c.displayColorSpace(MonitorId::Source), QStringLiteral("Rec.709"));
        auto centre = [&](MonitorId m) {
            MonitorPicture* w = c.picture(m);
            return w->grab().toImage().pixel(w->width() / 2, w->height() / 2);
        };
        QCOMPARE(centre(MonitorId::Record), qRgb(200, 40, 40));
        QMenu* menu = c.contextMenu(MonitorId::Record);
        auto* display = menu->findChild<QMenu*>(QStringLiteral("displayColorSpace"));
        QVERIFY(display);
        QStringList choices;
        for (QAction* a : display->actions()) choices << a->text();
        QCOMPARE(choices, (QStringList{QStringLiteral("Rec.709"), QStringLiteral("Rec.2020"), QStringLiteral("DCI-P3")}));
        QVERIFY(display->findChild<QAction*>(QStringLiteral("Rec.709"))->isChecked());
        display->findChild<QAction*>(QStringLiteral("Rec.2020"))->trigger();
        delete menu;
        settle();
        QCOMPARE(c.displayColorSpace(MonitorId::Record), QStringLiteral("Rec.2020"));
        QCOMPARE(c.displayColorSpace(MonitorId::Source), QStringLiteral("Rec.709"));
        // A Rec.709 red on a Rec.2020 display is less saturated: its green and blue code values rise.
        const QRgb shown = centre(MonitorId::Record);
        QVERIFY2(qGreen(shown) > 40 && qBlue(shown) > 40 && qRed(shown) < 200, qPrintable(QString::number(shown, 16)));
        QCOMPARE(centre(MonitorId::Source), qRgb(200, 40, 40));
        // A 2020 project's pictures start on a Rec.2020 display.
        c.setProjectColorSpace(QStringLiteral("RGB 2020"));
        QCOMPARE(c.displayColorSpace(MonitorId::Source), QStringLiteral("Rec.2020"));
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

    // Dynamic Play (p.401-402): normal speed first, then 12 steps double it; Alt starts below normal.
    void dynamicPlay() {
        Composer c;
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("a"), 10000));
        c.setDynamicPlay(3, 3);
        QVERIFY(c.perform(QStringLiteral("play.dynamic_play_forward")));
        QCOMPARE(c.rate(), 1.0);
        // "the default setting of 3 means that pressing the key 4 times (3 x 4 = 12) doubles the play
        // speed" (p.402).
        for (int i = 0; i < 3; ++i) c.playDynamic(1, false);
        QVERIFY(c.rate() > 1.5 && c.rate() < 1.75);
        QCOMPARE(c.playDisplay().kind, Composer::PlayDisplay::Kind::Forward);
        c.playDynamic(1, false);
        QCOMPARE(c.rate(), 2.0);
        // Frames move at that rate: 2 x 25 fps for one second.
        const std::int64_t before = c.state(MonitorId::Source).position();
        c.advance(1000);
        QCOMPARE(c.state(MonitorId::Source).position() - before, 50);
        // The other way starts again at normal speed.
        c.playDynamic(-1, false);
        QCOMPARE(c.rate(), -1.0);
        c.stop();
        // Alt: "The Slow Start Speed setting is the number of keystrokes it will take to get to 1x speed".
        c.playDynamic(1, true);
        QVERIFY(c.rate() > 0.5 && c.rate() < 0.6); // 2^(-9/12)
        for (int i = 0; i < 3; ++i) c.playDynamic(1, false);
        QCOMPARE(c.rate(), 1.0);
        // A higher acceleration doubles in fewer presses.
        c.stop();
        c.setDynamicPlay(6, 3);
        c.playDynamic(1, false);
        c.playDynamic(1, false);
        c.playDynamic(1, false);
        QCOMPARE(c.rate(), 2.0);
        c.stop();
        QCOMPARE(c.rate(), 0.0);
    }

    // Display During Segment Drag (p.617-618, p.1219): the four-frame display and its offset counter,
    // the Position Bar frame, or nothing.
    void segmentDragDisplay() {
        Composer c;
        Timeline timeline(c);
        c.setPictureResolver([](MonitorId, std::int64_t frame, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(static_cast<int>(frame % 256), 0, 0));
            return image;
        });
        c.resize(900, 360);
        c.show();
        BinItem item;
        item.id = model::newItemId();
        item.name = QStringLiteral("Cut");
        item.kind = BinItemKind::Sequence;
        item.editRate = Rational(25);
        Sequence seq = Sequence::withDefaultTracks();
        Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("a");
        a.sourceTrack = QStringLiteral("V1");
        a.length = 50;
        Segment b = a;
        b.clipId = QStringLiteral("b");
        b.name = QStringLiteral("b");
        seq.splice(QStringLiteral("V1"), 0, {a, b, a});
        seq.storeIn(item);
        c.load(MonitorId::Record, QStringLiteral("bin"), item);
        timeline.setSelection({{QStringLiteral("V1"), 50}}); // b, frames 50-99
        Timeline::Options o = timeline.options();
        o.segmentDragDisplay = Timeline::Options::DragDisplay::FourFrame;
        timeline.setOptions(o);
        timeline.showSegmentDrag(30);
        const Composer::DragDisplay& d = c.dragDisplay();
        QVERIFY(d.on && d.fourFrame);
        QCOMPARE(d.offset, 30);
        QCOMPARE(d.frames[0], 79);  // before where b's head would go (80)
        QCOMPARE(d.frames[1], 50);  // b's first frame
        QCOMPARE(d.frames[2], 99);  // b's last frame
        QCOMPARE(d.frames[3], 130); // after where it would end
        QVERIFY(c.dragCounter()->isVisible());
        // The Source monitor shows frames 79 and 50 side by side.
        const QImage shot = c.picture(MonitorId::Source)->grab().toImage();
        const QRect pic = c.picture(MonitorId::Source)->pictureRect();
        QCOMPARE(qRed(shot.pixel(pic.left() + pic.width() / 4, pic.center().y())), 79);
        QCOMPARE(qRed(shot.pixel(pic.left() + 3 * pic.width() / 4, pic.center().y())), 50);
        // The drag ends: back to the monitors.
        timeline.showSegmentDrag(std::nullopt);
        QVERIFY(!c.dragDisplay().on);
        QVERIFY(!c.dragCounter()->isVisible());
        // Position Bar: the Record monitor shows the frame where the head would go.
        o.segmentDragDisplay = Timeline::Options::DragDisplay::PositionBar;
        timeline.setOptions(o);
        timeline.showSegmentDrag(-20);
        QVERIFY(c.dragDisplay().on && !c.dragDisplay().fourFrame);
        QCOMPARE(c.dragDisplay().frames[0], 30);
        QVERIFY(!c.dragCounter()->isVisible());
        const QImage record = c.picture(MonitorId::Record)->grab().toImage();
        QCOMPARE(qRed(record.pixel(record.width() / 2, record.height() / 2)), 30);
        // None.
        o.segmentDragDisplay = Timeline::Options::DragDisplay::None;
        timeline.setOptions(o);
        timeline.showSegmentDrag(10);
        QVERIFY(!c.dragDisplay().on);
    }
};

QTEST_MAIN(TestComposerDisplay)
#include "tst_composer_display.moc"

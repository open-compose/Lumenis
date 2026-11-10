// The Composer settings' Window tab (Editing Guide p.380, p.384-385, p.1158-1159;
// docs/spec/composer-display.md): two rows of data with a second tracking display, the Fast Menu
// and the gang icon; Flow Data Dynamically; Tick Marks in Position Bars; the middle Composer button
// panel. Also Dynamic Play (p.401-402), Display During Segment Drag (p.617-618, p.1219) and the monitors' Display Color
// Space (p.544) and Target Mask (p.552, 8.5e3).
#include "lumenis/media/display_color.h"
#include "lumenis/media/target_mask.h"
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
        QCOMPARE(c.displayColorSpace(MonitorId::Source), QStringLiteral("Project"));
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
        // The Composer setting's Viewer tab's choices (p.1161); ACESproxy only in ACES projects.
        QCOMPARE(choices, media::displayColorSpaces());
        QCOMPARE(choices.size(), 7);
        QVERIFY(display->findChild<QAction*>(QStringLiteral("Project"))->isChecked());
        QVERIFY(!display->findChild<QAction*>(QStringLiteral("ACESproxy"))->isEnabled());
        display->findChild<QAction*>(QStringLiteral("Rec2020"))->trigger();
        delete menu;
        settle();
        QCOMPARE(c.displayColorSpace(MonitorId::Record), QStringLiteral("Rec2020"));
        QCOMPARE(c.displayColorSpace(MonitorId::Source), QStringLiteral("Project"));
        // A Rec.709 red on a Rec.2020 display is less saturated: its green and blue code values rise.
        const QRgb shown = centre(MonitorId::Record);
        QVERIFY2(qGreen(shown) > 40 && qBlue(shown) > 40 && qRed(shown) < 200, qPrintable(QString::number(shown, 16)));
        QCOMPARE(centre(MonitorId::Source), qRgb(200, 40, 40));
        // Rec709 shows a 709 project's picture as it is.
        c.setDisplayColorSpace(MonitorId::Record, QStringLiteral("Rec709"));
        settle();
        QCOMPARE(centre(MonitorId::Record), qRgb(200, 40, 40));
    }

    // "Right-click in the source or record viewer and select Target Mask" (p.552): the project's mask margins drawn as
    // the monitor says. Also the burn-in of Export's Enable Mask Regions (p.900).
    void targetMask() {
        Composer c;
        c.resize(900, 360);
        c.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(200, 40, 40));
            return image;
        });
        c.show();
        c.load(MonitorId::Record, QStringLiteral("bin"), clip(QStringLiteral("b"), 100));
        settle();
        c.setMaskMargins(model::maskForPreset(QStringLiteral("2.39:1"), 16.0 / 9.0));
        MonitorPicture* w = c.picture(MonitorId::Record);
        auto centre = [&] {
            const QImage shot = w->grab().toImage();
            return shot.pixel(shot.width() / 2, shot.height() / 2);
        };
        QCOMPARE(centre(), qRgb(200, 40, 40));
        QMenu* menu = c.contextMenu(MonitorId::Record);
        auto* target = menu->findChild<QMenu*>(QStringLiteral("targetMask"));
        QVERIFY(target);
        QStringList choices;
        for (QAction* a : target->actions()) choices << a->text();
        QCOMPARE(choices, model::targetMaskLabels());
        QVERIFY(target->findChild<QAction*>(QStringLiteral("none"))->isChecked());
        target->findChild<QAction*>(QStringLiteral("blackMask"))->trigger();
        delete menu;
        settle();
        QCOMPARE(c.targetMask(MonitorId::Record), model::TargetMask::BlackMask);
        QCOMPARE(c.targetMask(MonitorId::Source), model::TargetMask::None);
        // The centre stays; a row just inside the picture's top is in the masked band: black.
        const QImage shot = w->grab().toImage();
        QCOMPARE(shot.pixel(shot.width() / 2, shot.height() / 2), qRgb(200, 40, 40));
        int firstRed = -1;
        for (int y = 0; y < shot.height() && firstRed < 0; ++y) {
            if (shot.pixel(shot.width() / 2, y) == qRgb(200, 40, 40)) firstRed = y;
        }
        QVERIFY(firstRed > 0);
        c.setTargetMask(MonitorId::Record, model::TargetMask::MixToWhite);
        settle();
        const QImage mixed = w->grab().toImage();
        const QRgb band = mixed.pixel(mixed.width() / 2, firstRed - 2);
        QVERIFY2(qGreen(band) > 100 && qRed(band) > 200, qPrintable(QString::number(band, 16)));
        // Burned in for export: outside black, inside kept, the format kept.
        QImage frame(160, 90, QImage::Format_RGB32);
        frame.fill(QColor(200, 40, 40));
        const QImage burned = media::burnMaskMargins(frame, model::maskForPreset(QStringLiteral("2.39:1"), 16.0 / 9.0));
        QCOMPARE(burned.format(), frame.format());
        QCOMPARE(burned.pixel(80, 2), qRgb(0, 0, 0));
        QCOMPARE(burned.pixel(80, 45), qRgb(200, 40, 40));
        QCOMPARE(media::burnMaskMargins(frame, {}), frame);
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

    // Single-field step (p.399) and editing at 60 fps (p.1295; 8.7c3): field steps beep in progressive projects and
    // park on field 2 in interlaced ones; Draft Quality plays 60 fps clips at 30.
    void fieldStepsAndSixtyFps() {
        Composer c;
        // Even lines red, odd lines blue: field 2 (the lower field) is blue.
        c.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            for (int y = 0; y < size.height(); ++y) {
                const QRgb colour = y % 2 ? qRgb(0, 0, 255) : qRgb(255, 0, 0);
                for (int x = 0; x < size.width(); ++x) image.setPixel(x, y, colour);
            }
            return image;
        });
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("a"), 100));
        c.moveTo(MonitorId::Source, 10);
        // "The single-field step feature is not available when you work with progressive formats" (p.399).
        QVERIFY(!c.interlacedProject());
        QVERIFY(c.perform(QStringLiteral("move.step_forward_one_field")));
        QCOMPARE(c.parkedField(MonitorId::Source), 0);
        QCOMPARE(c.state(MonitorId::Source).position(), 10);
        c.setInterlacedProject(true);
        QVERIFY(c.perform(QStringLiteral("move.step_forward_one_field")));
        QCOMPARE(c.parkedField(MonitorId::Source), 1);
        QCOMPARE(c.state(MonitorId::Source).position(), 10);
        QCOMPARE(c.picture(MonitorId::Source, QSize(16, 8)).pixel(0, 0), qRgb(0, 0, 255));
        // Frame steps keep the field ("Subsequent single-frame steps are based on this field", p.399).
        QVERIFY(c.perform(QStringLiteral("move.step_forward_one_frame")));
        QCOMPARE(c.parkedField(MonitorId::Source), 1);
        QVERIFY(c.perform(QStringLiteral("move.step_forward_one_field")));
        QCOMPARE(c.state(MonitorId::Source).position(), 12);
        QCOMPARE(c.parkedField(MonitorId::Source), 0);
        QCOMPARE(c.picture(MonitorId::Source, QSize(16, 8)).pixel(0, 0), qRgb(255, 0, 0));
        QVERIFY(c.perform(QStringLiteral("move.step_backward_one_field")));
        QCOMPARE(c.state(MonitorId::Source).position(), 11);
        QCOMPARE(c.parkedField(MonitorId::Source), 1);
        QVERIFY(c.perform(QStringLiteral("move.step_backward_one_field")));
        QCOMPARE(c.parkedField(MonitorId::Source), 0);
        QCOMPARE(c.state(MonitorId::Source).position(), 11);
        // Playing goes back to whole frames.
        QVERIFY(c.perform(QStringLiteral("move.step_forward_one_field")));
        c.play(1);
        QCOMPARE(c.parkedField(MonitorId::Source), 0);
        c.stop();

        // "Draft Quality plays back at 30 fps. Full Quality plays back at 60 fps" (p.1295).
        BinItem sixty = clip(QStringLiteral("b"), 600);
        sixty.editRate = Rational(60000, 1001);
        c.load(MonitorId::Source, QStringLiteral("bin"), sixty);
        c.setVideoQuality(Composer::VideoQuality::Draft);
        QCOMPARE(c.shownFrame(MonitorId::Source, 7), 7); // parked
        c.play(1);
        QCOMPARE(c.shownFrame(MonitorId::Source, 7), 6);
        QCOMPARE(c.shownFrame(MonitorId::Source, 8), 8);
        c.setVideoQuality(Composer::VideoQuality::Full);
        QCOMPARE(c.shownFrame(MonitorId::Source, 7), 7);
        c.stop();
        // 25 fps clips play every frame at Draft Quality.
        c.load(MonitorId::Source, QStringLiteral("bin"), clip(QStringLiteral("c"), 100));
        c.setVideoQuality(Composer::VideoQuality::Draft);
        c.play(1);
        QCOMPARE(c.shownFrame(MonitorId::Source, 7), 7);
        c.stop();
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

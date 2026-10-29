// A marker in the Source and Record monitors (Editing Guide p.413, p.416; docs/spec/ui-inventory.md
// "Markers in the monitors"): the frame keeps its picture, an upright oval in the marker's colour sits
// at the bottom with the comment under it, and clicking the oval asks for Edit Marker.
#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"

#include <QSignalSpy>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// Within a few levels of each channel.
bool near(QColor a, QColor b) {
    return std::abs(a.red() - b.red()) < 24 && std::abs(a.green() - b.green()) < 24 && std::abs(a.blue() - b.blue()) < 24;
}

model::BinItem clipWithMarker(const QString& comment) {
    model::BinItem clip;
    clip.id = QStringLiteral("c");
    clip.name = QStringLiteral("Clip");
    clip.kind = model::BinItemKind::MasterClip;
    clip.editRate = Rational(25);
    clip.durationFrames = 100;
    model::Marker m;
    m.id = QStringLiteral("m");
    m.color = QStringLiteral("Red");
    m.comment = comment;
    m.track = QStringLiteral("V1");
    m.frame = 10;
    model::setMarkers(clip, {m});
    return clip;
}

} // namespace

class TestMonitorMarkers : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void ovalAndComment() {
        Composer composer;
        composer.resize(1000, 600);
        composer.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), clipWithMarker(QStringLiteral("This shot needs color correction")));
        // The Record monitor active, in two-monitor view: its highlight outline is drawn too.
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, 10);
        settle();
        auto* monitor = composer.findChild<MonitorPicture*>(QStringLiteral("recordMonitor"));
        QVERIFY(monitor);
        const QImage shot = monitor->grab().toImage();
        const QRect pic = monitor->pictureRect();
        const QColor red = QColor::fromRgb(model::markerColor(QStringLiteral("Red")));
        // The picture is not painted in the marker's colour (the bug: the whole frame turned red).
        QVERIFY(!near(shot.pixelColor(pic.center()), red));
        QVERIFY(!near(shot.pixelColor(pic.left() + 10, pic.top() + 10), red));
        // An upright oval in the marker's colour above the bottom of the picture, centred.
        int top = -1;
        int bottom = -1;
        int left = pic.right();
        int right = pic.left();
        for (int y = pic.top(); y <= pic.bottom(); ++y) {
            for (int x = pic.left(); x <= pic.right(); ++x) {
                if (!near(shot.pixelColor(x, y), red)) continue;
                if (top < 0) top = y;
                bottom = y;
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        QVERIFY(top >= 0);
        QVERIFY(bottom - top > right - left); // taller than wide
        QVERIFY(std::abs((left + right) / 2 - pic.center().x()) <= 2);
        QVERIFY(bottom > pic.top() + pic.height() * 3 / 4);
        // The comment under it, in light text between the oval and the bottom edge.
        QVERIFY(bottom < pic.bottom() - 4);
        bool text = false;
        for (int y = bottom + 1; y <= pic.bottom() && !text; ++y) {
            for (int x = pic.left(); x <= pic.right() && !text; ++x) text = shot.pixelColor(x, y).lightness() > 150;
        }
        QVERIFY(text);

        // Clicking the oval asks for Edit Marker (p.416).
        QSignalSpy edit(&composer, &Composer::markerEditRequested);
        QTest::mouseClick(monitor, Qt::LeftButton, {}, QPoint((left + right) / 2, (top + bottom) / 2));
        QCOMPARE(edit.count(), 1);
        QCOMPARE(edit.at(0).at(1).toString(), QStringLiteral("m"));

        // Off the marked frame: no oval.
        composer.moveTo(MonitorId::Record, 20);
        settle();
        const QImage off = monitor->grab().toImage();
        QVERIFY(!near(off.pixelColor((left + right) / 2, (top + bottom) / 2), red));
    }
};

QTEST_MAIN(TestMonitorMarkers)
#include "tst_monitor_markers.moc"

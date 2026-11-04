// The Color Correction tool (FX Guide p.595-605, p.631-644; docs/spec/color-correction.md, 6.10b2): the Correction Type
// button, the tabs and their Enable buttons with Alt+click resets, Page Up/Down, the Controls sliders, the ChromaWheels,
// the Hue Offsets tab's Gain/Gamma/Setup and the Luma Ranges graph.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/correction_monitors.h"
#include "lumenis/ui/correction_tool.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// V1: A (20 frames), B (20); 25 frames a second.
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
    a.length = 20;
    model::Segment b = a;
    b.clipId = QStringLiteral("b");
    b.name = QStringLiteral("B");
    seq.splice(QStringLiteral("V1"), 0, {a, b});
    seq.storeIn(s);
    return s;
}

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

struct Rig {
    Composer composer;
    Timeline timeline{composer};
    CorrectionTool tool{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        QObject::connect(&timeline, &Timeline::changed, &tool, &CorrectionTool::reload);
        timeline.setCorrectionMode(true);
        composer.moveTo(MonitorId::Record, 5);
        tool.resize(760, 330);
        tool.show();
        tool.reload();
        settle();
    }
    model::Segment segment(int index) const {
        return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(index);
    }
    QDoubleSpinBox* value(const QString& path) const { return tool.control(path)->findChild<QDoubleSpinBox*>(QStringLiteral("value")); }
};

} // namespace

class TestCorrectionTool : public QObject {
    Q_OBJECT

private slots:
    void typesAndTabs() {
        Rig r;
        // "The default display shows the controls in the Hue Offsets tab within the HSL ... tab" (p.595).
        QCOMPARE(r.tool.group(), QStringLiteral("hsl"));
        QCOMPARE(r.tool.hslTab(), QStringLiteral("hueOffsets"));
        QVERIFY(r.tool.tabButton(QStringLiteral("hsl/hueOffsets"))->isChecked());
        // The Correction Type button: Src Segment and Prog Segment work; the other relationships wait (p.597).
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Src Segment"));
        QAction* prog = nullptr;
        for (QAction* a : r.tool.typeButton()->menu()->actions()) {
            if (a->text() == QLatin1String("Prog Segment")) prog = a;
            if (a->text() == QLatin1String("Src Tape")) QVERIFY(!a->isEnabled());
        }
        QVERIFY(prog);
        prog->trigger();
        QCOMPARE(r.timeline.correctionSide(), model::CorrectionSide::Program);
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Prog Segment"));
        // Clicking a tab's name shows it (p.600); Page Up/Down go from group to group.
        r.tool.tabButton(QStringLiteral("hsl/controls"))->click();
        QCOMPARE(r.tool.hslTab(), QStringLiteral("controls"));
        r.tool.tabButton(QStringLiteral("hsl/controls/midtones"))->click();
        QCOMPARE(r.tool.shownPath(), QStringLiteral("hsl/controls/midtones"));
        QTest::keyClick(&r.tool, Qt::Key_PageDown);
        QCOMPARE(r.tool.group(), QStringLiteral("channels"));
        QTest::keyClick(&r.tool, Qt::Key_PageUp);
        QTest::keyClick(&r.tool, Qt::Key_PageUp);
        QCOMPARE(r.tool.group(), QStringLiteral("secondary"));
    }

    void slidersAndEnableButtons() {
        Rig r;
        r.tool.showHslTab(QStringLiteral("controls"));
        // A slider's text box sets the control on the current segment's Source side, one undo step.
        r.value(QStringLiteral("hsl/controls/master/saturation"))->setValue(40);
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 40.0);
        QVERIFY(!r.segment(1).hasCorrection());
        // "When the Enable button for the Hue slider is turned on, the Master tab, Controls tab, and HSL tab Enable
        // buttons are also turned on" (p.604).
        for (const char* p : {"hsl", "hsl/controls", "hsl/controls/master", "hsl/controls/master/saturation"}) {
            QVERIFY2(r.tool.enableButton(QLatin1String(p))->isChecked(), p);
        }
        QVERIFY(!r.tool.enableButton(QStringLiteral("hsl/controls/master/hue"))->isChecked());
        // Turning a tab off keeps the values (p.605); on again.
        r.tool.enableButton(QStringLiteral("hsl/controls"))->click();
        QVERIFY(!r.segment(0).sourceCorrection.enabled(QStringLiteral("hsl/controls")));
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 40.0);
        QVERIFY(!r.tool.enableButton(QStringLiteral("hsl/controls"))->isChecked());
        r.tool.enableButton(QStringLiteral("hsl/controls"))->click();
        QVERIFY(r.segment(0).sourceCorrection.applies(QStringLiteral("hsl/controls/master/saturation")));
        // The direction buttons step the value (p.633).
        r.tool.control(QStringLiteral("hsl/controls/master/saturation"))->findChild<QToolButton*>(QStringLiteral("up"))->click();
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 41.0);
        // Alt+click on a tab not shown displays it without resetting (p.603); again, it resets.
        r.tool.showHslTab(QStringLiteral("hueOffsets"));
        r.tool.clickEnable(QStringLiteral("hsl/controls"), true);
        QCOMPARE(r.tool.hslTab(), QStringLiteral("controls"));
        QVERIFY(r.segment(0).hasCorrection());
        r.tool.clickEnable(QStringLiteral("hsl/controls"), true);
        QVERIFY(!r.segment(0).hasCorrection());
        r.timeline.perform(QStringLiteral("edit.undo"));
        QCOMPARE(r.tool.correction().number(QStringLiteral("hsl/controls/master/saturation")), 41.0);
        // Invert Luma on the Master tab only (p.634).
        QVERIFY(r.tool.control(QStringLiteral("hsl/controls/master/invertLuma")));
        QVERIFY(!r.tool.control(QStringLiteral("hsl/controls/shadows/invertLuma")));
        r.tool.control(QStringLiteral("hsl/controls/master/invertLuma"))->findChild<QToolButton*>(QStringLiteral("toggle"))->click();
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/invertLuma")), 1.0);
        // Away from a clip there is nothing to correct.
        r.composer.moveTo(MonitorId::Record, 60);
        r.tool.reload();
        QCOMPARE(r.tool.correction(), model::ColorCorrection{});
    }

    void hueOffsets() {
        Rig r;
        // Dragging a ChromaWheel's pointer (p.638): Red at 0, green +120, blue -120 (p.636).
        ChromaWheel* wheel = r.tool.wheel(QStringLiteral("midtones"));
        QVERIFY(wheel);
        const QPointF at = wheel->pointFor(-120, 50);
        const auto back = wheel->offsetAt(at);
        QVERIFY(std::abs(back.first + 120) < 0.5 && std::abs(back.second - 50) < 0.5);
        QTest::mousePress(wheel, Qt::LeftButton, {}, at.toPoint());
        QTest::mouseRelease(wheel, Qt::LeftButton, {}, at.toPoint());
        const model::ColorCorrection c = r.segment(0).sourceCorrection;
        QVERIFY(std::abs(c.number(QStringLiteral("hsl/hueOffsets/midtones/hue")) + 120) < 3);
        QVERIFY(std::abs(c.number(QStringLiteral("hsl/hueOffsets/midtones/amount")) - 50) < 3);
        QVERIFY(r.tool.enableButton(QStringLiteral("hsl/hueOffsets/midtones"))->isChecked());
        // The Hue and Amount text boxes (p.638).
        r.tool.findChild<QDoubleSpinBox*>(QStringLiteral("hsl/hueOffsets/master/amount"))->setValue(20);
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/hueOffsets/master/amount")), 20.0);
        // The tab's Gain is the Controls tab's Master Gain (p.603).
        r.value(QStringLiteral("hsl/hueOffsets/gain"))->setValue(150);
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/gain")), 150.0);
        QVERIFY(r.tool.enableButton(QStringLiteral("hsl/hueOffsets/gain"))->isChecked());
    }

    void lumaRanges() {
        Rig r;
        r.tool.tabButton(QStringLiteral("hsl/lumaRanges"))->click();
        QCOMPARE(r.tool.rangeTab(), QStringLiteral("highlights"));
        LumaRangesGraph* g = r.tool.lumaGraph();
        const int before = static_cast<int>(g->activeCurve().size());
        // "Click the curve at the point where you want to add the point" (p.643), then drag it up.
        const auto curve = g->activeCurve();
        const double x = 142;
        double y = 0;
        for (qsizetype i = 1; i < curve.size(); ++i) {
            if (x >= curve.at(i - 1).x() && x <= curve.at(i).x()) {
                y = curve.at(i - 1).y() + (curve.at(i).y() - curve.at(i - 1).y()) * (x - curve.at(i - 1).x()) / (curve.at(i).x() - curve.at(i - 1).x());
            }
        }
        const QPoint on = g->toWidget(QPointF(x, y)).toPoint();
        QTest::mousePress(g, Qt::LeftButton, {}, on);
        QTest::mouseMove(g, g->toWidget(QPointF(x, 80)).toPoint());
        QTest::mouseRelease(g, Qt::LeftButton, {}, g->toWidget(QPointF(x, 80)).toPoint());
        const QJsonArray stored = r.segment(0).sourceCorrection.values.value(QStringLiteral("hsl/lumaRanges/highlights")).toArray();
        QCOMPARE(stored.size(), before + 1);
        QVERIFY(model::lumaRangeWeights(r.segment(0).sourceCorrection, 142)[2] > model::lumaRangeWeights(model::ColorCorrection{}, 142)[2]);
        // Delete removes the selected point (p.643).
        g->setFocus();
        QTest::keyClick(g, Qt::Key_Delete);
        QCOMPARE(r.segment(0).sourceCorrection.values.value(QStringLiteral("hsl/lumaRanges/highlights")).toArray().size(), before);
        // The preview buttons (p.642).
        QSignalSpy preview(&r.tool, &CorrectionTool::lumaPreviewChanged);
        r.tool.setLumaPreview(CorrectionTool::LumaPreview::ThreeTone);
        QCOMPARE(preview.count(), 1);
        r.composer.correctionMonitors()->setPreview(CorrectionMonitors::Preview::ThreeTone, [&] { return r.tool.correction(); });
        r.composer.show();
        r.composer.correctionMonitors()->pictureWidget(1)->repaint();
    }
};

QTEST_MAIN(TestCorrectionTool)
#include "tst_correction_tool.moc"

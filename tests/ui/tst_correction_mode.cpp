// Color Correction mode and its monitors (FX Guide p.589-597; docs/spec/color-correction.md, 6.10b1): the mode
// button and the other modes, the three monitors and their Source menus, Reference Current and Escape, Dual Split,
// Go to Previous/Next Uncorrected Shot, Remove Effect, and Play Loop and Edit Review in the mode.
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/correction_monitors.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QSignalSpy>
#include <QMenu>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// V1: A (20 frames), filler (5), B (20), C (20); 25 frames a second. A has a Source correction.
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
    a.sourceCorrection.set(QStringLiteral("hsl/controls/master/saturation"), 0);
    model::Segment b = a;
    b.clipId = QStringLiteral("b");
    b.name = QStringLiteral("B");
    b.sourceCorrection = {};
    model::Segment c = b;
    c.clipId = QStringLiteral("c");
    c.name = QStringLiteral("C");
    seq.splice(QStringLiteral("V1"), 0, {a, model::Segment::filler(5), b, c});
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
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        composer.resize(900, 420);
        composer.show();
        timeline.resize(900, 300);
        timeline.show();
        at(30);
    }
    void at(std::int64_t frame) {
        composer.moveTo(MonitorId::Record, frame);
        settle();
    }
    std::int64_t position() const { return composer.state(MonitorId::Record).position(); }
    CorrectionMonitors* monitors() const { return composer.correctionMonitors(); }
    model::Segment segment(int index) const {
        return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(index);
    }
};

} // namespace

class TestCorrectionMode : public QObject {
    Q_OBJECT

private slots:
    void enteringAndLeaving() {
        Rig r;
        QSignalSpy changed(&r.timeline, &Timeline::correctionModeChanged);
        // "Click the Color Correction Mode button" (p.589): the three monitors take the Source and Record monitors'
        // place.
        QVERIFY(r.timeline.perform(QStringLiteral("cc.color_correction_mode")));
        QVERIFY(r.timeline.isCorrectionMode());
        QVERIFY(r.composer.isCorrectionMode());
        QVERIFY(r.monitors()->isVisible());
        QCOMPARE(changed.count(), 1);
        // "To exit Color Correction mode ... Click a mode button other than Color Correction, for example, the Effect
        // Mode button" (p.589).
        r.timeline.perform(QStringLiteral("fx.effect_mode"));
        QVERIFY(!r.timeline.isCorrectionMode());
        QVERIFY(r.timeline.isEffectMode());
        QVERIFY(!r.monitors()->isVisible());
        r.timeline.perform(QStringLiteral("cc.color_correction_mode"));
        QVERIFY(!r.timeline.isEffectMode());
        r.timeline.perform(QStringLiteral("edit.source_record_mode"));
        QVERIFY(!r.timeline.isCorrectionMode());
        r.timeline.perform(QStringLiteral("cc.color_correction_mode"));
        r.timeline.perform(QStringLiteral("trim.trim_mode"));
        QVERIFY(r.timeline.isTrimMode());
        QVERIFY(!r.timeline.isCorrectionMode());
    }

    void monitors() {
        Rig r;
        r.timeline.setCorrectionMode(true);
        CorrectionMonitors* m = r.monitors();
        // Previous, Current and Next by default: "the last frame of the segment preceding the current segment, and the
        // first frame of the segment following" (p.590). Filler is not a shot.
        QCOMPARE(m->source(0), CorrectionSource::Previous);
        QCOMPARE(m->frameOf(0), std::optional<std::int64_t>(19));
        QCOMPARE(m->frameOf(1), std::optional<std::int64_t>(30));
        QCOMPARE(m->frameOf(2), std::optional<std::int64_t>(45));
        QCOMPARE(m->sourceButton(0)->text(), QStringLiteral("Previous"));
        // Moving to another segment updates them all.
        r.at(50);
        QCOMPARE(m->frameOf(0), std::optional<std::int64_t>(44));
        QCOMPARE(m->frameOf(2), std::nullopt);
        // "Current ... is not available in the Source menu if another monitor is already set to Current" (p.591).
        QVERIFY(!m->setSource(0, CorrectionSource::Current));
        QVERIFY(m->setSource(2, CorrectionSource::SecondPrevious));
        QCOMPARE(m->frameOf(2), std::optional<std::int64_t>(19));
        QVERIFY(m->setSource(0, CorrectionSource::Empty));
        QCOMPARE(m->frameOf(0), std::nullopt);
        // Reference Current locks the frame in the right monitor (p.591, p.594); Escape switches between the centre and
        // right monitors (p.593).
        r.composer.perform(QStringLiteral("cc.reference_current"));
        QCOMPARE(m->source(2), CorrectionSource::Reference);
        QCOMPARE(m->referenceFrame(), std::optional<std::int64_t>(50));
        r.at(5);
        QCOMPARE(m->frameOf(2), std::optional<std::int64_t>(50));
        QCOMPARE(m->activeMonitor(), 1);
        QTest::keyClick(m, Qt::Key_Escape);
        QCOMPARE(m->activeMonitor(), 2);
        QTest::keyClick(m, Qt::Key_Escape);
        QCOMPARE(m->activeMonitor(), 1);
        // Dual Split on the active monitor (p.592), its box resized by its handles.
        r.composer.perform(QStringLiteral("cc.dual_split"));
        QVERIFY(m->dualSplit(1));
        QVERIFY(m->monitorButton(1, QStringLiteral("cc.dual_split"))->isChecked());
        m->setSplitBox(1, QRectF(0.1, 0.2, 0.3, 0.4));
        QCOMPARE(m->splitBox(1), QRectF(0.1, 0.2, 0.3, 0.4));
        m->monitorButton(1, QStringLiteral("cc.dual_split"))->click();
        QVERIFY(!m->dualSplit(1));
        m->pictureWidget(1)->repaint();
    }

    void uncorrectedShotsAndRemove() {
        Rig r;
        r.timeline.setCorrectionMode(true);
        r.at(5);
        // Go to Next Uncorrected Shot skips the filler to B (p.592).
        QVERIFY(r.timeline.perform(QStringLiteral("cc.next_uncorrected_shot")));
        QCOMPARE(r.position(), 25);
        // The monitor buttons ask the Timeline through the Composer.
        QSignalSpy asked(&r.composer, &Composer::commandRequested);
        r.monitors()->monitorButton(1, QStringLiteral("cc.next_uncorrected_shot"))->click();
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.at(0).at(0).toString(), QStringLiteral("cc.next_uncorrected_shot"));
        r.timeline.perform(QStringLiteral("cc.next_uncorrected_shot"));
        QCOMPARE(r.position(), 45);
        r.timeline.perform(QStringLiteral("cc.previous_uncorrected_shot"));
        QCOMPARE(r.position(), 25);
        QVERIFY(!r.timeline.goToUncorrectedShot(-1)); // A is corrected
        // Setting a correction on the current segment, then Remove Effect in the mode (p.592), each undoable.
        model::ColorCorrection c;
        c.set(QStringLiteral("hsl/controls/master/gain"), 120);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QCOMPARE(r.segment(2).sourceCorrection, c);
        QCOMPARE(r.timeline.currentCorrection(), std::optional<model::ColorCorrection>(c));
        r.timeline.setCorrectionSide(model::CorrectionSide::Program);
        QCOMPARE(r.timeline.currentCorrection(), std::optional<model::ColorCorrection>(model::ColorCorrection{}));
        r.timeline.setCorrectionSide(model::CorrectionSide::Source);
        QVERIFY(r.timeline.perform(QStringLiteral("fx.remove_effect")));
        QVERIFY(!r.segment(2).hasCorrection());
        r.timeline.perform(QStringLiteral("edit.undo"));
        QCOMPARE(r.segment(2).sourceCorrection, c);
        r.at(22); // filler: nothing to correct
        QVERIFY(!r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QVERIFY(!r.timeline.currentCorrection());
    }

    void playLoopAndEditReview() {
        Rig r;
        r.timeline.setCorrectionMode(true);
        r.at(30);
        // Play Loop "plays the whole sequence in the active monitor, starting from the current position" (p.594).
        QVERIFY(r.timeline.perform(QStringLiteral("trim.play_loop")));
        QVERIFY(r.composer.isLooping());
        r.composer.advance(400);
        QVERIFY(r.position() > 30);
        r.composer.advance(2000);
        QVERIFY(r.composer.isLooping()); // round again from the start
        r.composer.stop();
        // Edit Review: part of the previous clip, the current clip and part of the next, then back (p.594).
        r.at(30);
        QVERIFY(r.timeline.perform(QStringLiteral("play.edit_review")));
        QVERIFY(r.composer.isPlaying());
        QVERIFY(r.position() < 25);
        for (int i = 0; i < 100 && r.composer.isPlaying(); ++i) r.composer.advance(100);
        QVERIFY(!r.composer.isPlaying());
        QCOMPARE(r.position(), 30);
    }

    void scopes() {
        // The waveform and vectorscope commands in the Source menu (p.591, p.686-687): "Media Composer displays the
        // information for the currently active monitor".
        Rig r;
        r.timeline.setCorrectionMode(true);
        r.composer.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(Qt::red);
            return image;
        });
        CorrectionMonitors* m = r.monitors();
        QCOMPARE(m->activeMonitor(), 1);
        QToolButton* menu = m->sourceButton(2);
        QStringList entries;
        for (QAction* a : menu->menu()->actions()) {
            if (!a->isSeparator()) entries << a->text();
        }
        QCOMPARE(entries.mid(8), (QStringList{"Quad Display", "RGB Histogram", "RGB Parade", "Vectorscope", "Y Waveform", "Y Waveform HDR 1000",
                                              "Y Waveform HDR 10000", "YC Waveform", "YCbCr Histogram", "YCbCr Parade"}));
        QVERIFY(m->setSource(2, CorrectionSource::Vectorscope));
        QCOMPARE(menu->text(), QStringLiteral("Vectorscope"));
        QCOMPARE(m->frameOf(2), m->frameOf(1));
        // A scope monitor is not made active; the frame it measures follows the active monitor.
        m->setActiveMonitor(2);
        QCOMPARE(m->activeMonitor(), 1);
        m->setActiveMonitor(0);
        QCOMPARE(m->frameOf(2), m->frameOf(0));
        // Turning the active monitor into a scope makes another one active.
        QVERIFY(m->setSource(0, CorrectionSource::YWaveform));
        QCOMPARE(m->activeMonitor(), 1);
        // The scope is drawn from the frame: red lies off the vectorscope's centre in green.
        settle();
        QWidget* picture = m->pictureWidget(2);
        const QImage shot = picture->grab().toImage();
        int green = 0;
        for (int y = 0; y < shot.height(); ++y) {
            for (int x = 0; x < shot.width(); ++x) green += qGreen(shot.pixel(x, y)) > 150 && qRed(shot.pixel(x, y)) < 100 ? 1 : 0;
        }
        QVERIFY(green > 0);
        m->setPal(true);
        QVERIFY(m->isPal());
    }
};

QTEST_MAIN(TestCorrectionMode)
#include "tst_correction_mode.moc"

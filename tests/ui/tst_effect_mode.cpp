// Effect mode and the Effect Preview monitor (FX Guide p.48-50, p.75-82; docs/spec/effect-mode.md): the
// mode buttons, the Record monitor as the Effect Preview with its position bar over the effect, its
// length and position boxes, keyframes and toolbar, Reduce/Enlarge, and Delete in Effect mode.
#include "lumenis/model/safe_color_limiter.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

const QString kLevel = QStringLiteral("Foreground/Level");

// V1: A (50 frames) then B (50) with a Picture-in-Picture; 25 frames a second.
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
    EffectEditor editor{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        const EffectInfo* pip = EffectCatalog::builtin().find(QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"));
        Q_ASSERT(pip);
        timeline.dropEffect(QStringLiteral("V1"), 70, *pip);
        composer.resize(900, 420);
        composer.show();
        editor.resize(600, 700);
        editor.show();
        timeline.resize(900, 300);
        timeline.show();
        at(70);
    }
    void at(std::int64_t frame) {
        composer.moveTo(MonitorId::Record, frame);
        settle();
    }
    std::int64_t position() const { return composer.state(MonitorId::Record).position(); }
    QToolButton* button(const char* name) const { return composer.effectBar()->findChild<QToolButton*>(QString::fromLatin1(name)); }
    model::ParameterKeyframes keys(const QString& p) const {
        const model::Sequence s = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        const auto& effect = s.track(QStringLiteral("V1"))->segments.at(1).effect;
        return effect ? model::keyframesOf(*effect, p.section('/', 0, 0), p.section('/', 1)) : model::ParameterKeyframes();
    }
};

} // namespace

class TestEffectMode : public QObject {
    Q_OBJECT

private slots:
    void safeColorLimiterSeed() {
        // "When you first apply a Safe Color Limiter effect, its parameters default to the current limits in the Safe
        // Color settings" (FX Guide p.728): the Timeline's seed (MainWindow gives it the setting).
        Rig r;
        model::SafeColorLimits set;
        set.luma = {7.5, 100.0, model::SafeUnit::Ire};
        r.timeline.setEffectSeed([set](model::Effect& e) {
            if (model::isSafeColorLimiter(e)) model::seedSafeColorLimiter(e, set);
        });
        const EffectInfo* limiter = EffectCatalog::builtin().find(QStringLiteral("Image"), QStringLiteral("Safe Color Limiter"));
        QVERIFY(limiter);
        QVERIFY(r.timeline.dropEffect(QStringLiteral("V1"), 10, *limiter));
        const model::Sequence s = model::Sequence::fromItem(r.composer.state(MonitorId::Record).current()->item);
        const auto& effect = s.track(QStringLiteral("V1"))->segments.at(0).effect;
        QVERIFY(effect && model::isSafeColorLimiter(*effect));
        QCOMPARE(model::safeColorLimiting(*effect, 0.0, model::EffectSpan(10)).limits.luma, set.luma);
    }

    void safeColorLimiterAnalysis() {
        // "By default, when the Effect Editor is open, the effect changes the display in the Source monitor to an
        // analysis mode"; Source Monitoring – Off turns it off (FX Guide p.545, p.729).
        Rig r;
        const EffectInfo* limiter = EffectCatalog::builtin().find(QStringLiteral("Image"), QStringLiteral("Safe Color Limiter"));
        QVERIFY(limiter && r.timeline.dropEffect(QStringLiteral("V1"), 10, *limiter));
        r.at(10);
        r.editor.refresh();
        QCOMPARE(r.editor.effectName(), QStringLiteral("Safe Color Limiter"));
        const auto analysis = r.editor.safeColorAnalysis();
        QVERIFY(analysis && analysis->compositeLuma && analysis->rgb);
        QCOMPARE(analysis->limits, model::SafeColorLimits{});
        r.timeline.changeRecordSequence(QStringLiteral("Modify Effect"), [](model::Sequence& seq) {
            model::Effect& e = *seq.track(QStringLiteral("V1"))->segments[0].effect;
            QJsonObject p = e.parameters.value(model::kSafeColorLimiterCategory).toObject();
            p.insert(QStringLiteral("Source Monitor Analysis"), QStringLiteral("Source Monitoring – Off"));
            e.parameters.insert(model::kSafeColorLimiterCategory, p);
            return true;
        });
        r.editor.refresh();
        QVERIFY(!r.editor.safeColorAnalysis());
        // On another effect, nothing.
        r.at(70);
        r.editor.refresh();
        QVERIFY(!r.editor.safeColorAnalysis());
        // The Composer's override fills a monitor, loaded or not, while it gives a picture.
        QVERIFY(r.composer.state(MonitorId::Source).isEmpty());
        r.composer.setMonitorOverride([](MonitorId m, QSize size) {
            QImage image;
            if (m == MonitorId::Source) {
                image = QImage(size, QImage::Format_RGB32);
                image.fill(Qt::yellow);
            }
            return image;
        });
        QCOMPARE(r.composer.pictureAt(MonitorId::Source, 0, QSize(8, 8)).pixel(1, 1), qRgb(255, 255, 0));
        QCOMPARE(r.composer.overridePicture(MonitorId::Record, QSize(8, 8)), QImage());
    }

    void modeButtons() {
        Rig rig;
        QToolButton* sourceRecord = rig.timeline.toolButton(QStringLiteral("edit.source_record_mode"));
        QToolButton* effect = rig.timeline.toolButton(QStringLiteral("fx.effect_mode"));
        QToolButton* trim = rig.timeline.toolButton(QStringLiteral("trim.trim_mode"));
        QVERIFY(sourceRecord && effect && trim);
        QVERIFY(sourceRecord->isChecked());
        QSignalSpy spy(&rig.timeline, &Timeline::effectModeChanged);
        // "Click the Effect Mode button" (p.49).
        effect->click();
        QVERIFY(rig.timeline.isEffectMode());
        QVERIFY(effect->isChecked() && !sourceRecord->isChecked());
        QCOMPARE(spy.count(), 1);
        // Trim mode leaves Effect mode; Effect mode leaves Trim mode.
        QVERIFY(rig.timeline.perform(QStringLiteral("trim.trim_mode")));
        QVERIFY(rig.timeline.isTrimMode());
        QVERIFY(!rig.timeline.isEffectMode());
        QVERIFY(rig.timeline.perform(QStringLiteral("fx.effect_mode")));
        QVERIFY(rig.timeline.isEffectMode());
        QVERIFY(!rig.timeline.isTrimMode());
        // The Source/Record Mode button goes back (p.457, p.652).
        sourceRecord->click();
        QVERIFY(!rig.timeline.isEffectMode());
        QVERIFY(sourceRecord->isChecked() && !effect->isChecked());
        // A second click on Effect Mode leaves it too.
        effect->click();
        effect->click();
        QVERIFY(!rig.timeline.isEffectMode());
    }

    void effectPreview() {
        Rig rig;
        QVERIFY(!rig.composer.effectPreview().on);
        rig.timeline.setEffectMode(true);
        settle();
        // "The Record monitor becomes the Effect Preview monitor" (p.49): the bar spans the effect only.
        const Composer::EffectPreview& p = rig.composer.effectPreview();
        QVERIFY(p.on);
        QCOMPARE(p.start, 50);
        QCOMPARE(p.length, 50);
        PositionBar* bar = rig.composer.positionBar(MonitorId::Record);
        QCOMPARE(bar->frameAt(0), 50);
        QCOMPARE(bar->frameAt(bar->width() - 1), 99);
        // The length and position boxes replace the sequence's name (p.77-78).
        QVERIFY(rig.composer.effectLengthBox()->isVisible());
        QVERIFY(!rig.composer.clipNameButton(MonitorId::Record)->isVisible());
        QCOMPARE(rig.composer.effectLengthBox()->text(), QStringLiteral("2:00"));
        QCOMPARE(rig.composer.effectPositionBox()->text(), QStringLiteral("0:20"));
        // Its toolbar replaces the Record monitor's buttons (p.76-77).
        QVERIFY(rig.composer.effectBar()->isVisible());
        QVERIFY(rig.button("effect.add_keyframe")->isEnabled());
        QVERIFY(!rig.button("effect.dual_split")->isEnabled());
        // Away from any effect it is the Record monitor again.
        rig.at(10);
        QVERIFY(!rig.composer.effectPreview().on);
        QVERIFY(!rig.composer.effectBar()->isVisible());
        QVERIFY(rig.composer.clipNameButton(MonitorId::Record)->isVisible());
        rig.at(70);
        QVERIFY(rig.composer.effectPreview().on);
        // Leaving Effect mode ends it.
        rig.timeline.setEffectMode(false);
        settle();
        QVERIFY(!rig.composer.effectPreview().on);
    }

    void keyframesAndToolbar() {
        Rig rig;
        rig.timeline.setEffectMode(true);
        rig.editor.setKeyframeMode(EffectEditor::KeyframeMode::AllParameters);
        // The Add Keyframe button "adds one or more keyframes as determined by the current default
        // command" (p.105), with no menu.
        rig.at(60);
        rig.button("effect.add_keyframe")->click();
        settle();
        rig.at(80);
        rig.button("effect.add_keyframe")->click();
        settle();
        QCOMPARE(rig.keys(kLevel).keys.size(), 2);
        // Its keyframes are in the bar, one indicator per time, pink when selected (p.76, p.103).
        const Composer::EffectPreview& p = rig.composer.effectPreview();
        QCOMPARE(p.keyframes.size(), 2);
        QCOMPARE(p.keyframes.at(0).frame, 10.0);
        QCOMPARE(p.keyframes.at(1).frame, 30.0);
        QVERIFY(!p.keyframes.at(0).selected);
        QVERIFY(p.keyframes.at(1).selected);
        // Clicking one selects all the keyframes at that time and moves the position there.
        PositionBar* bar = rig.composer.positionBar(MonitorId::Record);
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(bar->xFor(60), bar->height() / 2));
        settle();
        QCOMPARE(rig.position(), 60);
        QVERIFY(rig.composer.effectPreview().keyframes.at(0).selected);
        QVERIFY(!rig.composer.effectPreview().keyframes.at(1).selected);
        QVERIFY(rig.editor.selectedKeyframes().contains(EffectEditor::KeyframeId{kLevel, 0}));
        // Alt+click on Add Keyframe deletes the selected keyframes (p.106).
        emit rig.composer.effectCommandRequested(QStringLiteral("effect.add_keyframe"), true);
        settle();
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
        // Go to Previous/Next Event: the keyframes (p.128).
        rig.at(50);
        rig.button("effect.next_event")->click();
        settle();
        QCOMPARE(rig.position(), 80);
        // The steps stay inside the effect.
        rig.at(99);
        rig.button("effect.step_forward")->click();
        QCOMPARE(rig.position(), 99);
        rig.button("effect.step_backward")->click();
        QCOMPARE(rig.position(), 98);
        // Play and Play Loop play the effect (p.55, p.77).
        rig.button("effect.play_loop")->click();
        QVERIFY(rig.composer.isLooping());
        rig.button("effect.play_loop")->click();
        QVERIFY(!rig.composer.isPlaying());
        rig.button("effect.play")->click();
        QVERIFY(rig.composer.isPlaying());
        rig.composer.stop();
    }

    void reduceAndEnlarge() {
        Rig rig;
        rig.timeline.setEffectMode(true);
        settle();
        MonitorPicture* picture = rig.composer.picture(MonitorId::Record);
        const QRect normal = picture->pictureRect();
        rig.button("effect.enlarge")->click();
        QCOMPARE(rig.composer.previewZoom(), 1);
        QCOMPARE(picture->pictureRect().width(), normal.width() * 2);
        rig.button("effect.reduce")->click();
        rig.button("effect.reduce")->click();
        QCOMPARE(rig.composer.previewZoom(), -1);
        // "Ctrl+click ... in the Effect Preview monitor to enlarge the image" (p.77).
        QTest::mouseClick(picture, Qt::LeftButton, Qt::ControlModifier, picture->rect().center());
        QCOMPARE(rig.composer.previewZoom(), 0);
        QTest::mouseClick(picture, Qt::LeftButton, Qt::ControlModifier, picture->rect().center());
        // Ctrl+Alt+drag moves the enlarged image (p.82).
        const QPoint from = picture->rect().center();
        QTest::mousePress(picture, Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier, from);
        QMouseEvent move(QEvent::MouseMove, from + QPoint(20, 10), picture->mapToGlobal(from + QPoint(20, 10)), Qt::NoButton,
                         Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier);
        QApplication::sendEvent(picture, &move);
        QCOMPARE(rig.composer.previewPan(), QPointF(20, 10));
        // Out of Effect mode the picture is its normal size again.
        rig.timeline.setEffectMode(false);
        settle();
        QCOMPARE(rig.composer.previewZoom(), 0);
        QCOMPARE(picture->pictureRect().size(), normal.size());
    }

    void deleteInEffectMode() {
        // "In Effect mode, select the effect and press Delete" (p.47).
        Rig rig;
        rig.timeline.setEffectMode(true);
        rig.timeline.toggleRecordTrack(QStringLiteral("V1"));
        rig.timeline.toggleRecordTrack(QStringLiteral("V1"));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.delete")));
        const model::Sequence s = model::Sequence::fromItem(rig.composer.state(MonitorId::Record).current()->item);
        QVERIFY(!s.track(QStringLiteral("V1"))->segments.at(1).effect);
    }
};

QTEST_MAIN(TestEffectMode)
#include "tst_effect_mode.moc"

// The Color Correction tool (FX Guide p.595-605, p.631-644; docs/spec/color-correction.md, 6.10b2): the Correction Type
// button, the tabs and their Enable buttons with Alt+click resets, Page Up/Down, the Controls sliders, the ChromaWheels,
// the Hue Offsets tab's Gain/Gamma/Setup and the Luma Ranges graph.
#include "lumenis/media/color_correction.h"
#include "lumenis/media/custom_color.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/correction_effect.h"
#include "lumenis/model/correction_keyframes.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/correction_monitors.h"
#include "lumenis/ui/correction_tool.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QMenu>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>
#include <QToolButton>

#include <memory>

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
        // The Correction Type button: the relationships (p.597); the Color Correction effect waits for 6.10e.
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Src Segment"));
        QAction* prog = nullptr;
        for (QAction* a : r.tool.typeButton()->menu()->actions()) {
            if (a->text() == QLatin1String("Prog Segment")) prog = a;
            if (a->text() == QLatin1String("Src Tape")) QVERIFY(a->isEnabled());
            if (a->text() == QLatin1String("Color Correction Effect")) QVERIFY(!a->isEnabled());
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

    // The relationships (p.596-598, 6.10d1): Prog Track corrects the whole track; a type chosen for the other side stays
    // while the position stays; moving to a corrected segment the button follows its type; a type of the correction's
    // side converts it. The Timeline Fast menu's Color Correction shows the indicator lines (p.616).
    void relationships() {
        Rig r;
        const auto choose = [&](const QString& key) {
            QAction* a = r.tool.typeButton()->menu()->findChild<QAction*>(QStringLiteral("type:") + key);
            if (!a) {
                for (QAction* x : r.tool.typeButton()->menu()->actions()) {
                    if (x->objectName() == QStringLiteral("type:") + key) a = x;
                }
            }
            QVERIFY2(a, qPrintable(key));
            a->trigger();
        };
        choose(QStringLiteral("programTrack"));
        QCOMPARE(r.timeline.correctionRelationship(), model::CorrectionRelationship::ProgramTrack);
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Prog Track"));
        model::ColorCorrection c;
        c.set(QStringLiteral("hsl/controls/master/gain"), 120);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QCOMPARE(r.segment(0).programCorrection, c);
        QCOMPARE(r.segment(1).programCorrection, c);
        QCOMPARE(r.segment(1).programRelationship, model::CorrectionRelationship::ProgramTrack);
        QVERIFY(r.segment(0).sourceCorrection.isEmpty());
        // Src Segment on a segment with only a Program correction: nothing to convert, and the button keeps it.
        choose(QStringLiteral("sourceSegment"));
        r.tool.reload();
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Src Segment"));
        // The next segment has a Program correction: the button follows it there.
        r.composer.moveTo(MonitorId::Record, 25);
        r.tool.reload();
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Prog Track"));
        QCOMPARE(r.timeline.correctionSide(), model::CorrectionSide::Program);
        // Prog Segment converts it (p.598): both segments keep the values, each as its own correction; one undo step.
        choose(QStringLiteral("programSegment"));
        QCOMPARE(r.segment(1).programRelationship, model::CorrectionRelationship::ProgramSegment);
        QCOMPARE(r.segment(0).programRelationship, model::CorrectionRelationship::ProgramSegment);
        QCOMPARE(r.segment(0).programCorrection, c);
        QCOMPARE(r.tool.typeButton()->text(), QStringLiteral("Prog Segment"));
        r.timeline.perform(QStringLiteral("edit.undo"));
        QCOMPARE(r.segment(0).programRelationship, model::CorrectionRelationship::ProgramTrack);

        // The indicator lines, kept with the Timeline's view (p.616).
        QVERIFY(!r.timeline.display().correctionLines);
        QAction* lines = nullptr;
        for (QAction* a : r.timeline.fastMenu()->actions()) {
            if (a->text() == QLatin1String("Color Correction")) lines = a;
        }
        QVERIFY(lines && lines->isEnabled() && lines->isCheckable());
        lines->trigger();
        QVERIFY(r.timeline.display().correctionLines);
        QVERIFY(Timeline::displayToJson(r.timeline.display()).value(QStringLiteral("correctionLines")).toBool());
        r.timeline.view()->grab(); // draws the lines
    }

    // Color Correction effect templates (p.610-615, 6.10d2a): the template button's drag, the buckets, a drop on a tab,
    // on a segment in the Timeline and on selected segments.
    void templates() {
        Rig r;
        QVERIFY(!r.tool.button(QStringLiteral("template"))->isEnabled()); // nothing to save yet
        QVERIFY(!r.tool.templateMime());
        model::ColorCorrection c;
        c.set(QStringLiteral("hsl/controls/master/gain"), 130);
        c.set(QStringLiteral("channels/red/2/amount"), 90);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        r.tool.reload();
        QVERIFY(r.tool.button(QStringLiteral("template"))->isEnabled());
        std::unique_ptr<QMimeData> mime(r.tool.templateMime());
        QVERIFY(mime && mime->hasFormat(correctionTemplateMime()));
        const QJsonObject o = QJsonDocument::fromJson(mime->data(correctionTemplateMime())).object();
        QCOMPARE(o.value(QStringLiteral("name")).toString(), QStringLiteral("A")); // the clip's name (p.613)
        QCOMPARE(r.timeline.droppedCorrectionTemplate(mime.get())->source, c);
        // Save Correction asks for a bin.
        QSignalSpy saved(&r.tool, &CorrectionTool::saveCorrectionRequested);
        QVERIFY(r.tool.saveCorrection());
        QCOMPARE(saved.count(), 1);

        // Buckets: empty ones say "C1"..."C8"; Alt+click saves, and the label gives way to the icon (p.613-614).
        QCOMPARE(r.tool.bucketButton(0)->text(), QStringLiteral("C1"));
        QVERIFY(!r.tool.clickBucket(0, false)); // empty: nothing to apply
        QVERIFY(r.tool.clickBucket(0, true));
        QVERIFY(r.tool.bucket(0));
        QVERIFY(r.tool.bucketButton(0)->text().isEmpty());
        // Clicked on the next segment: applied with the Correction Type's relationship (p.615).
        r.composer.moveTo(MonitorId::Record, 25);
        r.tool.reload();
        QVERIFY(r.segment(1).sourceCorrection.isEmpty());
        QVERIFY(r.tool.clickBucket(0, false));
        QCOMPARE(r.segment(1).sourceCorrection, c);
        // Alt+click on an uncorrected segment clears the bucket (p.614).
        r.timeline.perform(QStringLiteral("edit.undo"));
        r.tool.reload();
        QVERIFY(r.segment(1).sourceCorrection.isEmpty());
        QVERIFY(r.tool.clickBucket(0, true));
        QVERIFY(!r.tool.bucket(0));
        QCOMPARE(r.tool.bucketButton(0)->text(), QStringLiteral("C1"));
        // The bucket button clicked with Alt, through the mouse.
        r.composer.moveTo(MonitorId::Record, 5);
        r.tool.reload();
        QTest::mouseClick(r.tool.bucketButton(1), Qt::LeftButton, Qt::AltModifier);
        QVERIFY(r.tool.bucket(1));
        // A click without Alt (on an empty bucket, which does nothing) so the application's modifiers are not left at Alt.
        QTest::mouseClick(r.tool.bucketButton(7), Qt::LeftButton);
        QVERIFY(!r.tool.bucket(7));

        // Dropped on the tool: only the shown tab's controls (p.615).
        r.composer.moveTo(MonitorId::Record, 25);
        r.tool.reload();
        r.tool.tabButton(QStringLiteral("channels"))->click();
        QCOMPARE(r.tool.shownPath(), QStringLiteral("channels"));
        // Qt gives the drop to the widget that took the drag's enter.
        QDragEnterEvent enter(QPoint(20, 60), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&r.tool, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(20, 60), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&r.tool, &drop);
        QCOMPARE(r.segment(1).sourceCorrection.number(QStringLiteral("channels/red/2/amount")), 90.0);
        QVERIFY(!r.segment(1).sourceCorrection.values.contains(QStringLiteral("hsl/controls/master/gain")));

        // On the mode's monitors (p.614): through the hook MainWindow sets, with the template's relationships.
        CorrectionMonitors* monitors = r.composer.correctionMonitors();
        monitors->setTemplateDrop([&](const QMimeData* m, bool apply) {
            const auto t = r.timeline.droppedCorrectionTemplate(m);
            return t && (!apply || r.timeline.applyCorrectionTemplate(*t, true));
        });
        QVERIFY(monitors->acceptDrops());
        QDragEnterEvent onMonitor(QPoint(10, 10), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(monitors, &onMonitor);
        QVERIFY(onMonitor.isAccepted());
        QDropEvent dropped(QPointF(10, 10), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(monitors, &dropped);
        QCOMPARE(r.segment(1).sourceCorrection, c); // the whole template now
        // On a segment in the Timeline: the template's relationships, and the side it lacks goes (p.614-615).
        model::CorrectionTemplate program;
        program.program = c;
        program.programRelationship = model::CorrectionRelationship::ProgramTrack;
        QVERIFY(r.timeline.dropCorrectionTemplate(QStringLiteral("V1"), 25, program));
        QVERIFY(r.segment(1).sourceCorrection.isEmpty());
        QCOMPARE(r.segment(1).programRelationship, model::CorrectionRelationship::ProgramTrack);
        QCOMPARE(r.segment(0).programCorrection, c); // Prog Track reached A too
        QVERIFY(!r.timeline.dropCorrectionTemplate(QStringLiteral("A1"), 25, program));

        // Selected segments, outside the mode (p.615): each gets it with the Correction Type's relationship.
        r.timeline.perform(QStringLiteral("edit.undo"));
        r.timeline.setCorrectionMode(false);
        r.timeline.setSelection({{QStringLiteral("V1"), 0}, {QStringLiteral("V1"), 20}});
        model::CorrectionTemplate source;
        source.source.set(QStringLiteral("hsl/controls/master/saturation"), 50);
        QVERIFY(r.timeline.applyCorrectionTemplateToSelection(source));
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 50.0);
        QCOMPARE(r.segment(1).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 50.0);
        r.timeline.setSelection({});
        QVERIFY(!r.timeline.applyCorrectionTemplateToSelection(source));
    }

    // The Effect Palette's automatic templates and Color Correction Presets (p.611-612, 6.10d2b).
    void automaticTemplatesAndPresets() {
        Rig r;
        const QString seq = r.composer.state(MonitorId::Record).current()->item.id;
        QSignalSpy changed(&r.timeline, &Timeline::automaticCorrectionsChanged);
        model::ColorCorrection c;
        c.set(QStringLiteral("hsl/controls/master/gain"), 110);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(r.timeline.correctedSequences(), QStringList{seq});
        QCOMPARE(r.timeline.automaticCorrections(seq).size(), 1);
        QCOMPARE(r.timeline.automaticCorrections(seq).first().name, QStringLiteral("A")); // the clip's name
        // Another adjustment on the same segment and side updates it (p.611).
        c.set(QStringLiteral("hsl/controls/master/gain"), 120);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QCOMPARE(r.timeline.automaticCorrections(seq).size(), 1);
        QCOMPARE(r.timeline.automaticCorrections(seq).first().correction.source.number(QStringLiteral("hsl/controls/master/gain")), 120.0);
        // The other side is another template, with both sides in it (p.612); the newest is number 1.
        r.timeline.setCorrectionSide(model::CorrectionSide::Program);
        model::ColorCorrection p;
        p.set(QStringLiteral("hsl/controls/master/saturation"), 70);
        QVERIFY(r.timeline.setCurrentCorrection(p, QStringLiteral("Color Correction")));
        QCOMPARE(r.timeline.automaticCorrections(seq).size(), 2);
        QCOMPARE(r.timeline.automaticCorrections(seq).first().correction.program, p);
        QCOMPARE(r.timeline.automaticCorrections(seq).first().correction.source, c);
        // A comment names it (p.612).
        r.timeline.setCorrectionSide(model::CorrectionSide::Source);
        r.composer.moveTo(MonitorId::Record, 25);
        QVERIFY(r.timeline.setCorrectionComment(QStringLiteral("sky")));
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QCOMPARE(r.timeline.automaticCorrections(seq).first().name, QStringLiteral("sky"));
        // By the palette's ids: the bin and the number from 1; a drag of one carries it.
        const QString bin = Timeline::automaticCorrectionBin(seq);
        QCOMPARE(r.timeline.automaticCorrection(bin, QStringLiteral("2"))->program, p);
        QVERIFY(!r.timeline.automaticCorrection(bin, QStringLiteral("9")));
        QVERIFY(!r.timeline.automaticCorrection(QStringLiteral("bin"), QStringLiteral("1")));
        QMimeData drag;
        drag.setData(binItemsMime(), (bin + QStringLiteral("\n1")).toUtf8());
        QCOMPARE(r.timeline.droppedCorrectionTemplate(&drag)->source, c);
        // At most 16: the oldest goes (p.612). Alternate the segments so each is a new one.
        for (int i = 0; i < 20; ++i) {
            r.composer.moveTo(MonitorId::Record, i % 2 ? 25 : 5);
            c.set(QStringLiteral("hsl/controls/master/gain"), 130 + i);
            QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        }
        QCOMPARE(r.timeline.automaticCorrections(seq).size(), Timeline::kAutomaticCorrections);
        QCOMPARE(r.timeline.automaticCorrections(seq).first().correction.source.number(QStringLiteral("hsl/controls/master/gain")), 149.0);

        // A Color Correction Preset dragged from the Effect Palette is a template (p.611); double-clicked with segments
        // selected, it corrects them.
        const EffectInfo* preset = EffectCatalog::builtin().find(QStringLiteral("Color Correction Presets"), QStringLiteral("Black and White"));
        QVERIFY(preset);
        std::unique_ptr<QMimeData> presetMime(effectMime(*preset));
        const auto t = r.timeline.droppedCorrectionTemplate(presetMime.get());
        QVERIFY(t);
        QCOMPARE(t->source.number(QStringLiteral("hsl/controls/master/saturation")), 0.0);
        r.timeline.setCorrectionMode(false);
        r.timeline.setSelection({{QStringLiteral("V1"), 0}});
        QVERIFY(r.timeline.applyEffectToSelection(*preset));
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 0.0);
        QVERIFY(!r.segment(0).effect); // a correction, not an effect
    }

    // Keyframed corrections (p.683-686, 6.10d3a): the tool shows the values at the position; adjusting a keyframed
    // control there adds a keyframe; a plain control stays plain.
    void keyframedCorrections() {
        Rig r; // A: frames 0-19, the position at 5
        model::ColorCorrection c;
        const model::EffectSpan span(20);
        model::setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/gain"), 0.0, span, 100);
        model::setCorrectionKeyframe(c, QStringLiteral("hsl/controls/master/gain"), 1.0, span, 138);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Color Correction")));
        QCOMPARE(r.timeline.storedCorrection()->keyframes, c.keyframes);
        r.composer.moveTo(MonitorId::Record, 19);
        QCOMPARE(r.timeline.currentCorrection()->number(QStringLiteral("hsl/controls/master/gain")), 138.0);
        r.composer.moveTo(MonitorId::Record, 0);
        r.tool.reload();
        QCOMPARE(r.tool.correction().number(QStringLiteral("hsl/controls/master/gain")), 100.0);
        // At frame 10 the Gain slider is set to 90 and Saturation to 60.
        r.composer.moveTo(MonitorId::Record, 10);
        r.tool.reload();
        QCOMPARE(r.tool.correction().number(QStringLiteral("hsl/controls/master/gain")), 120.0);
        r.value(QStringLiteral("hsl/controls/master/gain"))->setValue(90);
        r.value(QStringLiteral("hsl/controls/master/saturation"))->setValue(60);
        const model::ColorCorrection stored = *r.timeline.storedCorrection();
        QCOMPARE(model::correctionKeyframesOf(stored, QStringLiteral("hsl/controls/master/gain")).keys.size(), 3);
        QVERIFY(!stored.keyframes.contains(QStringLiteral("hsl/controls/master/saturation")));
        QCOMPARE(model::correctionKeyframeFrames(stored, span), (QList<double>{0.0, 10.0, 19.0}));
        r.composer.moveTo(MonitorId::Record, 19);
        QCOMPARE(r.timeline.currentCorrection()->number(QStringLiteral("hsl/controls/master/gain")), 138.0);
        QCOMPARE(r.timeline.currentCorrection()->number(QStringLiteral("hsl/controls/master/saturation")), 60.0);
    }

    // The correction in the Effect Editor (p.683-685, 6.10d3b): its sliders ganged with the tool's, keyframes added in the
    // editor and then by adjusting in the tool, the editor's -360 to 360 hues.
    void effectEditorCorrection() {
        Rig r; // A: frames 0-19, the position at 5
        EffectEditor editor(r.timeline);
        editor.refresh();
        QSlider* gain = editor.slider(QStringLiteral("HSL Master"), QStringLiteral("Gain"));
        QVERIFY(gain);
        QVERIFY(editor.slider(QStringLiteral("HSL Shadows"), QStringLiteral("Setup")));
        QVERIFY(editor.slider(QStringLiteral("HSL Hue Offsets"), QStringLiteral("Midtones Hue Amount")));
        QVERIFY(editor.slider(QStringLiteral("Secondary Red"), QStringLiteral("Output Hue")));
        QVERIFY(editor.slider(QStringLiteral("Secondary Custom 1"), QStringLiteral("Input Saturation Width")));
        // Not shown (p.685): Isolate, Invert Last Vector, the Luma Ranges and Channels tabs.
        QVERIFY(!editor.slider(QStringLiteral("Secondary Red"), QStringLiteral("Isolate")));
        // The editor's slider sets the tool's control, and back.
        gain->setValue(130);
        QCOMPARE(r.timeline.storedCorrection()->number(QStringLiteral("hsl/controls/master/gain")), 130.0);
        r.tool.reload();
        QCOMPARE(r.tool.correction().number(QStringLiteral("hsl/controls/master/gain")), 130.0);
        r.value(QStringLiteral("hsl/controls/master/saturation"))->setValue(70);
        editor.refresh();
        QCOMPARE(editor.slider(QStringLiteral("HSL Master"), QStringLiteral("Saturation"))->value(), 70);
        // A first keyframe in the editor at frame 0, then an adjustment in the tool at frame 15 adds another (p.686).
        r.composer.moveTo(MonitorId::Record, 0);
        editor.refresh();
        editor.setActiveParameter(QStringLiteral("HSL Master/Gain"));
        QVERIFY(editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
        QCOMPARE(model::correctionKeyframesOf(*r.timeline.storedCorrection(), QStringLiteral("hsl/controls/master/gain")).keys.size(), 1);
        r.composer.moveTo(MonitorId::Record, 15);
        r.tool.reload();
        r.value(QStringLiteral("hsl/controls/master/gain"))->setValue(80);
        const model::ColorCorrection stored = *r.timeline.storedCorrection();
        QCOMPARE(model::correctionKeyframesOf(stored, QStringLiteral("hsl/controls/master/gain")).keys.size(), 2);
        r.composer.moveTo(MonitorId::Record, 0);
        QCOMPARE(r.timeline.currentCorrection()->number(QStringLiteral("hsl/controls/master/gain")), 130.0);
        // The editor follows the keyframes as the position moves.
        r.composer.moveTo(MonitorId::Record, 15);
        editor.refresh();
        QCOMPARE(editor.slider(QStringLiteral("HSL Master"), QStringLiteral("Gain"))->value(), 80);
        // "a value of +340 in the Effect Editor displays as -20 in the Color Correction tool" (p.685).
        editor.slider(QStringLiteral("HSL Master"), QStringLiteral("Hue"))->setValue(340);
        QCOMPARE(r.timeline.storedCorrection()->number(QStringLiteral("hsl/controls/master/hue")), -20.0);
        QCOMPARE(model::toolHue(-350), 10.0);
        // Out of the mode the editor shows effects again.
        r.timeline.setCorrectionMode(false);
        QVERIFY(!editor.slider(QStringLiteral("HSL Master"), QStringLiteral("Gain")));
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

    void channels() {
        Rig r;
        r.tool.showGroup(QStringLiteral("channels"));
        // The default rows (p.645): the green channel is 100 % Green and an Offset last.
        auto* type = r.tool.findChild<QToolButton*>(model::channelComponentPath(QStringLiteral("green"), 4, QStringLiteral("type")));
        QVERIFY(type);
        QCOMPARE(type->text(), QStringLiteral("% Offset"));
        // "redefine the green channel as 80% green and 20% red" (p.644).
        r.tool.findChild<QDoubleSpinBox*>(model::channelComponentPath(QStringLiteral("green"), 1, QStringLiteral("amount")))->setValue(80);
        auto* second = r.tool.findChild<QToolButton*>(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("type")));
        for (QAction* a : second->menu()->actions()) {
            if (a->text() == QLatin1String("Red")) a->trigger();
        }
        r.tool.findChild<QDoubleSpinBox*>(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("amount")))->setValue(20);
        const model::ColorCorrection c = r.segment(0).sourceCorrection;
        QCOMPARE(c.number(model::channelComponentPath(QStringLiteral("green"), 1, QStringLiteral("amount"))), 80.0);
        QCOMPARE(c.text(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("type"))), QStringLiteral("red"));
        QVERIFY(r.tool.enableButton(QStringLiteral("channels/green"))->isChecked());
        // Invert puts "A minus sign (–) ... after the component name" (p.647).
        for (QAction* a : second->menu()->actions()) {
            if (a->objectName() == QLatin1String("invert")) a->trigger();
        }
        QVERIFY(r.tool.findChild<QToolButton*>(model::channelComponentPath(QStringLiteral("green"), 2, QStringLiteral("type")))->text().endsWith(QChar(0x2013)));
        // The Preview pane (p.645-646).
        QSignalSpy preview(&r.tool, &CorrectionTool::channelPreviewChanged);
        r.tool.findChild<QRadioButton*>(QStringLiteral("channelPreview:0"))->click();
        QCOMPARE(preview.count(), 1);
        QCOMPARE(r.tool.channelPreview(), CorrectionTool::ChannelPreview::RedInput);
        r.composer.correctionMonitors()->setPreview(CorrectionMonitors::Preview::RedInput);
        r.composer.show();
        r.composer.correctionMonitors()->pictureWidget(1)->repaint();
    }

    void levels() {
        Rig r;
        // The histograms count the frame without and with corrections (p.654, p.657).
        r.tool.setPictureSource([](bool corrected) {
            QImage image(16, 9, QImage::Format_RGB32);
            image.fill(corrected ? QColor(255, 255, 255) : QColor(0, 0, 0));
            return image;
        });
        r.tool.showGroup(QStringLiteral("levels"));
        QCOMPARE(r.tool.shownPath(), QStringLiteral("levels/master"));
        QCOMPARE(r.tool.histogram(false)->counts()[16], 16 * 9 * 3);
        QCOMPARE(r.tool.histogram(true)->counts()[235], 16 * 9 * 3);
        // The Red tab's In Black Point text box (p.657).
        r.tool.tabButton(QStringLiteral("levels/red"))->click();
        QCOMPARE(r.tool.levelsTab(), QStringLiteral("red"));
        r.tool.findChild<QDoubleSpinBox*>(QStringLiteral("levels:inBlack"))->setValue(60);
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("levels/red/inBlack")), 60.0);
        QVERIFY(r.tool.enableButton(QStringLiteral("levels/red"))->isChecked());
        QCOMPARE(r.tool.histogram(false)->sliders(), (QList<double>{60, 128, 235}));
        // The ChromaCurve graph's gray point dragged: its input and output together (p.657).
        ChromaCurveGraph* g = r.tool.levelsGraph();
        QCOMPARE(g->points().size(), 3);
        QTest::mousePress(g, Qt::LeftButton, {}, g->toWidget(g->points().at(1)).toPoint());
        QTest::mouseMove(g, g->toWidget(QPointF(100, 140)).toPoint());
        QTest::mouseRelease(g, Qt::LeftButton, {}, g->toWidget(QPointF(100, 140)).toPoint());
        const model::ColorCorrection c = r.segment(0).sourceCorrection;
        QVERIFY(std::abs(c.number(QStringLiteral("levels/red/inGray")) - 100) < 3);
        QVERIFY(std::abs(c.number(QStringLiteral("levels/red/outGray")) - 140) < 3);
        // A histogram slider dragged and let go (p.656).
        Histogram* out = r.tool.histogram(true);
        const QPoint white(static_cast<int>(out->sliderX(2)), out->height() - 6);
        QTest::mousePress(out, Qt::LeftButton, {}, white);
        QTest::mouseMove(out, white - QPoint(out->width() / 4, 0));
        QTest::mouseRelease(out, Qt::LeftButton, {}, white - QPoint(out->width() / 4, 0));
        QVERIFY(r.segment(0).sourceCorrection.number(QStringLiteral("levels/red/outWhite")) < 235);
        // "You can make only black point and white point adjustments in the Composite and Luma tabs" (p.657).
        r.tool.tabButton(QStringLiteral("levels/luma"))->click();
        QVERIFY(r.tool.findChild<QDoubleSpinBox*>(QStringLiteral("levels:inGray"))->isHidden());
        QCOMPARE(r.tool.levelsGraph()->points().size(), 2);
    }

    void curves() {
        Rig r;
        r.tool.showGroup(QStringLiteral("curves"));
        ChromaCurveGraph* g = r.tool.curveGraph(QStringLiteral("red"));
        QVERIFY(g);
        QCOMPARE(g->points().size(), 2); // the 45 degree line (p.660)
        // "Click the curve line in the graph at the point where you want the new point to appear" (p.662), then drag.
        QTest::mousePress(g, Qt::LeftButton, {}, g->toWidget(QPointF(100, 100)).toPoint());
        QTest::mouseMove(g, g->toWidget(QPointF(100, 69)).toPoint());
        QTest::mouseRelease(g, Qt::LeftButton, {}, g->toWidget(QPointF(100, 69)).toPoint());
        QList<QPointF> curve = model::colorCurve(r.segment(0).sourceCorrection, QStringLiteral("red"));
        QCOMPARE(curve.size(), 3);
        QVERIFY(std::abs(curve.at(1).y() - 69) < 3);
        QVERIFY(r.tool.enableButton(QStringLiteral("curves/red"))->isChecked());
        // The Input and Output text boxes move the selected point (p.662-663).
        r.tool.findChild<QDoubleSpinBox*>(QStringLiteral("curves/red:out"))->setValue(80);
        curve = model::colorCurve(r.segment(0).sourceCorrection, QStringLiteral("red"));
        QCOMPARE(curve.at(1).y(), 80.0);
        // Delete removes it; two points always stay (p.663).
        g = r.tool.curveGraph(QStringLiteral("red"));
        g->selectPoint(1);
        g->setFocus();
        QTest::keyClick(g, Qt::Key_Delete);
        QCOMPARE(model::colorCurve(r.segment(0).sourceCorrection, QStringLiteral("red")).size(), 2);
        g->selectPoint(0);
        QTest::keyClick(g, Qt::Key_Delete);
        QCOMPARE(r.tool.curveGraph(QStringLiteral("red"))->points().size(), 2);
        // Master Saturation is the HSL group's (p.659).
        r.value(QStringLiteral("curves/masterSaturation"))->setValue(50);
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/saturation")), 50.0);
    }

    void secondary() {
        Rig r;
        r.tool.showGroup(QStringLiteral("secondary"));
        // Green selected at first (p.674); a swatch click selects a vector.
        QCOMPARE(r.tool.selectedVector(), QStringLiteral("green"));
        r.tool.findChild<QToolButton*>(QStringLiteral("swatch:red"))->click();
        QCOMPARE(r.tool.selectedVector(), QStringLiteral("red"));
        QVERIFY(r.tool.findChild<QToolButton*>(QStringLiteral("swatch:red"))->isChecked());
        // The Output Hue slider of the selected vector (p.680).
        r.value(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("hue")))->setValue(60);
        QCOMPARE(r.segment(0).sourceCorrection.number(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("hue"))), 60.0);
        QVERIFY(r.tool.enableButton(QStringLiteral("secondary/vectors/red"))->isChecked());
        // Lock Vectors carries input hue changes to the output (p.676).
        r.tool.findChild<QCheckBox*>(QStringLiteral("lockVectors"))->click();
        QVERIFY(r.tool.lockVectors());
        r.value(model::vectorPath(QStringLiteral("red"), QStringLiteral("input"), QStringLiteral("hue")))->setValue(10);
        QCOMPARE(r.segment(0).sourceCorrection.number(model::vectorPath(QStringLiteral("red"), QStringLiteral("output"), QStringLiteral("hue"))), 70.0);
        // Standard vectors show Sat Min, custom ones Sat Width (p.676).
        QVERIFY(r.tool.control(model::vectorPath(QStringLiteral("red"), QStringLiteral("input"), QStringLiteral("satMin"))));
        QVERIFY(r.tool.control(model::vectorPath(QStringLiteral("3"), QStringLiteral("input"), QStringLiteral("satWidth"))));
        QVERIFY(!r.tool.control(model::vectorPath(QStringLiteral("3"), QStringLiteral("input"), QStringLiteral("satMin"))));
        // Dragging the selected centre point in the Input wheel (p.675).
        VectorWheel* in = r.tool.vectorWheel(false);
        const QPointF from = in->pointFor(10, 60);
        const QPointF to = in->pointFor(30, 50);
        QTest::mousePress(in, Qt::LeftButton, {}, from.toPoint());
        QTest::mouseMove(in, to.toPoint());
        QTest::mouseRelease(in, Qt::LeftButton, {}, to.toPoint());
        const model::SecondaryVector v = model::secondaryVector(r.segment(0).sourceCorrection, QStringLiteral("red"));
        QVERIFY2(std::abs(v.hue - 30) < 3 && std::abs(v.saturation - 50) < 3, qPrintable(QStringLiteral("%1 %2").arg(v.hue).arg(v.saturation)));
        // Clicking another centre selects it.
        QTest::mouseClick(in, Qt::LeftButton, {}, in->pointFor(-120, 60).toPoint());
        QCOMPARE(r.tool.selectedVector(), QStringLiteral("blue"));
        // Isolate and Invert Last Vector (p.673-674).
        r.tool.findChild<QCheckBox*>(QStringLiteral("secondary/isolate"))->click();
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("secondary/isolate")), 1.0);
        // The syringe: samples from a monitor define a vector (p.678); the eyedroppers refine it (p.679).
        QSignalSpy selection(&r.tool, &CorrectionTool::colorSelectionChanged);
        r.tool.findChild<QToolButton*>(QStringLiteral("colorSelection:1"))->click();
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::Syringe);
        QCOMPARE(selection.count(), 1);
        r.tool.applySamples({QColor(40, 180, 40), QColor(50, 190, 30), QColor(30, 170, 50)});
        QCOMPARE(r.tool.selectedVector(), QStringLiteral("green"));
        const model::SecondaryVector g = model::secondaryVector(r.segment(0).sourceCorrection, QStringLiteral("green"));
        const QPointF sample = media::wheelHueSaturation({40 / 255.0, 180 / 255.0, 40 / 255.0});
        QCOMPARE(model::vectorMembership(g, sample.x(), sample.y()), 1.0);
        r.tool.findChild<QToolButton*>(QStringLiteral("colorSelection:2"))->click();
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::AddToSelection);
        r.tool.applySamples({QColor(150, 190, 30)});
        const QPointF added = media::wheelHueSaturation({150 / 255.0, 190 / 255.0, 30 / 255.0});
        QCOMPARE(model::vectorMembership(model::secondaryVector(r.segment(0).sourceCorrection, QStringLiteral("green")), added.x(), added.y()), 1.0);
        // Clicking the button again turns it off (p.678).
        r.tool.findChild<QToolButton*>(QStringLiteral("colorSelection:2"))->click();
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::None);
        // The monitors sample a drag over the picture.
        r.composer.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            image.fill(QColor(200, 40, 40));
            return image;
        });
        CorrectionMonitors* m = r.composer.correctionMonitors();
        r.composer.resize(900, 420);
        r.composer.show();
        m->setSampling(CorrectionMonitors::Sampling::Drag);
        QSignalSpy sampled(m, &CorrectionMonitors::colorsSampled);
        settle();
        QWidget* picture = m->pictureWidget(1);
        picture->repaint();
        const QPoint c = picture->rect().center();
        QTest::mousePress(picture, Qt::LeftButton, {}, c);
        QTest::mouseMove(picture, c + QPoint(5, 0));
        QTest::mouseRelease(picture, Qt::LeftButton, {}, c + QPoint(5, 0));
        QCOMPARE(sampled.count(), 1);
        const QList<QColor> colors = sampled.at(0).at(0).value<QList<QColor>>();
        QVERIFY(colors.size() >= 2);
        QCOMPARE(colors.at(0), QColor(200, 40, 40));
    }

    void settingOptions() {
        // The Correction setting (FX Guide p.606-608; Editing Guide p.1164-1166).
        Rig r;
        CorrectionTool::Options o;
        // Tabs: a group left out is not shown, and Page Up/Down passes over it.
        o.groups = {true, false, true, true, true};
        r.tool.setOptions(o);
        QVERIFY(r.tool.tabButton(QStringLiteral("channels"))->parentWidget()->isHidden());
        r.tool.showGroup(QStringLiteral("channels"));
        QCOMPARE(r.tool.group(), QStringLiteral("hsl"));
        r.tool.setFocus();
        QTest::keyClick(&r.tool, Qt::Key_PageDown);
        QCOMPARE(r.tool.group(), QStringLiteral("levels"));
        // With HSL left out, the tool moves to a group still shown.
        o.groups = {false, true, true, true, true};
        r.tool.setOptions(o);
        r.tool.showGroup(QStringLiteral("hsl"));
        QCOMPARE(r.tool.group(), QStringLiteral("levels"));
        o.groups = {true, true, true, true, true};

        // Units: 10 Bit puts reference black and white at 288 and 726 (p.606); typed numbers are on the unit too.
        o.units.luma = model::CorrectionUnit::TenBit;
        o.units.rgb = model::CorrectionUnit::Percent;
        r.tool.setOptions(o);
        r.tool.showGroup(QStringLiteral("hsl"));
        r.tool.showHslTab(QStringLiteral("controls"));
        r.tool.showRangeTab(QStringLiteral("master"));
        QDoubleSpinBox* clip = r.value(QStringLiteral("hsl/controls/master/clipHigh"));
        clip->setValue(235);
        QCOMPARE(clip->text(), QStringLiteral("726.00"));
        auto* edit = clip->findChild<QLineEdit*>();
        edit->selectAll();
        QTest::keyClicks(edit, QStringLiteral("288"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/clipHigh")), 16.0);
        // Setup is a change: twice as many 10-bit steps, no offset.
        QDoubleSpinBox* setup = r.value(QStringLiteral("hsl/controls/master/setup"));
        setup->setValue(10);
        QCOMPARE(setup->text(), QStringLiteral("20.00"));
        r.tool.showGroup(QStringLiteral("levels"));
        r.tool.showLevelsTab(QStringLiteral("red"));
        auto* inWhite = r.tool.findChild<QDoubleSpinBox*>(QStringLiteral("levels:inWhite"));
        QCOMPARE(inWhite->text(), QStringLiteral("92.2")); // 235 of 255 as a percentage
        r.tool.showLevelsTab(QStringLiteral("luma"));
        QCOMPARE(inWhite->text(), QStringLiteral("726"));
        QCOMPARE(model::fromCorrectionUnit(100, model::CorrectionUnit::Ire), 235.0);
        QCOMPARE(model::toCorrectionUnit(16, model::CorrectionUnit::Millivolts), 0.0);

        // Levels: histogram styles and Use Full Ranges' axis for the RGB and Master histograms (p.606-607).
        o.solidHistograms = false;
        o.colorHistograms = true;
        o.fullRanges = true;
        r.tool.setOptions(o);
        r.tool.showLevelsTab(QStringLiteral("master"));
        QVERIFY(!r.tool.histogram(false)->solid());
        QCOMPARE(r.tool.histogram(false)->axis(), (std::pair{-128.0, 384.0}));
        r.tool.showLevelsTab(QStringLiteral("luma"));
        QCOMPARE(r.tool.histogram(false)->axis(), (std::pair{0.0, 255.0}));

        // Dynamic Histograms: the output histogram follows a slider while it is dragged (p.607).
        r.tool.setPictureSource([](bool) {
            QImage image(8, 8, QImage::Format_RGB32);
            image.fill(QColor(128, 128, 128));
            return image;
        });
        r.tool.showLevelsTab(QStringLiteral("master"));
        Histogram* out = r.tool.histogram(true);
        auto peakAt = [out] { return static_cast<int>(std::max_element(out->counts().begin(), out->counts().end()) - out->counts().begin()); };
        const int before = peakAt();
        const QPoint white(static_cast<int>(out->sliderX(2)), out->height() - 6);
        const QPoint dragged = white - QPoint(out->width() / 6, 0);
        QTest::mousePress(out, Qt::LeftButton, {}, white);
        QTest::mouseMove(out, dragged);
        QCOMPARE(peakAt(), before); // off: nothing until it is let go
        QTest::mouseRelease(out, Qt::LeftButton, {}, dragged);
        o.dynamicHistograms = true;
        r.tool.setOptions(o);
        const int committed = peakAt();
        const QPoint white2(static_cast<int>(out->sliderX(2)), out->height() - 6);
        QTest::mousePress(out, Qt::LeftButton, {}, white2);
        QTest::mouseMove(out, white2 + QPoint(out->width() / 8, 0));
        QVERIFY2(peakAt() != committed, qPrintable(QStringLiteral("%1").arg(peakAt())));
        QTest::mouseRelease(out, Qt::LeftButton, {}, white2 + QPoint(out->width() / 8, 0));

        // Features: which vectors the Input wheel shows (p.672-673), Green selected.
        r.tool.showGroup(QStringLiteral("secondary"));
        auto shown = [&r] { return r.tool.vectorWheel(false)->vectors().size(); };
        QCOMPARE(shown(), 6); // Show Standard, the default
        o.vectors = CorrectionTool::Options::Vectors::All;
        r.tool.setOptions(o);
        QCOMPARE(shown(), 12);
        o.vectors = CorrectionTool::Options::Vectors::Custom;
        r.tool.setOptions(o);
        QCOMPARE(shown(), 7); // the custom ones and the selected one
        o.vectors = CorrectionTool::Options::Vectors::Enabled;
        r.tool.setOptions(o);
        QCOMPARE(shown(), 1);
        // The colour backgrounds of the ChromaWheels (p.608).
        o.backgrounds = true;
        r.tool.setOptions(o);
        QVERIFY(r.tool.wheel(QStringLiteral("master"))->background());

        // Eyedropper 3 x 3 Averaging in the monitors: columns of black and white average to a grey.
        r.composer.setPictureResolver([](MonitorId, std::int64_t, QSize size, bool*) {
            QImage image(size, QImage::Format_RGB32);
            for (int x = 0; x < size.width(); ++x) {
                for (int y = 0; y < size.height(); ++y) image.setPixelColor(x, y, x % 2 ? Qt::white : Qt::black);
            }
            return image;
        });
        CorrectionMonitors* m = r.composer.correctionMonitors();
        r.composer.resize(900, 420);
        r.composer.show();
        m->setSampling(CorrectionMonitors::Sampling::Click);
        QSignalSpy sampled(m, &CorrectionMonitors::colorsSampled);
        settle();
        QWidget* picture = m->pictureWidget(1);
        picture->repaint();
        const QPoint c = picture->rect().center();
        QTest::mouseClick(picture, Qt::LeftButton, {}, c);
        m->setEyedropperAveraging(true);
        QTest::mouseClick(picture, Qt::LeftButton, {}, c);
        QCOMPARE(sampled.count(), 2);
        const QColor plain = sampled.at(0).at(0).value<QList<QColor>>().at(0);
        const QColor averaged = sampled.at(1).at(0).value<QList<QColor>>().at(0);
        QVERIFY(plain == QColor(Qt::black) || plain == QColor(Qt::white));
        QVERIFY2(averaged.red() > 40 && averaged.red() < 215, qPrintable(averaged.name()));
    }

    void useMarks() {
        // Use Marks for Segment Correction (p.608): the correction goes to every whole segment the marks touch.
        Rig r;
        Timeline::Options o = r.timeline.options();
        o.correctionUseMarks = true;
        r.timeline.setOptions(o);
        model::MonitorState& rec = r.composer.state(MonitorId::Record);
        rec.setPosition(25);
        rec.markOut();
        rec.setPosition(10);
        rec.markIn();
        rec.setPosition(5);
        model::ColorCorrection c;
        c.set(QStringLiteral("hsl/controls/master/saturation"), 50);
        QVERIFY(r.timeline.setCurrentCorrection(c, QStringLiteral("Test")));
        QCOMPARE(r.segment(0).sourceCorrection, c);
        QCOMPARE(r.segment(1).sourceCorrection, c);
        // Off, only the current segment.
        o.correctionUseMarks = false;
        r.timeline.setOptions(o);
        model::ColorCorrection d;
        d.set(QStringLiteral("hsl/controls/master/saturation"), 70);
        QVERIFY(r.timeline.setCurrentCorrection(d, QStringLiteral("Test")));
        QCOMPARE(r.segment(0).sourceCorrection, d);
        QCOMPARE(r.segment(1).sourceCorrection, c);
    }

    void buttonsAndComments() {
        // The tool buttons (FX Guide p.605): the template and Safe Color buttons wait for 6.10d and 6.10e.
        Rig r;
        QVERIFY(!r.tool.button(QStringLiteral("template"))->isEnabled());
        QVERIFY(!r.tool.button(QStringLiteral("safeColors"))->isEnabled());
        QSignalSpy effect(&r.tool, &CorrectionTool::effectModeRequested);
        QSignalSpy settings(&r.tool, &CorrectionTool::settingsRequested);
        r.tool.button(QStringLiteral("effectMode"))->click();
        r.tool.button(QStringLiteral("settings"))->click();
        QCOMPARE(effect.count(), 1);
        QCOMPARE(settings.count(), 1);

        // Comments (p.609-610): OK keeps the text, the button turns yellow, Clip Text > Comments shows it.
        QVERIFY(!r.tool.hasComment());
        QString shown;
        r.tool.setCommentAsker([&shown](const QString& current) -> std::optional<QString> {
            shown = current;
            return QStringLiteral("Warm the skin tones");
        });
        r.tool.button(QStringLiteral("comments"))->click();
        QCOMPARE(r.segment(0).correctionComment, QStringLiteral("Warm the skin tones"));
        QVERIFY(r.tool.hasComment());
        QCOMPARE(r.segment(1).correctionComment, QString());
        Timeline::Display d = r.timeline.display();
        d.clipComments = true;
        r.timeline.setDisplay(d);
        const model::Sequence seq = model::Sequence::fromItem(r.composer.state(MonitorId::Record).current()->item);
        QVERIFY(r.timeline.clipText(*seq.track(QStringLiteral("V1")), r.segment(0)).contains(QStringLiteral("Warm the skin tones")));
        // The dialog shows the comment again; Remove takes it off; Cancel changes nothing.
        r.tool.setCommentAsker([&shown](const QString& current) -> std::optional<QString> {
            shown = current;
            return QString();
        });
        r.tool.editComment();
        QCOMPARE(shown, QStringLiteral("Warm the skin tones"));
        QCOMPARE(r.segment(0).correctionComment, QString());
        QVERIFY(!r.tool.hasComment());
        r.tool.setCommentAsker([](const QString&) -> std::optional<QString> { return std::nullopt; });
        r.tool.editComment();
        QCOMPARE(r.segment(0).correctionComment, QString());

        // The Tab buttons' paths (p.601).
        QVERIFY(r.tool.showPath(QStringLiteral("hsl/lumaRanges/highlights")));
        QCOMPARE(r.tool.shownPath(), QStringLiteral("hsl/lumaRanges/highlights"));
        QVERIFY(r.tool.showPath(QStringLiteral("levels/red")));
        QCOMPARE(r.tool.shownPath(), QStringLiteral("levels/red"));
        QVERIFY(r.tool.showPath(QStringLiteral("secondary")));
        QVERIFY(!r.tool.showPath(QStringLiteral("nowhere")));
        QCOMPARE(r.tool.group(), QStringLiteral("secondary"));
    }

    void automaticCorrections() {
        // The Auto buttons measure the uncorrected frame at the position (FX Guide p.620-621).
        Rig r;
        r.tool.setPictureSource([](bool) {
            QImage image(64, 16, QImage::Format_RGB32);
            for (int x = 0; x < 64; ++x) {
                for (int y = 0; y < 16; ++y) image.setPixelColor(x, y, QColor::fromRgbF(0.3f + 0.4f * static_cast<float>(x) / 63.0f, 0.3f + 0.4f * static_cast<float>(x) / 63.0f, 0.3f + 0.4f * static_cast<float>(x) / 63.0f));
            }
            return image;
        });
        r.tool.showGroup(QStringLiteral("hsl"));
        r.tool.showHslTab(QStringLiteral("hueOffsets"));
        r.tool.findChild<QToolButton*>(QStringLiteral("auto:hueOffsets:contrast"))->click();
        QVERIFY(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/controls/master/gain")) > 100);
        QVERIFY(r.tool.enableButton(QStringLiteral("hsl/hueOffsets/gain"))->isChecked());
        // The Curves tab's Auto Contrast sets the Master curve.
        r.tool.showGroup(QStringLiteral("curves"));
        r.tool.findChild<QToolButton*>(QStringLiteral("auto:curves:contrast"))->click();
        QCOMPARE(model::colorCurve(r.segment(0).sourceCorrection, QStringLiteral("master")).size(), 2);
        // Remove Color Cast: the button lights, a sample from the monitor makes that colour neutral, the button goes off
        // (p.622).
        r.tool.showGroup(QStringLiteral("hsl"));
        QSignalSpy selection(&r.tool, &CorrectionTool::colorSelectionChanged);
        QToolButton* cast = r.tool.findChild<QToolButton*>(QStringLiteral("removeCast:%1").arg(static_cast<int>(media::CastRemoval::Midtones)));
        cast->click();
        QVERIFY(cast->isChecked());
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::RemoveColorCast);
        r.tool.applySamples({QColor(120, 120, 150)});
        QVERIFY(r.segment(0).sourceCorrection.number(QStringLiteral("hsl/hueOffsets/midtones/amount")) > 0);
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::None);
        QVERIFY(!cast->isChecked());
        QCOMPARE(selection.count(), 2);
    }

    void colorMatch() {
        // Each group has a Color Match control (FX Guide p.624): pick the two colours with the eyedropper, choose a Match
        // Type, click Match Color (p.625-626).
        Rig r;
        for (const char* name : {"controls", "hueOffsets", "channels", "levels", "curves", "secondary"}) QVERIFY2(r.tool.colorMatch(QLatin1String(name)), name);
        r.tool.showGroup(QStringLiteral("curves"));
        ColorMatchControl* m = r.tool.colorMatch(QStringLiteral("curves"));
        QVERIFY(!m->matchButton()->isEnabled());
        QTest::mouseClick(m->swatch(false), Qt::LeftButton);
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::MatchColor);
        r.tool.applySamples({QColor(100, 110, 150)});
        QCOMPARE(m->color(false), QColor(100, 110, 150));
        QCOMPARE(r.tool.colorSelection(), CorrectionTool::ColorSelection::None);
        QTest::mouseClick(m->swatch(true), Qt::LeftButton);
        r.tool.applySamples({QColor(160, 120, 90)});
        QCOMPARE(m->color(true), QColor(160, 120, 90));
        // The swatches show their RGB values by default (p.631), on the video scale.
        QVERIFY(m->showInfo());
        QCOMPARE(m->swatch(true)->text(), m->infoText(true));
        QVERIFY(m->infoText(true).startsWith(QStringLiteral("R: ")));
        // R+G+B with NaturalMatch, then Match Color: points on the colour curves.
        m->setMatchType(QStringLiteral("rgb"));
        m->setNaturalMatch(true);
        QCOMPARE(m->typeButton()->text(), QStringLiteral("R+G+B (Nat)"));
        QVERIFY(m->matchButton()->isEnabled());
        m->matchButton()->click();
        QCOMPARE(model::colorCurve(r.segment(0).sourceCorrection, QStringLiteral("red")).size(), 3);
        // Show Eyedropper Info off hides the numbers.
        CorrectionTool::Options o;
        o.eyedropperInfo = false;
        r.tool.setOptions(o);
        QVERIFY(m->swatch(true)->text().isEmpty());
    }

    void customColors() {
        // A swatch's colour as a drag for a bin, named by Saved Color Labels (FX Guide p.630, p.608).
        Rig r;
        CorrectionTool::Options o;
        o.savedColorLabels = media::SavedColorLabels::Name;
        r.tool.setOptions(o);
        ColorMatchControl* hsl = r.tool.colorMatch(QStringLiteral("controls"));
        hsl->setColor(true, QColor(255, 0, 0));
        std::unique_ptr<QMimeData> mime(hsl->colorMime(true));
        const QJsonObject drag = QJsonDocument::fromJson(mime->data(customColorMime())).object();
        QCOMPARE(drag.value(QStringLiteral("name")).toString(), QStringLiteral("Red"));
        QCOMPARE(drag.value(QStringLiteral("color")).toString(), QStringLiteral("#ff0000"));
        // Another swatch takes it.
        ColorMatchControl* curves = r.tool.colorMatch(QStringLiteral("curves"));
        QVERIFY(curves->dropColor(false, mime.get()));
        QCOMPARE(curves->color(false), QColor(255, 0, 0));
        // "Drag the color icon from the bin to the appropriate color swatch" (p.631): bin items by the resolver.
        const model::BinItem saved = media::customColorItem(QColor(110, 70, 56), QStringLiteral("Skin"), QDateTime::currentDateTimeUtc());
        r.tool.setItemResolver([saved](const QString& bin, const QString& id) -> std::optional<model::BinItem> {
            if (bin == QLatin1String("bin1") && id == saved.id) return saved;
            return std::nullopt;
        });
        QMimeData items;
        items.setData(binItemsMime(), QStringLiteral("bin1\n%1\n").arg(saved.id).toUtf8());
        QVERIFY(curves->dropColor(true, &items));
        QCOMPARE(curves->color(true), QColor(110, 70, 56));
        QMimeData other;
        other.setData(binItemsMime(), QByteArrayLiteral("bin1\nnothing\n"));
        QVERIFY(!curves->dropColor(true, &other));
        // Eyedropper Picks from Anywhere: the colour the application draws where the mouse is let go (p.608).
        o.eyedropperAnywhere = true;
        r.tool.setOptions(o);
        QVERIFY(curves->pickAnywhere());
        QWidget patch;
        patch.setAutoFillBackground(true);
        QPalette pal = patch.palette();
        pal.setColor(QPalette::Window, QColor(30, 160, 90));
        patch.setPalette(pal);
        patch.resize(60, 40);
        patch.move(900, 500);
        patch.show();
        settle();
        QCOMPARE(ColorMatchControl::colorAt(patch.mapToGlobal(QPoint(30, 20))), QColor(30, 160, 90));
    }
};

QTEST_MAIN(TestCorrectionTool)
#include "tst_correction_tool.moc"

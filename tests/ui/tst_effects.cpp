#include "lumenis/model/correction_template.h"
#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/command_palette.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QSlider>
#include <QTabBar>
#include <QTest>
#include <QComboBox>
#include <QSpinBox>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// A sequence of two clips, `a` then `b`, on V1, A1 and A2. `b` starts
// `bIn` frames into its clip, so a transition can use that many frames
// before the cut.
BinItem sequence(std::int64_t frames, std::int64_t bIn = 0) {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("A");
        a.sourceTrack = t;
        a.length = frames / 2;
        model::Segment b = a;
        b.clipId = QStringLiteral("b");
        b.name = QStringLiteral("B");
        b.sourceIn = bIn;
        b.length = frames - frames / 2;
        seq.splice(t, 0, {a, b});
    }
    seq.storeIn(s);
    return s;
}

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

const EffectInfo& effect(const char* category, const char* name) {
    const EffectInfo* e = EffectCatalog::builtin().find(QString::fromUtf8(category), QString::fromUtf8(name));
    Q_ASSERT(e);
    return *e;
}

struct Rig {
    Composer composer;
    Timeline timeline{composer};
    EffectEditor editor{timeline};
    QStringList asked; // Insufficient Source dialogs
    bool sizeToFit = true;
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        timeline.setInsufficientSource([this](const QString& which) {
            asked << which;
            return sizeToFit;
        });
        timeline.resize(900, 300);
        timeline.show();
        settle();
    }
    model::Sequence seq() const {
        return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
    }
    const model::Segment& v1(int i) const {
        static model::Sequence held;
        held = seq();
        return held.track(QStringLiteral("V1"))->segments.at(i);
    }
    // Drags an effect from the palette and drops it at a frame of a track.
    bool drop(const EffectInfo& e, const QString& track, std::int64_t frame) {
        TimelineView* view = timeline.view();
        int y = -1;
        for (const auto& r : view->rows()) {
            if (r.track == track) y = r.top + r.height / 2;
        }
        const QPointF at(view->xFor(frame) + 0.5, y);
        std::unique_ptr<QMimeData> mime(effectMime(e));
        QDragEnterEvent enter(at.toPoint(), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &enter);
        QDragMoveEvent move(at.toPoint(), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &move);
        if (!move.isAccepted()) return false;
        QDropEvent dropped(at, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &dropped);
        return dropped.isAccepted();
    }
};

} // namespace

class TestEffects : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void catalog() {
        // FX guide p.37: the tabs, and the categories of the picture.
        const EffectCatalog& c = EffectCatalog::builtin();
        QCOMPARE(c.tabs().size(), 4);
        QCOMPARE(c.tabs().at(0).name, QStringLiteral("Filters"));
        QCOMPARE(c.tabs().at(3).name, QStringLiteral("Audio Clip"));
        QCOMPARE(c.categories().first(), QStringLiteral("Blend"));
        QCOMPARE(c.categories().last(), QStringLiteral("Xpress 3D Effect"));
        QVERIFY(c.categories().contains(QStringLiteral("PlasmaWipe Avid Techno")));
        // Dissolve is a transition (p.457); Flop a segment effect (p.469).
        QVERIFY(effect("Blend", "Dissolve").transition);
        QVERIFY(!effect("Blend", "Dissolve").segment);
        QVERIFY(effect("Image", "Flop").segment);
        QVERIFY(!effect("Illusion FX", "FluidBlur").realTime);
        // The Color Correction Presets (FX Guide p.611, ours) are templates for segments.
        int presets = 0;
        for (const EffectInfo& e : c.all()) {
            if (e.category != QLatin1String("Color Correction Presets")) {
                QVERIFY2(e.correctionPreset.isEmpty(), qPrintable(e.name));
                continue;
            }
            ++presets;
            QVERIFY(e.segment);
            QVERIFY2(!model::CorrectionTemplate::fromJson(e.correctionPreset).isEmpty(), qPrintable(e.name));
        }
        QVERIFY(presets >= 5);
        // Every parameter category an effect names is described.
        for (const EffectInfo& e : c.all()) {
            for (const QString& p : e.parameters) QVERIFY2(effectParameters(p), qPrintable(e.name + ": " + p));
        }
    }

    void palette() {
        EffectPalette palette;
        palette.resize(480, 420);
        palette.show();
        // "All" first; Illusion FX lists its effects alphabetically (p.37).
        QCOMPARE(palette.categoryList()->item(0)->text(), QStringLiteral("All"));
        palette.setCategory(QStringLiteral("Illusion FX"));
        QCOMPARE(palette.shownEffects().first(), QStringLiteral("Color Mix"));
        QVERIFY(palette.shownEffects().contains(QStringLiteral("Iris")));
        // Transitions: only effects for transitions.
        palette.setTab(1);
        QCOMPARE(palette.category(), QStringLiteral("Illusion FX"));
        QVERIFY(palette.shownEffects().contains(QStringLiteral("Iris")));
        QVERIFY(!palette.shownEffects().contains(QStringLiteral("Color Mix")));
        QVERIFY(palette.categoryList()->findItems(QStringLiteral("Film"), Qt::MatchExactly).size() == 1);
        QVERIFY(palette.categoryList()->findItems(QStringLiteral("Generator"), Qt::MatchExactly).isEmpty());
        // Quick Find searches all categories, or the selected one.
        palette.setTab(0);
        palette.setCategory(QStringLiteral("All"));
        palette.setQuickFind(QStringLiteral("dissolve"));
        QCOMPARE(palette.shownEffects(), (QStringList{"Dissolve", "Film Dissolve"}));
        palette.setCategory(QStringLiteral("Film"));
        QCOMPARE(palette.shownEffects(), QStringList{"Film Dissolve"});
        // Enter on an effect applies it, as a double-click does (p.612).
        QStringList activated;
        connect(&palette, &EffectPalette::effectActivated, this, [&](const EffectInfo& e) { activated << e.name; });
        palette.effectList()->setCurrentRow(0);
        QTest::keyClick(palette.effectList(), Qt::Key_Return);
        QCOMPARE(activated, QStringList{QStringLiteral("Film Dissolve")});
        // Audio effects come with plug-ins.
        palette.setQuickFind({});
        palette.setTab(3);
        QVERIFY(palette.shownEffects().isEmpty());
        // Effects drag as effect data.
        palette.setTab(0);
        palette.setCategory(QStringLiteral("Blend"));
        const auto e = palette.effectAt(0);
        QVERIFY(e);
        std::unique_ptr<QMimeData> mime(effectMime(*e));
        QCOMPARE(effectFromMime(mime.get())->name, e->name);
    }

    void dropOnTimeline() {
        // p.44: drag an effect over a transition or a segment.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.drop(effect("Image", "Flop"), QStringLiteral("V1"), 20));
        QCOMPARE(rig.v1(0).effect->name, QStringLiteral("Flop"));
        QVERIFY(!rig.v1(1).effect);
        // A transition on the cut: one second (p.1296), centered.
        QVERIFY(rig.drop(effect("Blend", "Dissolve"), QStringLiteral("V1"), 50));
        QVERIFY(rig.v1(1).transition);
        QCOMPARE(rig.v1(1).transition->duration, 25);
        QCOMPARE(rig.v1(1).transition->alignment, model::TransitionAlignment::CenteredOnCut);
        QVERIFY(rig.asked.isEmpty());
        // Not on audio or data tracks (p.44).
        QVERIFY(!rig.drop(effect("Image", "Flop"), QStringLiteral("A1"), 20));
        // Undo takes the transition away.
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(!rig.v1(1).transition);
    }

    void insufficientSource() {
        // p.48: not enough frames before the incoming clip: Size To Fit
        // shortens the effect, Cancel leaves it out.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 4));
        settle();
        rig.sizeToFit = false;
        QVERIFY(!rig.timeline.dropEffect(QStringLiteral("V1"), 50, effect("Blend", "Dissolve"), 50));
        QCOMPARE(rig.asked, QStringList{"Media B"});
        QVERIFY(!rig.v1(1).transition);
        rig.sizeToFit = true;
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 50, effect("Blend", "Dissolve"), 50));
        QCOMPARE(rig.v1(1).transition->duration, 8);
        QCOMPARE(rig.v1(1).transition->alignment, model::TransitionAlignment::CenteredOnCut);
    }

    void doubleClickApplies() {
        // p.44: select segments, then double-click the effect.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(!rig.timeline.applyEffectToSelection(effect("Image", "Flop")));
        QVERIFY(!rig.timeline.lastMessage().isEmpty());
        rig.timeline.setSelection({model::SegmentRef{QStringLiteral("V1"), 0}, model::SegmentRef{QStringLiteral("V1"), 50},
                                   model::SegmentRef{QStringLiteral("A1"), 0}});
        QVERIFY(rig.timeline.applyEffectToSelection(effect("Image", "Flop")));
        QVERIFY(rig.v1(0).effect && rig.v1(1).effect);
        QVERIFY(!rig.seq().track(QStringLiteral("A1"))->segments.at(0).effect);
        // In Trim mode, on the transitions with rollers (p.44-45).
        rig.timeline.setRollers({model::TrimRoller{QStringLiteral("V1"), 50, model::TrimSide::Both}}, model::TrimKind::Ripple);
        QVERIFY(rig.timeline.isTrimMode());
        QVERIFY(rig.timeline.applyEffectToSelection(effect("Blend", "Dip to Color")));
        QCOMPARE(rig.v1(1).transition->effect.name, QStringLiteral("Dip to Color"));
    }

    void removeEffect() {
        // p.47: Remove Effect at the position on the selected track.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 50, effect("Blend", "Dissolve"), 50));
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Image", "Flop")));
        rig.composer.moveTo(MonitorId::Record, 49); // inside the dissolve
        QVERIFY(rig.timeline.perform(QStringLiteral("fx.remove_effect")));
        QVERIFY(!rig.v1(1).transition);
        QVERIFY(rig.v1(1).effect);
        // With V1 deselected nothing is found.
        rig.composer.moveTo(MonitorId::Record, 70);
        rig.timeline.toggleRecordTrack(QStringLiteral("V1"));
        rig.timeline.perform(QStringLiteral("fx.remove_effect"));
        QVERIFY(rig.v1(1).effect);
        rig.timeline.toggleRecordTrack(QStringLiteral("V1"));
        rig.timeline.perform(QStringLiteral("fx.remove_effect"));
        QVERIFY(!rig.v1(1).effect);
    }

    void fadeEffect() {
        // The Fade Effect button (FX Guide p.46-47): Foreground Level keyframes on the effect at the
        // position, from the frames typed for fading up and down; undoable.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Blend", "Picture-in-Picture")));
        rig.composer.moveTo(MonitorId::Record, 70);
        int asked = 0;
        rig.timeline.setFadeAsk([&]() -> std::optional<std::pair<std::int64_t, std::int64_t>> {
            ++asked;
            return std::pair<std::int64_t, std::int64_t>{10, 5};
        });
        QVERIFY(rig.timeline.perform(QStringLiteral("fx.fade_effect")));
        QCOMPARE(asked, 1);
        const model::Effect fx = *rig.v1(1).effect;
        QCOMPARE(model::keyframedParameters(fx), QStringList{QStringLiteral("Foreground/Level")});
        // 50 frames long: up over frames 0-10, down over frames 44-49.
        QCOMPARE(*model::numberAt(fx, QStringLiteral("Foreground"), QStringLiteral("Level"), 0.0, 50), 0.0);
        QCOMPARE(*model::numberAt(fx, QStringLiteral("Foreground"), QStringLiteral("Level"), 10.0 / 49, 50), 100.0);
        QCOMPARE(*model::numberAt(fx, QStringLiteral("Foreground"), QStringLiteral("Level"), 1.0, 50), 0.0);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(model::keyframedParameters(*rig.v1(1).effect).isEmpty());
        // Cancelled: nothing changes.
        rig.timeline.setFadeAsk([]() -> std::optional<std::pair<std::int64_t, std::int64_t>> { return std::nullopt; });
        rig.timeline.perform(QStringLiteral("fx.fade_effect"));
        QVERIFY(model::keyframedParameters(*rig.v1(1).effect).isEmpty());
    }

    void effectEditor() {
        // p.83-84: the effect under the position; blank otherwise.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.editor.isBlank());
        QVERIFY(!rig.editor.durationBox()->isEnabled());
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Blend", "Picture-in-Picture")));
        rig.composer.moveTo(MonitorId::Record, 70);
        QVERIFY(!rig.editor.isBlank());
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Picture-in-Picture"));
        // Its parameter categories (p.458), with openers.
        QCOMPARE(rig.editor.categories(), (QStringList{"Border", "Foreground", "Scaling", "Position", "Crop", "Grid"}));
        QVERIFY(rig.editor.isOpen(QStringLiteral("Scaling")));
        rig.editor.setOpen(QStringLiteral("Scaling"), false);
        QVERIFY(!rig.editor.isOpen(QStringLiteral("Scaling")));
        // A slider change is an undoable change of the effect.
        QSlider* width = rig.editor.slider(QStringLiteral("Border"), QStringLiteral("Width"));
        QVERIFY(width);
        QCOMPARE(width->maximum(), 100);
        width->setValue(12);
        QCOMPARE(rig.v1(1).effect->parameters.value("Border").toObject().value("Width").toInt(), 12);
        // Shift+arrow moves by ten (FX guide p.87).
        QTest::keyClick(width, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(rig.v1(1).effect->parameters.value("Border").toObject().value("Width").toInt(), 22);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(rig.editor.slider(QStringLiteral("Border"), QStringLiteral("Width"))->value(), 12);
        QCheckBox* fixed = rig.editor.checkBox(QStringLiteral("Border"), QStringLiteral("Fixed Border"));
        QVERIFY(fixed);
        fixed->setChecked(true);
        QVERIFY(rig.v1(1).effect->parameters.value("Border").toObject().value("Fixed Border").toBool());
        // Deselecting the track blanks the editor (p.84).
        rig.timeline.toggleRecordTrack(QStringLiteral("V1"));
        QVERIFY(rig.editor.isBlank());
    }

    void customGrid() {
        // The Custom Grid's More Options (p.494): columns and rows on the shown Matrix Wipe, 2 x 2 at least.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Matrix Wipe", "Spiral")));
        rig.composer.moveTo(MonitorId::Record, 70);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Spiral"));
        QVERIFY(rig.editor.findChild<QToolButton*>(QStringLiteral("param:Custom Grid/More Options")));
        rig.editor.setCustomGrid(4, 1);
        const QJsonObject grid = rig.v1(1).effect->parameters.value("Custom Grid").toObject();
        QCOMPARE(grid.value("Columns").toInt(), 4);
        QCOMPARE(grid.value("Rows").toInt(), 2);
    }

    void effectDefaults() {
        // The Effect Editor shows the values the compositor uses until they are set
        // (media::parameterDefault): a Picture-in-Picture at half size (ours).
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Blend", "Picture-in-Picture")));
        rig.composer.moveTo(MonitorId::Record, 70);
        QCOMPARE(rig.editor.slider(QStringLiteral("Scaling"), QStringLiteral("X"))->value(), 50);
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Image", "Resize")));
        rig.composer.moveTo(MonitorId::Record, 71);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Resize"));
        QCOMPARE(rig.editor.slider(QStringLiteral("Scaling"), QStringLiteral("Y"))->value(), 100);
        // Fixed Aspect gangs X and Y (p.499).
        rig.editor.checkBox(QStringLiteral("Scaling"), QStringLiteral("Fixed Aspect"))->setChecked(true);
        rig.editor.slider(QStringLiteral("Scaling"), QStringLiteral("X"))->setValue(70);
        QCOMPARE(rig.editor.slider(QStringLiteral("Scaling"), QStringLiteral("Y"))->value(), 70);
        // The Color Effect's two Invert boxes (p.533, p.535) are kept apart; its gains start at 100.
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Image", "Color Effect")));
        rig.composer.moveTo(MonitorId::Record, 72);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Color Effect"));
        const QString color = QStringLiteral("Color Effect Parameters");
        QCheckBox* luma = rig.editor.checkBox(color, QStringLiteral("Invert"));
        QCheckBox* chroma = rig.editor.checkBox(color, QStringLiteral("Chroma Adjust Parameters: Invert"));
        QVERIFY(luma && chroma && luma != chroma);
        QCOMPARE(chroma->text(), QStringLiteral("Invert"));
        chroma->setChecked(true);
        QVERIFY(rig.v1(1).effect->parameters.value(color).toObject().value("Chroma Adjust Parameters: Invert").toBool());
        QVERIFY(!luma->isChecked());
        QCOMPARE(rig.editor.slider(color, QStringLiteral("Red"))->value(), 100);
        // A Chroma Key's Gain starts at 20 (ours); its Secondary Key and Spill Suppression Gains, which
        // share the name, are stored apart.
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Key", "Chroma Key")));
        rig.composer.moveTo(MonitorId::Record, 73);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Chroma Key"));
        QCOMPARE(rig.editor.slider(QStringLiteral("Key Effects Key Parameters"), QStringLiteral("Gain"))->value(), 20);
        const QString more = QStringLiteral("Chroma Key Profile, Secondary Key, and Spill Suppression Parameters");
        QVERIFY(rig.editor.slider(more, QStringLiteral("Gain")));
        QVERIFY(rig.editor.slider(more, QStringLiteral("Spill Suppression Parameters: Gain")));
    }

    void burnInPanel() {
        // Timecode Burn-In's Display 1-3 and Notes tabs (FX Guide p.505-508; docs/spec/burn-in.md).
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Generator", "Timecode Burn-In")));
        rig.composer.moveTo(MonitorId::Record, 70);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Timecode Burn-In"));
        auto* reader = qobject_cast<QComboBox*>(rig.editor.burnInControl(QStringLiteral("Display 1/Reader")));
        QVERIFY(reader);
        QCOMPARE(reader->currentText(), QStringLiteral("Timecode"));
        auto* enable2 = qobject_cast<QCheckBox*>(rig.editor.burnInControl(QStringLiteral("Display 2/Enable")));
        QVERIFY(enable2 && !enable2->isChecked());
        QVERIFY(qobject_cast<QCheckBox*>(rig.editor.burnInControl(QStringLiteral("Display 1/Enable")))->isChecked());
        reader->setCurrentText(QStringLiteral("Source Clip Name"));
        const QJsonObject stored = rig.v1(1).effect->parameters.value("Timecode Burn-In Effect Parameters").toObject();
        QCOMPARE(stored.value("Display 1").toObject().value("Reader").toString(), QStringLiteral("Source Clip Name"));
        // Opacities in tenths.
        auto* opacity = qobject_cast<QSpinBox*>(rig.editor.burnInControl(QStringLiteral("Display 1/Border Opacity")));
        QVERIFY(opacity);
        QCOMPARE(opacity->value(), 5);
        opacity->setValue(8);
        QCOMPARE(rig.v1(1).effect->parameters.value("Timecode Burn-In Effect Parameters").toObject().value("Display 1").toObject()
                     .value("Border Opacity").toDouble(), 0.8);
        // The Notes Track's text.
        rig.editor.setBurnIn(0, QStringLiteral("Text"), QStringLiteral("DRAFT"));
        QCOMPARE(qobject_cast<QLineEdit*>(rig.editor.burnInControl(QStringLiteral("Notes/Text")))->text(), QStringLiteral("DRAFT"));
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(qobject_cast<QLineEdit*>(rig.editor.burnInControl(QStringLiteral("Notes/Text")))->text(), QString());
    }

    void transitionParameters() {
        // p.63, p.500: a duration typed for a cut in Trim mode adds a
        // Dissolve; the alignment menu moves it.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        rig.timeline.setRollers({model::TrimRoller{QStringLiteral("V1"), 50, model::TrimSide::Both}}, model::TrimKind::Ripple);
        QVERIFY(rig.editor.durationBox()->isEnabled());
        QVERIFY(rig.editor.setDuration(QStringLiteral("0:20")));
        QCOMPARE(rig.v1(1).transition->effect.name, QStringLiteral("Dissolve"));
        QCOMPARE(rig.v1(1).transition->duration, 20);
        rig.timeline.exitTrimMode();
        rig.composer.moveTo(MonitorId::Record, 45);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Dissolve"));
        QCOMPARE(rig.editor.durationBox()->text(), QStringLiteral("0:20"));
        rig.editor.setAlignment(model::TransitionAlignment::EndingAtCut);
        QCOMPARE(rig.v1(1).transition->alignment, model::TransitionAlignment::EndingAtCut);
        QCOMPARE(rig.v1(1).transition->before(), 20);
        QVERIFY(rig.editor.setDuration(QStringLiteral("30")));
        QCOMPARE(rig.v1(1).transition->duration, 30);
        QVERIFY(!rig.editor.setDuration(QStringLiteral("x")));
        // Off the transition, outside Trim mode, the editor is blank again (p.84).
        rig.composer.moveTo(MonitorId::Record, 90);
        QVERIFY(rig.editor.isBlank());
        QVERIFY(!rig.editor.durationBox()->isEnabled());
    }

    void colourParameterShowsDefault() {
        // A colour with no stored value shows its default on the swatch.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        QVERIFY(rig.timeline.dropEffect(QStringLiteral("V1"), 70, effect("Blend", "Picture-in-Picture")));
        rig.composer.moveTo(MonitorId::Record, 70);
        auto* swatch = rig.editor.findChild<QToolButton*>(QStringLiteral("param:Border/Color"));
        QVERIFY(swatch);
        QVERIFY(!rig.v1(1).effect->parameters.value("Border").toObject().contains("Color"));
        QCOMPARE(swatch->property("color").value<QColor>(), QColor(Qt::black));
        QVERIFY(swatch->styleSheet().contains(QStringLiteral("#000000")));
    }

    void noSequenceLoaded() {
        // The shown transition outlives its sequence until the next refresh: nothing may assume
        // the Record monitor still holds one.
        Rig rig;
        rig.composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100, 40));
        settle();
        rig.timeline.setRollers({model::TrimRoller{QStringLiteral("V1"), 50, model::TrimSide::Both}}, model::TrimKind::Ripple);
        QVERIFY(rig.editor.setDuration(QStringLiteral("0:20")));
        rig.timeline.exitTrimMode();
        rig.composer.moveTo(MonitorId::Record, 45);
        QCOMPARE(rig.editor.effectName(), QStringLiteral("Dissolve"));
        rig.composer.state(MonitorId::Record).clearMonitor(); // no Composer::changed
        QVERIFY(!rig.composer.state(MonitorId::Record).current());
        rig.editor.setAlignment(model::TransitionAlignment::EndingAtCut);
        QVERIFY(!rig.editor.setDuration(QStringLiteral("30")));
        rig.editor.refresh();
        QVERIFY(rig.editor.isBlank());
    }

    void timelineDisplay() {
        // FX guide p.40: Effect Icons and Dissolve Icons in the Fast menu.
        Rig rig;
        QMenu* fast = rig.timeline.fastMenu();
        QAction* icons = nullptr;
        QAction* dissolves = nullptr;
        for (QAction* a : fast->actions()) {
            if (a->text() == QLatin1String("Effect Icons")) icons = a;
            if (a->text() == QLatin1String("Dissolve Icons")) dissolves = a;
        }
        QVERIFY(icons && dissolves);
        QVERIFY(icons->isEnabled() && icons->isChecked() && dissolves->isEnabled());
        icons->trigger();
        QVERIFY(!rig.timeline.display().effectIcons);
        QVERIFY(!dissolves->isEnabled());
    }

    void mainWindowTools() {
        // Tools > Effect Palette (Ctrl+8) and Tools > Effect Editor.
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        commands.action(QStringLiteral("tools.effect_palette"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("EffectPalette"))));
        commands.action(QStringLiteral("tools.effect_editor"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("EffectEditor"))));
        // Remove Effect is on the Command Palette's FX tab (p.97, FX p.46).
        QVERIFY(window.commandPalette().commandsOn(QStringLiteral("FX")).contains(QStringLiteral("fx.remove_effect")));
        QVERIFY(window.commandPalette().commandsOn(QStringLiteral("FX")).contains(QStringLiteral("fx.fade_effect")));
        // The Effect Mode button opens the Effect Editor (FX p.49).
        window.panels().closePanel(window.panels().panel(QStringLiteral("EffectEditor")));
        QVERIFY(!window.panels().isOpen(window.panels().panel(QStringLiteral("EffectEditor"))));
        commands.action(QStringLiteral("fx.effect_mode"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("EffectEditor"))));
        commands.action(QStringLiteral("edit.source_record_mode"))->trigger();
    }
};

QTEST_MAIN(TestEffects)
#include "tst_effects.moc"

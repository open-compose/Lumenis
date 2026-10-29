// The Effect Editor's advanced keyframes (FX Guide ch.4, p.94-131; docs/spec/effect-keyframes.md):
// the keyframe tracks and graphs, selection, dragging, the tracks' shortcut menus, the Add Keyframe
// button and its modes, Go to Previous/Next Event, and sliders following the keyframes.
#include "lumenis/model/bin.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QLayout>
#include <QMenu>
#include <QScrollBar>
#include <QSlider>
#include <QTest>
#include <QToolButton>

#include <memory>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

const QString kLevel = QStringLiteral("Foreground/Level");
const QString kWidth = QStringLiteral("Border/Width");

// V1: A (50 frames) then B (50), with B starting 40 frames into its clip.
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
    b.sourceIn = 40;
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

// A Picture-in-Picture on B (frames 50-99), the Effect Editor showing it with its graphs.
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
        composer.moveTo(MonitorId::Record, 50);
        editor.resize(700, 900);
        editor.show();
        settle();
    }
    model::Effect effect() const {
        const model::Sequence s = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        return *s.track(QStringLiteral("V1"))->segments.at(1).effect;
    }
    model::ParameterKeyframes keys(const QString& p) const {
        return model::keyframesOf(effect(), p.section('/', 0, 0), p.section('/', 1));
    }
    // Record frame of the effect's frame f.
    void at(std::int64_t f) {
        composer.moveTo(MonitorId::Record, 50 + f);
        settle();
    }
    double time(std::int64_t f) const { return editor.span().timeOf(static_cast<double>(f)); }
    void click(const QString& track, double t, Qt::KeyboardModifiers m = Qt::NoModifier, double value = 0.0) {
        QTest::mouseClick(editor.keyframeTrack(track), Qt::LeftButton, m, editor.keyframePoint(track, t, value));
        settle();
    }
    // Triggers an action of a track's shortcut menu by its text.
    bool menu(const QString& track, const QString& text) {
        std::unique_ptr<QMenu> m(editor.keyframeMenu(track));
        for (QAction* a : m->actions()) {
            if (a->text() == text) {
                a->trigger();
                settle();
                return true;
            }
        }
        return false;
    }
    bool undo() { return timeline.perform(QStringLiteral("edit.undo")); }
};

} // namespace

class TestEffectKeyframesUi : public QObject {
    Q_OBJECT

private slots:
    void tracksAndGraphs() {
        Rig rig;
        QCOMPARE(rig.editor.span().length, 50);
        // "Keyframe graphs are hidden when the Effect Editor first opens" (p.96).
        QVERIFY(!rig.editor.keyframeGraphsShown());
        QVERIFY(!rig.editor.keyframeTrack(QStringLiteral("effect")));
        auto* button = rig.editor.findChild<QToolButton*>(QStringLiteral("effectKeyframes"));
        QVERIFY(button && button->isEnabled());
        button->click();
        settle();
        // The effect track, group tracks and parameter tracks (p.95).
        QVERIFY(rig.editor.keyframeTrack(QStringLiteral("effect")));
        QVERIFY(rig.editor.keyframeTrack(QStringLiteral("group:Foreground")));
        QVERIFY(rig.editor.keyframeTrack(kLevel));
        // Parameters that are not keyframeable have no keyframes (p.96; the Grid, p.496).
        QVERIFY(!rig.editor.keyframeableParameters().contains(QStringLiteral("Grid/Sub")));
        QVERIFY(rig.editor.keyframeableParameters().contains(QStringLiteral("Border/Color")));
        // A graph opens from its parameter's opener (p.96-97).
        QVERIFY(!rig.editor.keyframeTrack(QStringLiteral("graph:") + kLevel));
        auto* opener = rig.editor.findChild<QToolButton*>(QStringLiteral("graphOpener:") + kLevel);
        QVERIFY(opener && opener->isVisible());
        opener->click();
        settle();
        QVERIFY(rig.editor.isGraphOpen(kLevel));
        QVERIFY(rig.editor.keyframeTrack(QStringLiteral("graph:") + kLevel));
        // The tracks line up: time 0.5 is at the same place in every one.
        const int effectX = rig.editor.keyframeTrack(QStringLiteral("effect"))->mapTo(&rig.editor, rig.editor.keyframePoint(QStringLiteral("effect"), 0.5)).x();
        const int levelX = rig.editor.keyframeTrack(kLevel)->mapTo(&rig.editor, rig.editor.keyframePoint(kLevel, 0.5)).x();
        QCOMPARE(levelX, effectX);
        // Clicking a track moves the position (p.96).
        rig.click(kLevel, rig.time(20));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 70);
    }

    void addKeyframeButtonAndModes() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setShowsKeyframeModeMenu(false);
        // Ours: the default command is Add to Open Groups; every group starts open.
        QCOMPARE(rig.editor.keyframeMode(), EffectEditor::KeyframeMode::OpenGroups);
        rig.at(10);
        QVERIFY(rig.editor.addKeyframes(rig.editor.keyframeMode()));
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
        QCOMPARE(rig.keys(kWidth).keys.size(), 1);
        // The new keyframes are active (p.104).
        QVERIFY(rig.editor.selectedKeyframes().contains(EffectEditor::KeyframeId{kLevel, 0}));
        QVERIFY(rig.undo());
        QVERIFY(model::keyframedParameters(rig.effect()).isEmpty());
        // A closed group takes no keyframes in that mode.
        rig.editor.setOpen(QStringLiteral("Border"), false);
        rig.editor.findChild<QToolButton*>(QStringLiteral("effectAddKeyframe"))->click();
        settle();
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
        QVERIFY(rig.keys(kWidth).keys.isEmpty());
        // Add to Active Parameter: the control last used (p.107).
        rig.at(30);
        rig.editor.setActiveParameter(kWidth);
        QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
        QCOMPARE(rig.keys(kWidth).keys.size(), 1);
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
        // Delete From Active Parameter takes away its selected keyframes only.
        QVERIFY(rig.editor.deleteKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
        QVERIFY(rig.keys(kWidth).keys.isEmpty());
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
        // Add to Open Graphs.
        rig.editor.setGraphOpen(kWidth, true);
        rig.at(40);
        QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::OpenGraphs));
        QCOMPARE(rig.keys(kWidth).keys.size(), 1);
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
    }

    void slidersFollowKeyframes() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        // Level keyframed 0 at frame 0 and 98 at frame 49.
        rig.at(0);
        QSlider* level = rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"));
        rig.editor.setActiveParameter(kLevel);
        QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
        rig.editor.deselectAllKeyframes();
        level->setValue(0);
        rig.at(49);
        level->setValue(98);
        // Adjusting with no keyframe at the position added one (p.103).
        QCOMPARE(rig.keys(kLevel).keys.size(), 2);
        rig.at(0);
        QCOMPARE(rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->value(), 0);
        rig.at(25);
        QCOMPARE(rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->value(), 50);
        // With a keyframe selected the slider changes it, wherever the position is.
        rig.editor.selectKeyframe({kLevel, 1}, false);
        rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->setValue(60);
        QCOMPARE(rig.keys(kLevel).keys.size(), 2);
        QCOMPARE(rig.keys(kLevel).keys.at(1).value.toDouble(), 60.0);
    }

    void selectingAndEvents() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setShowsKeyframeModeMenu(false);
        for (std::int64_t f : {10, 30}) {
            rig.at(f);
            QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::OpenGroups));
        }
        rig.at(0);
        rig.editor.deselectAllKeyframes();
        // Clicking a keyframe selects it and moves the position to it (Set Position To Keyframe, p.132).
        rig.click(kLevel, rig.time(10));
        QCOMPARE(rig.editor.selectedKeyframes(), (QList<EffectEditor::KeyframeId>{{kLevel, 0}}));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 60);
        // Ctrl+click adds one (p.111) and takes it away again.
        rig.click(kLevel, rig.time(30), Qt::ControlModifier);
        QCOMPARE(rig.editor.selectedKeyframes().size(), 2);
        rig.click(kLevel, rig.time(30), Qt::ControlModifier);
        QCOMPARE(rig.editor.selectedKeyframes().size(), 1);
        // A group track's indicator selects every keyframe of the group at that time (p.102).
        rig.click(QStringLiteral("group:Border"), rig.time(30));
        for (const EffectEditor::KeyframeId& id : rig.editor.selectedKeyframes()) {
            QVERIFY(id.parameter.startsWith(QStringLiteral("Border/")));
            QCOMPARE(id.index, 1);
        }
        QVERIFY(rig.editor.selectedKeyframes().contains(EffectEditor::KeyframeId{kWidth, 1}));
        // Without Set Position To Keyframe the position stays.
        rig.editor.setSetsPositionToKeyframe(false);
        rig.at(5);
        rig.click(QStringLiteral("effect"), rig.time(10));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 55);
        QVERIFY(rig.editor.selectedKeyframes().contains(EffectEditor::KeyframeId{kLevel, 0}));
        // Go to Next Event and Go to Previous Event (p.128).
        rig.at(0);
        QVERIFY(rig.editor.goToEvent(true));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 60);
        QVERIFY(rig.editor.goToEvent(true));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 80);
        QVERIFY(rig.editor.selectedKeyframes().contains(EffectEditor::KeyframeId{kLevel, 1}));
        QVERIFY(!rig.editor.goToEvent(true));
        QVERIFY(rig.editor.goToEvent(false));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 60);
        // Delete deletes the selected keyframes (p.105).
        QTest::keyClick(&rig.editor, Qt::Key_Delete);
        settle();
        QCOMPARE(rig.keys(kLevel).keys.size(), 1);
        QCOMPARE(rig.keys(kWidth).keys.size(), 1);
        // Edit > Deselect All Keyframes clears the active parameter's (p.112): here none is active.
        rig.editor.selectKeyframe({kLevel, 0}, false);
        rig.editor.deselectAllKeyframes();
        QVERIFY(rig.editor.selectedKeyframes().isEmpty());
    }

    void dragging() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setGraphOpen(kLevel, true);
        rig.editor.setActiveParameter(kLevel);
        for (std::int64_t f : {10, 30}) {
            rig.at(f);
            QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
        }
        rig.editor.selectKeyframe({kLevel, 0}, false);
        // In a track a drag moves the keyframe in time, a frame short of its neighbour (p.113-114).
        rig.editor.dragKeyframes({kLevel, 0}, 25, 0, true);
        QCOMPARE(rig.keys(kLevel).keys.at(0).at, rig.time(10)); // only previewed
        rig.editor.finishDrag();
        settle();
        QCOMPARE(rig.keys(kLevel).keys.at(0).at, rig.time(29));
        QVERIFY(rig.undo());
        QCOMPARE(rig.keys(kLevel).keys.at(0).at, rig.time(10));
        // In a graph a drag changes the value, within the parameter's range (p.112-113).
        rig.editor.selectKeyframe({kLevel, 0}, false);
        rig.editor.dragKeyframes({kLevel, 0}, 0, 40, false);
        QCOMPARE(rig.keys(kLevel).keys.at(0).value.toDouble(), 40.0);
        rig.editor.dragKeyframes({kLevel, 0}, 0, 500, false);
        QCOMPARE(rig.keys(kLevel).keys.at(0).value.toDouble(), 100.0);
        // With the mouse: a plain drag in the graph changes the value only.
        const QString graph = QStringLiteral("graph:") + kLevel;
        QWidget* g = rig.editor.keyframeTrack(graph);
        QVERIFY(g);
        const QPoint from = rig.editor.keyframePoint(graph, rig.time(30), rig.keys(kLevel).keys.at(1).value.toDouble());
        const QPoint to = rig.editor.keyframePoint(graph, rig.time(35), 20);
        QTest::mousePress(g, Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, g->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(g, &move);
        QTest::mouseRelease(g, Qt::LeftButton, Qt::NoModifier, to);
        settle();
        QCOMPARE(rig.keys(kLevel).keys.at(1).at, rig.time(30));
        QVERIFY(std::abs(rig.keys(kLevel).keys.at(1).value.toDouble() - 20.0) <= 2.0);
    }

    void trackMenu() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setActiveParameter(kLevel);
        for (const auto& [f, v] : QList<std::pair<std::int64_t, int>>{{0, 0}, {20, 40}, {40, 80}}) {
            rig.at(f);
            QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
            rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->setValue(v);
        }
        // "Apply to Group" while a group track's menu is open (p.104).
        {
            std::unique_ptr<QMenu> m(rig.editor.keyframeMenu(QStringLiteral("group:Foreground")));
            const QList<QAction*> actions = m->actions();
            QCOMPARE(actions.first()->text(), QStringLiteral("Add Keyframe"));
            for (const char* text : {"Delete Keyframe", "Add Start and End Keyframes", "Remove Redundant Keyframes",
                                        "Select All Keyframes", "Deselect All Keyframes", "Align Keyframes", "Slip Keyframes",
                                        "Reverse Keyframes", "Shelf", "Linear", "Spline", "Bézier", "Fixed", "Elastic",
                                        "Extrapolate", "Hold"}) {
                QVERIFY2(std::any_of(actions.begin(), actions.end(), [&](QAction* a) { return a->text() == QString::fromUtf8(text); }),
                         text);
            }
        }
        // Remove Redundant Keyframes: 0, 40, 80 lie on a straight line (p.106).
        QVERIFY(rig.menu(kLevel, QStringLiteral("Remove Redundant Keyframes")));
        QCOMPARE(rig.keys(kLevel).keys.size(), 2);
        // Add Start and End Keyframes: the first and last frames (p.130).
        QVERIFY(rig.menu(kLevel, QStringLiteral("Add Start and End Keyframes")));
        QCOMPARE(rig.keys(kLevel).keys.size(), 3);
        QCOMPARE(rig.keys(kLevel).keys.last().at, 1.0);
        // The options (p.120-127), checked as they are.
        QVERIFY(rig.menu(kLevel, QStringLiteral("Spline")));
        QCOMPARE(rig.keys(kLevel).interpolation, model::Interpolation::Spline);
        QVERIFY(rig.menu(kLevel, QStringLiteral("Fixed")));
        QVERIFY(rig.keys(kLevel).fixed);
        QCOMPARE(rig.keys(kLevel).keys.at(1).at, 40.0);
        QVERIFY(rig.menu(kLevel, QStringLiteral("Extrapolate")));
        QVERIFY(rig.keys(kLevel).extrapolate);
        {
            std::unique_ptr<QMenu> m(rig.editor.keyframeMenu(kLevel));
            for (QAction* a : m->actions()) {
                if (a->text() == QLatin1String("Spline") || a->text() == QLatin1String("Fixed") || a->text() == QLatin1String("Extrapolate")) {
                    QVERIFY(a->isChecked());
                }
                if (a->text() == QLatin1String("Linear") || a->text() == QLatin1String("Elastic") || a->text() == QLatin1String("Hold")) {
                    QVERIFY(!a->isChecked());
                }
            }
        }
        // Align Keyframes: the selected keyframe to the position (p.114).
        rig.editor.selectKeyframe({kLevel, 1}, false);
        rig.at(10);
        QVERIFY(rig.menu(kLevel, QStringLiteral("Align Keyframes")));
        QCOMPARE(rig.keys(kLevel).keys.at(1).at, 10.0);
        // Reverse Keyframes from the effect track: the whole effect (p.124).
        QVERIFY(rig.menu(QStringLiteral("effect"), QStringLiteral("Reverse Keyframes")));
        QCOMPARE(rig.keys(kLevel).keys.at(1).at, 39.0);
        // Select All Keyframes in a track (p.111).
        QVERIFY(rig.menu(kLevel, QStringLiteral("Select All Keyframes")));
        QCOMPARE(rig.editor.selectedKeyframes().size(), 3);
        QVERIFY(rig.menu(kLevel, QStringLiteral("Delete Keyframe")));
        QVERIFY(rig.keys(kLevel).keys.isEmpty());
    }

    void graphViews() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setGraphOpen(kLevel, true);
        rig.editor.setActiveParameter(kLevel);
        for (const auto& [f, v] : QList<std::pair<std::int64_t, int>>{{0, 20}, {49, 40}}) {
            rig.at(f);
            QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
            rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->setValue(v);
        }
        // The horizontal scale bar zooms around the position indicator (p.99).
        rig.at(25);
        const double at = rig.editor.positionTime();
        const int before = rig.editor.keyframePoint(kLevel, at).x();
        rig.editor.horizontalScaleBar()->setValue(20);
        settle();
        QCOMPARE(rig.editor.viewSpan(), 0.5);
        QCOMPARE(rig.editor.keyframePoint(kLevel, at).x(), before);
        // The scroll thumbwheel moves the view (p.101).
        QVERIFY(rig.editor.scrollThumbwheel()->isVisible());
        const double start = rig.editor.viewStart();
        rig.editor.scrollThumbwheel()->setValue(rig.editor.scrollThumbwheel()->value() + 100);
        QVERIFY(std::abs(rig.editor.viewStart() - (start + 0.1)) < 0.002); // in thousandths
        // A graph shows its parameter's range at first (p.97); the vertical scale bar zooms it, beyond
        // the range too (p.99).
        QCOMPARE(rig.editor.graphRange(kLevel), (std::pair<double, double>{0.0, 100.0}));
        auto* scale = rig.editor.findChild<QSlider*>(QStringLiteral("graphScale:") + kLevel);
        QVERIFY(scale);
        scale->setValue(75);
        QCOMPARE(rig.editor.graphRange(kLevel), (std::pair<double, double>{-50.0, 150.0}));
        // Zoom to Curve Height: the curve, 20 to 40, over the time shown, with a tenth spare (p.100).
        rig.editor.resetGraphViews();
        QCOMPARE(rig.editor.viewSpan(), 1.0);
        QCOMPARE(rig.editor.graphRange(kLevel), (std::pair<double, double>{0.0, 100.0}));
        rig.editor.findChild<QToolButton*>(QStringLiteral("graphFit:") + kLevel)->click();
        const auto [lo, hi] = rig.editor.graphRange(kLevel);
        QVERIFY(std::abs(lo - 18.0) < 0.01 && std::abs(hi - 42.0) < 0.01);
        // Alt+drag on the graph away from keyframes scrolls it (p.101).
        const QString graph = QStringLiteral("graph:") + kLevel;
        QWidget* g = rig.editor.keyframeTrack(graph);
        const QPoint from = rig.editor.keyframePoint(graph, 0.5, 35);
        const QPoint to = from + QPoint(-30, 0);
        QTest::mousePress(g, Qt::LeftButton, Qt::AltModifier, from);
        QMouseEvent move(QEvent::MouseMove, to, g->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        QApplication::sendEvent(g, &move);
        QTest::mouseRelease(g, Qt::LeftButton, Qt::AltModifier, to);
        QVERIFY(rig.editor.viewStart() > 0.0);
        QCOMPARE(rig.keys(kLevel).keys.size(), 2); // no keyframe touched
        // The menu has Zoom to Curve Height and Reset All Graph Views (p.130).
        QVERIFY(rig.menu(kLevel, QStringLiteral("Reset All Graph Views")));
        QCOMPARE(rig.editor.viewStart(), 0.0);
    }

    void copyAndPaste() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setActiveParameter(kLevel);
        for (const auto& [f, v] : QList<std::pair<std::int64_t, int>>{{0, 10}, {10, 30}, {40, 70}}) {
            rig.at(f);
            QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
            rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->setValue(v);
        }
        // Nothing selected: nothing to copy.
        rig.editor.deselectAllKeyframes();
        QVERIFY(!rig.editor.copyKeyframes());
        // One keyframe's value onto the selected keyframe (p.117).
        rig.editor.selectKeyframe({kLevel, 1}, false);
        QVERIFY(rig.editor.copyKeyframes());
        rig.editor.selectKeyframe({kLevel, 2}, false);
        QVERIFY(rig.editor.pasteKeyframes());
        QCOMPARE(rig.keys(kLevel).keys.at(2).value.toDouble(), 30.0);
        // Several keyframes keep their distance; the leftmost goes to the position (p.117).
        rig.editor.selectKeyframe({kLevel, 0}, false);
        rig.editor.selectKeyframe({kLevel, 1}, true);
        QVERIFY(rig.editor.copyKeyframes());
        rig.editor.deselectAllKeyframes();
        rig.at(20);
        QVERIFY(rig.editor.pasteKeyframes());
        const model::ParameterKeyframes keys = rig.keys(kLevel);
        QCOMPARE(keys.keys.size(), 5);
        QCOMPARE(model::keyframeAt(keys, rig.time(20), rig.editor.span()) >= 0, true);
        QCOMPARE(keys.keys.at(model::keyframeAt(keys, rig.time(30), rig.editor.span())).value.toDouble(), 30.0);
        QVERIFY(rig.undo());
        QCOMPARE(rig.keys(kLevel).keys.size(), 3);
        // Copy All Values at Position: every parameter gets a keyframe with its value there (p.118).
        rig.at(5);
        QVERIFY(rig.menu(kLevel, QStringLiteral("Copy All Values at Position")));
        rig.at(30);
        QVERIFY(rig.editor.pasteKeyframes());
        for (const QString& p : rig.editor.keyframeableParameters()) {
            QVERIFY2(model::keyframeAt(rig.keys(p), rig.time(30), rig.editor.span()) >= 0, qPrintable(p));
        }
        const model::ParameterKeyframes level = rig.keys(kLevel);
        QCOMPARE(level.keys.at(model::keyframeAt(level, rig.time(30), rig.editor.span())).value.toDouble(), 20.0);
        // Ctrl+Alt+C in a graph copies all values too.
        rig.editor.setGraphOpen(kLevel, true);
        QWidget* g = rig.editor.keyframeTrack(QStringLiteral("graph:") + kLevel);
        rig.at(0);
        QTest::keyClick(g, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(rig.editor.hasCopiedKeyframes());
    }

    void bezierHandles() {
        Rig rig;
        rig.editor.setKeyframeGraphsShown(true);
        rig.editor.setGraphOpen(kLevel, true);
        rig.editor.setActiveParameter(kLevel);
        for (const auto& [f, v] : QList<std::pair<std::int64_t, int>>{{0, 0}, {20, 50}, {40, 100}}) {
            rig.at(f);
            QVERIFY(rig.editor.addKeyframes(EffectEditor::KeyframeMode::ActiveParameter));
            rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"))->setValue(v);
        }
        QVERIFY(rig.menu(kLevel, QStringLiteral("Bézier")));
        rig.editor.selectKeyframe({kLevel, 1}, false);
        // A selected Bézier keyframe shows its direction handles (p.123); dragging the way out turns
        // both, symmetrically by default (p.122).
        QCOMPARE(rig.editor.handleMode(), EffectEditor::HandleMode::Symmetrical);
        const QString graph = QStringLiteral("graph:") + kLevel;
        QWidget* g = rig.editor.keyframeTrack(graph);
        const auto [in0, out0] = model::effectiveHandles(rig.keys(kLevel), 1, rig.editor.span());
        const double t1 = rig.time(20);
        const QPoint outPoint = rig.editor.keyframePoint(graph, t1 + out0.time, 50 + out0.value);
        const QPoint to = rig.editor.keyframePoint(graph, t1 + out0.time, 50 + out0.value + 30);
        auto drag = [&](const QPoint& a, const QPoint& b, Qt::KeyboardModifiers m) {
            QTest::mousePress(g, Qt::LeftButton, m, a);
            QMouseEvent move(QEvent::MouseMove, b, g->mapToGlobal(b), Qt::NoButton, Qt::LeftButton, m);
            QApplication::sendEvent(g, &move);
            QTest::mouseRelease(g, Qt::LeftButton, m, b);
            settle();
        };
        drag(outPoint, to, Qt::NoModifier);
        const auto [in1, out1] = model::effectiveHandles(rig.keys(kLevel), 1, rig.editor.span());
        QVERIFY(out1.value > out0.value + 20);
        QVERIFY(in1.value < in0.value - 20); // the way in turned and grew alike
        QVERIFY(std::abs(in1.value + out1.value) < 1.0);
        QVERIFY(rig.undo());
        // Alt+click moves to the next way of adjusting: Asymmetrical, the other handle keeps its length.
        drag(outPoint, to, Qt::AltModifier);
        QCOMPARE(rig.editor.handleMode(), EffectEditor::HandleMode::Asymmetrical);
        const auto [in2, out2] = model::effectiveHandles(rig.keys(kLevel), 1, rig.editor.span());
        QVERIFY(in2.value < in0.value);
        QVERIFY(std::abs(in2.value) < std::abs(out2.value));
        // And Independent: the other handle stays.
        QVERIFY(rig.undo());
        drag(outPoint, to, Qt::AltModifier);
        QCOMPARE(rig.editor.handleMode(), EffectEditor::HandleMode::Independent);
        const auto [in3, out3] = model::effectiveHandles(rig.keys(kLevel), 1, rig.editor.span());
        QVERIFY(std::abs(in3.value - in0.value) < 1e-6);
        QVERIFY(out3.value > out0.value + 20);
    }

    void options() {
        Rig rig;
        // The Effect Editor shortcut menu (p.131-132) sets the options and says so.
        int changed = 0;
        connect(&rig.editor, &EffectEditor::optionsChanged, [&] { ++changed; });
        std::unique_ptr<QMenu> menu(rig.editor.effectEditorMenu());
        auto action = [&](const char* text) -> QAction* {
            for (QAction* a : menu->actions()) {
                if (a->text() == QString::fromUtf8(text)) return a;
            }
            return nullptr;
        };
        QVERIFY(action("Indent Rows") && action("Indent Rows")->isChecked());
        QVERIFY(action("Thumbwheels") && !action("Thumbwheels")->isEnabled());
        action("Large Text")->trigger();
        QVERIFY(rig.editor.options().largeText);
        QCOMPARE(rig.editor.font().pointSize(), 12);
        action("Add Keyframes to All Parameters")->trigger();
        QCOMPARE(rig.editor.keyframeMode(), EffectEditor::KeyframeMode::AllParameters);
        action("Set Position To Keyframe")->trigger();
        QVERIFY(!rig.editor.setsPositionToKeyframe());
        QCOMPARE(changed, 3);
        // Indent Rows off: the parameter rows line up with the category rows.
        EffectEditor::Options o = rig.editor.options();
        o.indentRows = false;
        rig.editor.setOptions(o);
        QSlider* level = rig.editor.slider(QStringLiteral("Foreground"), QStringLiteral("Level"));
        QCOMPARE(level->parentWidget()->layout()->contentsMargins().left(), 0);
    }

    void blankEditor() {
        // No effect: the keyframe buttons are off.
        Composer composer;
        Timeline timeline{composer};
        EffectEditor editor{timeline};
        editor.show();
        settle();
        QVERIFY(!editor.findChild<QToolButton*>(QStringLiteral("effectAddKeyframe"))->isEnabled());
        QVERIFY(!editor.addKeyframes(EffectEditor::KeyframeMode::AllParameters));
        QVERIFY(!editor.goToEvent(true));
    }
};

QTEST_MAIN(TestEffectKeyframesUi)
#include "tst_effect_keyframes_ui.moc"

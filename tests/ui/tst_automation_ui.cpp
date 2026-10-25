// Volume and pan automation in the Timeline and the Audio Mixer (Editing Guide p.714-723;
// docs/spec/automation.md): Add Keyframe, the Audio Data graph and its mouse editing, copy and
// paste, the Auto mode faders, recording and the Auto mode Fast menu. The model's side is in
// tests/model/tst_automation.cpp, the sound's in tests/media/tst_mixdown.cpp.
#include "lumenis/model/automation.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QSlider>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::AutomationParam;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using lumenis::model::KeyRef;

namespace {

// A sequence with clip `a` (frames 0-49) and `b` (50-99) on V1, A1 and A2.
BinItem sequence() {
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
        a.sourceIn = 100;
        a.length = 50;
        model::Segment b = a;
        b.clipId = QStringLiteral("b");
        b.sourceIn = 0;
        seq.splice(t, 0, {a, b});
    }
    seq.storeIn(s);
    return s;
}

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

struct Rig {
    Composer composer;
    Timeline timeline{composer};
    AudioMixer mixer{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        timeline.resize(900, 400);
        timeline.show();
        mixer.resize(600, 440);
        mixer.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        // A1 and A2 selected, V1 not.
        for (const QString& t : timeline.patch().selectedRecordTracks(seq())) {
            if (t.startsWith(QLatin1Char('V'))) timeline.toggleRecordTrack(t);
        }
        settle();
    }
    model::Sequence seq() const { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    QList<model::AudioKey> keys(const char* track, int segment, AutomationParam param = AutomationParam::Volume) const {
        const model::Sequence s = seq();
        return model::keysOf(s.track(QString::fromLatin1(track))->segments.at(segment), param);
    }
    void at(std::int64_t frame) { composer.moveTo(MonitorId::Record, frame); }
    // Marks on the Record monitor, the position kept.
    void setIn(std::int64_t frame) { mark(frame, true); }
    void setOut(std::int64_t frame) { mark(frame, false); }
    void mark(std::int64_t frame, bool in) {
        model::MonitorState& state = composer.state(MonitorId::Record);
        const std::int64_t was = state.position();
        state.setPosition(frame);
        if (in) {
            state.markIn();
        } else {
            state.markOut();
        }
        state.setPosition(was);
    }
    QAction* fastItem(const QString& text) {
        for (QAction* a : mixer.fastMenu()->actions()) {
            if (a->text() == text) return a;
        }
        return nullptr;
    }
};

} // namespace

class TestAutomationUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void addKeyframe() {
        Rig rig;
        QVERIFY(Timeline::handles(QStringLiteral("fx.add_keyframe")));
        rig.at(10);
        // On each selected audio track; the Volume graph shows (p.716).
        QVERIFY(rig.timeline.perform(QStringLiteral("fx.add_keyframe")));
        QCOMPARE(rig.timeline.display().automation, std::optional(AutomationParam::Volume));
        QCOMPARE(rig.keys("A1", 0), (QList<model::AudioKey>{{110, 0}}));
        QCOMPARE(rig.keys("A2", 0), (QList<model::AudioKey>{{110, 0}}));
        // A second one there: nothing, with a reason.
        rig.timeline.perform(QStringLiteral("fx.add_keyframe"));
        QVERIFY(!rig.timeline.lastMessage().isEmpty());
        // One undo step.
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(rig.keys("A1", 0).isEmpty());
        // Pan keyframes take the pan that plays: A1 left, A2 right.
        rig.timeline.setAudioData(false, AutomationParam::Pan);
        rig.timeline.perform(QStringLiteral("fx.add_keyframe"));
        QCOMPARE(rig.keys("A1", 0, AutomationParam::Pan), (QList<model::AudioKey>{{110, -100}}));
        QCOMPARE(rig.keys("A2", 0, AutomationParam::Pan), (QList<model::AudioKey>{{110, 100}}));
        // Ctrl+Shift+click places one; with Alt between frames (p.721).
        QVERIFY(rig.timeline.addKeyframeAt(QStringLiteral("A1"), 20.5));
        QCOMPARE(rig.keys("A1", 0, AutomationParam::Pan).last(), (model::AudioKey{120.5, -100}));
        // The Audio Data menu shows the graph's state.
        QMenu* audioData = rig.timeline.fastMenu()->findChild<QMenu*>(QStringLiteral("audioData"));
        QVERIFY(audioData);
        QStringList checked;
        for (QAction* a : audioData->actions()) {
            if (a->isChecked()) checked << a->text();
        }
        QCOMPARE(checked, QStringList{QStringLiteral("Pan")});
        // Audio rows grow while a graph shows.
        int height = 0;
        for (const TimelineView::Row& r : rig.timeline.view()->rows()) {
            if (r.track == QLatin1String("A1")) height = r.height;
        }
        QVERIFY(height > 40);
        QVERIFY(!rig.timeline.view()->grab().isNull());
    }

    void autoModeFaders() {
        Rig rig;
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        QVERIFY(!rig.mixer.recordButton()->isHidden());
        MixerStrip* a1 = rig.mixer.strip(QStringLiteral("A1"));
        // No keyframes yet: the fader makes a first one, which sets the whole segment (p.716).
        rig.at(60);
        emit a1->gainSet(-3, true);
        QCOMPARE(rig.keys("A1", 1), (QList<model::AudioKey>{{10, -3}}));
        rig.at(90);
        QCOMPARE(*rig.timeline.automationAt(QStringLiteral("A1"), AutomationParam::Volume), -3.0);
        QCOMPARE(a1->gain(), -3.0);
        // On a keyframe: that one.
        rig.at(60);
        emit a1->gainSet(-6, true);
        QCOMPARE(rig.keys("A1", 1), (QList<model::AudioKey>{{10, -6}}));
        // Between two: both move, so the value at the position becomes the one set.
        QVERIFY(rig.timeline.addKeyframeAt(QStringLiteral("A1"), 80));
        rig.timeline.setKeySelection({});
        rig.at(70);
        emit a1->gainSet(-2, true);
        QCOMPARE(rig.keys("A1", 1), (QList<model::AudioKey>{{10, -2}, {30, -2}}));
        // In the marked region: every keyframe in it.
        rig.setIn(55);
        rig.setOut(75);
        rig.at(65);
        emit a1->gainSet(-5, true);
        QCOMPARE(rig.keys("A1", 1), (QList<model::AudioKey>{{10, -5}, {30, -2}}));
        rig.composer.state(MonitorId::Record).clearBoth();
        // The pan knob too.
        rig.at(20);
        emit a1->panSet(30, true);
        QCOMPARE(rig.keys("A1", 0, AutomationParam::Pan), (QList<model::AudioKey>{{120, 30}}));
        // In Clip mode the display says Auto where there is automation (p.694).
        rig.mixer.setMode(AudioMixer::Mode::Clip);
        QCOMPARE(a1->levelBox()->text(), QStringLiteral("Auto"));
    }

    void timelineEditing() {
        Rig rig;
        rig.timeline.setAudioData(false, AutomationParam::Volume);
        for (std::int64_t f : {10, 20, 30}) {
            rig.at(f);
            rig.timeline.perform(QStringLiteral("fx.add_keyframe"));
        }
        // Grabbing one moves it and the same one on the other selected track (p.716).
        const KeyRef k20{QStringLiteral("A1"), 20};
        QCOMPARE(rig.timeline.keyframesMovedWith(k20),
                 (QList<KeyRef>{k20, {QStringLiteral("A2"), 20}}));
        QVERIFY(rig.timeline.dragKeyframes(k20, 0, -4, false));
        QCOMPARE(rig.keys("A2", 0).at(1).value, -4.0);
        QCOMPARE(rig.timeline.keySelection(), (QList<KeyRef>{k20, {QStringLiteral("A2"), 20}}));
        // Ctrl snaps to the scale's lines: -4.6 lands on -3.
        rig.timeline.setKeySelection({});
        QVERIFY(rig.timeline.dragKeyframes(k20, 0, -0.6, true));
        QCOMPARE(rig.keys("A1", 0).at(1).value, -3.0);
        // In a marked region, all of its keyframes move (p.716).
        rig.timeline.setKeySelection({});
        rig.setIn(5);
        rig.setOut(25);
        QCOMPARE(rig.timeline.keyframesMovedWith(k20).size(), 4);
        rig.composer.state(MonitorId::Record).clearBoth();
        // Alt-drag in time; then nudge (p.723).
        rig.timeline.clickKeyframe(KeyRef{QStringLiteral("A1"), 30}, false);
        QVERIFY(rig.timeline.dragKeyframes(KeyRef{QStringLiteral("A1"), 30}, 2, 0, false));
        QCOMPARE(rig.keys("A1", 0).last().frame, 132.0);
        // The drag took A2's twin along, and both stay selected for the nudge.
        QCOMPARE(rig.timeline.keySelection().size(), 2);
        QVERIFY(rig.timeline.nudgeKeyframes(-1, 1));
        QCOMPARE(rig.keys("A1", 0).last(), (model::AudioKey{131, 1}));
        QCOMPARE(rig.keys("A2", 0).last(), (model::AudioKey{131, 1}));
        // Delete: the selected keyframes; then the one under the pointer and its twin (p.717).
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.delete")));
        QCOMPARE(rig.keys("A1", 0).size(), 2);
        QCOMPARE(rig.keys("A2", 0).size(), 2);
        QVERIFY(rig.timeline.deleteKeyframeAt(KeyRef{QStringLiteral("A1"), 10}));
        QCOMPARE(rig.keys("A1", 0).size(), 1);
        QCOMPARE(rig.keys("A2", 0).size(), 1);
        // Go to Next/Previous Event in Auto mode (p.720).
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        rig.at(0);
        QVERIFY(rig.mixer.goToKeyframe(1));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), std::int64_t{20});
        QVERIFY(!rig.mixer.goToKeyframe(1)); // the only one left
        rig.at(60);
        QVERIFY(rig.mixer.goToKeyframe(-1));
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), std::int64_t{20});
        rig.mixer.setMode(AudioMixer::Mode::Clip);
        QVERIFY(!rig.mixer.goToKeyframe(1));
    }

    void mouseEditing() {
        Rig rig;
        rig.timeline.setAudioData(true, AutomationParam::Volume);
        rig.at(10);
        rig.timeline.perform(QStringLiteral("fx.add_keyframe"));
        TimelineView* view = rig.timeline.view();
        settle();
        TimelineView::Row a1;
        for (const TimelineView::Row& r : view->rows()) {
            if (r.track == QLatin1String("A1")) a1 = r;
        }
        QVERIFY(a1.height > 0);
        const QPoint key(static_cast<int>(std::lround(view->xFor(10))),
                         static_cast<int>(std::lround(view->yForValue(a1, AutomationParam::Volume, 0))));
        QCOMPARE(view->keyframeAt(key), std::optional(KeyRef{QStringLiteral("A1"), 10}));
        QVERIFY(!view->keyframeAt(key + QPoint(0, 15)));
        // A click selects it; a drag up raises it (and A2's twin).
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, key);
        QCOMPARE(rig.timeline.keySelection().size(), 1);
        QTest::mouseMove(view, key - QPoint(0, 12));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, key - QPoint(0, 12));
        QVERIFY2(rig.keys("A1", 0).first().value > 0, qPrintable(QString::number(rig.keys("A1", 0).first().value)));
        QCOMPARE(rig.keys("A2", 0).first().value, rig.keys("A1", 0).first().value);
        // Ctrl+Shift+click adds one at a frame; with Alt between frames.
        const int x = static_cast<int>(std::lround(view->xFor(30) + rig.timeline.scale() * 0.5));
        const int y = static_cast<int>(std::lround(view->yForValue(a1, AutomationParam::Volume, -20)));
        QTest::mouseClick(view, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier, QPoint(x, y));
        QCOMPARE(rig.keys("A1", 0).size(), 2);
        QCOMPARE(rig.keys("A1", 0).last().frame, 130.0);
        QTest::mouseClick(view, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier,
                          QPoint(static_cast<int>(std::lround(view->xFor(40) + rig.timeline.scale() * 0.5)), y));
        QCOMPARE(rig.keys("A1", 0).size(), 3);
        QVERIFY(rig.keys("A1", 0).last().frame > 140.0 && rig.keys("A1", 0).last().frame < 141.0);
        QVERIFY(!view->grab().isNull());
    }

    void copyAndPaste() {
        Rig rig;
        rig.timeline.setAudioData(false, AutomationParam::Volume);
        for (std::int64_t f : {10, 14}) {
            rig.at(f);
            rig.timeline.perform(QStringLiteral("fx.add_keyframe"));
        }
        rig.timeline.setKeySelection({});
        rig.timeline.dragKeyframes(KeyRef{QStringLiteral("A1"), 14}, 0, -6, false);
        // Individual keyframes: Ctrl+C, then Ctrl+V at the IN on the selected track (p.721-722).
        rig.timeline.setKeySelection({{QStringLiteral("A1"), 10}, {QStringLiteral("A1"), 14}});
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.copy")));
        QVERIFY(!rig.timeline.clipboard().has_value()); // the segments were not copied
        QCOMPARE(rig.timeline.keyClipboard().tracks.size(), 1);
        rig.timeline.toggleRecordTrack(QStringLiteral("A1")); // only A2 now
        rig.setIn(60);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.paste")));
        QCOMPARE(rig.keys("A2", 1), (QList<model::AudioKey>{{10, 0}, {14, -6}}));
        // A marked region, then Paste Audio Keyframes, asking which (p.722).
        rig.timeline.setKeySelection({});
        rig.setIn(5);
        rig.setOut(20);
        rig.timeline.toggleRecordTrack(QStringLiteral("A1"));
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        rig.timeline.perform(QStringLiteral("edit.copy"));
        QVERIFY(rig.timeline.clipboard().has_value()); // the segments too, as before
        rig.timeline.toggleRecordTrack(QStringLiteral("A1"));
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        rig.composer.state(MonitorId::Record).clearBoth();
        rig.at(80);
        int asked = 0;
        rig.timeline.setPasteKeysAsk([&asked]() -> std::optional<std::pair<bool, bool>> {
            ++asked;
            return std::pair{true, false};
        });
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.paste_audio_keyframes")));
        QCOMPARE(asked, 1);
        // From the IN at 5: keyframes at 10 and 14 land at 85 and 89 of A2 ("b" frames 35, 39).
        QCOMPARE(rig.keys("A2", 1), (QList<model::AudioKey>{{10, 0}, {14, -6}, {35, 0}, {39, -6}}));
    }

    void fastMenu() {
        Rig rig;
        rig.timeline.setAudioData(false, AutomationParam::Volume);
        for (std::int64_t f : {10, 20, 30, 40}) {
            rig.at(f);
            rig.timeline.perform(QStringLiteral("fx.add_keyframe"));
        }
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        // Adjust: exactly -1 dB (p.719).
        rig.mixer.setAdjustAsk([] { return std::optional(std::pair{-1.0, 0}); });
        QAction* adjust = rig.fastItem(QStringLiteral("Adjust Volume/Pan on Track..."));
        QVERIFY(adjust && adjust->isEnabled());
        adjust->trigger();
        for (const model::AudioKey& k : rig.keys("A1", 0)) QCOMPARE(k.value, -1.0);
        // Filter: the points on a straight line go (here, all but the first and last).
        rig.fastItem(QStringLiteral("Filter volume automation on Track"))->trigger();
        QCOMPARE(rig.keys("A1", 0).size(), 2);
        // Remove between the marks only.
        rig.setOut(20);
        rig.fastItem(QStringLiteral("Remove volume automation on Track"))->trigger();
        QCOMPARE(rig.keys("A1", 0), (QList<model::AudioKey>{{140, -1}}));
        rig.composer.state(MonitorId::Record).clearBoth();
        // Hidden ones: after a trim that hides A2's keyframe at "a" frame 140, it goes, and one at
        // the material's last frame keeps what plays.
        QCOMPARE(rig.keys("A2", 0), (QList<model::AudioKey>{{140, -1}}));
        rig.timeline.changeRecordSequence(QStringLiteral("trim"), [](model::Sequence& s) {
            s.track(QStringLiteral("A2"))->segments[0].length = 30;
            s.track(QStringLiteral("A2"))->segments.insert(1, model::Segment::filler(20));
            return true;
        });
        rig.fastItem(QStringLiteral("Remove Hidden Volume Automation on Track - Global"))->trigger();
        QCOMPARE(rig.keys("A2", 0), (QList<model::AudioKey>{{129, -1}}));
        // Nothing to do there: a message, and no undo step.
        rig.fastItem(QStringLiteral("Remove Hidden Volume Automation on Track - Global"))->trigger();
        QVERIFY(!rig.timeline.lastMessage().isEmpty());
        // Dimmed without selected tracks (p.719).
        rig.timeline.toggleRecordTrack(QStringLiteral("A1"));
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        QVERIFY(!rig.fastItem(QStringLiteral("Filter Pan on Track"))->isEnabled());
    }

    void recording() {
        Rig rig;
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        MixerStrip* a1 = rig.mixer.strip(QStringLiteral("A1"));
        const int steps = static_cast<int>(rig.timeline.history().undoCount());
        rig.at(20);
        rig.mixer.startRecording(false);
        QVERIFY(rig.mixer.isRecording());
        QVERIFY(rig.mixer.recordButton()->isChecked());
        // Moving the fader writes at the position, and keeps writing as the position moves.
        emit a1->gainSet(-10, false);
        rig.at(21);
        rig.at(22);
        emit a1->gainSet(-12, false);
        rig.at(23);
        rig.mixer.stopRecording(false);
        QVERIFY(!rig.mixer.isRecording());
        QCOMPARE(rig.keys("A1", 0), (QList<model::AudioKey>{{120, -10}, {121, -10}, {122, -12}, {123, -12}}));
        QVERIFY(rig.keys("A2", 0).isEmpty());
        // All of it is one undo step.
        QCOMPARE(static_cast<int>(rig.timeline.history().undoCount()), steps + 1);
        rig.timeline.perform(QStringLiteral("edit.undo"));
        QVERIFY(rig.keys("A1", 0).isEmpty());
        // Cancel puts it back; between the marks only.
        rig.setIn(30);
        rig.at(28);
        rig.mixer.startRecording(false);
        emit a1->gainSet(-4, false);
        rig.at(29);
        rig.at(30);
        rig.at(31);
        QCOMPARE(rig.keys("A1", 0), (QList<model::AudioKey>{{130, -4}, {131, -4}}));
        rig.mixer.cancelButton()->click();
        QVERIFY(rig.keys("A1", 0).isEmpty());
        QCOMPARE(static_cast<int>(rig.timeline.history().undoCount()), steps);
        // Past the OUT plus the postroll it stops by itself.
        rig.setOut(35);
        rig.mixer.startRecording(false);
        emit a1->gainSet(-8, false);
        rig.at(36);
        QVERIFY(!rig.mixer.isRecording());
        // The B key in the mixer records (p.718).
        rig.composer.state(MonitorId::Record).clearBoth();
        rig.mixer.setFocus();
        QTest::keyClick(&rig.mixer, Qt::Key_B);
        QVERIFY(rig.mixer.isRecording());
        rig.mixer.setMode(AudioMixer::Mode::Clip); // leaving Auto mode stops it
        QVERIFY(!rig.mixer.isRecording());
    }
};

QTEST_MAIN(TestAutomationUi)
#include "tst_automation_ui.moc"

// The Audio Mixer's remaining controls (Editing Guide p.690-726; docs/spec/audio-mixer.md): Audio
// Loop Play and its loop range (p.712), the strips' Audio Track Effect insert buttons (p.790), Render
// Effect (p.692), the Clip mode Fast menu's Adjust Pan/Vols, Remove/Commit Default Clip Pan, Disable
// Track Monitoring and Set Display Options (p.711), and Live mode's Set Live Mix to/as Automation
// (p.725-726). The older mixer checks are in tst_audio_media.cpp and tst_automation_ui.cpp.
#include "lumenis/media/audio_fx.h"
#include "lumenis/model/automation.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QHash>
#include <QMenu>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::AudioFormat;
using lumenis::model::AutomationParam;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

QAction* findAction(QMenu* menu, const QString& text) {
    if (!menu) return nullptr;
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
        if (QAction* sub = findAction(a->menu(), text)) return sub;
    }
    return nullptr;
}

bool visible(const QWidget* w) {
    return w && w->isVisible();
}

} // namespace

class TestAudioMixer : public QObject {
    Q_OBJECT

private:
    // A stereo sequence at 25 fps: A1 (mono) with segments of 10 and 30 frames, A2 (mono) with one
    // of 20 frames. The mixer in Clip mode.
    struct Rig {
        Composer composer;
        Timeline timeline{composer};
        AudioMixer mixer{timeline};
        QHash<QString, BinItem> items;
        MixerStrip none{QString(), false}; // what strip() gives for a track without one

        Rig() {
            composer.setItemResolver([this](const QString&, const QString& id) -> std::optional<BinItem> {
                if (items.contains(id)) return items.value(id);
                return std::nullopt;
            });
            timeline.setSequenceStore([this](const QString&, const BinItem& item, bool) {
                items.insert(item.id, item);
                return true;
            });
            BinItem clip;
            clip.id = model::newItemId();
            clip.name = QStringLiteral("Clip");
            clip.kind = BinItemKind::MasterClip;
            clip.editRate = Rational(25);
            clip.durationFrames = 100;
            items.insert(clip.id, clip);
            model::Sequence seq;
            auto segment = [&](std::int64_t length) {
                model::Segment s;
                s.clipId = clip.id;
                s.binId = QStringLiteral("bin");
                s.name = clip.name;
                s.sourceTrack = QStringLiteral("A1");
                s.length = length;
                return s;
            };
            seq.ensureTrack(QStringLiteral("A1"), AudioFormat::Mono)->segments = {segment(10), segment(30)};
            seq.ensureTrack(QStringLiteral("A2"), AudioFormat::Mono)->segments = {segment(20)};
            BinItem item;
            item.id = model::newItemId();
            item.name = QStringLiteral("Seq");
            item.kind = BinItemKind::Sequence;
            item.editRate = Rational(25);
            seq.storeIn(item);
            items.insert(item.id, item);
            composer.load(MonitorId::Record, QStringLiteral("bin"), item);
            mixer.refresh();
            mixer.resize(900, 500);
        }

        MixerStrip& strip(const QString& track) {
            MixerStrip* s = mixer.strip(track);
            return s ? *s : none;
        }
        model::Sequence shown() const {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        }
        model::Segment segment(const QString& track, int index) const {
            const model::Sequence s = shown();
            const model::Track* t = s.track(track);
            return t && index < t->segments.size() ? t->segments.at(index) : model::Segment{};
        }
        void selectOnly(const QString& track) {
            for (const QString& t : mixer.tracks()) {
                if (timeline.patch().isRecordSelected(t) != (t == track)) timeline.toggleRecordTrack(t);
            }
        }
        bool trigger(const QString& text) {
            QAction* a = findAction(mixer.fastMenu(), text);
            if (!a || !a->isEnabled()) return false;
            a->trigger();
            return true;
        }
    };

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // p.712: IN to OUT; without marks the shortest segment under the position on the selected audio
    // tracks; with one mark, to the segment's end or from its start.
    void loopRange() {
        Rig rig;
        using Range = std::pair<std::int64_t, std::int64_t>;
        QCOMPARE(rig.timeline.audioLoopRange(), std::optional<Range>(Range{0, 9})); // A1's 10 frames, not A2's 20
        rig.selectOnly(QStringLiteral("A2"));
        QCOMPARE(rig.timeline.audioLoopRange(), std::optional<Range>(Range{0, 19}));
        model::MonitorState& record = rig.composer.state(MonitorId::Record);
        rig.composer.moveTo(MonitorId::Record, 15);
        record.markIn();
        QCOMPARE(rig.timeline.audioLoopRange(), std::optional<Range>(Range{15, 19}));
        record.clearBoth();
        rig.composer.moveTo(MonitorId::Record, 5);
        record.markOut();
        rig.composer.moveTo(MonitorId::Record, 3);
        QCOMPARE(rig.timeline.audioLoopRange(), std::optional<Range>(Range{0, 5}));
        rig.composer.moveTo(MonitorId::Record, 2);
        record.markIn();
        QCOMPARE(rig.timeline.audioLoopRange(), std::optional<Range>(Range{2, 5}));
        // Past every segment there is nothing to loop.
        record.clearBoth();
        rig.composer.moveTo(MonitorId::Record, 39);
        QVERIFY(rig.timeline.audioLoopRange()); // A1's second segment is still there
        rig.selectOnly(QString());
        rig.composer.moveTo(MonitorId::Record, 45);
        QVERIFY(!rig.timeline.audioLoopRange());
        QVERIFY(!rig.timeline.toggleAudioLoopPlay());
    }

    // The loop plays again from its start when it reaches its end, until it is stopped; the Loop
    // buttons follow it.
    void loopPlays() {
        Rig rig;
        QToolButton* loop = rig.mixer.loopButton();
        QVERIFY(loop && loop->isEnabled() && loop->isCheckable());
        QSignalSpy started(&rig.composer, &Composer::playStarted);
        loop->click();
        QVERIFY(rig.composer.isLooping());
        QVERIFY(rig.composer.isPlaying());
        QVERIFY(loop->isChecked());
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 0);
        rig.composer.advance(200); // 5 frames
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 5);
        const auto before = started.count();
        rig.composer.advance(400); // past frame 9: the next pass, and the sound starts again
        QVERIFY(rig.composer.isPlaying());
        QCOMPARE(rig.composer.state(MonitorId::Record).position(), 0);
        QCOMPARE(started.count(), before + 1);
        // Playing faster ends the loop; so does stopping.
        rig.composer.play(2);
        QVERIFY(!rig.composer.isLooping());
        QVERIFY(!loop->isChecked());
        loop->click();
        QVERIFY(rig.composer.isLooping());
        loop->click();
        QVERIFY(!rig.composer.isLooping() && !rig.composer.isPlaying());
        // The command (Play tab of the Command Palette, p.712).
        QVERIFY(rig.timeline.perform(QStringLiteral("play.audio_loop_play")));
        QVERIFY(rig.composer.isLooping());
        rig.composer.stop();
        QVERIFY(!loop->isChecked());
    }

    // A segment's own centred pan stays centred on an odd track; without one it plays at the
    // track's default and the strip shows that (p.711, p.1147).
    void ownPans() {
        Rig rig;
        QVERIFY(rig.mixer.strip(QStringLiteral("A1")));
        MixerStrip& a1 = rig.strip(QStringLiteral("A1"));
        QCOMPARE(a1.pan(), -model::kPanMax); // odd tracks left
        QVERIFY(rig.timeline.setClipAudio(QStringLiteral("A1"), std::nullopt, 0, false, QStringLiteral("Clip Pan")));
        QVERIFY(rig.segment(QStringLiteral("A1"), 0).ownPan());
        QCOMPARE(rig.segment(QStringLiteral("A1"), 0).panOr(-100), 0);
        rig.mixer.refresh();
        QCOMPARE(a1.pan(), 0);
        // It survives the bin file.
        model::Segment s;
        s.clipId = QStringLiteral("c");
        s.length = 5;
        s.hasPan = true;
        model::Sequence seq;
        seq.ensureTrack(QStringLiteral("A1"), AudioFormat::Mono)->segments = {s};
        BinItem item;
        item.kind = BinItemKind::Sequence;
        seq.storeIn(item);
        QVERIFY(model::Sequence::fromItem(item).track(QStringLiteral("A1"))->segments.first().ownPan());
        // Remove Pan on Track takes it away.
        rig.selectOnly(QStringLiteral("A1"));
        QVERIFY(rig.trigger(QStringLiteral("Remove Pan on Track")));
        QVERIFY(!rig.segment(QStringLiteral("A1"), 0).ownPan());
        QCOMPARE(a1.pan(), -model::kPanMax);
    }

    // Commit Default Clip Pan on Track and Remove Default Clip Pan on Track (p.711).
    void defaultClipPan() {
        Rig rig;
        rig.selectOnly(QStringLiteral("A1"));
        QVERIFY(rig.trigger(QStringLiteral("Commit Default Clip Pan on Track")));
        for (int i : {0, 1}) {
            QVERIFY(rig.segment(QStringLiteral("A1"), i).ownPan());
            QCOMPARE(rig.segment(QStringLiteral("A1"), i).pan, -model::kPanMax);
        }
        QVERIFY(!rig.segment(QStringLiteral("A2"), 0).ownPan()); // not selected
        // The default changes; committed pans stay where they were.
        rig.timeline.setAlternatingPan(false);
        QCOMPARE(rig.segment(QStringLiteral("A1"), 0).panOr(0), -model::kPanMax);
        // Remove Default Clip Pan removes only pans equal to the default, now the middle.
        QVERIFY(rig.timeline.setClipAudio(QStringLiteral("A1"), std::nullopt, 0, false, QStringLiteral("Clip Pan")));
        QVERIFY(rig.trigger(QStringLiteral("Remove Default Clip Pan on Track")));
        QVERIFY(!rig.segment(QStringLiteral("A1"), 0).ownPan());
        QVERIFY(rig.segment(QStringLiteral("A1"), 1).ownPan());
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(rig.segment(QStringLiteral("A1"), 0).ownPan());
    }

    // Adjust Pan/Vols on Track: the dialog's dB and pan are added to every segment there (p.711).
    void adjustPanVols() {
        Rig rig;
        QVERIFY(rig.timeline.setClipAudio(QStringLiteral("A1"), -3.0, std::nullopt, true, QStringLiteral("Set Level")));
        rig.mixer.setAdjustAsk([] { return std::optional(std::pair{-1.5, 20}); });
        rig.selectOnly(QStringLiteral("A1"));
        QVERIFY(rig.trigger(QStringLiteral("Adjust Pan/Vols on Track...")));
        for (int i : {0, 1}) {
            QCOMPARE(rig.segment(QStringLiteral("A1"), i).gain, -4.5);
            QCOMPARE(rig.segment(QStringLiteral("A1"), i).pan, -80); // from the default L100
        }
        QCOMPARE(rig.segment(QStringLiteral("A2"), 0).gain, 0.0);
        // Dimmed until a track is selected (p.711).
        rig.selectOnly(QString());
        QVERIFY(!rig.trigger(QStringLiteral("Adjust Pan/Vols on Track...")));
    }

    void disableMonitoring() {
        Rig rig;
        rig.selectOnly(QStringLiteral("A2"));
        QVERIFY(rig.trigger(QStringLiteral("Disable Track Monitoring")));
        QVERIFY(!rig.timeline.isMonitored(QStringLiteral("A2")));
        QVERIFY(rig.timeline.isMonitored(QStringLiteral("A1")));
    }

    // Set Display Options (p.711): faders, legends, effect buttons and solo/mute come and go.
    void displayOptions() {
        Rig rig;
        rig.mixer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rig.mixer));
        MixerStrip& a1 = rig.strip(QStringLiteral("A1"));
        QVERIFY(visible(a1.fader()) && visible(a1.insertButton()) && visible(a1.soloButton()));
        rig.mixer.setDisplayAsk([](const MixerDisplay& d) {
            MixerDisplay out = d;
            out.faders = false;
            out.effects = false;
            out.soloMute = false;
            return std::optional(out);
        });
        rig.selectOnly(QString()); // it does not need a selected track
        QVERIFY(rig.trigger(QStringLiteral("Set Display Options...")));
        QVERIFY(!visible(a1.fader()));
        QVERIFY(!visible(a1.insertButton()));
        QVERIFY(!visible(a1.soloButton()) && !visible(a1.muteButton()));
        QVERIFY(!rig.mixer.display().faders);
        // Strips made later show the same.
        rig.mixer.setDisplay(MixerDisplay{});
        QVERIFY(visible(a1.fader()));
    }

    // The insert button's menu puts an effect in an insert; the button shows the inserts' letters.
    void insertButton() {
        Rig rig;
        MixerStrip& a2 = rig.strip(QStringLiteral("A2"));
        QToolButton* b = a2.insertButton();
        QVERIFY(b && b->isEnabled());
        QCOMPARE(b->text(), QStringLiteral("+"));
        QMenu* menu = b->menu();
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QMenu* slotB = menu->findChild<QMenu*>(QStringLiteral("insert:1"));
        QVERIFY(slotB);
        const media::AudioEffectInfo info = media::audioEffects().first();
        QAction* effect = findAction(slotB, info.name);
        QVERIFY(effect);
        effect->trigger();
        QCOMPARE(rig.timeline.trackInsert(QStringLiteral("A2"), 1)->effect, info.id);
        rig.mixer.refresh();
        QCOMPARE(b->text(), QStringLiteral("b"));
        QSignalSpy tool(&rig.mixer, &AudioMixer::trackEffectToolRequested);
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QAction* open = findAction(menu, QStringLiteral("Audio Track Effect Tool"));
        QVERIFY(open);
        open->trigger();
        QCOMPARE(tool.count(), 1);
        QCOMPARE(tool.first().first().toString(), QStringLiteral("A2"));
    }

    // Render Effect needs a renderer and something to render (p.692).
    void renderEffect() {
        Rig rig;
        QVERIFY(rig.mixer.renderButton()->isEnabled());
        QVERIFY(!rig.mixer.renderEffect());
        int calls = 0;
        rig.mixer.setRenderer([&calls](const model::Segment&, QString*) {
            ++calls;
            return std::optional<model::AudioSuiteEffect>{};
        });
        QVERIFY(!rig.mixer.renderEffect()); // no AudioSuite effect there
        QCOMPARE(calls, 0);
        QVERIFY(!rig.mixer.lastMessage().isEmpty());
    }

    // Set Live Mix to Automation takes the automation's values at the position; Set Live Mix as
    // Automation writes the Live Mix between the marks, with ramps at the marks (p.725-726).
    void liveMixAndAutomation() {
        Rig rig;
        QVERIFY(rig.timeline.setAutomationValue(QStringLiteral("A2"), AutomationParam::Volume, -6.0));
        rig.mixer.setMode(AudioMixer::Mode::Live);
        QCOMPARE(rig.mixer.liveValue(QStringLiteral("A2")).first, 0.0);
        QSignalSpy live(&rig.mixer, &AudioMixer::liveMixChanged);
        QVERIFY(rig.trigger(QStringLiteral("Set Live Mix to Automation")));
        QCOMPARE(rig.mixer.liveValue(QStringLiteral("A2")).first, -6.0);
        QVERIFY(live.count() >= 1);
        // A Live Mix of -12 dB on A2, between frames 5 and 14.
        MixerStrip& a2 = rig.strip(QStringLiteral("A2"));
        if (QSlider* f = a2.fader()) f->setValue(fader::position(-12.0));
        QCOMPARE(rig.mixer.liveValue(QStringLiteral("A2")).first, -12.0);
        model::MonitorState& record = rig.composer.state(MonitorId::Record);
        rig.composer.moveTo(MonitorId::Record, 5);
        record.markIn();
        rig.composer.moveTo(MonitorId::Record, 14);
        record.markOut();
        rig.selectOnly(QStringLiteral("A2"));
        QVERIFY(rig.trigger(QStringLiteral("Set Live Mix as Automation")));
        const model::Segment s = rig.segment(QStringLiteral("A2"), 0);
        QCOMPARE(model::volumeAt(s, 5), -6.0);  // the IN keeps its value
        QCOMPARE(model::volumeAt(s, 6), -12.0); // and ramps to the Live Mix
        QCOMPARE(model::volumeAt(s, 10), -12.0);
        QCOMPARE(model::volumeAt(s, 13), -12.0);
        QCOMPARE(model::volumeAt(s, 14), -6.0); // the OUT keeps its value
        QCOMPARE(model::volumeAt(s, 18), -6.0);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(model::volumeAt(rig.segment(QStringLiteral("A2"), 0), 10), -6.0);
    }

    // Without marks each segment holds the value throughout (model::setAutomationTo).
    void automationToWholeSegments() {
        model::Sequence seq;
        model::Segment a;
        a.clipId = QStringLiteral("c");
        a.length = 10;
        a.volumeKeys = {model::AudioKey{2, -3.0}, model::AudioKey{8, 3.0}};
        model::Segment b = a;
        b.length = 6;
        seq.ensureTrack(QStringLiteral("A1"), AudioFormat::Mono)->segments = {a, b};
        QCOMPARE(model::setAutomationTo(seq, QStringLiteral("A1"), 0, 16, AutomationParam::Volume, -9.0, false), 2);
        for (int i : {0, 1}) {
            const model::Segment& s = seq.track(QStringLiteral("A1"))->segments.at(i);
            QCOMPARE(model::volumeAt(s, 0), -9.0);
            QCOMPARE(model::volumeAt(s, 5), -9.0);
        }
        // Again: nothing changes.
        QCOMPARE(model::setAutomationTo(seq, QStringLiteral("A1"), 0, 16, AutomationParam::Volume, -9.0, false), 0);
    }
};

QTEST_MAIN(TestAudioMixer)
#include "tst_audio_mixer.moc"

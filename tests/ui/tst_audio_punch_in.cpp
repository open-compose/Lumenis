// Audio Punch-In (Editing Guide p.746-753; docs/spec/punch-in.md): the input recording, simulated
// (media::AudioInput), the punch-ins edited into the sequence with an undo step each
// (Timeline::punchIn), the tool's scenarios as it plays with the Composer (p.749-751), and Tools >
// Audio Punch-In writing the take as managed media.
#include "lumenis/ext/provider.h"
#include "lumenis/media/audio_input.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_punch_in.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <cmath>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using PunchIn = lumenis::ui::Timeline::PunchIn;

namespace {

// A sequence of `frames` frames of clip "a" on V1, A1 and A2, at 25 fps.
BinItem sequence(std::int64_t frames = 100) {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Story");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.name = QStringLiteral("A");
        a.sourceTrack = t;
        a.length = frames;
        seq.splice(t, 0, {a});
    }
    seq.storeIn(s);
    return s;
}

// A voice-over clip of `channels` audio tracks, `frames` long.
BinItem voiceOver(int channels, std::int64_t frames) {
    BinItem c;
    c.id = model::newItemId();
    c.mobId = MobId::generate();
    c.name = QStringLiteral("Story Voice Over.1");
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = frames;
    c.fields.insert(QStringLiteral("Tracks"), channels == 1 ? QStringLiteral("A1") : QStringLiteral("A1-%1").arg(channels));
    return c;
}

// Channel c's sample n of the simulated input: a ramp each channel can be told apart by.
float simulated(int channel, std::int64_t n) {
    return static_cast<float>(channel + 1) * 0.1f + static_cast<float>(n % 48000) / 1e6f;
}

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

struct Rig {
    Composer composer;
    Timeline timeline{composer};
    AudioPunchInTool tool{timeline};
    QList<AudioPunchInTool::Take> takes;
    Rig(std::int64_t frames = 100) {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        timeline.resize(900, 400);
        timeline.show();
        tool.resize(320, 520);
        tool.show();
        tool.setInputStarter([](media::AudioInput& in, const QString&, int channels) {
            return in.startSimulated(channels, simulated);
        });
        tool.setClipMaker([this](const AudioPunchInTool::Take& take, QString*) -> std::optional<BinItem> {
            takes << take;
            const auto length = static_cast<std::int64_t>(media::samplesToFrames(static_cast<std::int64_t>(take.channels.front().size()), take.editRate));
            return voiceOver(static_cast<int>(take.channels.size()), length);
        });
        tool.setTargets({{QStringLiteral("Media"), QStringLiteral("/media")}}, QStringLiteral("/media"),
                        {{QStringLiteral("bin1"), QStringLiteral("Voice")}}, QStringLiteral("bin1"));
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(frames));
        tool.refresh();
        settle();
    }
    model::Sequence seq() const { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    std::optional<model::Track> track(const char* name) const {
        const model::Sequence s = seq();
        const model::Track* t = s.track(QString::fromLatin1(name));
        return t ? std::optional(*t) : std::nullopt;
    }
    void mark(std::optional<std::int64_t> in, std::optional<std::int64_t> out) {
        model::MonitorState& s = composer.state(MonitorId::Record);
        const std::int64_t was = s.position();
        s.clearBoth();
        if (in) {
            s.setPosition(*in);
            s.markIn();
        }
        if (out) {
            s.setPosition(*out);
            s.markOut();
        }
        s.setPosition(was);
    }
    std::int64_t position() const { return composer.state(MonitorId::Record).position(); }
};

} // namespace

class TestAudioPunchIn : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // The input records at the real rate, each channel apart, and hands a copy to the monitor.
    void input() {
        media::AudioInput in;
        QVERIFY(in.startSimulated(2, simulated));
        QVERIFY(in.isRunning());
        QCOMPARE(in.deviceName(), QStringLiteral("Simulated"));
        QTest::qWait(300);
        in.stop();
        const std::int64_t n = in.recorded();
        // About 0.3 s (at least the blocks due; a busy machine may run late but not early).
        QVERIFY2(n >= 9600 && n <= 48000, qPrintable(QString::number(n)));
        const std::vector<float> left = in.samples(0);
        const std::vector<float> right = in.samples(1);
        QCOMPARE(static_cast<std::int64_t>(left.size()), n);
        for (std::int64_t i : {std::int64_t{0}, n / 2, n - 1}) {
            QCOMPARE(left[static_cast<std::size_t>(i)], simulated(0, i));
            QCOMPARE(right[static_cast<std::size_t>(i)], simulated(1, i));
        }
        // Monitoring: the newest 50 ms at most, the channels' average on both sides.
        std::vector<float> out(2 * 48000, 0.0f);
        const int got = in.addMonitor(out.data(), 48000);
        QCOMPARE(got, 2400);
        const float want = (simulated(0, n - 2400) + simulated(1, n - 2400)) / 2;
        QVERIFY(std::abs(static_cast<double>(out[0] - want)) < 1e-6);
        QCOMPARE(out[0], out[1]);
        QCOMPARE(in.addMonitor(out.data(), 10), 0);
        // A new start drops what was recorded.
        QVERIFY(in.startSimulated(1, simulated));
        in.stop();
        QVERIFY(in.recorded() < n);
        QCOMPARE(in.channels(), 1);
    }

    // The output adds the monitored input to what plays.
    void monitorOutput() {
        media::AudioInput in;
        QVERIFY(in.startSimulated(1, [](int, std::int64_t) { return 0.0f; }));
        in.stop();
        std::vector<float> drain(2 * 48000);
        in.addMonitor(drain.data(), 48000);
        in.push(std::vector<float>(100, 0.5f).data(), 100);
        media::AudioPlayer player;
        player.setMonitor(&in);
        std::vector<float> out(2 * 50, 1.0f);
        player.pull(out.data(), 50);
        QCOMPARE(out[0], 0.5f);
        QCOMPARE(out[99], 0.5f);
        player.setMonitor(nullptr);
    }

    // Punch-ins of one take go to new or existing tracks, an undo step each (p.749-751).
    void punchInEdits() {
        Rig rig;
        const BinItem clip = voiceOver(2, 200);
        const auto steps = rig.timeline.history().undoCount();
        const auto used = rig.timeline.punchIn(QStringLiteral("bin1"), clip, {QString(), QStringLiteral("A1")},
                                               {PunchIn{10, 20, 15}, PunchIn{50, 10, 120}});
        QVERIFY(used);
        QCOMPARE(*used, (QStringList{"A3", "A1"}));
        QCOMPARE(rig.timeline.history().undoCount(), steps + 2);
        // A3 is new: filler, then the take from frame 15, then the second punch-in.
        const auto a3 = rig.track("A3");
        QVERIFY(a3);
        QCOMPARE(a3->segments.size(), 4);
        QVERIFY(a3->segments.at(0).isFiller());
        QCOMPARE(a3->segments.at(1).clipId, clip.id);
        QCOMPARE(a3->segments.at(1).sourceTrack, QStringLiteral("A1"));
        QCOMPARE(a3->segments.at(1).sourceIn, 15);
        QCOMPARE(a3->segments.at(1).length, 20);
        QCOMPARE(a3->segments.at(3).sourceIn, 120);
        // A1 is overwritten with the take's second channel (p.749, "overwrites the audio on that track").
        const auto a1 = rig.track("A1");
        QCOMPARE(a1->segments.at(1).clipId, clip.id);
        QCOMPARE(a1->segments.at(1).sourceTrack, QStringLiteral("A2"));
        QCOMPARE(a1->segments.at(1).binId, QStringLiteral("bin1"));
        QCOMPARE(a1->segments.at(1).sourceMob, clip.mobId);
        QCOMPARE(a1->segments.at(2).clipId, QStringLiteral("a"));
        QCOMPARE(a1->length(), 100);
        // "The first undo removes the most recent punch-in, the second undo removes the second-to-last"
        // (p.751); the new track goes with the first.
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(rig.track("A3")->segments.size(), 2);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(!rig.track("A3"));
        QCOMPARE(rig.track("A1")->segments.size(), 1);

        // Mono tracks only, not locked, and one channel per track (p.749).
        rig.timeline.setTracksLocked({QStringLiteral("A2")}, true);
        QVERIFY(!rig.timeline.punchIn(QStringLiteral("bin1"), clip, {QStringLiteral("A2")}, {PunchIn{0, 5, 0}}));
        QVERIFY(rig.timeline.lastMessage().contains(QStringLiteral("locked")));
        QVERIFY(!rig.timeline.punchIn(QStringLiteral("bin1"), clip, {QStringLiteral("A1"), QStringLiteral("A1")}, {PunchIn{0, 5, 0}}));
        QVERIFY(!rig.timeline.punchIn(QStringLiteral("bin1"), clip, {QStringLiteral("V1")}, {PunchIn{0, 5, 0}}));
        QVERIFY(!rig.timeline.punchIn(QStringLiteral("bin1"), clip, {QString()}, {}));
        QCOMPARE(rig.timeline.history().undoCount(), steps);
    }

    // The menus and buttons of the picture on p.748.
    void controls() {
        Rig rig;
        QStringList tracks;
        for (int i = 0; i < rig.tool.trackBox(0)->count(); ++i) tracks << rig.tool.trackBox(0)->itemText(i);
        QCOMPARE(tracks, (QStringList{"New Track", "A1", "A2"}));
        // Locked tracks are not offered (p.749).
        rig.timeline.setTracksLocked({QStringLiteral("A2")}, true);
        rig.tool.refresh();
        QCOMPARE(rig.tool.trackBox(0)->count(), 2);
        // CH1 is selected; channels the input does not have are dimmed.
        QVERIFY(rig.tool.channelButton(0)->isChecked());
        QVERIFY(rig.tool.channelButton(1)->isEnabled());
        QVERIFY(!rig.tool.channelButton(2)->isEnabled());
        rig.tool.setInputs({{QStringLiteral("Default"), 4}, {QStringLiteral("Mic"), 1}});
        QVERIFY(rig.tool.channelButton(3)->isEnabled());
        rig.tool.inputSourceBox()->setCurrentIndex(1);
        QVERIFY(!rig.tool.channelButton(1)->isEnabled());
        rig.tool.inputSourceBox()->setCurrentIndex(0);
        QCOMPARE(rig.tool.targets(), (QStringList{QString()}));
        rig.tool.channelButton(1)->click();
        rig.tool.trackBox(1)->setCurrentIndex(1);
        QCOMPARE(rig.tool.targets(), (QStringList{QString(), QStringLiteral("A1")}));
        // Alt+click picks another input channel.
        rig.tool.setHardwareChannel(1, 3);
        QCOMPARE(rig.tool.hardwareChannel(1), 3);
        QCOMPARE(rig.tool.channelButton(1)->text(), QStringLiteral("CH 2 (4)"));
        // Monitor Mode Input (p.749, p.753).
        QCOMPARE(rig.tool.monitoring(), AudioPunchInTool::Monitoring::Automatic);
        QVERIFY(rig.tool.monitorsInput()); // a simulated input has no delay
        rig.tool.setMonitoring(AudioPunchInTool::Monitoring::Off);
        QVERIFY(!rig.tool.monitorsInput());
        rig.tool.setMonitoring(AudioPunchInTool::Monitoring::Manual);
        QVERIFY(!rig.tool.monitorsInput());
        rig.tool.setMonitoring(AudioPunchInTool::Monitoring::On);
        QVERIFY(rig.tool.monitorsInput());
        QCOMPARE(rig.tool.monitorButton()->text(), QStringLiteral("On"));
        QVERIFY(rig.tool.stopAtEndBox()->isChecked());
        // Go to Mark In: the IN, else the start (p.748).
        rig.composer.moveTo(MonitorId::Record, 40);
        rig.tool.goToIn();
        QCOMPARE(rig.position(), 0);
        rig.mark(12, std::nullopt);
        rig.tool.goToIn();
        QCOMPARE(rig.position(), 12);
    }

    // Scenario 2 (p.749): IN and OUT, Record: recording starts at the IN and stops at the OUT, then
    // the take is kept.
    void inToOut() {
        Rig rig;
        rig.mark(10, 25);
        QSignalSpy active(&rig.tool, &AudioPunchInTool::activeChanged);
        rig.tool.record();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Recording);
        QVERIFY(rig.composer.isPlaying());
        QCOMPARE(rig.tool.silencedTracks(), QStringList());
        QTRY_COMPARE_WITH_TIMEOUT(rig.tool.state(), AudioPunchInTool::State::Idle, 5000);
        QCOMPARE(active.count(), 2);
        QVERIFY(!rig.composer.isPlaying());
        QCOMPARE(rig.takes.size(), 1);
        QCOMPARE(rig.takes.first().sequenceName, QStringLiteral("Story"));
        QCOMPARE(rig.takes.first().binId, QStringLiteral("bin1"));
        QCOMPARE(rig.takes.first().drive, QStringLiteral("/media"));
        QCOMPARE(rig.takes.first().channels.size(), 1);
        // The take's first samples are the input's.
        QCOMPARE(rig.takes.first().channels.front().front(), simulated(0, 0));
        // A new track with the take from IN through OUT.
        const auto a3 = rig.track("A3");
        QVERIFY(a3);
        QCOMPARE(a3->segments.size(), 2);
        QCOMPARE(a3->segments.at(0).length, 10);
        QCOMPARE(a3->segments.at(1).length, 16);
        QVERIFY(a3->segments.at(1).sourceIn >= 0);
    }

    // Scenario 3 (p.750): no marks; Record starts and ends each punch-in while play goes on, then
    // Stop keeps them all as one clip, an undo step each; an existing track is silenced meanwhile.
    void severalPunchIns() {
        Rig rig;
        rig.tool.trackBox(0)->setCurrentIndex(1); // A1
        rig.composer.moveTo(MonitorId::Record, 5);
        rig.tool.record();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Recording);
        QCOMPARE(rig.tool.silencedTracks(), (QStringList{"A1"}));
        QTRY_VERIFY_WITH_TIMEOUT(rig.position() >= 12, 3000);
        rig.tool.record();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Playing);
        QTRY_VERIFY_WITH_TIMEOUT(rig.position() >= 20, 3000);
        // B in the tool records too (p.751).
        QTest::keyClick(&rig.tool, Qt::Key_B);
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Recording);
        QTRY_VERIFY_WITH_TIMEOUT(rig.position() >= 26, 3000);
        const auto steps = rig.timeline.history().undoCount();
        rig.tool.stopTake();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Idle);
        QVERIFY(rig.tool.silencedTracks().isEmpty());
        QCOMPARE(rig.takes.size(), 1);
        QCOMPARE(rig.timeline.history().undoCount(), steps + 2);
        const QList<PunchIn> punches = rig.tool.punches();
        QCOMPARE(punches.size(), 2);
        QCOMPARE(punches.at(0).at, 5);
        QVERIFY(punches.at(0).length >= 7);
        QVERIFY(punches.at(1).at >= 20);
        // The second punch-in's source is later in the one recording by about the frames between.
        QVERIFY(punches.at(1).source - punches.at(0).source >= punches.at(1).at - punches.at(0).at - 3);
        const auto a1 = rig.track("A1");
        QCOMPARE(a1->segments.at(1).sourceIn, punches.at(0).source);
        QCOMPARE(a1->segments.at(1).name, QStringLiteral("Story Voice Over.1"));
        QVERIFY(!rig.track("A3"));
    }

    // Preroll: play backs up, and recording waits for the IN; Record during the preroll starts at
    // once (p.749). Cancel keeps nothing (p.748).
    void prerollAndCancel() {
        Rig rig;
        rig.mark(40, std::nullopt);
        rig.tool.prerollBox()->setValue(1);
        rig.tool.record();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Armed);
        QVERIFY(rig.position() <= 16);
        QTRY_COMPARE_WITH_TIMEOUT(rig.tool.state(), AudioPunchInTool::State::Recording, 4000);
        rig.tool.cancelTake();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Idle);
        QVERIFY(rig.takes.isEmpty());
        QVERIFY(!rig.track("A3"));

        rig.tool.record();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Armed);
        const std::int64_t at = rig.position();
        rig.tool.record();
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Recording);
        QTRY_VERIFY_WITH_TIMEOUT(rig.position() >= at + 3, 3000);
        rig.tool.stopTake();
        QCOMPARE(rig.tool.punches().first().at, at);
        QVERIFY(rig.track("A3"));
    }

    // Play In/Out loops IN to OUT while rehearsing (p.748, p.750-751); a recording that completes at the
    // OUT ends the looping, and the take ends after the postroll.
    void loopPlay() {
        Rig rig;
        rig.mark(4, 9);
        QTest::keyClick(&rig.tool, Qt::Key_V);
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Playing);
        // Two passes of 6 frames take about half a second: still playing, still inside the range.
        QTest::qWait(700);
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Playing);
        QVERIFY(rig.composer.isPlaying());
        QVERIFY(rig.position() >= 4 && rig.position() <= 12);
        // Record now, then it stops at the OUT and play goes on for the postroll.
        rig.tool.postrollBox()->setValue(1);
        QTRY_VERIFY_WITH_TIMEOUT(rig.position() <= 6, 3000);
        rig.tool.record();
        QTRY_COMPARE_WITH_TIMEOUT(rig.tool.state(), AudioPunchInTool::State::Finishing, 3000);
        QVERIFY(rig.composer.isPlaying());
        QTRY_COMPARE_WITH_TIMEOUT(rig.tool.state(), AudioPunchInTool::State::Idle, 4000);
        QCOMPARE(rig.tool.punches().size(), 1);
        QCOMPARE(rig.tool.punches().first().at + rig.tool.punches().first().length, 10);
        QVERIFY(rig.position() >= 30);
    }

    // Stop at End unchecked (Extended Audio Punch-In, p.751): recording goes past the OUT.
    void extended() {
        Rig rig;
        rig.mark(std::nullopt, 8);
        rig.tool.stopAtEndBox()->setChecked(false);
        rig.tool.record();
        QTRY_VERIFY_WITH_TIMEOUT(rig.position() >= 15, 3000);
        QCOMPARE(rig.tool.state(), AudioPunchInTool::State::Recording);
        rig.tool.stopTake();
        QVERIFY(rig.tool.punches().first().length >= 15);
    }

    // Play that runs to the end of the sequence ends the recording there and keeps the take.
    void toTheEnd() {
        Rig rig(20);
        rig.composer.moveTo(MonitorId::Record, 10);
        rig.tool.record();
        QTRY_COMPARE_WITH_TIMEOUT(rig.tool.state(), AudioPunchInTool::State::Idle, 3000);
        QCOMPARE(rig.tool.punches().size(), 1);
        QCOMPARE(rig.tool.punches().first().at, 10);
        QCOMPARE(rig.tool.punches().first().length, 10);
    }

    // Tools > Audio Punch-In, and the take written as managed media "<sequence> Voice Over.<n>" (p.751).
    void mainWindow() {
        QTemporaryDir dir;
        const QString drive = dir.filePath(QStringLiteral("Drive"));
        QDir().mkpath(drive);
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        commands.action(QStringLiteral("tools.audio_punch_in"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioPunchIn"))));
        AudioPunchInTool& tool = window.audioPunchIn();
        QVERIFY(tool.driveBox()->findData(QDir::cleanPath(drive)) >= 0 || tool.driveBox()->count() > 0);
        QVERIFY(tool.binBox()->findData(binId) >= 0);

        AudioPunchInTool::Take take;
        take.sequenceName = QStringLiteral("Story");
        take.editRate = Rational(25);
        take.drive = QDir::cleanPath(drive);
        take.binId = binId;
        take.channels = {std::vector<float>(48000, 0.25f)};
        QString error;
        const auto first = window.punchInClip(take, &error);
        QVERIFY2(first, qPrintable(error));
        QCOMPARE(first->name, QStringLiteral("Story Voice Over.1"));
        QCOMPARE(first->durationFrames, 25);
        QVERIFY(bin->bin().item(first->id));
        const auto second = window.punchInClip(take, &error);
        QVERIFY(second);
        QCOMPARE(second->name, QStringLiteral("Story Voice Over.2"));
        // Nothing recorded, or no bin: nothing made.
        take.channels = {std::vector<float>()};
        QVERIFY(!window.punchInClip(take, &error));
        take.binId = QStringLiteral("none");
        QVERIFY(!window.punchInClip(take, &error));
        // The Audio Tool button opens the Audio Tool (p.748).
        tool.audioToolButton()->click();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioTool"))));
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestAudioPunchIn)
#include "tst_audio_punch_in.moc"

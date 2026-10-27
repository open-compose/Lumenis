#include "lumenis/ext/provider.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/clip_audio.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDial>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <atomic>
#include <thread>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// A sequence with clips `a` and `b` on V1, A1 and A2.
BinItem sequence(std::int64_t frames) {
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

struct Rig {
    Composer composer;
    Timeline timeline{composer};
    AudioMixer mixer{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        mixer.resize(600, 440);
        mixer.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence(100));
        settle();
    }
    const model::Segment& a(const char* track, int i) {
        held = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        return held.track(QString::fromLatin1(track))->segments.at(i);
    }
    model::Sequence held;
};

} // namespace

class TestAudioMedia : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void faderScale() {
        // The scale of the picture on p.693: +12 at the top, -∞ at the bottom.
        QCOMPARE(fader::position(12), fader::kRange);
        QCOMPARE(fader::position(model::kGainOff), 0);
        QCOMPARE(fader::gain(fader::position(0)), 0.0);
        QCOMPARE(fader::gain(fader::position(-7)), -7.0);
        QCOMPARE(fader::gain(fader::position(-30)), -30.0);
        QVERIFY(fader::position(3) > fader::position(0));
        QCOMPARE(fader::text(model::kGainOff), QStringLiteral("-∞"));
        QCOMPARE(fader::text(6), QStringLiteral("+6.0"));
        QCOMPARE(*fader::parse(QStringLiteral("40")), model::kGainMax); // typed, to +36 (p.670)
        QCOMPARE(fader::panText(-57), QStringLiteral("L57"));
        QCOMPARE(fader::panText(0), QStringLiteral("MID"));
        QCOMPARE(*fader::parsePan(QStringLiteral("r20")), 20);
        QVERIFY(!fader::parsePan(QStringLiteral("x")));
    }

    void stripsFollowTheSequence() {
        // A strip per audio track and the Master strip (p.693).
        Rig rig;
        QCOMPARE(rig.mixer.tracks(), (QStringList{"A1", "A2"}));
        QVERIFY(rig.mixer.master());
        QVERIFY(rig.timeline.perform(QStringLiteral("timeline.new.audio_track.mono")));
        QCOMPARE(rig.mixer.tracks(), (QStringList{"A1", "A2", "A3"}));
        // The track button selects the Timeline track (p.698).
        rig.mixer.strip(QStringLiteral("A1"))->trackButton()->click();
        QVERIFY(!rig.timeline.patch().isRecordSelected(QStringLiteral("A1")));
        QVERIFY(!rig.mixer.strip(QStringLiteral("A1"))->trackButton()->isChecked());
    }

    void clipMode() {
        // p.702: the fader sets the clip gain of the segment under the
        // position; values are undoable edits.
        Rig rig;
        QCOMPARE(rig.mixer.mode(), AudioMixer::Mode::Clip);
        rig.composer.moveTo(MonitorId::Record, 10);
        MixerStrip* a1 = rig.mixer.strip(QStringLiteral("A1"));
        a1->levelBox()->setText(QStringLiteral("-6"));
        a1->levelBox()->setModified(true);
        emit a1->levelBox()->editingFinished();
        QCOMPARE(rig.a("A1", 0).gain, -6.0);
        QCOMPARE(rig.a("A1", 1).gain, 0.0);
        QCOMPARE(rig.mixer.strip(QStringLiteral("A1"))->fader()->value(), fader::position(-6));
        // Typed levels over +12 dB (p.669-670).
        a1 = rig.mixer.strip(QStringLiteral("A1"));
        a1->levelBox()->setText(QStringLiteral("20"));
        a1->levelBox()->setModified(true);
        emit a1->levelBox()->editingFinished();
        QCOMPARE(rig.a("A1", 0).gain, 20.0);
        // Alt-click resets the fader to 0 dB (p.703).
        a1 = rig.mixer.strip(QStringLiteral("A1"));
        QTest::mouseClick(a1->fader(), Qt::LeftButton, Qt::AltModifier);
        QCOMPARE(rig.a("A1", 0).gain, 0.0);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(rig.a("A1", 0).gain, 20.0);
        // Pan.
        a1 = rig.mixer.strip(QStringLiteral("A1"));
        a1->panBox()->setText(QStringLiteral("L30"));
        a1->panBox()->setModified(true);
        emit a1->panBox()->editingFinished();
        QCOMPARE(rig.a("A1", 0).pan, -30);
        // Ganged faders move together (p.704).
        rig.mixer.setGang(QStringLiteral("A1"), QStringLiteral("A"));
        rig.mixer.setGang(QStringLiteral("A2"), QStringLiteral("A"));
        a1 = rig.mixer.strip(QStringLiteral("A1"));
        a1->levelBox()->setText(QStringLiteral("17"));
        a1->levelBox()->setModified(true);
        emit a1->levelBox()->editingFinished();
        QCOMPARE(rig.a("A2", 0).gain, -3.0);
    }

    void fastMenuOnTracks() {
        // p.710-711: on whole tracks without marks, between them with.
        Rig rig;
        rig.composer.moveTo(MonitorId::Record, 10);
        rig.timeline.toggleRecordTrack(QStringLiteral("A2")); // only A1 (and V1) selected
        rig.mixer.strip(QStringLiteral("A1"))->show(-4, 0);   // the fader, as left
        QMenu* menu = rig.mixer.fastMenu();
        QAction* setLevel = nullptr;
        QAction* removeGain = nullptr;
        for (QAction* a : menu->actions()) {
            if (a->text() == QLatin1String("Set Level on Track")) setLevel = a;
            if (a->text() == QLatin1String("Remove Clip Gain on Track")) removeGain = a;
        }
        QVERIFY(setLevel && setLevel->isEnabled());
        setLevel->trigger();
        QCOMPARE(rig.a("A1", 0).gain, -4.0);
        QCOMPARE(rig.a("A1", 1).gain, -4.0);
        QCOMPARE(rig.a("A2", 0).gain, 0.0);
        // With an IN only: from the IN to the end.
        rig.mixer.fastMenu();
        rig.composer.state(MonitorId::Record).setPosition(60);
        rig.composer.state(MonitorId::Record).markIn();
        for (QAction* a : rig.mixer.fastMenu()->actions()) {
            if (a->text() == QLatin1String("Remove Clip Gain on Track")) removeGain = a;
        }
        removeGain->trigger();
        QCOMPARE(rig.a("A1", 0).gain, -4.0);
        QCOMPARE(rig.a("A1", 1).gain, 0.0);
    }

    void modes() {
        // p.692, p.701-725.
        Rig rig;
        rig.composer.moveTo(MonitorId::Record, 10);
        rig.timeline.setClipAudio(QStringLiteral("A1"), -5.0, std::nullopt, false, QStringLiteral("Clip Gain"));
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        // Auto mode shows "Clip" when the track has clip volume (p.694).
        QCOMPARE(rig.mixer.strip(QStringLiteral("A1"))->levelBox()->text(), QStringLiteral("Clip"));
        // Its faders set the automation (Phase 5.2, tst_automation_ui).
        QVERIFY(rig.mixer.strip(QStringLiteral("A1"))->fader()->isEnabled());
        // Live mode: temporary values, not in the sequence; no Bypass.
        rig.mixer.setMode(AudioMixer::Mode::Live);
        QVERIFY(rig.mixer.bypassButton()->isHidden());
        QTest::mouseClick(rig.mixer.strip(QStringLiteral("A2"))->fader(), Qt::LeftButton, Qt::AltModifier);
        MixerStrip* a2 = rig.mixer.strip(QStringLiteral("A2"));
        a2->levelBox()->setText(QStringLiteral("-9"));
        a2->levelBox()->setModified(true);
        emit a2->levelBox()->editingFinished();
        QCOMPARE(rig.mixer.strip(QStringLiteral("A2"))->gain(), -9.0);
        QCOMPARE(rig.a("A2", 0).gain, 0.0);
        rig.mixer.setMode(AudioMixer::Mode::Clip);
        QCOMPARE(rig.mixer.strip(QStringLiteral("A1"))->gain(), -5.0);
        QVERIFY(!rig.mixer.bypassButton()->isHidden());
        // Solo and mute stay through mode changes (p.694).
        rig.mixer.strip(QStringLiteral("A1"))->soloButton()->click();
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        QVERIFY(rig.mixer.isSoloed(QStringLiteral("A1")));
        QVERIFY(rig.mixer.strip(QStringLiteral("A1"))->soloButton()->isChecked());
    }

    void audioTool() {
        // p.157-158: RP, In/Out per channel, PH menu, meters.
        AudioTool tool(8);
        QCOMPARE(tool.meters()->channels(), 8);
        QVERIFY(tool.isOutput(0));
        tool.ioButton(0)->click();
        QVERIFY(!tool.isOutput(0));
        QCOMPARE(tool.ioButton(0)->text(), QStringLiteral("I"));
        tool.meters()->setLevel(2, -10);
        QCOMPARE(tool.meters()->peak(2), -10.0);
        tool.resetPeakButton()->click();
        QCOMPARE(tool.meters()->peak(2), -90.0);
        // Green below the -20 dB reference, yellow to -3, red above.
        QCOMPARE(AudioMeters::colourFor(-30, -20), QColor(0xff3cc85a));
        QCOMPARE(AudioMeters::colourFor(-10, -20), QColor(0xffd8c040));
        QCOMPARE(AudioMeters::colourFor(-1, -20), QColor(0xffd84040));
        // The calibration tone defaults to -20 dB at 1000 Hz (p.920).
        QCOMPARE(tool.calibrationTone().level, -20);
        QCOMPARE(tool.calibrationTone().frequency, 1000);
        QStringList items;
        for (QAction* a : tool.peakHoldButton()->menu()->actions()) {
            if (!a->isSeparator()) items << a->text();
        }
        QCOMPARE(items, (QStringList{"Peak Hold", "Infinite Hold", "Set Calibration Tone...", "Play Calibration Tone",
                                     "Create Tone Media..."}));
    }

    // PH > Play Calibration Tone (p.920): the tone on L and R, shown in the meters, until RP (p.157);
    // PH > Create Tone Media (p.158-159): a master clip of tone in the chosen bin.
    void calibrationTone() {
        QTemporaryDir dir;
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        AudioTool& tool = window.audioTool();
        QVERIFY(tool.playToneAction()->isEnabled());
        QVERIFY(tool.createToneMediaAction()->isEnabled());
        // Without a bin there is nowhere to put the clip.
        window.setToneMediaHook([](CreateToneMediaDialog&) { return true; });
        QVERIFY(!window.createToneMedia());

        tool.setCalibrationTone(CalibrationTone{-12, 1000});
        QVERIFY(window.playCalibrationTone(false));
        QVERIFY(window.isPlayingTone());
        std::vector<float> out(2 * 4800);
        int got = 0;
        for (int tries = 0; tries < 200 && got == 0; ++tries) {
            got = window.audioPlayer().pull(out.data(), 4800);
            if (got == 0) QTest::qWait(5);
        }
        QVERIFY(got > 0);
        window.updateMeters();
        QVERIFY(std::abs(tool.meters()->peak(0) - -12.0) < 0.2);
        QVERIFY(std::abs(tool.meters()->peak(1) - -12.0) < 0.2);
        QCOMPARE(tool.meters()->peak(2), -90.0); // a stereo output
        tool.resetPeakButton()->click();
        QVERIFY(!window.isPlayingTone());
        QVERIFY(!window.audioPlayer().isPlaying());

        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const int before = static_cast<int>(bin->bin().items.size());
        window.setToneMediaHook([&](CreateToneMediaDialog& d) {
            d.findChild<QSpinBox*>(QStringLiteral("toneFrequency"))->setValue(440);
            d.findChild<QSpinBox*>(QStringLiteral("toneTracks"))->setValue(2);
            d.findChild<QSpinBox*>(QStringLiteral("toneSeconds"))->setValue(2);
            // Only 1-20000 Hz, 0 (noise) and -777 (a sweep) are taken (p.159).
            d.findChild<QSpinBox*>(QStringLiteral("toneFrequency"))->setValue(-5);
            if (d.isValid()) return false;
            d.findChild<QSpinBox*>(QStringLiteral("toneFrequency"))->setValue(440);
            auto* drives = d.findChild<QComboBox*>(QStringLiteral("toneDrive"));
            drives->setCurrentIndex(drives->findData(drive));
            return d.choice().tone.level == -12 && d.choice().bin == binId && d.choice().drive == drive;
        });
        const auto clip = window.createToneMedia();
        QVERIFY(clip);
        QCOMPARE(static_cast<int>(bin->bin().items.size()), before + 1);
        QCOMPARE(clip->name, QStringLiteral("Tone 440 Hz -12 dB 2 Tracks"));
        QCOMPARE(clip->durationFrames, std::int64_t{50});
        const auto link = media::linkOf(*clip);
        QVERIFY(link);
        QCOMPARE(link->audioPaths.size(), 2);
        QVERIFY(link->audioFile(1).startsWith(drive));
        media::AudioReader reader;
        QVERIFY(reader.open(link->audioFile(1)));
        const std::vector<float> samples = reader.samples(0, 48000);
        QCOMPARE(samples.size(), std::size_t{48000});
        float peak = 0;
        int crossings = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            peak = std::max(peak, std::abs(samples[i]));
            if (i > 0 && samples[i - 1] < 0 && samples[i] >= 0) ++crossings;
        }
        QVERIFY(std::abs(peak - 0.2512f) < 2e-3f);
        QVERIFY(std::abs(crossings - 440) <= 1);
        media::setExtraMediaDrives({});
    }

    // The Audio Project setting's Sample Rate (p.1148): new media at that rate; and Show Mismatched
    // Sample Rates as Different Color (p.1149) in the Timeline.
    void projectSampleRate() {
        QTemporaryDir dir;
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.projectSampleRate(), 48000);
        window.setToneMediaHook([&](CreateToneMediaDialog& d) {
            d.findChild<QSpinBox*>(QStringLiteral("toneSeconds"))->setValue(1);
            auto* drives = d.findChild<QComboBox*>(QStringLiteral("toneDrive"));
            drives->setCurrentIndex(drives->findData(drive));
            return true;
        });
        const auto at48 = window.createToneMedia();
        QVERIFY(at48);
        QCOMPARE(at48->fields.value(QStringLiteral("Audio SR")), QStringLiteral("48000"));
        window.settings().setValue(QStringLiteral("Audio Project"), QStringLiteral("sampleRate"), QStringLiteral("44100"));
        QCOMPARE(window.projectSampleRate(), 44100);
        const auto at44 = window.createToneMedia();
        QVERIFY(at44);
        QCOMPARE(at44->fields.value(QStringLiteral("Audio SR")), QStringLiteral("44100"));
        const auto link = media::linkOf(*at44);
        QVERIFY(link && link->audioFile(0).endsWith(QLatin1String(".wav")));
        media::AudioReader reader;
        QVERIFY(reader.open(link->audioFile(0)));
        QCOMPARE(reader.sampleRate(), 44100);

        // A sequence of both: with the option on, the 48 kHz segment takes the mismatch colour.
        model::Segment a;
        a.binId = binId;
        a.clipId = at48->id;
        a.sourceTrack = QStringLiteral("A1");
        a.length = 25;
        model::Segment b = a;
        b.clipId = at44->id;
        Timeline& timeline = window.timeline();
        QVERIFY(timeline.segmentColor(a).isEmpty());
        window.settings().setValue(QStringLiteral("Audio Project"), QStringLiteral("showMismatchedRates"), true);
        QCOMPARE(timeline.segmentColor(a), QString::fromLatin1(Timeline::kMismatchedRateColor));
        QVERIFY(timeline.segmentColor(b).isEmpty());
        window.settings().setValue(QStringLiteral("Audio Project"), QStringLiteral("sampleRate"), QStringLiteral("48000"));
        QVERIFY(timeline.segmentColor(a).isEmpty());
        QCOMPARE(timeline.segmentColor(b), QString::fromLatin1(Timeline::kMismatchedRateColor));
        media::setExtraMediaDrives({});
    }

    // The Import settings' Audio tab (p.1198-1199) through File > Import: the source's rate and bit
    // depth kept, centred mono clips, a monophonic Broadcast WAV group as one clip on its time stamp.
    void importAudioSettings() {
        QTemporaryDir dir;
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        auto wav = [&](const QString& name, int rate, const QString& codec, std::int64_t timeReference) {
            media::WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = codec;
            s.channels = 1;
            s.sampleRate = rate;
            if (timeReference >= 0) {
                s.formatOptions = {{QStringLiteral("write_bext"), QStringLiteral("1")}};
                s.metadata = {{QStringLiteral("time_reference"), QString::number(timeReference)}};
            }
            media::MediaWriter w;
            if (!w.open(dir.filePath(name), s)) return QString();
            std::vector<float> v(static_cast<std::size_t>(rate / 2), 0.25f);
            return w.writeAudio(v.data(), rate / 2) && w.finish() ? dir.filePath(name) : QString();
        };
        const QString music = wav(QStringLiteral("music.wav"), 44100, QStringLiteral("pcm_s16le"), -1);
        QVERIFY(!music.isEmpty());

        // By default: converted to the project's 48 kHz, 24 bit (convertBitDepth is off: the
        // source's 16 bit kept), no pan of its own.
        QStringList ids = window.importFiles(binId, {music});
        QCOMPARE(ids.size(), 1);
        const model::BinItem* plain = bin->bin().item(ids.first());
        QVERIFY(plain);
        QCOMPARE(plain->fields.value(QStringLiteral("Audio SR")), QStringLiteral("48000"));
        QCOMPARE(plain->fields.value(QStringLiteral("Audio Bit Depth")), QStringLiteral("16 bit"));
        QVERIFY(!model::clipPan(*plain));

        // At the source's rate, converted to the project's bit depth, centred.
        const QString imp = QStringLiteral("Import");
        window.settings().setValue(imp, QStringLiteral("convertSampleRate"), false);
        window.settings().setValue(imp, QStringLiteral("convertBitDepth"), true);
        window.settings().setValue(imp, QStringLiteral("centerPanMono"), true);
        ids = window.importFiles(binId, {music});
        QCOMPARE(ids.size(), 1);
        const model::BinItem* kept = bin->bin().item(ids.first());
        QVERIFY(kept);
        QCOMPARE(kept->fields.value(QStringLiteral("Audio SR")), QStringLiteral("44100"));
        QCOMPARE(kept->fields.value(QStringLiteral("Audio Bit Depth")), QStringLiteral("24 bit"));
        QCOMPARE(model::clipPan(*kept), std::optional<int>(0));

        // Two files of a group, stamped 10:00:00:00 plus three quarters of a frame: one clip of two
        // tracks; with Subframe Alignment it starts on the next frame.
        window.settings().setValue(imp, QStringLiteral("convertSampleRate"), true);
        window.settings().setValue(imp, QStringLiteral("autodetectBwfGroups"), true);
        window.settings().setValue(imp, QStringLiteral("bwfAlignment"), true);
        const std::int64_t tr = std::int64_t{48000} * 36000 + 1440;
        const QString a = wav(QStringLiteral("Take_1.wav"), 48000, QStringLiteral("pcm_s24le"), tr);
        const QString b = wav(QStringLiteral("Take_2.wav"), 48000, QStringLiteral("pcm_s24le"), tr);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        ids = window.importFiles(binId, {a, b});
        QCOMPARE(ids.size(), 1);
        const model::BinItem* take = bin->bin().item(ids.first());
        QVERIFY(take);
        QCOMPARE(take->name, QStringLiteral("Take"));
        QCOMPARE(take->fields.value(QStringLiteral("Tracks")), QStringLiteral("A1-2"));
        QCOMPARE(take->fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:01"));
        QVERIFY(!model::clipPan(*take)); // not mono
        media::setExtraMediaDrives({});
    }

    void mainWindowTools() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath("Users"), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath("Film");
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        commands.action(QStringLiteral("tools.audio_mixer"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioMixer"))));
        commands.action(QStringLiteral("tools.audio_tool"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioTool"))));
        // The mixer mode is an Audio Project setting; Live is not saved
        // (p.692).
        window.audioMixer().setMode(AudioMixer::Mode::Auto);
        QCOMPARE(window.settings().text(QStringLiteral("Audio Project"), QStringLiteral("mixerMode")), QStringLiteral("auto"));
        window.audioMixer().setMode(AudioMixer::Mode::Live);
        QCOMPARE(window.settings().text(QStringLiteral("Audio Project"), QStringLiteral("mixerMode")), QStringLiteral("auto"));

        // Tools > Media Tool: the display dialog first (p.346).
        bool asked = false;
        bool masterClips = false;
        QString projectName;
        window.setMediaToolDisplayHook([&](MediaToolDisplayDialog& d) -> bool {
            asked = true;
            masterClips = d.option(QStringLiteral("masterClips"))->isChecked();
            projectName = d.projectList()->item(0)->text();
            return true;
        });
        commands.action(QStringLiteral("tools.media_tool"))->trigger();
        QVERIFY(asked);
        QVERIFY(masterClips);
        QCOMPARE(projectName, QStringLiteral("Film"));
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("MediaTool"))));
        QCOMPARE(window.mediaTool().table()->columnCount(), 6);
        QVERIFY(!window.mediaTool().deleteSelected());

        // Help > About: Configuration, Usage and Hardware (p.102-105).
        std::unique_ptr<QMessageBox> box(window.createAboutBox());
        QVERIFY(box->findChild<QPushButton*>(QStringLiteral("aboutHardware")));
        std::unique_ptr<AboutInfoDialog> info(window.createAboutInfo());
        QStringList tabs;
        for (int i = 0; i < info->tabs()->count(); ++i) tabs << info->tabs()->tabText(i);
        QCOMPARE(tabs, (QStringList{"Configuration", "Usage", "Hardware"}));
        info->showTab(QStringLiteral("Usage"));
        QCOMPARE(info->tabs()->currentIndex(), 1);
        QCOMPARE(info->hardwareTabs()->tabText(0), QStringLiteral("Drives"));
        QCOMPARE(info->hardwareTabs()->tabText(1), QStringLiteral("System"));
    }

    void mediaToolList() {
        // Master clips, not sequences or subclips (p.345).
        MediaTool tool;
        BinItem clip;
        clip.id = QStringLiteral("c");
        clip.name = QStringLiteral("Interview");
        clip.kind = BinItemKind::MasterClip;
        clip.durationFrames = 50;
        clip.editRate = Rational(25);
        BinItem seq = clip;
        seq.name = QStringLiteral("Cut");
        seq.kind = BinItemKind::Sequence;
        BinItem sub = clip;
        sub.name = QStringLiteral("Sub");
        sub.kind = BinItemKind::Subclip;
        MediaToolDisplay display;
        tool.setItems({clip, seq, sub}, display);
        QCOMPARE(tool.names(), QStringList{"Interview"});
        QCOMPARE(tool.table()->item(0, 2)->text(), QStringLiteral("00:00:02:00"));
        display.masterClips = false;
        tool.setItems({clip}, display);
        QVERIFY(tool.names().isEmpty());
    }

    void meterScaleMapping() {
        // The digital scale's marks equally spaced (p.158): levelAt() undoes scalePosition().
        QCOMPARE(AudioMeters::scalePosition(0), 0.0);
        QCOMPARE(AudioMeters::scalePosition(-90), 1.0);
        QCOMPARE(AudioMeters::scalePosition(-20), 0.4);
        for (double db : {0.0, -2.0, -4.0, -11.0, -20.0, -33.0, -50.0, -89.0, -90.0}) {
            QVERIFY(std::abs(AudioMeters::levelAt(AudioMeters::scalePosition(db)) - db) < 1e-9);
        }
        // The bar's colours change at the scale's marks: drawn at 0 dB, a line just above the -20
        // mark is yellow and one just below it green; just above -3, red.
        AudioMeters meters(1);
        meters.resize(80, 260);
        meters.setLevel(0, 0);
        const QImage image = meters.grab().toImage();
        const int top = 8;
        const int span = meters.height() - 16;
        const int x = 26 + 1 + 2;
        auto at = [&](double db, int dy) {
            return image.pixelColor(x, top + static_cast<int>(AudioMeters::scalePosition(db) * span) + dy).rgb();
        };
        QCOMPARE(at(-20, -2), QColor(0xffd8c040).rgb());
        QCOMPARE(at(-20, 2), QColor(0xff3cc85a).rgb());
        QCOMPARE(at(-14, 0), QColor(0xffd8c040).rgb());
        QCOMPARE(at(-3, -2), QColor(0xffd84040).rgb());
    }

    void peakHold() {
        AudioTool tool(2);
        AudioMeters* meters = tool.meters();
        qint64 now = 0;
        meters->setClock([&now] { return now; });
        QCOMPARE(tool.hold(), AudioTool::Hold::PeakHold);
        // Peak Hold: the peak holds, then falls to the level once its time is up.
        meters->setLevel(0, -6);
        now = AudioMeters::kPeakHoldMs - 1;
        meters->setLevel(0, -30);
        QCOMPARE(meters->peak(0), -6.0);
        now = AudioMeters::kPeakHoldMs;
        meters->setLevel(0, -30);
        QCOMPARE(meters->peak(0), -30.0);
        // A higher level always raises it and restarts the time.
        meters->setLevel(0, -10);
        now += 10;
        meters->setLevel(0, -40);
        QCOMPARE(meters->peak(0), -10.0);
        // Infinite Hold, from the PH menu: kept until Reset Peak.
        QAction* infinite = nullptr;
        for (QAction* a : tool.peakHoldButton()->menu()->actions()) {
            if (a->text() == QLatin1String("Infinite Hold")) infinite = a;
        }
        QVERIFY(infinite);
        infinite->trigger();
        QCOMPARE(tool.hold(), AudioTool::Hold::Infinite);
        now += 100000;
        meters->setLevel(0, -50);
        QCOMPARE(meters->peak(0), -10.0);
        tool.resetPeakButton()->click();
        QCOMPARE(meters->peak(0), -90.0);
    }

    void masterFader() {
        Rig rig;
        MixerStrip* master = rig.mixer.master();
        QCOMPARE(rig.mixer.masterGain(), 0.0);
        QSignalSpy spy(&rig.mixer, &AudioMixer::masterGainChanged);
        master->levelBox()->setText(QStringLiteral("-6"));
        master->levelBox()->setModified(true);
        emit master->levelBox()->editingFinished();
        QCOMPARE(rig.mixer.masterGain(), -6.0);
        QCOMPARE(spy.count(), 1);
        // The tracks keep their own levels.
        QCOMPARE(rig.a("A1", 0).gain, 0.0);
        // It stays through refreshes and mode changes.
        rig.mixer.setMode(AudioMixer::Mode::Live);
        rig.mixer.refresh();
        QCOMPARE(master->gain(), -6.0);
        // Alt-click resets it to 0 dB (p.703).
        QTest::mouseClick(master->fader(), Qt::LeftButton, Qt::AltModifier, master->fader()->rect().center());
        QCOMPARE(rig.mixer.masterGain(), 0.0);
    }

    void aboutTabsByKey() {
        AboutInfoDialog info({}, ProjectUsage{});
        info.tabs()->setTabText(1, QStringLiteral("Nutzung")); // as if translated
        info.showTab(QStringLiteral("Usage"));
        QCOMPARE(info.tabs()->currentIndex(), 1);
        info.showTab(QStringLiteral("Hardware"));
        QCOMPARE(info.tabs()->currentIndex(), 2);
        info.showTab(QStringLiteral("Configuration"));
        QCOMPARE(info.tabs()->currentIndex(), 0);
        info.showTab(QStringLiteral("Unknown"));
        QCOMPARE(info.tabs()->currentIndex(), 0);
    }

    void freeSpaceInDecimalGigabytes() {
        QCOMPARE(freeSpaceText(1'500'000'000), QStringLiteral("1.5 GB free"));
        QCOMPARE(freeSpaceText(0), QStringLiteral("0.0 GB free"));
        QCOMPARE(freeSpaceText(Q_INT64_C(1073741824)), QStringLiteral("1.1 GB free"));
    }

    void mediaToolDisplayNeedsADrive() {
        // The tool loads only the drives selected (p.345): with none, OK waits.
        MediaToolDisplayDialog dialog(QStringLiteral("Film"),
                                      {{QStringLiteral("One"), QStringLiteral("/one")},
                                       {QStringLiteral("Two"), QStringLiteral("/two")}},
                                      {}, MediaToolDisplay{});
        QPushButton* ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        QCOMPARE(dialog.display().drives, QStringList{});
        dialog.driveList()->item(0)->setSelected(false);
        QVERIFY(ok->isEnabled());
        QCOMPARE(dialog.display().drives, QStringList{"/two"});
        dialog.driveList()->clearSelection();
        QVERIFY(!ok->isEnabled());
        dialog.driveList()->item(1)->setSelected(true);
        QVERIFY(ok->isEnabled());
    }

    void mediaToolFiltersLinkedClipsByDrive() {
        QTemporaryDir dir;
        const QString one = dir.filePath(QStringLiteral("one"));
        const QString two = dir.filePath(QStringLiteral("two"));
        const QString nested = dir.filePath(QStringLiteral("one/nested"));
        auto linked = [](const QString& name, const QString& path) {
            BinItem clip;
            clip.id = name;
            clip.name = name;
            clip.kind = BinItemKind::MasterClip;
            clip.durationFrames = 50;
            clip.editRate = Rational(25);
            media::Link link;
            link.path = path;
            link.videoStream = 0;
            link.frames = 50;
            clip.link = link.toJson();
            return clip;
        };
        const QList<BinItem> items{linked(QStringLiteral("A"), one + QStringLiteral("/a.mov")),
                                   linked(QStringLiteral("B"), two + QStringLiteral("/b.mov")),
                                   linked(QStringLiteral("C"), nested + QStringLiteral("/c.mov"))};
        MediaTool tool;
        MediaToolDisplay display;
        tool.setItems(items, display);
        QCOMPARE(tool.names(), (QStringList{"A", "B", "C"}));
        display.knownDrives = {one, two, nested};
        display.drives = {two};
        tool.setItems(items, display);
        QCOMPARE(tool.names(), QStringList{"B"});
        // A file on a volume mounted inside another belongs to the inner one.
        display.drives = {one};
        tool.setItems(items, display);
        QCOMPARE(tool.names(), QStringList{"A"});
    }

    void cancelAsJobFinishes() {
        // Cancel pressed after the work's last progress call: its success stands.
        BackgroundQueue queue;
        std::atomic<bool> go{false};
        std::atomic<bool> returned{false};
        const int id = queue.add(QStringLiteral("Clip"), QStringLiteral("bin"),
                                 [&](const std::function<bool(double)>& progress) {
                                     progress(0.5);
                                     while (!go.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                     returned.store(true);
                                     return media::ConsolidateResult{};
                                 });
        go.store(true);
        while (!returned.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // the thread marks itself done
        queue.cancel(id);
        queue.waitForIdle();
        QCOMPARE(queue.jobs().constFirst().state, BackgroundQueue::State::Completed);
        // A job told to stop is cancelled.
        const int second = queue.add(QStringLiteral("Clip 2"), QStringLiteral("bin"),
                                     [&](const std::function<bool(double)>& progress) {
                                         media::ConsolidateResult r;
                                         while (progress(0.1)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                         r.error = QStringLiteral("Cancelled");
                                         return r;
                                     });
        queue.cancel(second);
        queue.waitForIdle();
        QCOMPARE(queue.jobs().constLast().state, BackgroundQueue::State::Cancelled);
    }
};

QTEST_MAIN(TestAudioMedia)
#include "tst_audio_media.moc"

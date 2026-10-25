// The Audio EQ tool (Editing Guide p.736-745; docs/spec/audio-eq.md): its controls set the EQ of
// the segment under the Record position, Set EQ - In/Out and the Remove EQs items act between the
// marks, Remove Effect takes an EQ off, and Bypass RT EQ keeps it from what plays. The filters and
// the sound are in tests/media/tst_audio_eq.cpp.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_eq.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::AudioEq;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;

namespace {

// Clips a (0-29), b (30-59) and c (60-89) on V1, A1 and A2.
BinItem sequence() {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Cut");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        QList<model::Segment> segments;
        for (const char* id : {"a", "b", "c"}) {
            model::Segment g;
            g.clipId = QString::fromLatin1(id);
            g.sourceTrack = t;
            g.length = 30;
            segments << g;
        }
        seq.splice(t, 0, segments);
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
    AudioEqTool tool{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        timeline.resize(900, 400);
        timeline.show();
        tool.resize(620, 300);
        tool.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        // A1 alone selected.
        for (const QString& t : timeline.patch().selectedRecordTracks(seq())) {
            if (t != QLatin1String("A1")) timeline.toggleRecordTrack(t);
        }
        settle();
    }
    model::Sequence seq() const { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    std::optional<AudioEq> eq(const char* track, int segment) const {
        return seq().track(QString::fromLatin1(track))->segments.at(segment).eq;
    }
    void at(std::int64_t frame) {
        composer.moveTo(MonitorId::Record, frame);
        settle();
    }
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
        for (QAction* a : tool.fastMenu()->actions()) {
            if (a->text() == text) return a;
        }
        return nullptr;
    }
};

} // namespace

class TestAudioEqUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // The controls (p.737-739) set the segment under the position, one undo step each.
    void controls() {
        Rig rig;
        QCOMPARE(rig.tool.track(), QStringLiteral("A1"));
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("A1"));
        QVERIFY(!rig.tool.hasEq());
        // The defaults of the picture on p.737.
        QCOMPARE(rig.tool.bandButton(AudioEqTool::Low)->text(), QStringLiteral("50 Hz"));
        QCOMPARE(rig.tool.bandButton(AudioEqTool::Mid)->text(), QStringLiteral("2 Oct"));
        QCOMPARE(rig.tool.bandButton(AudioEqTool::High)->text(), QStringLiteral("15 kHz"));
        QCOMPARE(rig.tool.rangeReadout()->text(), QStringLiteral("500 Hz"));

        rig.at(40);
        // Typing on a slider: cumulative until Enter (p.739).
        QSlider* low = rig.tool.slider(AudioEqTool::Low);
        low->setFocus();
        QTest::keyClicks(low, QStringLiteral("-12"));
        QCOMPARE(rig.tool.gainBox(AudioEqTool::Low)->text(), QStringLiteral("-12"));
        QVERIFY(!rig.eq("A1", 1));
        QTest::keyClick(low, Qt::Key_Return);
        settle();
        QVERIFY(rig.eq("A1", 1));
        QCOMPARE(rig.eq("A1", 1)->low, -12.0);
        QVERIFY(!rig.eq("A1", 0));
        QVERIFY(rig.tool.hasEq());
        QCOMPARE(low->value(), -120);
        const auto steps = rig.timeline.history().undoCount();

        // The EQ Parameter display, clamped to the range (p.739).
        QLineEdit* high = rig.tool.gainBox(AudioEqTool::High);
        high->setText(QStringLiteral("40"));
        emit high->editingFinished();
        settle();
        QCOMPARE(rig.eq("A1", 1)->high, 15.0);
        QCOMPARE(high->text(), QStringLiteral("15.0"));
        // The 3-band buttons step through their values.
        rig.tool.bandButton(AudioEqTool::Low)->click();
        rig.tool.bandButton(AudioEqTool::Mid)->click();
        rig.tool.bandButton(AudioEqTool::High)->click();
        settle();
        QCOMPARE(rig.eq("A1", 1)->lowTurnover, 80);
        QVERIFY(!rig.eq("A1", 1)->wideMid);
        QCOMPARE(rig.eq("A1", 1)->highTurnover, 6000);
        QCOMPARE(rig.tool.bandButton(AudioEqTool::Mid)->text(), QStringLiteral("1/4 Oct"));
        // The EQ Range slider, log scaled.
        QCOMPARE(AudioEqTool::rangePosition(50), 0);
        QCOMPARE(AudioEqTool::rangePosition(15000), AudioEqTool::kRangeSteps);
        QCOMPARE(AudioEqTool::rangeFrequency(AudioEqTool::rangePosition(866)), 866.0);
        rig.tool.rangeSlider()->setValue(AudioEqTool::rangePosition(2000));
        settle();
        QCOMPARE(rig.eq("A1", 1)->midFrequency, 2000.0);
        QCOMPARE(rig.tool.rangeReadout()->text(), QStringLiteral("2 kHz"));
        // In off: the effect stays, not heard (p.738).
        rig.tool.inButton()->click();
        settle();
        QVERIFY(!rig.eq("A1", 1)->in);
        QCOMPARE(rig.timeline.history().undoCount(), steps + 6);
        // The graph follows.
        QCOMPARE(rig.tool.graph()->eq(), *rig.eq("A1", 1));
        // Undo takes the last step back.
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        settle();
        QVERIFY(rig.eq("A1", 1)->in);
        QVERIFY(rig.tool.inButton()->isChecked());

        // Another segment without one shows the last values; a change there gives it them all.
        rig.at(5);
        QVERIFY(!rig.tool.hasEq());
        QCOMPARE(rig.tool.eq().low, -12.0);
        rig.tool.slider(AudioEqTool::Mid)->setValue(30);
        settle();
        AudioEq want = *rig.eq("A1", 1);
        want.mid = 3;
        QCOMPARE(rig.eq("A1", 0), std::optional(want));

        // Clicking a number on a slider's edge sets it (p.739).
        QWidget* scale = nullptr;
        for (QWidget* w : rig.tool.findChildren<QWidget*>()) {
            if (w->parentWidget() == rig.tool.slider(AudioEqTool::Mid)->parentWidget() && w->width() == 26 &&
                w->geometry().right() < rig.tool.slider(AudioEqTool::Mid)->geometry().left() + 1 &&
                w->geometry().right() > rig.tool.slider(AudioEqTool::Mid)->geometry().left() - 30) {
                scale = w;
            }
        }
        QVERIFY(scale);
        QTest::mouseClick(scale, Qt::LeftButton, {}, QPoint(10, scale->height() - 8));
        settle();
        QCOMPARE(rig.eq("A1", 0)->mid, -15.0);
    }

    // Several selected tracks: plus signs, and the controls set them all (p.737).
    void tracks() {
        Rig rig;
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        settle();
        QCOMPARE(rig.tool.tracks(), (QStringList{"A1", "A2"}));
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("A1 +"));
        QMenu* menu = rig.tool.trackButton()->menu();
        emit menu->aboutToShow();
        QStringList items;
        for (QAction* a : menu->actions()) items << a->text();
        QCOMPARE(items, (QStringList{"+ A1", "+ A2"}));
        rig.at(10);
        rig.tool.slider(AudioEqTool::High)->setValue(-60);
        settle();
        QCOMPARE(rig.eq("A1", 0)->high, -6.0);
        QCOMPARE(rig.eq("A2", 0)->high, -6.0);
        // The menu deselects a track in the Timeline.
        menu->actions().at(0)->trigger();
        settle();
        QCOMPARE(rig.tool.track(), QStringLiteral("A2"));
        QVERIFY(!rig.timeline.patch().isRecordSelected(QStringLiteral("A1")));
        // No audio track: nothing set, with a reason.
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        settle();
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("No track"));
        rig.tool.slider(AudioEqTool::High)->setValue(-90);
        settle();
        QVERIFY(!rig.tool.messageLabel()->text().isEmpty());
    }

    // The Fast menu (p.740-741, picture on p.744).
    void fastMenu() {
        Rig rig;
        QStringList items;
        for (QAction* a : rig.tool.fastMenu()->actions()) {
            if (!a->isSeparator()) items << a->text();
        }
        QCOMPARE(items.mid(0, 3), (QStringList{"Set EQ - In/Out", "Remove EQs On Track - In/Out",
                                               "Remove EQs On Enabled Tracks - In/Out"}));
        // The twelve templates, dimmed until their values are known.
        QCOMPARE(items.size(), 15);
        QCOMPARE(items.at(3), QStringLiteral("Female Voice with Presence"));
        QCOMPARE(items.last(), QStringLiteral("Telephone EQ \"B\""));
        QVERIFY(!rig.fastItem(QStringLiteral("NTSC Hum Buster"))->isEnabled());

        // An EQ on b, then Set EQ - In/Out with no marks: on the whole track (p.740).
        rig.at(40);
        AudioEq e;
        e.low = 6;
        rig.tool.setEq(e);
        settle();
        rig.fastItem(QStringLiteral("Set EQ - In/Out"))->trigger();
        settle();
        QCOMPARE(rig.eq("A1", 0), rig.eq("A1", 1));
        QCOMPARE(rig.eq("A1", 2), rig.eq("A1", 1));
        QVERIFY(!rig.eq("A2", 0));
        // From c, which has one too, with an IN in a: on a to the end; then from a segment without
        // one, between marks: removed from those the marks touch (p.741).
        rig.at(5);
        QVERIFY(rig.timeline.setEq({QStringLiteral("A1")}, std::nullopt, false, QStringLiteral("Remove")));
        settle();
        QVERIFY(!rig.eq("A1", 0));
        rig.mark(40, true);
        rig.mark(70, false);
        rig.fastItem(QStringLiteral("Set EQ - In/Out"))->trigger();
        settle();
        QVERIFY(!rig.eq("A1", 0));
        QVERIFY(!rig.eq("A1", 1));
        QVERIFY(!rig.eq("A1", 2));

        // Remove EQs: on the tool's track, or on every selected one, between the marks.
        rig.composer.state(MonitorId::Record).clearBoth();
        rig.timeline.toggleRecordTrack(QStringLiteral("A2"));
        settle();
        rig.at(40);
        rig.tool.setEq(e);
        rig.fastItem(QStringLiteral("Set EQ - In/Out"))->trigger();
        settle();
        QVERIFY(rig.eq("A1", 0) && rig.eq("A2", 1) && !rig.eq("A2", 0));
        rig.mark(30, true);
        rig.fastItem(QStringLiteral("Remove EQs On Track - In/Out"))->trigger();
        settle();
        QVERIFY(rig.eq("A1", 0));
        QVERIFY(!rig.eq("A1", 1) && !rig.eq("A1", 2));
        QVERIFY(rig.eq("A2", 1));
        rig.composer.state(MonitorId::Record).clearBoth();
        rig.fastItem(QStringLiteral("Remove EQs On Enabled Tracks - In/Out"))->trigger();
        settle();
        QVERIFY(!rig.eq("A1", 0) && !rig.eq("A2", 1));
    }

    // Remove Effect at the position takes the EQ off the selected audio tracks (p.741).
    void removeEffect() {
        Rig rig;
        rig.at(40);
        AudioEq e;
        e.mid = -9;
        rig.tool.setEq(e);
        settle();
        QVERIFY(rig.eq("A1", 1));
        QVERIFY(rig.timeline.removeEffectAtPosition());
        settle();
        QVERIFY(!rig.eq("A1", 1));
        QVERIFY(!rig.tool.hasEq());
        QVERIFY(!rig.timeline.removeEffectAtPosition());
    }

    // Tools > Audio EQ opens the panel, and Bypass RT EQ keeps the EQ from what plays (p.737).
    void mainWindow() {
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
        commands.action(QStringLiteral("tools.audio_eq"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioEQ"))));

        // A sequence of a linked sound file with an EQ on A1.
        const QString wav = dir.filePath(QStringLiteral("tone.wav"));
        {
            media::WriterSettings ws;
            ws.format = QStringLiteral("wav");
            ws.audioCodec = QStringLiteral("pcm_f32le");
            ws.channels = 1;
            media::MediaWriter w;
            QVERIFY(w.open(wav, ws));
            std::vector<float> samples(48000, 0.25f);
            QVERIFY(w.writeAudio(samples.data(), 48000) && w.finish());
        }
        const QString binId = window.binContainer().newBin();
        QVERIFY(window.binContainer().openBin(binId));
        const QStringList ids = window.linkFiles(binId, {wav});
        QCOMPARE(ids.size(), 1);
        BinItem item = sequence();
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment g;
        g.binId = binId;
        g.clipId = ids.first();
        g.sourceTrack = QStringLiteral("A1");
        g.length = 20;
        seq.splice(QStringLiteral("A1"), 0, {g});
        AudioEq e;
        e.low = 3;
        QCOMPARE(model::setEq(seq, QStringLiteral("A1"), 0, 20, e), 1);
        seq.storeIn(item);
        model::LoadedClip loaded;
        loaded.item = item;
        auto anyEq = [&] {
            bool any = false;
            const auto pieces = window.soundOf(loaded, {});
            for (const media::AudioPiece& p : pieces) any = any || p.eq.has_value();
            return !pieces.empty() && any;
        };
        QVERIFY(anyEq());
        QSignalSpy bypass(&window.audioEq(), &AudioEqTool::bypassChanged);
        window.audioEq().bypassButton()->click();
        QCOMPARE(bypass.count(), 1);
        QVERIFY(window.audioEq().bypassed());
        QVERIFY(!anyEq());
    }
};

QTEST_MAIN(TestAudioEqUi)
#include "tst_audio_eq_ui.moc"

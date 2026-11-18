// Surround panning in the Audio Mixer (Editing Guide p.704-710; docs/spec/surround.md): the Pan grids
// in place of the pan knob in a 5.1 or 7.1 sequence, storing a pan as an undo step, Stereo Link and
// Mirror, the Advanced Panner's controls and speaker snapping, Live mode's surround pans, and LFE-only tracks. The pan law and
// playback are in tests/media/tst_surround.cpp.
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/surround_pan.h"
#include "lumenis/ui/audio_tools.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/surround_panner.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QApplication>
#include <QDial>
#include <QHash>
#include <QLabel>
#include <QSignalSpy>
#include <QSlider>
#include <QTest>
#include <QMenu>
#include <QToolButton>

#include <memory>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::AudioFormat;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using lumenis::model::SurroundPan;

namespace {

bool visible(const QWidget* w) {
    return w && w->isVisible();
}

SurroundPan gridPan(const MixerStrip* strip, int channel) {
    const PanGrid* g = strip ? strip->panGrid(channel) : nullptr;
    return g ? g->pan() : SurroundPan{};
}

} // namespace

class TestSurroundUi : public QObject {
    Q_OBJECT

private:
    // A 5.1 sequence with a mono track A1, a stereo track A2 and a 5.1 track A3, each with one
    // segment; the mixer in Clip mode.
    struct Rig {
        Composer composer;
        Timeline timeline{composer};
        AudioMixer mixer{timeline};
        QHash<QString, BinItem> items;

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
            seq.setMixFormat(AudioFormat::Surround51);
            auto add = [&](const char* track, AudioFormat format) {
                model::Segment s;
                s.clipId = clip.id;
                s.binId = QStringLiteral("bin");
                s.name = clip.name;
                s.sourceTrack = QStringLiteral("A1");
                s.length = 20;
                seq.ensureTrack(QString::fromLatin1(track), format)->segments = {s};
            };
            add("A1", AudioFormat::Mono);
            add("A2", AudioFormat::Stereo);
            add("A3", AudioFormat::Surround51);
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

        model::Sequence shown() const {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        }
        QList<SurroundPan> pans(const QString& track) const {
            const model::Sequence s = shown();
            const model::Track* t = s.track(track);
            return t && !t->segments.isEmpty() ? t->segments.first().surround : QList<SurroundPan>{};
        }
        bool lfeOnly(const QString& track) const {
            const model::Sequence s = shown();
            const model::Track* t = s.track(track);
            return t && t->lfeOnly;
        }
    };

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // A grid for each channel of mono and stereo tracks, none for the 5.1 track (p.706).
    void gridsInPlaceOfTheKnob() {
        Rig rig;
        rig.mixer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rig.mixer));
        MixerStrip* mono = rig.mixer.strip(QStringLiteral("A1"));
        MixerStrip* stereo = rig.mixer.strip(QStringLiteral("A2"));
        MixerStrip* five = rig.mixer.strip(QStringLiteral("A3"));
        QVERIFY(mono && stereo && five);
        QVERIFY(visible(mono->panGrid(0)) && !visible(mono->panGrid(1)));
        QVERIFY(!visible(mono->panKnob()));
        QVERIFY(visible(stereo->panGrid(0)) && visible(stereo->panGrid(1)));
        QVERIFY(!visible(five->panGrid(0)));
        QVERIFY(!visible(five->panKnob())); // its channels have their own speakers
        // The defaults: the mono track on its side (odd tracks left), the stereo channels left and right.
        QCOMPARE(gridPan(mono, 0), SurroundPan::at(-100, 100));
        QCOMPARE(gridPan(stereo, 1), SurroundPan::at(100, 100));
        // A stereo sequence shows the knob again.
        QVERIFY(rig.mixer.setSequenceMixFormat(AudioFormat::Stereo));
        QVERIFY(visible(mono->panKnob()));
        QVERIFY(!visible(mono->panGrid(0)));
    }

    // The Timeline's Clip Volume/Pan menu in a 5.1 sequence (p.679-680): Pan L and, for stereo, Pan R > Front, Rear, Front
    // and Rear, Center %; the 5.1 track keeps Pan. A keyframe added on the graph shown is a surround pan keyframe.
    void timelineSurroundGraphs() {
        Rig rig;
        auto submenus = [&](const QString& track) {
            std::unique_ptr<QMenu> menu(rig.timeline.trackAudioDataMenu(track, false, nullptr));
            QStringList out;
            for (QAction* a : menu->actions()) out << (a->menu() ? a->text() + QStringLiteral(">") : a->text());
            return out;
        };
        QCOMPARE(submenus(QStringLiteral("A1")), (QStringList{"Clip Volume", "Volume", "Pan L>"}));
        QCOMPARE(submenus(QStringLiteral("A2")), (QStringList{"Clip Volume", "Volume", "Pan L>", "Pan R>"}));
        QCOMPARE(submenus(QStringLiteral("A3")), (QStringList{"Clip Volume", "Volume", "Pan"}));
        std::unique_ptr<QMenu> menu(rig.timeline.trackAudioDataMenu(QStringLiteral("A2"), false, nullptr));
        QMenu* right = menu->actions().last()->menu();
        QStringList layouts;
        for (QAction* a : right->actions()) layouts << a->text();
        QCOMPARE(layouts, (QStringList{"Front", "Rear", "Front and Rear", "Center %"}));
        right->actions().at(1)->trigger();
        const model::Sequence seq = rig.shown();
        QCOMPARE(rig.timeline.automationShown(*seq.track(QStringLiteral("A2"))), model::AutomationParam::RearR);
        QCOMPARE(seq.track(QStringLiteral("A2"))->showSurround, QStringLiteral("R.rear"));
        // Add Keyframe there holds the pan that plays: the R channel's rear at the right.
        QVERIFY2(rig.timeline.addKeyframeAt(QStringLiteral("A2"), 5), qPrintable(rig.timeline.lastMessage()));
        const model::Segment s = rig.shown().track(QStringLiteral("A2"))->segments.first();
        QCOMPARE(s.surroundKeys.size(), 2);
        QCOMPARE(s.surroundKeys.at(1).rear, (QList<model::AudioKey>{{5, 100}}));
        // Its value as the mixer reads it.
        rig.composer.moveTo(MonitorId::Record, 5);
        QCOMPARE(rig.timeline.automationAt(QStringLiteral("A2"), model::AutomationParam::RearR), 100.0);
    }

    // Auto mode on the Pan grids (p.706, p.716-717): a move sets surround pan automation at the position as one undo
    // step; while recording, the members moved are written as the position moves; Remove Pan on Track takes them away.
    void autoModeGrids() {
        Rig rig;
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        rig.mixer.show();
        MixerStrip* mono = rig.mixer.strip(QStringLiteral("A1"));
        QVERIFY(mono && mono->panGrid(0) && mono->panGrid(0)->isEnabled());
        rig.composer.moveTo(MonitorId::Record, 5);
        const int steps = static_cast<int>(rig.timeline.history().undoCount());
        rig.mixer.setSurroundPan(QStringLiteral("A1"), 0, SurroundPan::at(30, -40), true);
        QCOMPARE(static_cast<int>(rig.timeline.history().undoCount()), steps + 1);
        QCOMPARE(rig.timeline.surroundPansAt(QStringLiteral("A1")).first(), SurroundPan::at(30, -40));
        model::Segment seg = rig.shown().track(QStringLiteral("A1"))->segments.first();
        QCOMPARE(seg.surroundKeys.size(), 1);
        QCOMPARE(seg.surroundKeys.first().frontRear, (QList<model::AudioKey>{{5, -40}}));
        QVERIFY(seg.surroundKeys.first().center.isEmpty()); // unchanged members get none
        QVERIFY(seg.surround.isEmpty());                    // the clip pan stays as it was
        // Recording: a move to the left at frame 10, held through 12.
        rig.composer.moveTo(MonitorId::Record, 10);
        rig.mixer.startRecording(false);
        rig.mixer.setSurroundPan(QStringLiteral("A1"), 0, SurroundPan::at(-50, -40), false);
        rig.composer.moveTo(MonitorId::Record, 11);
        rig.composer.moveTo(MonitorId::Record, 12);
        rig.mixer.stopRecording(false);
        seg = rig.shown().track(QStringLiteral("A1"))->segments.first();
        QCOMPARE(seg.surroundKeys.first().front, (QList<model::AudioKey>{{5, 30}, {10, -50}, {11, -50}, {12, -50}}));
        QCOMPARE(seg.surroundKeys.first().frontRear, (QList<model::AudioKey>{{5, -40}})); // not moved, not written
        QCOMPARE(static_cast<int>(rig.timeline.history().undoCount()), steps + 2);
        // A stereo track's other channel follows the link.
        rig.mixer.setStereoLink(model::StereoLink::Linked);
        rig.mixer.setSurroundPan(QStringLiteral("A2"), 0, SurroundPan::at(-100, 0), true);
        const QList<SurroundPan> pans = rig.timeline.surroundPansAt(QStringLiteral("A2"));
        QCOMPARE(pans.size(), 2);
        QCOMPARE(pans.at(1).frontRear, 0);
        // Remove Pan on Track in a 5.1 sequence takes the surround pan keyframes too.
        QVERIFY2(rig.timeline.editAutomation(Timeline::AutomationEdit::Remove, model::AutomationParam::Pan),
                 qPrintable(rig.timeline.lastMessage()));
        QVERIFY(rig.shown().track(QStringLiteral("A1"))->segments.first().surroundKeys.isEmpty());
    }

    // Live mode (p.724-726; docs/spec/audio-mixer.md "Live mode"): the grids set the track's Live Mix surround
    // pans, heard at once in place of the segments' and their automation, never stored; Set Live Mix as Automation
    // writes them as surround pan keyframes.
    void liveModeGrids() {
        Rig rig;
        rig.mixer.setMode(AudioMixer::Mode::Live);
        rig.mixer.show();
        rig.composer.moveTo(MonitorId::Record, 5);
        MixerStrip* mono = rig.mixer.strip(QStringLiteral("A1"));
        QVERIFY(mono && mono->panGrid(0) && mono->panGrid(0)->isEnabled());
        const int steps = static_cast<int>(rig.timeline.history().undoCount());
        QSignalSpy live(&rig.mixer, &AudioMixer::liveMixChanged);
        rig.mixer.setSurroundPan(QStringLiteral("A1"), 0, SurroundPan::at(30, -40), false);
        QCOMPARE(live.count(), 1); // heard while dragging
        QVERIFY(rig.mixer.hasLiveSurround(QStringLiteral("A1")));
        QCOMPARE(rig.mixer.liveSurround(QStringLiteral("A1")).first(), SurroundPan::at(30, -40));
        QCOMPARE(static_cast<int>(rig.timeline.history().undoCount()), steps); // not in the sequence
        QVERIFY(rig.pans(QStringLiteral("A1")).isEmpty());
        // The stereo link moves the other channel.
        rig.mixer.setStereoLink(model::StereoLink::Mirror);
        rig.mixer.setSurroundPan(QStringLiteral("A2"), 0, SurroundPan::at(-80, 100), true);
        QCOMPARE(rig.mixer.liveSurround(QStringLiteral("A2")),
                 (QList<SurroundPan>{SurroundPan::at(-80, 100), SurroundPan::at(80, 100)}));
        QCOMPARE(gridPan(rig.mixer.strip(QStringLiteral("A2")), 1), SurroundPan::at(80, 100));

        // What plays: each channel's piece takes its Live Mix surround pan, without its automation.
        std::vector<media::AudioPiece> pieces(3);
        pieces[0].track = QStringLiteral("A1");
        pieces[1].track = QStringLiteral("A2");
        pieces[2].track = media::channelTrackKey(QStringLiteral("A2"), 1);
        for (media::AudioPiece& p : pieces) {
            p.surroundMix = AudioFormat::Surround51;
            p.surround = media::surroundGains(SurroundPan{}, AudioFormat::Surround51);
            p.surroundKeys.front << model::AudioKey{0, 50};
        }
        rig.mixer.applyLiveMix(pieces);
        QCOMPARE(pieces[0].surroundPan, SurroundPan::at(30, -40));
        QCOMPARE(pieces[1].surroundPan, SurroundPan::at(-80, 100));
        QCOMPARE(pieces[2].surroundPan, SurroundPan::at(80, 100));
        QVERIFY(pieces[2].surroundKeys.front.isEmpty());
        QVERIFY(*pieces[2].surround == media::surroundGains(SurroundPan::at(80, 100), AudioFormat::Surround51));
        // Not in Clip mode.
        std::vector<media::AudioPiece> clip(1);
        clip[0].track = QStringLiteral("A1");
        clip[0].surround = media::surroundGains(SurroundPan{}, AudioFormat::Surround51);
        rig.mixer.setMode(AudioMixer::Mode::Clip);
        rig.mixer.applyLiveMix(clip);
        QCOMPARE(clip[0].surroundPan, SurroundPan{});
        // Back in Live mode the grids show the Live Mix again.
        rig.mixer.setMode(AudioMixer::Mode::Live);
        QCOMPARE(gridPan(mono, 0), SurroundPan::at(30, -40));

        // Set Live Mix as Automation on the selected A1, without marks: the segment holds the pan's automated members.
        for (const char* t : {"A1", "A2", "A3"}) rig.timeline.patch().setRecordSelected(QString::fromLatin1(t), t[1] == '1');
        QVERIFY2(rig.mixer.setLiveMixAsAutomation(), qPrintable(rig.timeline.lastMessage()));
        const model::Segment seg = rig.shown().track(QStringLiteral("A1"))->segments.first();
        QCOMPARE(seg.surroundKeys.size(), 1);
        QCOMPARE(seg.surroundKeys.first().front, (QList<model::AudioKey>{{0, 30}, {19, 30}}));
        QCOMPARE(seg.surroundKeys.first().frontRear, (QList<model::AudioKey>{{0, -40}, {19, -40}}));
        // Set Live Mix to Default forgets them; Set Live Mix to Automation takes the automation's back.
        rig.mixer.setLiveMixToAutomation();
        QCOMPARE(rig.mixer.liveSurround(QStringLiteral("A1")).first().front, 30);
    }

    // A finished drag stores the pan as one undo step; Stereo Link moves the other channel.
    void storingPans() {
        Rig rig;
        rig.mixer.setSurroundPan(QStringLiteral("A1"), 0, SurroundPan::at(30, -40), false);
        QVERIFY(rig.pans(QStringLiteral("A1")).isEmpty()); // not until done
        rig.mixer.setSurroundPan(QStringLiteral("A1"), 0, SurroundPan::at(30, -40), true);
        QCOMPARE(rig.pans(QStringLiteral("A1")), QList<SurroundPan>{SurroundPan::at(30, -40)});
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(rig.pans(QStringLiteral("A1")).isEmpty());
        // Mirror: moving the left channel right moves the right channel left.
        rig.mixer.setStereoLink(model::StereoLink::Mirror);
        rig.mixer.setSurroundPan(QStringLiteral("A2"), 0, SurroundPan::at(-80, 100), true);
        QCOMPARE(rig.pans(QStringLiteral("A2")), (QList<SurroundPan>{SurroundPan::at(-80, 100), SurroundPan::at(80, 100)}));
        rig.mixer.setStereoLink(model::StereoLink::Linked);
        rig.mixer.setSurroundPan(QStringLiteral("A2"), 0, SurroundPan::at(-80, 0), true);
        QCOMPARE(rig.pans(QStringLiteral("A2")).value(1), SurroundPan::at(80, 0));
        // Not in Auto mode (ours).
        rig.mixer.setMode(AudioMixer::Mode::Auto);
        rig.mixer.setSurroundPan(QStringLiteral("A1"), 0, SurroundPan::at(0, 0), true);
        QVERIFY(rig.pans(QStringLiteral("A1")).isEmpty());
    }

    // The Advanced Panner: its knobs, LFE slider and speaker snapping (p.705-710).
    void advancedPanner() {
        Rig rig;
        QVERIFY(!rig.mixer.openAdvancedPanner(QStringLiteral("A3"))); // a 5.1 track has no panner
        AdvancedPanner* p = rig.mixer.openAdvancedPanner(QStringLiteral("A1"));
        QVERIFY(p);
        QCOMPARE(p->channels(), 1);
        QVERIFY(!visible(p->sideDial())); // 7.1 only
        QVERIFY(!p->linkButton()->isEnabled());
        p->frontDial()->setValue(-50);
        QCOMPARE(rig.pans(QStringLiteral("A1")).value(0).front, -50);
        p->frontRearDial()->setValue(-100);
        QCOMPARE(rig.pans(QStringLiteral("A1")).value(0).frontRear, -100);
        const QLabel* frValue = p->findChild<QLabel*>(QStringLiteral("frontRearValue"));
        QVERIFY(frValue);
        QCOMPARE(frValue->text(), QStringLiteral("R100"));
        p->lfeSlider()->setValue(30);
        QCOMPARE(rig.pans(QStringLiteral("A1")).value(0).lfe, 30);
        // Double-clicking the right rear speaker snaps the cursor there.
        PanGrid* grid = p->grid(0);
        QVERIFY(grid);
        grid->resize(200, 200);
        QTest::mouseDClick(grid, Qt::LeftButton, Qt::NoModifier, grid->pointFor(100, -100).toPoint());
        const SurroundPan snapped = rig.pans(QStringLiteral("A1")).value(0);
        QCOMPARE(snapped.x(), 100.0);
        QCOMPARE(snapped.frontRear, -100);
        QCOMPARE(snapped.lfe, 30); // the percentages stay
        // A stereo track's panner has two grids and the link buttons, which set the mixer's link.
        AdvancedPanner* two = rig.mixer.openAdvancedPanner(QStringLiteral("A2"));
        QVERIFY(two && two->channels() == 2);
        QToolButton* link = two->linkButton();
        QVERIFY(link);
        link->click();
        QCOMPARE(rig.mixer.stereoLink(), model::StereoLink::Linked);
        two->movePan(1, SurroundPan::at(100, -100), true);
        QCOMPARE(rig.pans(QStringLiteral("A2")), (QList<SurroundPan>{SurroundPan::at(-100, -100), SurroundPan::at(100, -100)}));
    }

    // "This track is LFE only" (p.710): the strip shows LFE and no panner; undo takes it back.
    void lfeOnly() {
        Rig rig;
        rig.mixer.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rig.mixer));
        QVERIFY(rig.timeline.setLfeOnly(QStringLiteral("A1"), true));
        QVERIFY(rig.lfeOnly(QStringLiteral("A1")));
        MixerStrip* mono = rig.mixer.strip(QStringLiteral("A1"));
        QVERIFY(visible(mono->findChild<QLabel*>(QStringLiteral("lfeOnly"))));
        QVERIFY(!visible(mono->panGrid(0)));
        QVERIFY(!rig.mixer.openAdvancedPanner(QStringLiteral("A1")));
        QVERIFY(!rig.timeline.setLfeOnly(QStringLiteral("A2"), true)); // stereo
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(!rig.lfeOnly(QStringLiteral("A1")));
    }
};

QTEST_MAIN(TestSurroundUi)
#include "tst_surround_ui.moc"

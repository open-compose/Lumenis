// Surround panning in the Audio Mixer (Editing Guide p.704-710; docs/spec/surround.md): the Pan grids
// in place of the pan knob in a 5.1 or 7.1 sequence, storing a pan as an undo step, Stereo Link and
// Mirror, the Advanced Panner's controls and speaker snapping, and LFE-only tracks. The pan law and
// playback are in tests/media/tst_surround.cpp.
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

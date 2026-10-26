// Audio Track Effects in the UI (Editing Guide p.790-795; docs/spec/track-effects.md): inserts set,
// moved, copied and removed through the Timeline with undo; the track button's menu; the Audio Track
// Effect tool; the Effect Palette's Audio Track tab and drops; and what MainWindow plays. The effects
// and the mix are in tests/media/tst_audio_fx.cpp.
#include "lumenis/ext/provider.h"
#include "lumenis/media/audio_fx.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_track_effect.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QSignalSpy>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using lumenis::model::BinItem;
using lumenis::model::BinItemKind;
using lumenis::model::TrackInsert;

namespace {

const QString kReverb = QStringLiteral("builtin.reverb");
const QString kDelay = QStringLiteral("builtin.delay");

BinItem sequence() {
    BinItem s;
    s.id = model::newItemId();
    s.name = QStringLiteral("Mix");
    s.kind = BinItemKind::Sequence;
    s.editRate = Rational(25);
    model::Sequence seq = model::Sequence::withDefaultTracks();
    for (const QString& t : seq.trackNames()) {
        model::Segment a;
        a.clipId = QStringLiteral("a");
        a.sourceTrack = t;
        a.length = 50;
        seq.splice(t, 0, {a});
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
    AudioTrackEffectTool tool{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        timeline.resize(900, 400);
        timeline.show();
        tool.resize(460, 360);
        tool.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        settle();
    }
    model::Sequence seq() const { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    std::optional<TrackInsert> insert(const char* track, int slot) const {
        return seq().track(QString::fromLatin1(track))->inserts[static_cast<std::size_t>(slot)];
    }
};

QAction* find(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
    }
    return nullptr;
}

} // namespace

class TestTrackEffects : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // Set, move, copy and remove (p.791-795), an undo step each; refusals.
    void timelineInserts() {
        Rig rig;
        const auto steps = rig.timeline.history().undoCount();
        const TrackInsert reverb = Timeline::newInsert(kReverb);
        QCOMPARE(reverb.name, QStringLiteral("Reverb"));
        QCOMPARE(reverb.params.value(QStringLiteral("mix")), 25.0);
        QVERIFY(rig.timeline.setTrackInsert(QStringLiteral("A1"), 0, reverb, QStringLiteral("x")));
        QCOMPARE(rig.insert("A1", 0), std::optional(reverb));
        QCOMPARE(rig.timeline.trackInsert(QStringLiteral("A1"), 0), std::optional(reverb));
        // Saved with the sequence.
        QVERIFY(rig.seq().toJson().value(QStringLiteral("tracks")).toArray().at(1).toObject().contains(QStringLiteral("inserts")));
        // Already so: no step.
        QVERIFY(rig.timeline.setTrackInsert(QStringLiteral("A1"), 0, reverb, QStringLiteral("x")));
        QCOMPARE(rig.timeline.history().undoCount(), steps + 1);
        // Move to A2 insert c, replacing what is there; copy back with Alt (p.795).
        QVERIFY(rig.timeline.setTrackInsert(QStringLiteral("A2"), 2, Timeline::newInsert(kDelay), QStringLiteral("x")));
        QVERIFY(rig.timeline.moveTrackInsert(QStringLiteral("A1"), 0, QStringLiteral("A2"), 2, false));
        QVERIFY(!rig.insert("A1", 0));
        QCOMPARE(rig.insert("A2", 2), std::optional(reverb));
        QVERIFY(rig.timeline.moveTrackInsert(QStringLiteral("A2"), 2, QStringLiteral("A1"), 4, true));
        QCOMPARE(rig.insert("A1", 4), std::optional(reverb));
        QCOMPARE(rig.insert("A2", 2), std::optional(reverb));
        // Remove.
        QVERIFY(rig.timeline.setTrackInsert(QStringLiteral("A2"), 2, std::nullopt, QStringLiteral("x")));
        QVERIFY(!rig.insert("A2", 2));
        QCOMPARE(rig.timeline.history().undoCount(), steps + 5);
        QVERIFY(rig.timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(rig.insert("A2", 2), std::optional(reverb));
        // Refusals: a video track, a slot out of range, a locked track, nothing to move.
        QVERIFY(!rig.timeline.setTrackInsert(QStringLiteral("V1"), 0, reverb, QStringLiteral("x")));
        QVERIFY(!rig.timeline.setTrackInsert(QStringLiteral("A1"), 5, reverb, QStringLiteral("x")));
        QVERIFY(!rig.timeline.moveTrackInsert(QStringLiteral("A1"), 1, QStringLiteral("A2"), 0, false));
        rig.timeline.setTracksLocked({QStringLiteral("A2")}, true);
        QVERIFY(!rig.timeline.setTrackInsert(QStringLiteral("A2"), 0, reverb, QStringLiteral("x")));
        QVERIFY(rig.timeline.lastMessage().contains(QStringLiteral("locked")));
        // The Clipboard does not carry them.
        QVERIFY(!rig.seq().excerpt({QStringLiteral("A1")}, 0, 10).track(QStringLiteral("A1"))->hasInserts());
    }

    // Right-click a track button: Audio Track Effects n > Insert a-e > effect, and the tool (p.791, p.793).
    void trackMenu() {
        Rig rig;
        QMenu* menu = rig.timeline.contextMenu(QStringLiteral("A2"), std::nullopt);
        QMenu* fx = menu->findChild<QMenu*>(QStringLiteral("audioTrackEffects"));
        QVERIFY(fx);
        QCOMPARE(fx->title(), QStringLiteral("Audio Track Effects 2"));
        QCOMPARE(fx->actions().size(), 5);
        QMenu* insertB = fx->actions().at(1)->menu();
        QCOMPARE(insertB->title(), QStringLiteral("Insert b"));
        QVERIFY(!find(insertB, QStringLiteral("No Insert"))->isEnabled());
        QMenu* reverbCategory = find(insertB, QStringLiteral("Reverb"))->menu();
        find(reverbCategory, QStringLiteral("Reverb"))->trigger();
        settle();
        QCOMPARE(rig.insert("A2", 1)->effect, kReverb);
        delete menu;
        // Now named, and No Insert removes it.
        menu = rig.timeline.contextMenu(QStringLiteral("A2"), std::nullopt);
        insertB = menu->findChild<QMenu*>(QStringLiteral("audioTrackEffects"))->actions().at(1)->menu();
        QCOMPARE(insertB->title(), QStringLiteral("Insert b (Reverb)"));
        QSignalSpy tool(&rig.timeline, &Timeline::trackEffectToolRequested);
        find(menu, QStringLiteral("Audio Track Effect Tool"))->trigger();
        QCOMPARE(tool.count(), 1);
        QCOMPARE(tool.first().first().toString(), QStringLiteral("A2"));
        find(insertB, QStringLiteral("No Insert"))->trigger();
        QVERIFY(!rig.insert("A2", 1));
        delete menu;
        // Video tracks have no such menu.
        menu = rig.timeline.contextMenu(QStringLiteral("V1"), std::nullopt);
        QVERIFY(!menu->findChild<QMenu*>(QStringLiteral("audioTrackEffects")));
        delete menu;
    }

    // The tool (p.793-795).
    void tool() {
        Rig rig;
        QCOMPARE(rig.tool.track(), QStringLiteral("A1"));
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("A1"));
        QCOMPARE(rig.tool.insertButton()->text(), QStringLiteral("Insert a"));
        QCOMPARE(rig.tool.effectButton()->text(), QStringLiteral("No Effect"));
        // Select Effect.
        QVERIFY(rig.tool.setEffect(kDelay));
        QCOMPARE(rig.tool.effectButton()->text(), QStringLiteral("Delay"));
        QSlider* time = rig.tool.paramSlider(QStringLiteral("time"));
        QVERIFY(time);
        QCOMPARE(rig.tool.paramValue(QStringLiteral("time"))->text(), QStringLiteral("250 ms"));
        // A drag previews (what plays) without an undo step; letting go makes one.
        const auto steps = rig.timeline.history().undoCount();
        QSignalSpy preview(&rig.tool, &AudioTrackEffectTool::previewChanged);
        QVERIFY(rig.tool.setParam(QStringLiteral("time"), 500, false));
        QCOMPARE(preview.count(), 1);
        QVERIFY(rig.tool.preview());
        QCOMPARE(rig.tool.preview()->insert.params.value(QStringLiteral("time")), 500.0);
        QCOMPARE(rig.insert("A1", 0)->params.value(QStringLiteral("time")), 250.0);
        QCOMPARE(rig.timeline.history().undoCount(), steps);
        QVERIFY(rig.tool.setParam(QStringLiteral("time"), 500, true));
        QVERIFY(!rig.tool.preview());
        QCOMPARE(rig.insert("A1", 0)->params.value(QStringLiteral("time")), 500.0);
        QCOMPARE(rig.timeline.history().undoCount(), steps + 1);
        QCOMPARE(rig.tool.paramValue(QStringLiteral("time"))->text(), QStringLiteral("500 ms"));
        // The slider sets values too, clamped.
        time->setValue(time->maximum());
        settle();
        QCOMPARE(rig.insert("A1", 0)->params.value(QStringLiteral("time")), 2000.0);
        // Bypass: saved on the insert; Ctrl+Bypass: every track, not saved (p.794).
        rig.tool.bypass(false);
        QVERIFY(rig.insert("A1", 0)->bypass);
        QVERIFY(rig.tool.bypassButton()->isChecked());
        rig.tool.bypass(false);
        QVERIFY(!rig.insert("A1", 0)->bypass);
        rig.tool.bypass(true);
        QVERIFY(rig.tool.bypassedAll());
        QVERIFY(!rig.insert("A1", 0)->bypass);
        rig.tool.bypass(true);
        QVERIFY(!rig.tool.bypassedAll());
        // Copy and Paste Settings: onto the same effect only.
        rig.tool.copySettings();
        rig.tool.select(QStringLiteral("A2"), 1);
        QVERIFY(!rig.tool.pasteSettings());
        QVERIFY(rig.tool.setEffect(kDelay));
        QVERIFY(rig.tool.pasteSettings());
        QCOMPARE(rig.insert("A2", 1)->params.value(QStringLiteral("time")), 2000.0);
        // The arrow keys: up and down the tracks, left and right the inserts (p.794).
        rig.tool.setFocus();
        QTest::keyClick(&rig.tool, Qt::Key_Up);
        QCOMPARE(rig.tool.track(), QStringLiteral("A1"));
        QTest::keyClick(&rig.tool, Qt::Key_Left);
        QCOMPARE(rig.tool.slot(), 0);
        QCOMPARE(rig.tool.effectButton()->text(), QStringLiteral("Delay"));
        QTest::keyClick(&rig.tool, Qt::Key_Right);
        QCOMPARE(rig.tool.slot(), 1);
        QCOMPARE(rig.tool.effectButton()->text(), QStringLiteral("No Effect"));
        QVERIFY(!rig.tool.paramSlider(QStringLiteral("time")));
        // Select Insert > No Insert removes it (p.795).
        rig.tool.select(QStringLiteral("A1"), 0);
        emit rig.tool.insertButton()->menu()->aboutToShow();
        find(rig.tool.insertButton()->menu(), QStringLiteral("No Insert"))->trigger();
        QVERIFY(!rig.insert("A1", 0));
        // An effect not installed: named, "Unavailable Effect" (p.790).
        rig.timeline.setTrackInsert(QStringLiteral("A1"), 3, TrackInsert{QStringLiteral("gone.plugin"), QStringLiteral("Gone"), {}, false},
                                    QStringLiteral("x"));
        rig.tool.select(QStringLiteral("A1"), 3);
        QCOMPARE(rig.tool.effectButton()->text(), QStringLiteral("Gone"));
        QVERIFY(rig.tool.messageLabel()->text().startsWith(QStringLiteral("Unavailable Effect: Gone")));
    }

    // The Effect Palette's Audio Track tab, its drags and double-clicks (p.791-792).
    void palette() {
        EffectPalette palette;
        int tab = -1;
        for (int i = 0; i < palette.tabBar()->count(); ++i) {
            if (palette.tabBar()->tabText(i) == QLatin1String("Audio Track")) tab = i;
        }
        QVERIFY(tab >= 0);
        palette.setTab(tab);
        QStringList categories;
        for (int i = 0; i < palette.categoryList()->count(); ++i) categories << palette.categoryList()->item(i)->text();
        QCOMPARE(categories.mid(0, 5), (QStringList{"All", "Dynamics", "EQ", "Delay", "Modulation"}));
        palette.setCategory(QStringLiteral("Dynamics"));
        QCOMPARE(palette.shownEffects(), (QStringList{"Compressor/Limiter", "Expander/Gate", "De-Esser"}));
        const auto effect = palette.effectAt(0);
        QVERIFY(effect);
        QCOMPARE(effect->audioEffect, QStringLiteral("builtin.compressor"));
        // Through a drag and back.
        std::unique_ptr<QMimeData> mime(effectMime(*effect));
        const auto back = effectFromMime(mime.get());
        QVERIFY(back);
        QCOMPARE(back->audioEffect, effect->audioEffect);
        // The video tabs do not list them.
        palette.setTab(0);
        QVERIFY(!palette.shownEffects().contains(QStringLiteral("Compressor/Limiter")));

        // Dropped on A1: the Select Insert dialog picks insert d.
        Rig rig;
        rig.timeline.setSelectInsertAsk([](const QString& track) -> std::optional<int> {
            return track == QLatin1String("A1") ? std::optional(3) : std::nullopt;
        });
        QVERIFY(rig.timeline.dropTrackEffect(QStringLiteral("A1"), effect->audioEffect));
        QCOMPARE(rig.insert("A1", 3)->effect, QStringLiteral("builtin.compressor"));
        QVERIFY(!rig.timeline.dropTrackEffect(QStringLiteral("A2"), effect->audioEffect));
        // Double-clicked: the first empty insert of each selected audio track.
        for (const QString& t : rig.timeline.patch().selectedRecordTracks(rig.seq())) {
            if (t != QLatin1String("A1") && t != QLatin1String("A2")) rig.timeline.toggleRecordTrack(t);
        }
        QVERIFY(rig.timeline.applyEffectToSelection(*effect));
        QCOMPARE(rig.insert("A1", 0)->effect, QStringLiteral("builtin.compressor"));
        QCOMPARE(rig.insert("A2", 0)->effect, QStringLiteral("builtin.compressor"));
        QVERIFY(!rig.insert("A1", 1));
    }

    // Tools > Audio Track Effect, the track button's tool item, and what plays (insertsOf).
    void mainWindow() {
        QTemporaryDir dir;
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
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        commands.action(QStringLiteral("tools.audio_track_effect"))->trigger();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioTrackEffect"))));

        BinItem item = sequence();
        model::Sequence seq = model::Sequence::fromItem(item);
        seq.track(QStringLiteral("A1"))->inserts[1] = Timeline::newInsert(kDelay);
        seq.track(QStringLiteral("A2"))->inserts[0] = Timeline::newInsert(kReverb);
        seq.track(QStringLiteral("A2"))->inserts[0]->bypass = true;
        seq.storeIn(item);
        window.composer().load(MonitorId::Record, QStringLiteral("bin"), item);
        settle();
        model::LoadedClip loaded;
        loaded.item = item;
        media::TrackInserts plays = window.insertsOf(loaded);
        QCOMPARE(plays.size(), 1);
        QCOMPARE(plays.at(QStringLiteral("A1")).first().effect, kDelay);
        // A value being dragged plays in place of the saved one.
        AudioTrackEffectTool& tool = window.audioTrackEffect();
        tool.select(QStringLiteral("A1"), 1);
        QVERIFY(tool.setParam(QStringLiteral("mix"), 80, false));
        plays = window.insertsOf(loaded);
        QCOMPARE(plays.at(QStringLiteral("A1")).first().params.value(QStringLiteral("mix")), 80.0);
        // Ctrl+Bypass: nothing.
        tool.bypass(true);
        QVERIFY(window.insertsOf(loaded).empty());
        tool.bypass(true);
        // The track button's tool item opens the tool on that track.
        emit window.timeline().trackEffectToolRequested(QStringLiteral("A2"));
        QCOMPARE(tool.track(), QStringLiteral("A2"));
        QCOMPARE(tool.slot(), 0);
    }
};

QTEST_MAIN(TestTrackEffects)
#include "tst_track_effects.moc"

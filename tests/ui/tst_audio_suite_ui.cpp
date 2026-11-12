// The AudioSuite tool (Editing Guide p.796-800; docs/spec/audiosuite.md): the Track Selection and
// Plug-In Selection menus put a plug-in on the segments under the Record position, Activate opens
// its dialog (OK and Render), Render Effect renders through the renderer, the Fast menu's items work
// over the marks, Remove Effect takes an effect off; in the application, Tools > AudioSuite renders
// into managed media that plays, and Audio Mixdown renders what is not rendered first (p.800). Master
// clip processing (p.800-804): dropped clips, the controls, new master clips with their templates,
// Find Source From Effect, templates in the Fast menu and dropped in bins. The processes and the
// sound are in tests/media/tst_audio_suite.cpp.
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/audio_suite.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/mixdown_dialog.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
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
    AudioSuiteTool tool{timeline};
    Rig() {
        timeline.setSequenceStore([](const QString&, const BinItem&, bool) { return true; });
        timeline.resize(900, 400);
        timeline.show();
        tool.resize(480, 160);
        tool.show();
        composer.load(MonitorId::Record, QStringLiteral("bin"), sequence());
        tool.chooseTrack(QStringLiteral("A1"), false);
        settle();
    }
    model::Sequence seq() const { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); }
    std::optional<model::AudioSuiteEffect> effect(const char* track, int segment) const {
        return seq().track(QString::fromLatin1(track))->segments.at(segment).audioSuite;
    }
    void at(std::int64_t frame) {
        composer.moveTo(MonitorId::Record, frame);
        settle();
    }
    void mark(std::optional<std::int64_t> in, std::optional<std::int64_t> out) {
        model::MonitorState& state = composer.state(MonitorId::Record);
        const std::int64_t was = state.position();
        state.clearBoth();
        if (in) {
            state.setPosition(*in);
            state.markIn();
        }
        if (out) {
            state.setPosition(*out);
            state.markOut();
        }
        state.setPosition(was);
        settle();
    }
    QAction* fastItem(const QString& text) {
        for (QAction* a : tool.fastMenu()->actions()) {
            if (a->text() == text) return a;
        }
        return nullptr;
    }
};

// A renderer that makes no media: it notes the segments and says they are rendered.
struct FakeRenderer {
    QStringList seen;
    QString failWith;
    AudioSuiteTool::Renderer renderer() {
        return [this](const model::Segment& s, const QString& drive, QString* error) -> std::optional<model::AudioSuiteEffect> {
            seen << s.clipId + QLatin1Char('@') + drive;
            if (!failWith.isEmpty()) {
                *error = failWith;
                return std::nullopt;
            }
            model::AudioSuiteEffect e = *s.audioSuite;
            e.rendered = QStringLiteral("/rendered/") + s.clipId;
            e.renderedKey = model::audioSuiteKey(s);
            return e;
        };
    }
};

// A master clip with picture and two sound tracks, in no bin of its own.
BinItem toneClip(const char* id = "tone") {
    BinItem c;
    c.id = QString::fromLatin1(id);
    c.name = QStringLiteral("Tone");
    c.kind = BinItemKind::MasterClip;
    c.editRate = Rational(25);
    c.durationFrames = 100;
    c.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1-2"));
    return c;
}

// Media Composer's names differ by platform (p.803-804).
constexpr bool kMac =
#ifdef Q_OS_MACOS
    true;
#else
    false;
#endif

} // namespace

class TestAudioSuiteUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void controls() {
        Rig rig;
        QCOMPARE(rig.tool.tracks(), QStringList{QStringLiteral("A1")});
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("A1"));
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("no Audio Processing effect")));
        QVERIFY(!rig.tool.activateButton()->isEnabled());
        // The picture's controls: the ones that come later are there, dimmed.
        for (const char* name : {"effectIcon", "nestedLevel"}) {
            auto* b = rig.tool.findChild<QToolButton*>(QString::fromLatin1(name));
            QVERIFY2(b && !b->isEnabled(), name);
        }
        // Audio Loop Play works (p.712; tests/ui/tst_audio_mixer.cpp).
        auto* loop = rig.tool.findChild<QToolButton*>(QStringLiteral("audioLoopPlay"));
        QVERIFY(loop && loop->isEnabled() && loop->isCheckable());
        // The Plug-In Selection menu: the processes, the effects by category, and None.
        QMenu* menu = rig.tool.pluginButton()->menu();
        emit menu->aboutToShow();
        QStringList groups;
        for (QAction* a : menu->actions()) {
            if (a->menu()) groups << a->text();
        }
        QVERIFY(groups.contains(QLatin1String("Process")));
        QVERIFY(groups.contains(QLatin1String("Dynamics")));
        QCOMPARE(menu->actions().last()->text(), QStringLiteral("None"));
        // Choosing in the menu applies the plug-in.
        rig.at(35);
        for (QAction* g : menu->actions()) {
            if (!g->menu()) continue;
            for (QAction* a : g->menu()->actions()) {
                if (a->text() == QLatin1String("Reverse")) a->trigger();
            }
        }
        QCOMPARE(rig.effect("A1", 1)->effect, QStringLiteral("suite.reverse"));
        QCOMPARE(rig.tool.pluginButton()->text(), QStringLiteral("Reverse"));
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("not rendered")));

        // The Track Selection Menu: a track alone, or added with Shift (p.797).
        rig.tool.chooseTrack(QStringLiteral("A2"), true);
        QCOMPARE(rig.tool.tracks(), (QStringList{"A1", "A2"}));
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("A1 +"));
        rig.tool.chooseTrack(QStringLiteral("A2"), false);
        QCOMPARE(rig.tool.tracks(), QStringList{QStringLiteral("A2")});
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("no Audio Processing effect")));
    }

    void pluginOnSegments() {
        Rig rig;
        rig.tool.chooseTrack(QStringLiteral("A2"), true);
        rig.at(35);
        const int before = rig.timeline.history().undoCount();
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.gain")));
        QCOMPARE(rig.timeline.history().undoCount(), before + 1);
        // On the segment under the position on both tracks, with its defaults.
        for (const char* t : {"A1", "A2"}) {
            const auto e = rig.effect(t, 1);
            QVERIFY(e);
            QCOMPARE(e->name, QStringLiteral("Gain"));
            QCOMPARE(e->params.value(QStringLiteral("gain")), 0.0);
            QVERIFY(!rig.effect(t, 0) && !rig.effect(t, 2));
        }
        QVERIFY(rig.tool.setValues({{QStringLiteral("gain"), -6}}));
        QCOMPARE(rig.effect("A2", 1)->params.value(QStringLiteral("gain")), -6.0);
        QCOMPARE(rig.timeline.history().undoCount(), before + 2);
        // Where there is filler or nothing, it is refused.
        rig.at(200);
        QVERIFY(!rig.tool.choosePlugin(QStringLiteral("suite.invert")));
        QVERIFY(!rig.tool.messageLabel()->text().isEmpty());
        QVERIFY(!rig.tool.choosePlugin(QStringLiteral("suite.nothing")));
        // None takes the effect off; so does Remove Effect.
        rig.at(35);
        QVERIFY(rig.tool.choosePlugin(QString()));
        QVERIFY(!rig.effect("A1", 1) && !rig.effect("A2", 1));
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.invert")));
        QVERIFY(rig.timeline.removeEffectAtPosition());
        QVERIFY(!rig.effect("A1", 1) && !rig.effect("A2", 1));
    }

    // Activate Current Plug-In (p.797) and the plug-in dialog's common buttons (p.798).
    void dialog() {
        Rig rig;
        FakeRenderer fake;
        rig.tool.setRenderer(fake.renderer());
        rig.at(35);
        QVERIFY(!rig.tool.activate()); // no plug-in yet
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.normalize")));
        QVERIFY(rig.tool.activateButton()->isEnabled());
        AudioSuitePluginDialog* d = rig.tool.activate();
        QVERIFY(d);
        QCOMPARE(d->windowTitle(), QStringLiteral("Normalize"));
        for (const char* name : {"ok", "cancel", "render"}) QVERIFY2(d->button(QLatin1String(name))->isEnabled(), name);
        for (const char* name : {"preview", "bypass", "findLevel"}) QVERIFY2(!d->button(QLatin1String(name))->isEnabled(), name);
        QVERIFY(d->paramSlider(QStringLiteral("level")));
        QCOMPARE(d->paramChoice(QStringLiteral("mode"))->count(), 2);
        d->paramChoice(QStringLiteral("calibration"))->setCurrentIndex(1);
        d->setValue(QStringLiteral("level"), -3);
        QCOMPARE(d->paramValue(QStringLiteral("level"))->text(), QStringLiteral("-3.0 dB"));
        // Cancel keeps what was there.
        QPointer<AudioSuitePluginDialog> guard(d);
        d->button(QStringLiteral("cancel"))->click();
        settle();
        QVERIFY(!guard);
        QCOMPARE(rig.effect("A1", 1)->params.value(QStringLiteral("level")), 0.0);
        // OK saves the values.
        d = rig.tool.activate();
        d->setValue(QStringLiteral("level"), -3);
        d->paramChoice(QStringLiteral("calibration"))->setCurrentIndex(1);
        d->button(QStringLiteral("ok"))->click();
        settle();
        QCOMPARE(rig.effect("A1", 1)->params.value(QStringLiteral("level")), -3.0);
        QCOMPARE(rig.effect("A1", 1)->params.value(QStringLiteral("calibration")), 1.0);
        QVERIFY(fake.seen.isEmpty());
        // The dialog opens with the segment's values; Render saves and renders.
        d = rig.tool.activate();
        QCOMPARE(d->values().value(QStringLiteral("level")), -3.0);
        d->setValue(QStringLiteral("level"), -2);
        d->button(QStringLiteral("render"))->click();
        settle();
        QCOMPARE(rig.effect("A1", 1)->params.value(QStringLiteral("level")), -2.0);
        QCOMPARE(fake.seen, QStringList{QStringLiteral("b@")});
        QVERIFY(model::isRendered(rig.seq().track(QStringLiteral("A1"))->segments.at(1)));
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("rendered")));
        QVERIFY(!rig.tool.statusLabel()->text().contains(QLatin1String("not rendered")));
        // A plug-in without settings says so.
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.invert")));
        d = rig.tool.activate();
        QVERIFY(d->findChild<QLabel*>(QStringLiteral("noSettings")));
        d->reject();
    }

    void renderEffect() {
        Rig rig;
        FakeRenderer fake;
        rig.at(35);
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.invert")));
        // No renderer: nothing can be rendered.
        QVERIFY(!rig.tool.render());
        rig.tool.setRenderer(fake.renderer());
        rig.tool.setDrives({{QStringLiteral("Media"), QStringLiteral("/m")}, {QStringLiteral("Other"), QStringLiteral("/o")}},
                           QStringLiteral("/o"));
        QCOMPARE(rig.tool.drive(), QStringLiteral("/o"));
        const int before = rig.timeline.history().undoCount();
        rig.tool.renderButton()->click();
        QCOMPARE(fake.seen, QStringList{QStringLiteral("b@/o")});
        QCOMPARE(rig.timeline.history().undoCount(), before + 1);
        // Rendered already: nothing to do.
        QVERIFY(!rig.tool.render());
        QCOMPARE(fake.seen.size(), 1);
        // A changed value needs a new render; a renderer's failure is shown and nothing changes.
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.gain")));
        fake.failWith = QStringLiteral("The drive is full.");
        QVERIFY(!rig.tool.render());
        QCOMPARE(rig.tool.messageLabel()->text(), QStringLiteral("The drive is full."));
        QVERIFY(model::needsRender(rig.seq().track(QStringLiteral("A1"))->segments.at(1)));
        // The Timeline shows a segment's AudioSuite effect.
        rig.timeline.repaint();
    }

    // The Fast menu (p.799): its items name the marks, and work between them.
    void fastMenu() {
        Rig rig;
        FakeRenderer fake;
        rig.tool.setRenderer(fake.renderer());
        rig.tool.chooseTrack(QStringLiteral("A2"), true);
        rig.at(35);
        QVERIFY(!rig.fastItem(QStringLiteral("Set Audio Processing - Global"))->isEnabled()); // no plug-in yet
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.dc")));
        QVERIFY(rig.fastItem(QStringLiteral("Set Audio Processing - Global"))->isEnabled());
        rig.mark(40, std::nullopt);
        QVERIFY(rig.fastItem(QStringLiteral("Render Audio Processing - From IN")));
        rig.mark(20, 65);
        QCOMPARE(rig.tool.scope(), QStringLiteral("IN/OUT"));
        rig.fastItem(QStringLiteral("Set Audio Processing - IN/OUT"))->trigger();
        for (const char* t : {"A1", "A2"}) {
            for (int i = 0; i < 3; ++i) QCOMPARE(rig.effect(t, i)->effect, QStringLiteral("suite.dc"));
        }
        const int before = rig.timeline.history().undoCount();
        rig.fastItem(QStringLiteral("Render Audio Processing - IN/OUT"))->trigger();
        QCOMPARE(fake.seen.size(), 6);
        QCOMPARE(rig.timeline.history().undoCount(), before + 1); // one step for both tracks
        rig.mark(70, std::nullopt);
        rig.tool.chooseTrack(QStringLiteral("A1"), false);
        rig.fastItem(QStringLiteral("Remove Audio Processing - From IN"))->trigger();
        QVERIFY(!rig.effect("A1", 2));
        QVERIFY(rig.effect("A1", 1));
        QVERIFY(rig.effect("A2", 2));
        rig.mark(std::nullopt, std::nullopt);
        rig.fastItem(QStringLiteral("Remove Audio Processing - Global"))->trigger();
        for (int i = 0; i < 3; ++i) QVERIFY(!rig.effect("A1", i));
    }

    // Master clip processing (p.800-803): dropped clips, the controls, and what a render asks for.
    void masterClipMode() {
        Rig rig;
        QList<AudioSuiteTool::MasterClipRender> made;
        rig.tool.setBins({{QStringLiteral("b1"), QStringLiteral("Bin")}, {QStringLiteral("b2"), QStringLiteral("Other")}},
                         QStringLiteral("b2"));
        QCOMPARE(rig.tool.targetBin(), QStringLiteral("b2"));
        QVERIFY(!rig.tool.controlsShown());
        QVERIFY(!rig.tool.masterClipMode());
        // Only clips with sound are taken; dropping one enters master clip mode and shows the
        // controls (p.803).
        BinItem picture = toneClip("pic");
        picture.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
        QCOMPARE(rig.tool.addItems(QStringLiteral("b1"), {picture, sequence()}), 0);
        QVERIFY(!rig.tool.messageLabel()->text().isEmpty());
        const BinItem clip = toneClip();
        QCOMPARE(rig.tool.addItems(QStringLiteral("b1"), {clip}), 1);
        QVERIFY(rig.tool.masterClipMode());
        QVERIFY(rig.tool.masterModeButton()->isChecked());
        QVERIFY(!rig.tool.masterModeButton()->styleSheet().isEmpty()); // yellow (p.802)
        QVERIFY(rig.tool.controlsShown());
        QCOMPARE(rig.tool.clipMenu()->count(), 1);
        QCOMPARE(rig.tool.activeClip(), 0);
        QCOMPARE(rig.tool.inputTracks(), (QList<int>{0, 1}));
        QVERIFY(rig.tool.inputButton(1) && rig.tool.inputButton(1)->isChecked());
        QVERIFY(!rig.tool.inputButton(2));
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("Tone: 2 of 2 tracks")));
        QVERIFY(!rig.tool.trackButton()->isEnabled());

        // Plug-ins for master clips only are offered here; nothing changes on the Timeline.
        const int before = rig.timeline.history().undoCount();
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.duplicate")));
        QCOMPARE(rig.tool.processingModes(), QStringList{QStringLiteral("Mono")});
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.normalize")));
        QCOMPARE(rig.timeline.history().undoCount(), before);
        // Normalize's processing modes (p.803).
        QCOMPARE(rig.tool.processingModes(), (QStringList{"Levels On All Chans-Tracks", "Level On Each Chan-Track"}));
        rig.tool.setProcessingMode(1);
        QCOMPARE(rig.tool.values().value(QStringLiteral("mode")), 1.0);
        QCOMPARE(rig.tool.modeMenu()->currentIndex(), 1);
        rig.tool.inputButton(0)->click();
        QCOMPARE(rig.tool.inputTracks(), QList<int>{1});

        // The marks come from the Source monitor (p.802); the lights turn green.
        QVERIFY(!rig.tool.clipIn() && rig.tool.markIndicator(true)->styleSheet().isEmpty());
        QSignalSpy wanted(&rig.tool, &AudioSuiteTool::sourceRequested);
        QVERIFY(rig.tool.loadInSourceMonitor());
        QCOMPARE(wanted.count(), 1);
        rig.composer.load(MonitorId::Source, QStringLiteral("b1"), clip);
        model::MonitorState& source = rig.composer.state(MonitorId::Source);
        source.setPosition(5);
        source.markIn();
        source.setPosition(20);
        source.markOut();
        rig.composer.load(MonitorId::Source, QStringLiteral("b1"), toneClip("other")); // the marks stay with the clip
        settle();
        QCOMPARE(rig.tool.clipIn(), std::optional<std::int64_t>(5));
        QCOMPARE(rig.tool.clipOut(), std::optional<std::int64_t>(20));
        rig.tool.refresh();
        QVERIFY(!rig.tool.markIndicator(true)->styleSheet().isEmpty());
        rig.tool.setHandleSeconds(1.5);
        rig.tool.setLoadResult(true);

        QVERIFY(!rig.tool.render()); // no maker
        rig.tool.setClipMaker([&](const AudioSuiteTool::MasterClipRender& r, QString*) {
            made << r;
            return true;
        });
        rig.tool.setDrives({{QStringLiteral("Media"), QStringLiteral("/m")}}, QStringLiteral("/m"));
        QVERIFY(rig.tool.renderButton()->isEnabled());
        rig.tool.renderButton()->click();
        QCOMPARE(made.size(), 1);
        const AudioSuiteTool::MasterClipRender& r = made.first();
        QCOMPARE(r.source.binId, QStringLiteral("b1"));
        QCOMPARE(r.source.item.id, clip.id);
        QCOMPARE(r.tracks, QList<int>{1});
        QCOMPARE(r.in, std::optional<std::int64_t>(5));
        QCOMPARE(r.out, std::optional<std::int64_t>(20));
        QCOMPARE(r.handleSeconds, 1.5);
        QCOMPARE(r.effect.effect, QStringLiteral("suite.normalize"));
        QCOMPARE(r.effect.params.value(QStringLiteral("mode")), 1.0);
        QCOMPARE(r.targetBin, QStringLiteral("b2"));
        QCOMPARE(r.drive, QStringLiteral("/m"));
        QVERIFY(r.loadResult);
        QCOMPARE(rig.tool.messageLabel()->text(), model::audioSuiteClipName(QStringLiteral("Tone"), QStringLiteral("Normalize"), kMac) + QStringLiteral(" made."));
        // The effect icon's template carries the clip (p.804).
        const auto t = rig.tool.iconTemplate();
        QVERIFY(t && t->hasSource());
        QCOMPARE(t->sourceClip, clip.id);
        QCOMPARE(t->tracks, QList<int>{1});
        // A second clip joins the Clip Selection menu.
        QCOMPARE(rig.tool.addItems(QStringLiteral("b1"), {toneClip("second")}), 1);
        QCOMPARE(rig.tool.clipMenu()->count(), 2);
        QCOMPARE(rig.tool.activeClip(), 1);
        rig.tool.setActiveClip(0);
        QCOMPARE(rig.tool.activeClip(), 0);

        // Back on the Timeline: Duplicate is refused there (p.839).
        rig.tool.masterModeButton()->click();
        QVERIFY(!rig.tool.masterClipMode());
        rig.at(35);
        QVERIFY(!rig.tool.choosePlugin(QStringLiteral("suite.duplicate")));
        QVERIFY(rig.tool.messageLabel()->text().contains(QLatin1String("master clips")));
    }

    // AudioSuite templates (p.804): Find Source From Effect, the Fast menu, a drop on the window.
    void templates() {
        Rig rig;
        const BinItem clip = toneClip();
        rig.tool.setItemResolver([&](const QString& binId, const QString& id) -> std::optional<BinItem> {
            if (binId == QLatin1String("b1") && id == clip.id) return clip;
            return std::nullopt;
        });
        model::AudioSuiteTemplate t;
        t.effect.effect = QStringLiteral("suite.gain");
        t.effect.name = QStringLiteral("Gain");
        t.effect.params = {{QStringLiteral("gain"), -4}};
        t.sourceBin = QStringLiteral("b1");
        t.sourceClip = clip.id;
        t.tracks = {1};
        t.handleSeconds = 2;
        t.in = 3;
        t.out = 9;
        const BinItem item = model::audioSuiteTemplateItem(t, QStringLiteral("Tone - Audio Processing Effect: Gain"), QDateTime::currentDateTimeUtc());
        QVERIFY(!rig.tool.canFindSource());
        QVERIFY(!rig.tool.findSourceButton()->isEnabled());
        QCOMPARE(rig.tool.addItems(QStringLiteral("b1"), {item}), 1);
        QVERIFY(rig.tool.canFindSource());
        QVERIFY(rig.tool.findSourceButton()->isEnabled());
        rig.tool.findSourceButton()->click();
        QVERIFY(rig.tool.masterClipMode());
        QCOMPARE(rig.tool.clips().size(), 1);
        QCOMPARE(rig.tool.inputTracks(), QList<int>{1});
        QCOMPARE(rig.tool.handleSeconds(), 2.0);
        QCOMPARE(rig.tool.plugin(), QStringLiteral("suite.gain"));
        QCOMPARE(rig.tool.values().value(QStringLiteral("gain")), -4.0);
        // The template's marks, as the clip is not marked in the Source monitor.
        QCOMPARE(rig.tool.clipIn(), std::optional<std::int64_t>(3));
        QCOMPARE(rig.tool.clipOut(), std::optional<std::int64_t>(9));
        // A template whose clip is gone (p.802).
        model::AudioSuiteTemplate lost = t;
        lost.sourceClip = QStringLiteral("gone");
        rig.tool.addItems(QStringLiteral("b1"), {model::audioSuiteTemplateItem(lost, QStringLiteral("x"), QDateTime::currentDateTimeUtc())});
        QVERIFY(!rig.tool.findSourceFromEffect());
        QVERIFY(rig.tool.messageLabel()->text().contains(QLatin1String("cannot be found")));

        // The Fast menu's templates, on the Timeline: one undo step.
        rig.tool.setMasterClipMode(false);
        rig.tool.setTemplateSource([&] { return QList<BinItem>{item, sequence()}; });
        rig.at(35);
        const int before = rig.timeline.history().undoCount();
        QMenu* list = rig.tool.fastMenu()->findChild<QMenu*>(QStringLiteral("templates"));
        QVERIFY(list);
        QCOMPARE(list->actions().size(), 1);
        list->actions().first()->trigger();
        QCOMPARE(rig.timeline.history().undoCount(), before + 1);
        QCOMPARE(rig.effect("A1", 1)->params.value(QStringLiteral("gain")), -4.0);

        // Clips dragged from a bin onto the window.
        Rig other;
        other.tool.setItemResolver([&](const QString&, const QString& id) -> std::optional<BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        });
        auto* mime = new QMimeData;
        mime->setData(binItemsMime(), QStringLiteral("b1\n%1").arg(clip.id).toUtf8());
        QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&other.tool, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&other.tool, &drop);
        QVERIFY(other.tool.masterClipMode());
        QCOMPARE(other.tool.clips().size(), 1);
        delete mime;
    }

    // In the application: Tools > AudioSuite renders into managed media that plays in the segment's
    // place, and Audio Mixdown renders what is not rendered first (p.800).
    void mainWindow() {
        QTemporaryDir dir;
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
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
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
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeAudioDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {wav});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));

        commands.action(QStringLiteral("tools.audiosuite"))->trigger();
        settle();
        QVERIFY(window.panels().isOpen(window.panels().panel(QStringLiteral("AudioSuite"))));
        AudioSuiteTool& tool = window.audioSuite();
        QCOMPARE(tool.drive(), drive);
        tool.chooseTrack(QStringLiteral("A1"), false);
        composer.moveTo(MonitorId::Record, 5);
        settle();
        QVERIFY(tool.choosePlugin(QStringLiteral("suite.invert")));
        QVERIFY2(tool.render(), qPrintable(tool.messageLabel()->text()));
        const model::Segment seg =
            model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("A1"))->segments.at(0);
        QVERIFY(model::isRendered(seg));
        QVERIFY(seg.audioSuite->rendered.startsWith(drive));
        // What plays is the rendered file.
        const auto pieces = window.soundOf(*composer.state(MonitorId::Record).current(), {});
        QCOMPARE(pieces.size(), std::size_t{1});
        QCOMPARE(pieces.front().path, seg.audioSuite->rendered);
        media::AudioReader reader;
        QVERIFY(reader.open(seg.audioSuite->rendered));
        const std::vector<float> got = reader.samples(100, 10);
        QCOMPARE(got.size(), std::size_t{10});
        for (float v : got) QVERIFY(std::abs(v + 0.25f) < 1e-4f);

        // A new value is not rendered; Audio Mixdown renders it before it mixes, an undo step of
        // its own before the mixdown's.
        QVERIFY(tool.choosePlugin(QStringLiteral("suite.gain")));
        QVERIFY(tool.setValues({{QStringLiteral("gain"), -6.0206}}));
        const int before = window.timeline().history().undoCount();
        window.setMixdownHook([](AudioMixdownDialog& d) {
            d.setFormat(media::MixdownFormat::Mono);
            return true;
        });
        commands.action(QStringLiteral("timeline.mixdown.audio.to_sequence"))->trigger();
        settle();
        QCOMPARE(window.timeline().history().undoCount(), before + 2);
        const model::Sequence after = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QVERIFY(model::isRendered(after.track(QStringLiteral("A1"))->segments.at(0)));
        const model::BinItem* mix = nullptr;
        for (const model::BinItem& i : bin->bin().items) {
            if (i.name.endsWith(QLatin1String("Mixdown"))) mix = &i;
        }
        QVERIFY(mix);
        const auto link = media::linkOf(*mix);
        QVERIFY(link);
        QVERIFY(reader.open(link->audioPaths.first()));
        for (float v : reader.samples(100, 10)) QVERIFY(std::abs(v - 0.125f) < 1e-3f);
        media::setExtraMediaDrives({});
    }

    // In the application: a master clip dropped on the window makes a new master clip and a template
    // in the target bin, loaded into the Source monitor (p.802-804); the template, dropped back,
    // finds its clip; the effect icon's template can be dropped in a bin (p.798).
    void mainWindowMasterClip() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("tone.wav"));
        {
            media::WriterSettings ws;
            ws.format = QStringLiteral("wav");
            ws.audioCodec = QStringLiteral("pcm_f32le");
            ws.channels = 2;
            media::MediaWriter w;
            QVERIFY(w.open(wav, ws));
            std::vector<float> samples(2 * 48000, 0.25f);
            QVERIFY(w.writeAudio(samples.data(), 48000) && w.finish());
        }
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
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
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeVideoDrive"), drive);
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("transcodeAudioDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {wav});
        QCOMPARE(ids.size(), 1);
        const BinItem clip = *bin->bin().item(ids.first());
        commands.action(QStringLiteral("tools.audiosuite"))->trigger();
        settle();
        AudioSuiteTool& tool = window.audioSuite();
        QCOMPARE(tool.targetBin(), binId);
        QCOMPARE(tool.addItems(binId, {clip}), 1);
        QCOMPARE(tool.inputTracks(), (QList<int>{0, 1}));
        QVERIFY(tool.choosePlugin(QStringLiteral("suite.gain")));
        QVERIFY(tool.setValues({{QStringLiteral("gain"), -6.0206}}));
        tool.setLoadResult(true);
        QVERIFY2(tool.render(), qPrintable(tool.messageLabel()->text()));
        // The new clip and its template, by Media Composer's names.
        const QString name = model::audioSuiteClipName(clip.name, QStringLiteral("Gain"), kMac);
        const BinItem* made = nullptr;
        const BinItem* tmpl = nullptr;
        for (const BinItem& i : bin->bin().items) {
            if (i.name == name) made = &i;
            if (i.name == model::audioSuiteTemplateName(clip.name, QStringLiteral("Gain"), kMac)) tmpl = &i;
        }
        QVERIFY(made);
        QVERIFY(tmpl);
        QCOMPARE(made->durationFrames, clip.durationFrames);
        const auto link = media::linkOf(*made);
        QVERIFY(link && link->audioPaths.size() == 2);
        media::AudioReader reader;
        QVERIFY(reader.open(link->audioPaths.at(1)));
        for (float v : reader.samples(500, 10)) QVERIFY(std::abs(v - 0.125f) < 1e-3f);
        // Load Result (p.802).
        QCOMPARE(window.composer().state(MonitorId::Source).current()->item.id, made->id);
        const auto t = model::audioSuiteTemplateOf(*tmpl);
        QVERIFY(t);
        QCOMPARE(t->sourceClip, clip.id);
        QCOMPARE(t->sourceBin, binId);
        const BinItem templateItem = *tmpl;

        // The template, dropped on the window, finds its clip.
        tool.setMasterClipMode(false);
        QCOMPARE(tool.addItems(binId, {templateItem}), 1);
        QVERIFY(tool.findSourceFromEffect());
        QCOMPARE(tool.clips().at(tool.activeClip()).item.id, clip.id);
        QCOMPARE(tool.values().value(QStringLiteral("gain")), -6.0206);

        // The effect icon's template dropped in the bin becomes an item.
        const int count = static_cast<int>(bin->bin().items.size());
        BinItemModel itemModel(*bin);
        BinItemModel* model = &itemModel;
        const auto icon = tool.iconTemplate();
        QVERIFY(icon);
        QMimeData mime;
        mime.setData(audioSuiteTemplateMime(),
                     QJsonDocument(QJsonObject{{QStringLiteral("name"), QStringLiteral("My Gain")},
                                               {QStringLiteral("template"), model::audioSuiteTemplateToJson(*icon)}})
                         .toJson());
        QVERIFY(model->canDropMimeData(&mime, Qt::CopyAction, -1, -1, {}));
        QVERIFY(model->dropMimeData(&mime, Qt::CopyAction, -1, -1, {}));
        QCOMPARE(static_cast<int>(bin->bin().items.size()), count + 1);
        QCOMPARE(bin->bin().items.last().name, QStringLiteral("My Gain"));
        QVERIFY(model::audioSuiteTemplateOf(bin->bin().items.last()));
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestAudioSuiteUi)
#include "tst_audio_suite_ui.moc"

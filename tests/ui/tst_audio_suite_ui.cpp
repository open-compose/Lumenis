// The AudioSuite tool (Editing Guide p.796-800; docs/spec/audiosuite.md): the Track Selection and
// Plug-In Selection menus put a plug-in on the segments under the Record position, Activate opens
// its dialog (OK and Render), Render Effect renders through the renderer, the Fast menu's items work
// over the marks, Remove Effect takes an effect off; in the application, Tools > AudioSuite renders
// into managed media that plays, and Audio Mixdown renders what is not rendered first (p.800). The
// processes and the sound are in tests/media/tst_audio_suite.cpp.
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
#include <QLabel>
#include <QMenu>
#include <QPushButton>
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

} // namespace

class TestAudioSuiteUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void controls() {
        Rig rig;
        QCOMPARE(rig.tool.tracks(), QStringList{QStringLiteral("A1")});
        QCOMPARE(rig.tool.trackButton()->text(), QStringLiteral("A1"));
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("no AudioSuite effect")));
        QVERIFY(!rig.tool.activateButton()->isEnabled());
        // The picture's controls: the ones that come later are there, dimmed.
        for (const char* name : {"effectIcon", "audioLoopPlay", "nestedLevel"}) {
            auto* b = rig.tool.findChild<QToolButton*>(QString::fromLatin1(name));
            QVERIFY2(b && !b->isEnabled(), name);
        }
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
        QVERIFY(rig.tool.statusLabel()->text().contains(QLatin1String("no AudioSuite effect")));
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
        QVERIFY(!rig.fastItem(QStringLiteral("Set AudioSuite - Global"))->isEnabled()); // no plug-in yet
        QVERIFY(rig.tool.choosePlugin(QStringLiteral("suite.dc")));
        QVERIFY(rig.fastItem(QStringLiteral("Set AudioSuite - Global"))->isEnabled());
        rig.mark(40, std::nullopt);
        QVERIFY(rig.fastItem(QStringLiteral("Render AudioSuite - From IN")));
        rig.mark(20, 65);
        QCOMPARE(rig.tool.scope(), QStringLiteral("IN/OUT"));
        rig.fastItem(QStringLiteral("Set AudioSuite - IN/OUT"))->trigger();
        for (const char* t : {"A1", "A2"}) {
            for (int i = 0; i < 3; ++i) QCOMPARE(rig.effect(t, i)->effect, QStringLiteral("suite.dc"));
        }
        const int before = rig.timeline.history().undoCount();
        rig.fastItem(QStringLiteral("Render AudioSuite - IN/OUT"))->trigger();
        QCOMPARE(fake.seen.size(), 6);
        QCOMPARE(rig.timeline.history().undoCount(), before + 1); // one step for both tracks
        rig.mark(70, std::nullopt);
        rig.tool.chooseTrack(QStringLiteral("A1"), false);
        rig.fastItem(QStringLiteral("Remove AudioSuite - From IN"))->trigger();
        QVERIFY(!rig.effect("A1", 2));
        QVERIFY(rig.effect("A1", 1));
        QVERIFY(rig.effect("A2", 2));
        rig.mark(std::nullopt, std::nullopt);
        rig.fastItem(QStringLiteral("Remove AudioSuite - Global"))->trigger();
        for (int i = 0; i < 3; ++i) QVERIFY(!rig.effect("A1", i));
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
};

QTEST_MAIN(TestAudioSuiteUi)
#include "tst_audio_suite_ui.moc"

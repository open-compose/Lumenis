// Rendering in the application (FX Guide p.57-61; docs/spec/rendering.md): Timeline > Render > Render at
// Position and Render In/Out through the Render Effect dialog, the render kept with the effect as an undo
// step, Clear Renders with its dialog and Undo, and renders going out of date when the effect changes.
#include "lumenis/ext/provider.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/render_dialogs.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps QuickTime file of red frames, picture only.
bool writeMovie(const QString& path, int frames) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    for (int i = 0; i < frames; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(QColor(255, 0, 0));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

model::Sequence recordSequence(Composer& composer) {
    return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
}

model::RenderTarget pipTarget(const model::Sequence& seq) {
    return *model::renderTargetAt(seq, QStringLiteral("V1"), 5);
}

} // namespace

class TestRenderUi : public QObject {
    Q_OBJECT

private slots:
    void renderAndClear() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("red.mov"));
        QVERIFY(writeMovie(movie, 20));
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
        project.settings.format.width = 320;
        project.settings.format.height = 180;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("renderVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        // A Picture-in-Picture on V1's segment.
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Apply"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect =
                model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
            return true;
        }));
        composer.moveTo(MonitorId::Record, 5);
        settle();
        QVERIFY(!model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));

        // Render at Position: the Render Effect dialog names one effect and the drives.
        int shown = 0;
        window.setRenderHook([&](RenderEffectDialog& d) {
            ++shown;
            const int at = d.driveMenu()->findData(QDir::cleanPath(drive));
            if (at < 0) return false;
            d.driveMenu()->setCurrentIndex(at);
            return true;
        });
        const int before = window.timeline().history().undoCount();
        commands.action(QStringLiteral("timeline.render.render_at_position"))->trigger();
        settle();
        QCOMPARE(shown, 1);
        model::Sequence seq = recordSequence(composer);
        QVERIFY(model::isRendered(seq, pipTarget(seq)));
        const auto clip = model::binItemFromJson(model::renderOf(seq, pipTarget(seq))->clips.at(0));
        QVERIFY(clip);
        QCOMPARE(clip->kind, model::BinItemKind::RenderedEffect);
        QVERIFY(clip->link.value(QStringLiteral("path")).toString().startsWith(QDir::cleanPath(drive)));
        QCOMPARE(window.timeline().history().undoCount(), before + 1);
        media::VideoReader reader;
        QVERIFY(reader.open(clip->link.value(QStringLiteral("path")).toString()));
        QCOMPARE(reader.info().width, 320);
        // The precompute is in the sequence's bin as a reference clip, shown only with Show reference
        // clips (FX Guide p.155); Reveal File finds its media; the Media Tool lists it with Precompute
        // Clips - Rendered Effects (p.153).
        const QString firstPrecompute = clip->link.value(QStringLiteral("path")).toString();
        const model::BinItem* reference = bin->bin().item(clip->id);
        QVERIFY(reference);
        QVERIFY(!reference->userPlaced);
        QCOMPARE(reference->kind, model::BinItemKind::RenderedEffect);
        QString revealed;
        window.setRevealHook([&](const QString& f) { revealed = f; });
        window.binContainer().viewFor(bin)->selectIds({clip->id});
        QVERIFY(window.revealFile().isEmpty()); // not shown, so not selected
        // "Select Effects and Show reference clips" (p.155): ours, Rendered Effects.
        model::BinDisplay shownDisplay = bin->bin().display;
        shownDisplay.showReferenceClips = true;
        QVERIFY(shownDisplay.hiddenKinds.removeAll(model::BinItemKind::RenderedEffect) == 1);
        bin->setDisplay(shownDisplay);
        window.binContainer().viewFor(bin)->selectIds({clip->id});
        QCOMPARE(window.revealFile(), firstPrecompute);
        QCOMPARE(revealed, firstPrecompute);
        MediaTool tool;
        MediaToolDisplay display;
        display.project = QStringLiteral("Film");
        tool.setItems(bin->bin().items, display);
        QVERIFY(!tool.names().contains(clip->name));
        display.renderedEffects = true;
        display.masterClips = false;
        display.linkedMasterClips = false;
        tool.setItems(bin->bin().items, display);
        QCOMPARE(tool.names(), QStringList{clip->name});
        // Rendered effects are not rendered again unless asked (Shift, p.61).
        shown = 0;
        window.renderEffects(true);
        settle();
        QCOMPARE(shown, 0);
        auto* nothing = window.findChild<QMessageBox*>(QStringLiteral("renderFailed"));
        QVERIFY(nothing);
        nothing->close();
        settle();

        // Undo forgets the render; redo brings it back.
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        QVERIFY(!model::renderOf(recordSequence(composer), pipTarget(recordSequence(composer))));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.redo")));
        QVERIFY(model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));

        // A changed parameter: out of date, and Render In/Out needs marks.
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Change"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect->parameters =
                QJsonObject{{QStringLiteral("Foreground"), QJsonObject{{QStringLiteral("Level"), 60}}}};
            return true;
        }));
        QVERIFY(!model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));
        window.renderEffects(false);
        settle();
        auto* marks = window.findChild<QMessageBox*>(QStringLiteral("renderFailed"));
        QVERIFY(marks);
        marks->close();
        settle();
        composer.moveTo(MonitorId::Record, 0);
        composer.state(MonitorId::Record).markIn();
        composer.moveTo(MonitorId::Record, 19);
        composer.state(MonitorId::Record).markOut();
        composer.moveTo(MonitorId::Record, 5);
        shown = 0;
        // Save after Render (p.1208) and the Render setting's Render Completion Sound (p.1215-1216).
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("saveAfterRender"), true);
        window.settings().setValue(QStringLiteral("Render"), QStringLiteral("renderCompletionSound"), QStringLiteral("systemBeep"));
        QStringList sounds;
        window.setRenderSoundHook([&](const QString& sound) { sounds << sound; });
        QVERIFY(bin->isDirty());
        // Alt-click: the last drive, no dialog (p.58).
        window.renderEffects(false, false, true);
        settle();
        QCOMPARE(shown, 0);
        QVERIFY(model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));
        QVERIFY(!bin->isDirty());
        QCOMPARE(sounds, QStringList{QStringLiteral("systemBeep")});
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("saveAfterRender"), false);
        window.settings().setValue(QStringLiteral("Render"), QStringLiteral("renderCompletionSound"), QStringLiteral("none"));
        // The effect rendered a second time since the last save: the earlier precompute is deleted
        // (FX Guide p.154-155), from the drive and the bin.
        QVERIFY(!QFileInfo::exists(firstPrecompute));
        QVERIFY(!bin->bin().item(clip->id));

        // Clear Renders at Position, then Undo Clear Renders (p.60-61).
        bool asked = false;
        window.setClearRendersHook([&](ClearRendersDialog& d) {
            asked = true;
            return d.motionBox()->isChecked() && d.unknownBox()->isChecked();
        });
        commands.action(QStringLiteral("timeline.render.clear_renders_at_position"))->trigger();
        settle();
        QVERIFY(asked);
        QVERIFY(!model::renderOf(recordSequence(composer), pipTarget(recordSequence(composer))));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        QVERIFY(model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));

        // Partial render (p.150): interrupted, Keep saves what was rendered; the next render finishes
        // it as a second precompute.
        QVERIFY(window.timeline().perform(QStringLiteral("edit.redo")));
        QVERIFY(!model::renderOf(recordSequence(composer), pipTarget(recordSequence(composer))));
        auto interruptSoon = [&window] {
            QTimer::singleShot(0, &window, [&window] {
                if (auto* p = window.findChild<QProgressDialog*>(QStringLiteral("renderProgress"))) p->cancel();
            });
        };
        bool askedKeep = false;
        window.setRenderInterruptHook([&] {
            askedKeep = true;
            return true;
        });
        interruptSoon();
        window.renderEffects(true, false, true);
        settle();
        QVERIFY(askedKeep);
        seq = recordSequence(composer);
        QVERIFY(model::isPartlyRendered(seq, pipTarget(seq)));
        const std::int64_t kept = model::renderedFrames(*model::renderOf(seq, pipTarget(seq)));
        QVERIFY(kept > 0 && kept < 20);
        window.renderEffects(true, false, true);
        settle();
        seq = recordSequence(composer);
        QVERIFY(model::isRendered(seq, pipTarget(seq)));
        QCOMPARE(model::renderOf(seq, pipTarget(seq))->clips.size(), 2);
        // Discard keeps nothing and removes the file.
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        window.setRenderInterruptHook([] { return false; });
        interruptSoon();
        window.renderEffects(true, false, true);
        settle();
        QVERIFY(!model::renderOf(recordSequence(composer), pipTarget(recordSequence(composer))));

        // The playback performance indicators (p.136): reported when play stops, shown in TC1, cleared
        // when play starts again.
        const QList<Composer::PlaybackRange> trouble{{4, 3, true}, {12, 1, false}};
        emit composer.playbackDifficulties(MonitorId::Record, trouble);
        QCOMPARE(window.timeline().playbackIndicators(), trouble);
        emit composer.playStarted(MonitorId::Record, 1);
        QVERIFY(window.timeline().playbackIndicators().isEmpty());
        window.settings().setValue(QStringLiteral("Timeline"), QStringLiteral("highlightRenderAreas"), false);
        settle();
        emit composer.playbackDifficulties(MonitorId::Record, trouble);
        QVERIFY(window.timeline().playbackIndicators().isEmpty());

        // Render Ranges (p.150) is kept with the Timeline's display.
        Timeline::Display d;
        QCOMPARE(d.renderRanges, Timeline::RenderRanges::PartialOnly);
        d.renderRanges = Timeline::RenderRanges::All;
        QCOMPARE(Timeline::displayFromJson(Timeline::displayToJson(d)).renderRanges, Timeline::RenderRanges::All);

        // Composer > Render On-the-Fly (p.56) toggles.
        QAction* fly = commands.action(QStringLiteral("composer.render_on_the_fly"));
        QVERIFY(fly->isCheckable() && fly->isChecked());
        fly->trigger();
        QVERIFY(!fly->isChecked());
        fly->trigger();

        // ExpertRender In/Out (p.140-147), marks 0-19. The PIP plays in real time, so only Prepare
        // effects for Digital Cut asks for it.
        window.setClearRendersHook([](ClearRendersDialog&) { return true; });
        window.clearRenders(false);
        settle();
        QVERIFY(!model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));
        QString message;
        window.setExpertRenderHook([&](ExpertRenderDialog& dialog) {
            message = dialog.message()->text();
            dialog.setMode(model::ExpertMode::DigitalCut);
            dialog.choose(ExpertRenderDialog::Choice::Modify);
        });
        commands.action(QStringLiteral("timeline.render.expertrender_in_out"))->trigger();
        settle();
        QCOMPARE(message, QStringLiteral("No effects need rendering."));
        // Modify Selection: the PIP stays highlighted; Shift+click takes it out and puts it back.
        QVERIFY(window.timeline().isModifyingExpertSelection());
        QCOMPARE(window.timeline().expertSelection().size(), 1);
        QVERIFY(window.timeline().toggleExpertTarget(QStringLiteral("V1"), 5));
        QVERIFY(window.timeline().expertSelection().isEmpty());
        QVERIFY(window.timeline().toggleExpertTarget(QStringLiteral("V1"), 5));
        // The Render Effect button renders the set (Alt: the last drive).
        window.renderExpertSelection(true);
        settle();
        QVERIFY(!window.timeline().isModifyingExpertSelection());
        QVERIFY(model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));
        // Cancel takes the highlighting away; OK renders.
        window.clearRenders(false);
        settle();
        window.setExpertRenderHook([](ExpertRenderDialog& dialog) {
            dialog.setMode(model::ExpertMode::DigitalCut);
            dialog.choose(ExpertRenderDialog::Choice::Cancel);
        });
        window.expertRender();
        settle();
        QVERIFY(window.timeline().expertSelection().isEmpty());
        QVERIFY(!model::renderOf(recordSequence(composer), pipTarget(recordSequence(composer))));
        window.setExpertRenderHook([](ExpertRenderDialog& dialog) {
            dialog.setMode(model::ExpertMode::DigitalCut);
            dialog.choose(ExpertRenderDialog::Choice::Ok);
        });
        window.expertRender();
        settle();
        QVERIFY(model::isRendered(recordSequence(composer), pipTarget(recordSequence(composer))));
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestRenderUi)
#include "tst_render_ui.moc"

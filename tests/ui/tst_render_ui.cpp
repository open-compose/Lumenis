// Rendering in the application (FX Guide p.57-61; docs/spec/rendering.md): Timeline > Render > Render at
// Position and Render In/Out through the Render Effect dialog, the render kept with the effect as an undo
// step, Clear Renders with its dialog and Undo, and renders going out of date when the effect changes. Also the Format
// tab's Proxy (Editing Guide p.526-527, p.549-550; 8.5e2): renders at the proxy raster, offline at another mode and back
// when the mode returns.
#include "lumenis/ext/provider.h"
#include "lumenis/media/display_color.h"
#include "lumenis/media/dynamic_relink.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/clip_color_dialog.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/consolidate_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/render_dialogs.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/target_settings_dialog.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QProgressDialog>
#include <QToolButton>
#include <QMenu>
#include <QRadioButton>
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
        // The new bin's name editor commits (a rename, which saves the bin) when it loses focus; done
        // now, so that no save comes between the renders below (autodeletion, FX Guide p.155).
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
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
        {
            // Closed again at once: Windows cannot delete an open file, and the precompute is deleted
            // below.
            media::VideoReader reader;
            QVERIFY(reader.open(clip->link.value(QStringLiteral("path")).toString()));
            QCOMPARE(reader.info().width, 320);
        }
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

        // Background Process (FX Guide p.59): a Background Queue job writes into MXF/__bg.1 while
        // editing goes on; when done, the render is linked to its effect, which an edit made meanwhile
        // has moved (filler before everything keeps the render's key).
        window.clearRenders(false);
        settle();
        QVERIFY(!model::renderOf(recordSequence(composer), pipTarget(recordSequence(composer))));
        bool offered = false;
        window.setRenderHook([&](RenderEffectDialog& dialog) {
            offered = dialog.processingMenu()->itemText(1) == QLatin1String("Background Process");
            dialog.processingMenu()->setCurrentIndex(1);
            return dialog.background();
        });
        window.renderEffects(true);
        QVERIFY(offered);
        const QList<BackgroundQueue::Job> jobs = window.backgroundQueue().jobs();
        QVERIFY(!jobs.isEmpty());
        QVERIFY(jobs.last().name.contains(QStringLiteral("Render")));
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Add Filler"), [](model::Sequence& s) {
            QStringList video;
            for (const model::Track& t : s.tracks()) {
                if (t.kind == model::TrackKind::Video) video << model::trackName(t.kind, t.number);
            }
            for (const QString& name : video) s.track(name)->segments.prepend(model::Segment::filler(10));
            return true;
        }));
        window.backgroundQueue().waitForIdle();
        settle();
        QCOMPARE(window.backgroundQueue().jobs().last().state, BackgroundQueue::State::Completed);
        seq = recordSequence(composer);
        const model::RenderTarget moved = *model::renderTargetAt(seq, QStringLiteral("V1"), 15);
        QCOMPARE(moved.start, 10);
        QVERIFY(model::isRendered(seq, moved));
        const auto background = model::binItemFromJson(model::renderOf(seq, moved)->clips.at(0));
        QVERIFY(background);
        QVERIFY(background->link.value(QStringLiteral("path")).toString().contains(QStringLiteral("/MXF/__bg.1/")));
        QVERIFY(bin->bin().item(background->id));
        media::setExtraMediaDrives({});
    }

    void proxyMode() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("red.mov"));
        QVERIFY(writeMovie(movie, 12));
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
        project.folder = dir.filePath(QStringLiteral("Big"));
        project.file = QDir(project.folder).filePath(QStringLiteral("Big.cthproj"));
        project.settings.name = QStringLiteral("Big");
        project.settings.format.family = FormatFamily::HighRes;
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 1024;
        project.settings.format.height = 576;
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("renderVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Apply"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect =
                model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
            return true;
        }));
        composer.moveTo(MonitorId::Record, 5);
        settle();
        window.setRenderHook([&](RenderEffectDialog& d) {
            const int at = d.driveMenu()->findData(QDir::cleanPath(drive));
            if (at < 0) return false;
            d.driveMenu()->setCurrentIndex(at);
            return true;
        });
        auto renderedWidth = [&] {
            const model::Sequence seq = recordSequence(composer);
            const auto r = model::renderOf(seq, pipTarget(seq));
            if (!r || !model::isRendered(seq, pipTarget(seq))) return 0;
            const auto clip = model::binItemFromJson(r->clips.at(0));
            media::VideoReader reader;
            return clip && reader.open(clip->link.value(QStringLiteral("path")).toString()) ? reader.info().width : -1;
        };
        window.renderEffects(true);
        settle();
        QCOMPARE(renderedWidth(), 1024);

        // The Format tab offers Off, 1/4 and 1/16 in a high-resolution project.
        SettingsWindow* settings = window.showSettings();
        auto* proxy = settings->findChild<QComboBox*>(QStringLiteral("formatProxy"));
        QVERIFY(proxy);
        QVERIFY(proxy->isEnabled());
        QCOMPARE(proxy->count(), 3);
        QCOMPARE(proxy->itemText(1), QStringLiteral("1/4"));
        emit proxy->activated(1);
        settle();
        QCOMPARE(model::renderProxy(), 4);
        QCOMPARE(model::ProjectStore::read(project.file)->proxy, 4);
        // "any previously rendered media at that proxy mode will go offline" (p.527); rendered again at 1/4: 512 wide.
        QCOMPARE(renderedWidth(), 0);
        window.renderEffects(true);
        settle();
        QCOMPARE(renderedWidth(), 512);
        // "Should you switch back to that proxy mode, the rendered proxy media will still be available" (p.527).
        QVERIFY(window.setProxyMode(0));
        QCOMPARE(renderedWidth(), 1024);
        QVERIFY(window.setProxyMode(4));
        QCOMPARE(renderedWidth(), 512);
        QVERIFY(!window.setProxyMode(8));
        // Transcode's Project Dimensions follow the proxy mode (p.527).
        QCOMPARE(window.mediaCreation(QStringLiteral("transcode")).raster, QSize(512, 288));
        QCOMPARE(window.mediaCreation(QStringLiteral("import")).raster, QSize(1024, 576));
        QVERIFY(window.setProxyMode(0));

        // The Format tab's Mask Margins button and the Target Settings dialog (p.551; 8.5e3): a preset fills the
        // percentages, OK keeps them in the project file and the monitors show them.
        settings = window.showSettings();
        auto* maskButton = settings->findChild<QPushButton*>(QStringLiteral("maskMarginsButton"));
        QVERIFY(maskButton);
        settings->setMaskDialogHook([](TargetSettingsDialog& d) {
            auto* preset = d.findChild<QComboBox*>(QStringLiteral("maskPreset"));
            if (!preset || preset->currentText() != QStringLiteral("16:9")) return false;
            d.choosePreset(QStringLiteral("2.39:1"));
            return d.findChild<QDoubleSpinBox*>(QStringLiteral("maskTop"))->value() > 12.0;
        });
        maskButton->click();
        settle();
        QCOMPARE(window.composer().maskMargins().preset, QStringLiteral("2.39:1"));
        QCOMPARE(model::ProjectStore::read(project.file)->mask, window.composer().maskMargins());
        // The Composer setting's Viewer tab names each monitor's Target Mask (p.1161).
        window.settings().setValue(QStringLiteral("Composer"), QStringLiteral("recordTargetMask"), QStringLiteral("blackMask"));
        settle();
        QCOMPARE(window.composer().targetMask(MonitorId::Record), model::TargetMask::BlackMask);
        // And each monitor's Color Space (p.1161; 8.5e4a).
        window.settings().setValue(QStringLiteral("Composer"), QStringLiteral("sourceColorSpace"), QStringLiteral("srgb"));
        settle();
        QCOMPARE(window.composer().displayColorSpace(MonitorId::Source), QStringLiteral("SRGB"));
    }

    // ACES projects (p.558-560; 8.5e4b2): the Format tab's ACES version and Output Color Space, the blue Video Quality
    // Menu, the monitors' Project and exported frames following the Output Color Space.
    void acesProject() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Aces"));
        project.file = QDir(project.folder).filePath(QStringLiteral("Aces.cthproj"));
        project.settings.name = QStringLiteral("Aces");
        project.settings.format.family = FormatFamily::HighRes;
        project.settings.format.editRate = Rational(24);
        project.settings.format.width = 1024;
        project.settings.format.height = 576;
        project.settings.colorSpace = QStringLiteral("ACES RGB");
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        QVERIFY(window.timeline().floatingPointPrecision());
        SettingsWindow* settings = window.showSettings();
        QCOMPARE(settings->findChild<QLabel*>(QStringLiteral("formatAcesVersion"))->text(), QStringLiteral("v1.1"));
        auto* output = settings->findChild<QComboBox*>(QStringLiteral("formatOutputColorSpace"));
        QVERIFY(output);
        QCOMPARE(output->currentText(), QStringLiteral("Rec709"));
        // An ACEScct grey exported to Rec709, then to DCI P3 (gamma 2.6: a higher code value for the same light).
        QImage frame(4, 4, QImage::Format_RGB32);
        frame.fill(qRgb(106, 106, 106));
        const int rec709 = qGreen(window.outputFrame(frame).pixel(1, 1));
        QVERIFY(rec709 != 106);
        emit output->activated(output->findText(QStringLiteral("DCI P3")));
        settle();
        QCOMPARE(model::ProjectStore::read(project.file)->outputColorSpace, QStringLiteral("DCI P3"));
        QVERIFY(qGreen(window.outputFrame(frame).pixel(1, 1)) > rec709);
        QVERIFY(!window.setOutputColorSpace(QStringLiteral("ACESproxy")));
        // The monitors on Project show the working space as the Output Color Space.
        Composer& composer = window.composer();
        QCOMPARE(composer.displayColorSpace(MonitorId::Record), QStringLiteral("Project"));
        QCOMPARE(composer.displayed(MonitorId::Record, frame).pixel(1, 1),
                 media::toDisplayColorSpace(frame, QStringLiteral("ACES RGB"), QStringLiteral("DCI P3")).pixel(1, 1));
    }

    // Dynamic Relink (p.1106-1110, p.1169-1171; 8.6a): a clip with media at a second resolution, the setting applied to
    // the loaded clip, Offline when nothing matches, back on the clip's own media when turned off; Transcode's Create
    // new clips offered only with the setting on (p.354-355).
    void dynamicRelink() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("red.mov"));
        QVERIFY(writeMovie(movie, 12));
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
        project.folder = dir.filePath(QStringLiteral("Rez"));
        project.file = QDir(project.folder).filePath(QStringLiteral("Rez.cthproj"));
        project.settings.name = QStringLiteral("Rez");
        project.settings.format.family = FormatFamily::HighRes;
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        const model::BinItem clip = *bin->bin().item(ids.first());
        const QJsonObject original = clip.link;
        // The clip's media at a second resolution, as Transcode without Create new clips makes it.
        media::MediaCreation creation;
        creation.videoDrive = drive;
        creation.raster = QSize(256, 144);
        creation.resolution = *media::findResolution(QStringLiteral("dnx_lb"));
        creation.master = model::mobIdOf(clip);
        QVERIFY(media::transcodeClip(clip, 0, -1, creation));
        window.loadMediaDatabase();
        // Off: loading changes nothing.
        Composer& composer = window.composer();
        composer.loadDefault(binId, clip);
        QCOMPARE(bin->bin().item(clip.id)->link, original);
        // On: the loaded clip relinks to the most recent media (p.1170), the transcode.
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("enabled"), true);
        settle();
        QVERIFY(window.dynamicRelinkSettings().enabled);
        auto link = media::Link::fromJson(bin->bin().item(clip.id)->link);
        QVERIFY(link && link->isManaged());
        QVERIFY(media::Link::fromJson(composer.state(MonitorId::Source).current()->item.link)->isManaged());
        QCOMPARE(media::affinityLink(*bin->bin().item(clip.id)), original);
        // Specific Resolution that nothing has, Offline if no match is found.
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("working.videoMethod"),
                                   QStringLiteral("specificResolution"));
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("working.compression"),
                                   QStringLiteral("Avid DNx 444"));
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("working.noMatch"), QStringLiteral("offline"));
        settle();
        QVERIFY(media::Link::fromJson(bin->bin().item(clip.id)->link)->unlinked);
        // The Target tab matches the clip's own file (ffv1 is Other); Override uses it (p.1169).
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("target.videoMethod"),
                                   QStringLiteral("specificResolution"));
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("target.codecFamily"), QStringLiteral("Other"));
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("overrideWorking"), true);
        settle();
        QCOMPARE(bin->bin().item(clip.id)->link, original);
        // A clip loading is relinked as it loads: back to Working (offline), then load again.
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("overrideWorking"), false);
        settle();
        QVERIFY(media::Link::fromJson(bin->bin().item(clip.id)->link)->unlinked);
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("working.compression"),
                                   QStringLiteral("Avid DNx LB"));
        composer.loadDefault(binId, *bin->bin().item(clip.id));
        QVERIFY(media::Link::fromJson(bin->bin().item(clip.id)->link)->isManaged());
        // Off: back on its own media.
        window.settings().setValue(QStringLiteral("Dynamic Relink"), QStringLiteral("enabled"), false);
        settle();
        QCOMPARE(bin->bin().item(clip.id)->link, original);
        // Create new clips follows the setting.
        ConsolidateDialog d(Rational(25), false);
        auto* newClips = d.findChild<QCheckBox*>(QStringLiteral("createNewClips"));
        QVERIFY(newClips && !newClips->isEnabled());
        d.findChild<QRadioButton*>(QStringLiteral("transcodeMode"))->setChecked(true);
        QVERIFY(d.options().newClips);
        d.setDynamicRelink(true);
        newClips->setChecked(false);
        QVERIFY(!d.options().newClips);
        media::setExtraMediaDrives({});
    }

    // The MultiRez button (p.1111-1116; 8.6b): its displays and colours, Resolution Tracking clip colours, Find Next
    // Mismatched or Unavailable Clip, Clip Text > Clip Resolution, and its menu's Dynamic Relink items.
    void multiRezButton() {
        if (!media::patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("red.mov"));
        QVERIFY(writeMovie(movie, 12));
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
        project.folder = dir.filePath(QStringLiteral("Rez"));
        project.file = QDir(project.folder).filePath(QStringLiteral("Rez.cthproj"));
        project.settings.name = QStringLiteral("Rez");
        project.settings.format.family = FormatFamily::HighRes;
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        const model::BinItem clip = *bin->bin().item(ids.first());
        media::MediaCreation creation;
        creation.videoDrive = drive;
        creation.raster = QSize(256, 144);
        creation.resolution = *media::findResolution(QStringLiteral("dnx_lb"));
        creation.master = model::mobIdOf(clip);
        QVERIFY(media::transcodeClip(clip, 0, -1, creation));
        window.loadMediaDatabase();
        // Working: Avid DNx 444, which nothing has, keeping the existing media: the clip is mismatched.
        Settings& s = window.settings();
        const QString kind = QStringLiteral("Dynamic Relink");
        s.setValue(kind, QStringLiteral("working.videoMethod"), QStringLiteral("specificResolution"));
        s.setValue(kind, QStringLiteral("working.compression"), QStringLiteral("Avid DNx 444"));
        s.setValue(kind, QStringLiteral("enabled"), true);
        settle();
        // Two segments of the clip.
        Composer& composer = window.composer();
        Timeline& timeline = window.timeline();
        composer.loadDefault(binId, *bin->bin().item(clip.id));
        QVERIFY(timeline.perform(QStringLiteral("edit.splice_in")));
        composer.loadDefault(binId, *bin->bin().item(clip.id));
        QVERIFY(timeline.perform(QStringLiteral("edit.splice_in")));
        settle();
        const model::Sequence seq = recordSequence(composer);
        const model::Track* v1 = seq.track(QStringLiteral("V1"));
        QVERIFY(v1 && v1->segments.size() == 2);
        const model::Segment seg = v1->segments.at(0);
        // Inactive: gray, nothing marked; Find Next refuses.
        QCOMPARE(timeline.multiRezButtonColor(), QStringLiteral("gray"));
        QCOMPARE(timeline.segmentResolution(seg), model::ClipFacts::Resolution::None);
        QVERIFY(!timeline.perform(QStringLiteral("timeline.find_next_mismatched")));
        // A click: Show Mismatches, blue.
        QToolButton* button = timeline.toolButton(QStringLiteral("multiRez"));
        QVERIFY(button);
        button->click();
        settle();
        QCOMPARE(timeline.multiRezDisplay(), Timeline::MultiRezDisplay::Mismatches);
        QCOMPARE(timeline.segmentResolution(seg), model::ClipFacts::Resolution::Mismatched);
        QCOMPARE(timeline.multiRezButtonColor(), QStringLiteral("blue"));
        // Enable Clip Coloring: the segments blue.
        std::unique_ptr<QMenu> menu(timeline.multiRezMenu(nullptr));
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("multiRezEnable"))->isChecked());
        menu->findChild<QAction*>(QStringLiteral("multiRezClipColoring"))->setChecked(true);
        QVERIFY(timeline.clipColorOptions().resolution);
        QCOMPARE(timeline.segmentColor(seg), timeline.clipColorOptions().mismatchColor);
        // Find Next Mismatched or Unavailable Clip (Ctrl+/): to the second segment.
        composer.moveTo(MonitorId::Record, 0);
        QVERIFY(timeline.perform(QStringLiteral("timeline.find_next_mismatched")));
        QCOMPARE(composer.state(MonitorId::Record).position(), seg.length);
        QVERIFY(!timeline.perform(QStringLiteral("timeline.find_next_mismatched")));
        // Show Target Availability: the Target tab's Most Recent matches; then Avid DNx 444 there, yellow.
        button->click();
        settle();
        QCOMPARE(timeline.multiRezDisplay(), Timeline::MultiRezDisplay::TargetAvailability);
        QCOMPARE(timeline.multiRezButtonColor(), QStringLiteral("gray"));
        s.setValue(kind, QStringLiteral("target.videoMethod"), QStringLiteral("specificResolution"));
        s.setValue(kind, QStringLiteral("target.compression"), QStringLiteral("Avid DNx 444"));
        settle();
        timeline.updateMediaStatus();
        QCOMPARE(timeline.segmentResolution(seg), model::ClipFacts::Resolution::Unavailable);
        QCOMPARE(timeline.multiRezButtonColor(), QStringLiteral("yellow"));
        QCOMPARE(timeline.segmentColor(seg), timeline.clipColorOptions().unavailableColor);
        // Working matching the LB media: Update Media Status relinks in the Show Mismatches display, which clears.
        s.setValue(kind, QStringLiteral("working.compression"), QStringLiteral("Avid DNx LB"));
        settle();
        timeline.setMultiRezDisplay(Timeline::MultiRezDisplay::Mismatches);
        timeline.updateMediaStatus();
        QCOMPARE(timeline.segmentResolution(seg), model::ClipFacts::Resolution::Matched);
        QCOMPARE(timeline.multiRezButtonColor(), QStringLiteral("gray"));
        // Clip Text > Clip Resolution names the media played now.
        Timeline::Display display = timeline.display();
        display.clipNames = false;
        display.clipResolution = true;
        timeline.setDisplay(display);
        QCOMPARE(timeline.clipText(*v1, seg), QStringLiteral("Avid DNx LB"));
        // The menu's Override Working Settings with Target Settings and Enable Dynamic Relink change the setting.
        menu.reset(timeline.multiRezMenu(nullptr));
        menu->findChild<QAction*>(QStringLiteral("multiRezOverride"))->setChecked(true);
        settle();
        QVERIFY(window.dynamicRelinkSettings().overrideWorking);
        menu.reset(timeline.multiRezMenu(nullptr));
        menu->findChild<QAction*>(QStringLiteral("multiRezEnable"))->setChecked(false);
        settle();
        QVERIFY(!window.dynamicRelinkSettings().enabled);
        menu.reset(timeline.multiRezMenu(nullptr));
        QVERIFY(!menu->findChild<QAction*>(QStringLiteral("multiRezOverride"))->isEnabled());
        // The MultiRez bin headings (p.1116; 8.6c): the clip's LB media are online, nothing at 444; Choose Columns lists
        // the headings.
        QVERIFY(BinAppearance::multiRezHeadings().contains(QStringLiteral("Avid DNx LB")));
        QVERIFY(BinAppearance::multiRezCell());
        QCOMPARE(BinAppearance::multiRezCell()(*bin->bin().item(clip.id), QStringLiteral("Avid DNx LB")), QStringLiteral("\u25CF"));
        QCOMPARE(BinAppearance::multiRezCell()(*bin->bin().item(clip.id), QStringLiteral("Avid DNx 444")), QStringLiteral("\u25CB"));
        // Delete offers the clip's own media: the LB files, though the clip plays its linked file.
        int deletable = 0;
        window.setBinDeleteHook([&deletable](int, int files) -> std::optional<MainWindow::BinDeleteChoice> {
            deletable = files;
            return std::nullopt; // Cancel
        });
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({clip.id});
        QCOMPARE(window.binContainer().deleteSelection(), 0);
        QVERIFY(deletable >= 1);
        // The Clip Color dialog's Resolution Tracking is offered with Dynamic Relink only.
        ClipColorDialog colors(timeline.clipColorOptions());
        QVERIFY(!colors.option(QStringLiteral("resolution"))->isEnabled());
        colors.setMultiRez(true);
        QVERIFY(colors.option(QStringLiteral("resolution"))->isEnabled());
        QVERIFY(colors.swatch(QStringLiteral("unavailableColor")));
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestRenderUi)
#include "tst_render_ui.moc"

// Rendering in the application (FX Guide p.57-61; docs/spec/rendering.md): Timeline > Render > Render at
// Position and Render In/Out through the Render Effect dialog, the render kept with the effect as an undo
// step, Clear Renders with its dialog and Undo, and renders going out of date when the effect changes. Also the Format
// tab's Proxy (Editing Guide p.526-527, p.549-550; 8.5e2): renders at the proxy raster, offline at another mode and back
// when the mode returns.
#include "lumenis/ext/provider.h"
#include "lumenis/ext/shared_storage.h"
#include "lumenis/media/copy_media.h"
#include "lumenis/media/display_color.h"
#include "lumenis/media/dynamic_relink.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/proxy.h"
#include "lumenis/model/stereo_clip.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/ui/list_tool.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/clip_color_dialog.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/consolidate_dialog.h"
#include "lumenis/ui/full_screen.h"
#include "lumenis/ui/video_output_tool.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/proxy_dialogs.h"
#include "lumenis/ui/render_dialogs.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/target_settings_dialog.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QProgressDialog>
#include <QToolButton>
#include <QMenu>
#include <QRadioButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps QuickTime file of red (or `color`) frames, picture only.
bool writeMovie(const QString& path, int frames, QColor color = QColor(255, 0, 0)) {
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
        image.fill(color);
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

// A storage module with one workspace that takes Distributed Processing jobs (ext::SharedStorageProvider, p.510): a job
// runs until finish() gives it its proxies.
class FakeStorage : public ext::SharedStorageProvider {
public:
    explicit FakeStorage(QString root) : root_(std::move(root)) {}
    ext::ProviderInfo info() const override {
        return {"test.storage", "Test storage", ext::Category::SharedStorage, ext::LicenseTier::OpenSource, "test", "1"};
    }
    std::vector<ext::SharedWorkspace> workspaces() const override {
        return {{"ws1", "Shared Workspace", root_.toStdString(), true}};
    }
    bool submitProxyJob(const ext::ProxyJob& job, std::string* jobId, std::string* error) override {
        (void)error;
        jobs.push_back(job);
        *jobId = "job" + std::to_string(jobs.size());
        return true;
    }
    ext::ProxyJobStatus jobStatus(const std::string& jobId) const override {
        ext::ProxyJobStatus s;
        s.state = finished.empty() ? ext::ProxyJobStatus::State::Running : ext::ProxyJobStatus::State::Done;
        s.proxyJson = finished;
        (void)jobId;
        return s;
    }
    void finish(const model::ProxyMedia& proxy) {
        finished = QJsonDocument(proxy.toJson()).toJson(QJsonDocument::Compact).toStdString();
    }
    std::vector<ext::ProxyJob> jobs;
    std::string finished;

private:
    QString root_;
};

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

    // The project's Stereoscopic option and Stereo Master Clips (Editing Guide p.65-66, p.243; 8.6d1): Create
    // Stereoscopic Clip with the left eye found by name, the eye each mode shows in the Source monitor, the S3D Leading
    // Eye, a 2D clip doubled in Side by Side, and the bin's right-click menu.
    void stereoscopic() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString leftMovie = dir.filePath(QStringLiteral("shot_L.mov"));
        const QString rightMovie = dir.filePath(QStringLiteral("shot_R.mov"));
        QVERIFY(writeMovie(rightMovie, 10, QColor(0, 0, 255)));
        QVERIFY(writeMovie(leftMovie, 12, QColor(255, 0, 0)));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("S3D"));
        project.file = QDir(project.folder).filePath(QStringLiteral("S3D.cthproj"));
        project.settings.name = QStringLiteral("S3D");
        project.settings.format.family = FormatFamily::HighRes;
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        project.settings.stereoscopic = QStringLiteral("Right Eye Only");
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        // The right eye linked first: the left is still found by its name.
        const QStringList ids = window.linkFiles(binId, {rightMovie, leftMovie});
        QCOMPARE(ids.size(), 2);
        BinView* view = window.binContainer().activeView();
        QVERIFY(view);
        view->selectIds(ids);
        const QString stereoId = window.createStereoscopicClip();
        QVERIFY(!stereoId.isEmpty());
        const model::BinItem stereo = *bin->bin().item(stereoId);
        QCOMPARE(stereo.name, QStringLiteral("shot_L S3D"));
        QVERIFY(model::isStereoClip(stereo));
        QCOMPARE(stereo.durationFrames, std::int64_t(10));
        QCOMPARE(view->selectedIds(), QStringList{stereoId});
        Composer& composer = window.composer();
        auto colorAt = [&](int x) {
            const QImage picture = composer.pictureAt(MonitorId::Source, 0, QSize(128, 72));
            return picture.isNull() ? QColor() : QColor(picture.pixel(x, 36));
        };
        auto isRed = [](QColor c) { return c.isValid() && c.red() > 200 && c.blue() < 60; };
        auto isBlue = [](QColor c) { return c.isValid() && c.blue() > 200 && c.red() < 60; };
        // Right Eye Only: the right eye's blue.
        composer.loadDefault(binId, stereo);
        settle();
        QVERIFY(isBlue(colorAt(64)));
        // Leading Eye: the leading eye, Left until the S3D Leading Eye changes it.
        QVERIFY(window.setProjectStereoscopic(QStringLiteral("Leading Eye")));
        QCOMPARE(model::ProjectStore::read(project.file)->stereoscopic, QStringLiteral("Leading Eye"));
        QVERIFY(isRed(colorAt(64)));
        QCOMPARE(window.setStereoLeadingEye(model::Eye::Right), 1);
        QCOMPARE(bin->bin().item(stereoId)->fields.value(QLatin1String(model::kLeadingEyeHeading)), QStringLiteral("Right"));
        composer.loadDefault(binId, *bin->bin().item(stereoId));
        settle();
        QVERIFY(isBlue(colorAt(64)));
        // Side by Side: the left eye on the left, the right eye on the right (each linked clip Center Keep Size, p.475: the
        // picture in the middle of each half).
        QVERIFY(window.setProjectStereoscopic(QStringLiteral("Side by Side")));
        QVERIFY(isRed(colorAt(32)));
        QVERIFY(isBlue(colorAt(96)));
        // A 2D clip gives the same picture to both eyes.
        composer.loadDefault(binId, *bin->bin().item(ids.at(1)));
        settle();
        QVERIFY(isRed(colorAt(32)));
        QVERIFY(isRed(colorAt(96)));
        QVERIFY(!window.setProjectStereoscopic(QStringLiteral("Sideways")));
        // The bin's right-click menu: Create Stereoscopic Clip for two master clips, S3D Leading Eye for a stereo clip.
        QWidget* table = view->table();
        QVERIFY(table);
        view->selectIds(ids);
        std::unique_ptr<QMenu> menu(view->contextMenu(QPoint(-1, -1), table));
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("createStereoscopicClip")));
        view->selectIds({stereoId});
        menu.reset(view->contextMenu(QPoint(-1, -1), table));
        QVERIFY(menu->findChild<QMenu*>(QStringLiteral("s3dLeadingEye")));
        QVERIFY(!menu->findChild<QAction*>(QStringLiteral("createStereoscopicClip")));
    }

    // The S3D Views and Overlays (8.6d2; p.1162-1163, p.1190-1191, p.1216, p.1226-1227) in a Full project: the Source
    // monitor's view, the S3D Settings' parallax as the overlay's guides, Full Screen Playback's own view, and the Video
    // Output tool's choices.
    void stereoViews() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString leftMovie = dir.filePath(QStringLiteral("cam_left.mov"));
        const QString rightMovie = dir.filePath(QStringLiteral("cam_right.mov"));
        QVERIFY(writeMovie(leftMovie, 10, QColor(255, 0, 0)));
        QVERIFY(writeMovie(rightMovie, 10, QColor(0, 0, 255)));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Full"));
        project.file = QDir(project.folder).filePath(QStringLiteral("Full.cthproj"));
        project.settings.name = QStringLiteral("Full");
        project.settings.format.family = FormatFamily::HighRes;
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        project.settings.stereoscopic = QStringLiteral("Full");
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        const QStringList ids = window.linkFiles(binId, {leftMovie, rightMovie});
        QCOMPARE(ids.size(), 2);
        window.binContainer().activeView()->selectIds(ids);
        const QString stereoId = window.createStereoscopicClip();
        QVERIFY(!stereoId.isEmpty());
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(stereoId));
        settle();
        Settings& s = window.settings();
        auto colorAt = [&](int x) {
            const QImage picture = composer.pictureAt(MonitorId::Source, 0, QSize(128, 72));
            return picture.isNull() ? QColor() : QColor(picture.pixel(x, 36));
        };
        auto setView = [&](const char* value) {
            s.setValue(QStringLiteral("Composer"), QStringLiteral("sourceS3DView"), QString::fromLatin1(value));
            settle();
        };
        // Frame Compatible by default: a Full project's eyes side by side (each Center Keep Size, so in the middle of its half).
        QCOMPARE(colorAt(32), QColor(255, 0, 0));
        QCOMPARE(colorAt(96), QColor(0, 0, 255));
        // Anaglyph Red-Cyan: the left eye's red and the right eye's blue.
        setView("anaglyphRedCyan");
        QCOMPARE(colorAt(64), QColor(255, 0, 255));
        setView("mix");
        QCOMPARE(colorAt(64), QColor(127, 0, 127));
        setView("monoRight");
        QCOMPARE(colorAt(64), QColor(0, 0, 255));
        setView("monoLeading");
        QCOMPARE(colorAt(64), QColor(255, 0, 0));
        // The S3D Settings work out Parallax Near and Far (p.1216) and keep them.
        QCOMPARE(window.s3dSettings().rasterWidth, 1920);
        s.setValue(QStringLiteral("S3D"), QStringLiteral("depthLimitNear"), -4.0);
        settle();
        QCOMPARE(s.value(QStringLiteral("S3D"), QStringLiteral("parallaxNear")).toDouble(), -8.13);
        // Parallax Near set by hand: green guides 40 pixels of a 128-pixel raster apart about the centre.
        s.setValue(QStringLiteral("S3D"), QStringLiteral("rasterResolution"), 128);
        s.setValue(QStringLiteral("S3D"), QStringLiteral("parallaxNearMode"), QStringLiteral("manual"));
        s.setValue(QStringLiteral("S3D"), QStringLiteral("parallaxNear"), -40.0);
        s.setValue(QStringLiteral("Composer"), QStringLiteral("sourceS3DOverlay"), QStringLiteral("near"));
        settle();
        QCOMPARE(s.value(QStringLiteral("S3D"), QStringLiteral("parallaxNear")).toDouble(), -40.0);
        QCOMPARE(colorAt(44), QColor(0, 255, 0));
        QCOMPARE(colorAt(84), QColor(0, 255, 0));
        QCOMPARE(colorAt(64), QColor(255, 0, 0));
        s.setValue(QStringLiteral("Composer"), QStringLiteral("sourceS3DOverlay"), QStringLiteral("off"));
        settle();
        QCOMPARE(colorAt(44), QColor(0, 0, 0));
        // Full Screen Playback's own S3D View.
        s.setValue(QStringLiteral("Full Screen Playback"), QStringLiteral("s3dView"), QStringLiteral("monoRight"));
        window.toggleFullScreen();
        settle();
        FullScreenView* full = window.fullScreenView();
        QVERIFY(full);
        const QImage shown = full->shownPicture();
        QVERIFY(!shown.isNull());
        QCOMPARE(QColor(shown.pixel(shown.width() / 2, shown.height() / 2)), QColor(0, 0, 255));
        full->close();
        settle();
        // The monitor keeps its own view.
        QCOMPARE(colorAt(64), QColor(255, 0, 0));
        // The Video Output tool's S3D View and Overlay, kept with its values; Follow S/R to start with.
        VideoOutputTool& vo = window.videoOutputTool();
        QCOMPARE(vo.s3dView(), QStringLiteral("followSR"));
        QComboBox* voView = vo.findChild<QComboBox*>(QStringLiteral("voS3DView"));
        QVERIFY(voView);
        voView->setCurrentIndex(voView->findData(QStringLiteral("anaglyphGreenMagenta")));
        QCOMPARE(vo.values().value(QStringLiteral("s3dView")).toString(), QStringLiteral("anaglyphGreenMagenta"));
        // A group of the stereo clip and a 2D clip in the Source monitor (p.1124; 8.6d3): no S3D View, the leading eye.
        setView("anaglyphRedCyan");
        model::GroupOptions options;
        options.sync = model::GroupSync::Inpoints;
        options.marks = [](const model::BinItem&, bool) -> std::optional<std::int64_t> { return 0; };
        QString why;
        const auto group = model::groupClips(bin->bin(), {{binId, *bin->bin().item(stereoId)}, {binId, *bin->bin().item(ids.at(1))}},
                                             options, &why);
        QVERIFY2(group, qPrintable(why));
        bin->addItem(*group);
        composer.loadDefault(binId, *bin->bin().item(group->id));
        settle();
        QCOMPARE(colorAt(64), QColor(255, 0, 0));
        // The List Tool's S3D Contributor (p.959).
        QComboBox* contributor = window.listTool().findChild<QComboBox*>(QStringLiteral("listS3DContributor"));
        QVERIFY(contributor);
        contributor->setCurrentIndex(2);
        QCOMPARE(window.listTool().options().s3dContributor, model::S3DContributor::Right);
    }

    // Proxy media (Editing Guide p.508-510; 8.7a): Create Proxies on the selected clip, its orange icon and proxy columns,
    // the Play button's proxy modes and what the monitor plays, and proxies made as a Background Queue job.
    void proxyMedia() {
        // H.264 proxies: FFmpeg's encoder where the policy allows it, else the operating system's (docs/spec/release-codecs.md).
        if (!media::patentCodecsAllowed() && media::osEncoderFor(QStringLiteral("h264")).isEmpty()) {
            QSKIP("H.264 is left to the operating system, which has no encoder here (docs/spec/release-codecs.md)");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString first = dir.filePath(QStringLiteral("one.mov"));
        const QString second = dir.filePath(QStringLiteral("two.mov"));
        QVERIFY(writeMovie(first, 8));
        QVERIFY(writeMovie(second, 8, QColor(0, 0, 255)));
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("P"));
        project.file = QDir(project.folder).filePath(QStringLiteral("P.cthproj"));
        project.settings.name = QStringLiteral("P");
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
        const QStringList ids = window.linkFiles(binId, {first, second});
        QCOMPARE(ids.size(), 2);
        BinView* view = window.binContainer().activeView();
        QVERIFY(view);
        // The right-click menu offers Create Proxies (p.508).
        view->selectIds({ids.at(0)});
        std::unique_ptr<QMenu> menu(view->contextMenu(QPoint(-1, -1), view->table()));
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("createProxies")));
        // Application: made at once.
        QCOMPARE(window.makeProxies(drive, false), 1);
        const model::BinItem clip = *bin->bin().item(ids.at(0));
        const auto proxy = model::proxyOf(clip);
        QVERIFY(proxy);
        QVERIFY(proxy->folder.startsWith(drive));
        // The proxy columns follow the Timeline's video quality (p.510).
        QCOMPARE(window.proxyCell(clip, QStringLiteral("Proxy Video")), QStringLiteral("H.264"));
        window.composer().setVideoQuality(Composer::VideoQuality::Draft);
        QCOMPARE(window.proxyCell(clip, QStringLiteral("Proxy Video")), QStringLiteral("Avid DNx LB"));
        window.composer().setVideoQuality(Composer::VideoQuality::Full);
        QCOMPARE(window.proxyCell(clip, QStringLiteral("Proxy Offline")), QString());
        // The Play button's right-click menu and proxy modes (p.509): orange while a proxy is preferred.
        Composer& composer = window.composer();
        std::unique_ptr<QMenu> play(composer.proxyPlaybackMenu(nullptr));
        QCOMPARE(play->actions().size(), 3);
        QVERIFY(play->actions().at(0)->isChecked());
        QCOMPARE(model::ProxyMedia::pathOf(window.playedClip(clip).link), model::ProxyMedia::pathOf(clip.link));
        play->actions().at(1)->trigger();
        QCOMPARE(composer.proxyPlayback(), model::ProxyPlayback::ProxyPreferred);
        auto* playButton = composer.findChild<QToolButton*>(QStringLiteral("play.play"));
        QVERIFY(playButton);
        QVERIFY(playButton->property("proxyMode").toBool());
        QCOMPARE(model::ProxyMedia::pathOf(window.playedClip(clip).link), model::ProxyMedia::pathOf(proxy->h264Link));
        composer.setVideoQuality(Composer::VideoQuality::Draft);
        QCOMPARE(model::ProxyMedia::pathOf(window.playedClip(clip).link), model::ProxyMedia::pathOf(proxy->lbLink));
        composer.setVideoQuality(Composer::VideoQuality::Full);
        // The monitor plays the proxy: the clip's red at the proxy's raster.
        composer.loadDefault(binId, clip);
        settle();
        const QImage picture = composer.pictureAt(MonitorId::Source, 0, QSize(128, 72));
        QVERIFY(!picture.isNull());
        const QColor c(picture.pixel(64, 36));
        QVERIFY2(c.red() > 200 && c.blue() < 60, qPrintable(c.name()));
        // High-Resolution Preferred: the clip's own media while it is online.
        composer.setProxyPlayback(model::ProxyPlayback::HighResolutionPreferred);
        QCOMPARE(model::ProxyMedia::pathOf(window.playedClip(clip).link), model::ProxyMedia::pathOf(clip.link));
        composer.setProxyPlayback(model::ProxyPlayback::HighResolutionOnly);
        QVERIFY(!playButton->property("proxyMode").toBool());
        // Background Process: a job on the Background Queue; the clip takes its proxies when it is done.
        view->selectIds({ids.at(1)});
        QCOMPARE(window.makeProxies(drive, true), 1);
        window.backgroundQueue().waitForIdle();
        settle();
        QVERIFY(model::hasProxy(*bin->bin().item(ids.at(1))));
    }

    // Copy Media (p.512-513; 8.7a2): the right-click menus, the files a selection gives (managed media only, the bins), the
    // window's sizes, targets and Use same as above, and the copy to a folder and to a drive.
    void copyMedia() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("one.mov"));
        QVERIFY(writeMovie(movie, 8));
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
        project.folder = dir.filePath(QStringLiteral("P"));
        project.file = QDir(project.folder).filePath(QStringLiteral("P.cthproj"));
        project.settings.name = QStringLiteral("P");
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
        // A managed clip of the same media.
        media::MediaCreation creation;
        creation.videoDrive = drive;
        creation.raster = QSize(256, 144);
        creation.resolution = *media::findResolution(QStringLiteral("dnx_lb"));
        const auto managed = media::transcodeClip(*bin->bin().item(ids.first()), 0, -1, creation);
        QVERIFY(managed);
        bin->addItem(*managed);
        BinView* view = window.binContainer().activeView();
        QVERIFY(view);
        view->selectIds({ids.first(), managed->id});
        // The bin's right-click menu (p.512).
        std::unique_ptr<QMenu> menu(view->contextMenu(QPoint(-1, -1), view->table()));
        QVERIFY(menu->findChild<QAction*>(QStringLiteral("copyMedia")));
        // "Copy Media copies managed media. It will not copy Linked media" (p.512); the bin goes too.
        media::CopyMediaRequest request = window.copyMediaRequest();
        QCOMPARE(request.managedFiles.size(), int(media::linkOf(*managed)->files().size()));
        QVERIFY(!request.managedFiles.contains(QFileInfo(movie).absoluteFilePath()));
        QCOMPARE(request.binFiles, QStringList{QFileInfo(bin->path()).absoluteFilePath()});
        QVERIFY(!bin->isDirty());

        // The window: the sizes, the drive, then a folder with Use same as above (p.513).
        CopyMediaDialog dialog(window.mediaTargets(), request);
        QCOMPARE(dialog.status(), QStringLiteral("Calculating Size..."));
        QVERIFY(!dialog.canCopy());
        dialog.recalculate();
        QVERIFY2(dialog.status().contains(QStringLiteral("Media size required:")), qPrintable(dialog.status()));
        QVERIFY(dialog.status().contains(QStringLiteral("Bins size required:")));
        QVERIFY(dialog.canCopy());
        QCOMPARE(dialog.request().target, drive);
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("copySpace"))->text().startsWith(QStringLiteral("Available Space:")));
        dialog.setCopyToFolder(true);
        dialog.recalculate();
        QVERIFY(!dialog.canCopy()); // no folder set yet
        const QString out = dir.filePath(QStringLiteral("Out"));
        dialog.setFolder(out);
        dialog.recalculate();
        QVERIFY(dialog.canCopy());
        QCOMPARE(dialog.request().binsTarget, out);
        dialog.setChecked(QStringLiteral("bins"), false);
        dialog.recalculate();
        QVERIFY(!dialog.status().contains(QStringLiteral("Bins size required:")));
        dialog.setChecked(QStringLiteral("bins"), true);
        dialog.recalculate();
        const int files = int(request.managedFiles.size()) + 1;
        QCOMPARE(window.runCopyMedia(dialog.request()), files);
        QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("Avid Copied Media/") +
                                                     QFileInfo(request.managedFiles.first()).fileName())));
        QVERIFY(QFileInfo::exists(QDir(out).filePath(QStringLiteral("Avid Copied Bins/") + QFileInfo(bin->path()).fileName())));
        // Again: "All selected files already exist at the target location".
        dialog.recalculate();
        QVERIFY(dialog.status().contains(QStringLiteral("All selected files already exist at the target location")));
        QVERIFY(!dialog.canCopy());
        QCOMPARE(window.runCopyMedia(dialog.request()), 0);

        // The Sidebar: Copy Media on a bin, and on a folder holding it, takes all its items (p.512).
        BinSidebar* sidebar = window.binContainer().sidebar();
        QMenu sideMenu;
        window.binContainer().sidebarMenu(&sideMenu, binId, sidebar);
        QVERIFY(sideMenu.findChild<QAction*>(QStringLiteral("sidebarCopyMedia")));
        view->selectIds({});
        request = window.copyMediaRequest({binId});
        QCOMPARE(request.managedFiles.size(), int(media::linkOf(*managed)->files().size()));
        // To the drive the media is on: already there, only the bin goes.
        request.target = drive;
        request.binsTarget = drive;
        QCOMPARE(window.runCopyMedia(request), 1);
        // To another drive: its next MXF folder.
        const QString other = dir.filePath(QStringLiteral("Other"));
        QDir().mkpath(QDir(other).filePath(QStringLiteral("Avid MediaFiles/MXF/1")));
        request.target = other;
        request.binsTarget = other;
        QCOMPARE(QFileInfo(media::planCopyMedia(request).mediaFolder).fileName(), QStringLiteral("2"));
        QCOMPARE(window.runCopyMedia(request), files);
        QVERIFY(QFileInfo::exists(QDir(media::mediaFilesFolder(other)).filePath(
            QStringLiteral("MXF/2/") + QFileInfo(request.managedFiles.first()).fileName())));
        media::setExtraMediaDrives({});
    }

    // Distributed Processing through a storage module (p.510; ext::SharedStorageProvider): its workspace in the Create
    // Proxies window, the option only for it, the job sent and its proxies taken when it is done.
    void distributedProcessing() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("one.mov"));
        QVERIFY(writeMovie(movie, 8));
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        auto storage = std::make_shared<FakeStorage>(dir.filePath(QStringLiteral("Shared")));
        providers.add(storage);
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("P"));
        project.file = QDir(project.folder).filePath(QStringLiteral("P.cthproj"));
        project.settings.name = QStringLiteral("P");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 1920;
        project.settings.format.height = 1080;
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
        window.binContainer().activeView()->selectIds(ids);

        // The workspace after the drives; "The Distributed Processing option is only available after you select an Avid
        // NEXIS workspace" (p.510).
        const QList<MediaTarget> targets = window.mediaTargets();
        QVERIFY(targets.size() >= 2);
        QVERIFY(!targets.first().isWorkspace());
        QCOMPARE(targets.last().name, QStringLiteral("Shared Workspace"));
        CreateProxiesDialog dialog(targets);
        auto* option = dialog.findChild<QCheckBox*>(QStringLiteral("proxyDistributed"));
        QVERIFY(!option->isEnabled());
        dialog.setTarget(int(targets.size()) - 1);
        QVERIFY(option->isEnabled());
        dialog.setDistributed(true);
        QVERIFY(dialog.distributed());
        QVERIFY(!dialog.findChild<QComboBox*>(QStringLiteral("proxyProcessing"))->isEnabled());

        // One job per master clip, with the clip and the proxy raster.
        QCOMPARE(window.submitDistributedProxies(QStringLiteral("ws1")), 1);
        QCOMPARE(int(storage->jobs.size()), 1);
        QCOMPARE(storage->jobs.front().width, 960);
        QVERIFY(QString::fromStdString(storage->jobs.front().clipJson).contains(ids.first()));
        window.pollDistributedJobs();
        QCOMPARE(window.distributedJobsOut(), 1);
        QVERIFY(!model::hasProxy(*bin->bin().item(ids.first())));
        model::ProxyMedia proxy;
        proxy.folder = dir.filePath(QStringLiteral("Shared/Avid MediaFiles/Proxy/1"));
        proxy.h264Link = QJsonObject{{QStringLiteral("path"), QDir(proxy.folder).filePath(QStringLiteral("one.proxy.mp4"))}};
        proxy.lbLink = QJsonObject{{QStringLiteral("path"), QDir(proxy.folder).filePath(QStringLiteral("one.proxy_lb.mov"))}};
        proxy.raster = QSize(960, 540);
        storage->finish(proxy);
        window.pollDistributedJobs();
        QCOMPARE(window.distributedJobsOut(), 0);
        const auto taken = model::proxyOf(*bin->bin().item(ids.first()));
        QVERIFY(taken);
        QCOMPARE(taken->folder, proxy.folder);
        media::setExtraMediaDrives({});
    }

    // "Media Composer can only play back interlaced stereoscopic material using one of the Full Quality options" (p.403;
    // 8.6d3): in an interlaced stereoscopic project the Video Quality Menu keeps Full Quality.
    void interlacedStereoFullQuality() {
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
        project.folder = dir.filePath(QStringLiteral("I"));
        project.file = QDir(project.folder).filePath(QStringLiteral("I.cthproj"));
        project.settings.name = QStringLiteral("I");
        project.settings.format.family = FormatFamily::HD;
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 1920;
        project.settings.format.height = 1080;
        project.settings.format.scan = ScanMode::Interlaced;
        project.settings.stereoscopic = QStringLiteral("Side by Side");
        QDir().mkpath(project.folder);
        QVERIFY(model::ProjectStore::write(project.file, project.settings));
        window.setProject(project);
        settle();
        Composer& composer = window.composer();
        QVERIFY(composer.fullQualityOnly());
        composer.setVideoQuality(Composer::VideoQuality::Draft);
        QCOMPARE(composer.videoQuality(), Composer::VideoQuality::Full);
        // Off: the other qualities come back.
        QVERIFY(window.setProjectStereoscopic(QStringLiteral("Off")));
        settle();
        QVERIFY(!composer.fullQualityOnly());
        composer.setVideoQuality(Composer::VideoQuality::Draft);
        QCOMPARE(composer.videoQuality(), Composer::VideoQuality::Draft);
    }
};

QTEST_MAIN(TestRenderUi)
#include "tst_render_ui.moc"

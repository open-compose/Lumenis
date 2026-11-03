// Title Effect clips from the classic Title Tool through the main window (ROADMAP 6.8d4a; FX Guide p.357-360, p.365;
// src/model/title_clip.*, MainWindow::saveClassicTitle and reviseTitle): saving makes the fill and matte precomputes
// and the clip in the chosen bin and loads it into the Source monitor, which shows the title over black; Fast Save makes
// an "unrendered." clip without media, found by Bin > Select > Unrendered Titles; revising replaces the clip, and Save
// Title As keeping the name numbers the new one. Title styles are kept as User settings (6.8d4b). Promote Titles > Avid Titler+
// turns a classic title segment into a Titler+ one (6.8d4c). Title clips edited into sequences, faded and placed
// (6.8e1).
#include "lumenis/ext/provider.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/promote3d.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/titler.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/title_tool.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/media/classic_title.h"
#include "lumenis/model/effect_render.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

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
        image.fill(QColor(0, 0, 255));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

bool isRed(const QColor& c) {
    return c.red() > 200 && c.green() < 60 && c.blue() < 60;
}

bool isBlue(const QColor& c) {
    return c.blue() > 200 && c.red() < 60 && c.green() < 60;
}

} // namespace

class TestTitleClips : public QObject {
    Q_OBJECT

private slots:
    void saveAndRevise() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
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
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();

        // A title: a red box over the top-left quarter.
        model::ClassicTitle title;
        model::ClassicObject box;
        box.id = QStringLiteral("Box 1");
        box.kind = model::ClassicKind::Box;
        box.rect = QRectF(0, 0, 960, 540);
        box.fill.color = QStringLiteral("#ff0000");
        title.objects << box;
        TitleToolWindow::SaveRequest request;
        request.name = QStringLiteral("Lower Third");
        request.bin = binId;
        request.drive = drive;
        request.resolution = QStringLiteral("dnx_hq");
        QString error;
        const QString id = window.saveClassicTitle(title, request, QString(), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        // The clip and its two precomputes (reference clips) in the bin.
        const model::BinItem* clip = bin->bin().item(id);
        QVERIFY(clip);
        QCOMPARE(clip->kind, model::BinItemKind::Title);
        QCOMPARE(clip->name, QStringLiteral("Lower Third"));
        QCOMPARE(clip->durationFrames, 3000);
        int precomputes = 0;
        for (const model::BinItem& i : bin->bin().items) {
            if (i.kind == model::BinItemKind::RenderedEffect && !i.userPlaced) ++precomputes;
        }
        QCOMPARE(precomputes, 2);
        // Loaded into the Source monitor, which shows the title over black.
        const model::LoadedClip* loaded = window.composer().state(MonitorId::Source).current();
        QVERIFY(loaded && loaded->item.id == id);
        bool offline = false;
        const QImage shown = window.composer().pictureAt(MonitorId::Source, 10, QSize(320, 180), &offline);
        QVERIFY(!offline);
        QVERIFY(!shown.isNull());
        const QColor red = shown.pixelColor(40, 40), black = shown.pixelColor(280, 150);
        QVERIFY2(red.red() > 200 && red.green() < 60 && red.blue() < 60, qPrintable(red.name()));
        QVERIFY2(black.red() < 30 && black.green() < 30 && black.blue() < 30, qPrintable(black.name()));

        // Fast Save: "unrendered." and no media; offline in the monitor; Bin > Select > Unrendered Titles finds it.
        request.fastSave = true;
        request.name = QStringLiteral("Credits");
        const QString fast = window.saveClassicTitle(title, request, QString(), &error);
        QVERIFY(!fast.isEmpty());
        QCOMPARE(bin->bin().item(fast)->name, QStringLiteral("unrendered.Credits"));
        QVERIFY(model::isUnrenderedTitle(*bin->bin().item(fast)));
        window.composer().pictureAt(MonitorId::Source, 10, QSize(320, 180), &offline);
        QVERIFY(offline);
        commands.action(QStringLiteral("bin.select.unrendered_titles"))->trigger();
        QCOMPARE(window.binContainer().viewFor(bin)->selectedIds(), QStringList{fast});

        // Revising: the Title Tool opens with the clip; Save Title replaces it in the bin.
        QVERIFY(window.reviseTitle(binId, id));
        TitleToolWindow* tool = window.titleTool();
        QVERIFY(tool && tool->isVisible());
        QCOMPARE(tool->clipId(), id);
        QCOMPARE(tool->title(), title);
        QVERIFY(!window.reviseTitle(binId, QStringLiteral("nothing")));
        tool->select({QStringLiteral("Box 1")});
        tool->setSafeColors(false);
        tool->setColor(TitleToolWindow::Coat::Fill, QStringLiteral("#00ff00"));
        const qsizetype before = bin->bin().items.size();
        QVERIFY(tool->saveTitle());
        QCOMPARE(tool->clipId(), id);
        QCOMPARE(model::classicTitleOf(*bin->bin().item(id))->objects.at(0).fill.color, QStringLiteral("#00ff00"));
        QCOMPARE(bin->bin().items.size(), before + 2); // new precomputes, the same clip
        // Save Title As keeping the name: "Lower Third.01".
        tool->setSaveDialog([](const TitleToolWindow::SaveRequest& r) -> std::optional<TitleToolWindow::SaveRequest> { return r; });
        QVERIFY(tool->saveTitleAs());
        QCOMPARE(bin->bin().item(tool->clipId())->name, QStringLiteral("Lower Third.01"));
        // Title styles are "Title Style" User settings (FX Guide p.364): saved from the tool, listed back, replaced by name.
        model::ClassicTitleStyle style = model::classicStyleOf(title.objects.at(0), QStringLiteral("Red"));
        style.functionKey = 2;
        tool->saveStyle(style);
        QCOMPARE(window.titleStyles().size(), 1);
        QCOMPARE(window.titleStyles().first(), style);
        style.functionKey = 4;
        window.saveTitleStyle(style);
        QCOMPARE(window.titleStyles().size(), 1);
        QCOMPARE(tool->styles().first().functionKey, 4);
        QVERIFY(tool->templateFolder().endsWith(QLatin1String("Title_Templates")));
        tool->setClosePrompt([] { return int(QMessageBox::Discard); });

        // Promote Titles > Avid Titler+ (FX Guide p.326, 6.8d4c): a sequence with the title on V2 over a filler V1.
        model::BinItem sequenceItem = model::newSequence(bin->bin(), Rational(25), QDateTime::currentDateTimeUtc());
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.ensureTrack(QStringLiteral("V2"));
        seq.track(QStringLiteral("V1"))->segments = {model::Segment::filler(100)};
        seq.track(QStringLiteral("V2"))->segments = {*model::titleSegmentOf(*bin->bin().item(id))};
        seq.track(QStringLiteral("V2"))->segments[0].length = 100;
        seq.storeIn(sequenceItem);
        bin->addItem(sequenceItem);
        window.composer().load(MonitorId::Record, binId, sequenceItem);
        settle();
        QMenu* menu = window.timeline().contextMenu(QString(), model::SegmentRef{QStringLiteral("V2"), 0});
        QAction* promote = menu->findChild<QAction*>(QStringLiteral("promoteTitles:titler"));
        QVERIFY(promote && promote->isEnabled());
        promote->trigger();
        delete menu;
        const model::Sequence after = model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item);
        const model::Segment& promoted = after.track(QStringLiteral("V2"))->segments.at(0);
        QVERIFY(promoted.effect && model::isTitlerEffect(*promoted.effect));
        QVERIFY(!promoted.hasNest());
        QCOMPARE(model::titleOf(*promoted.effect).objects.size(), 1);
        QCOMPARE(model::titleOf(*promoted.effect).objects.at(0).kind, model::TitleObjectKind::Rectangle);
        // Nothing classic is left to promote; one undo step brings the classic title back.
        QCOMPARE(window.timeline().promoteTitles(model::SegmentRef{QStringLiteral("V2"), 0}), 0);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        const model::Sequence undone = model::Sequence::fromItem(window.composer().state(MonitorId::Record).current()->item);
        QVERIFY(model::isClassicTitleEffect(*undone.track(QStringLiteral("V2"))->segments.at(0).effect));
        tool->hide();
        media::setExtraMediaDrives({});
    }

    // Editing Title Effect clips into a sequence (6.8e1, FX Guide p.365-371): overwriting from the Source monitor with
    // marks, keyed over the video below; the Fade Effect button; Place in Timeline; a rolling title's start from its
    // source IN; a title dragged from a bin.
    void editingTitles() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        const QString movie = dir.filePath(QStringLiteral("blue.mov"));
        QVERIFY(writeMovie(movie, 60));
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
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        // A sequence of the blue clip on V1.
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        Timeline& timeline = window.timeline();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        QVERIFY(timeline.perform(QStringLiteral("edit.splice_in")));
        QVERIFY(timeline.perform(QStringLiteral("timeline.new.video_track")));
        auto record = [&] { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); };
        QVERIFY(record().track(QStringLiteral("V2")));

        // A red box over the top-left quarter, saved: it loads into the Source monitor.
        model::ClassicTitle title;
        model::ClassicObject box;
        box.id = QStringLiteral("Box 1");
        box.kind = model::ClassicKind::Box;
        box.rect = QRectF(0, 0, 960, 540);
        box.fill.color = QStringLiteral("#ff0000");
        title.objects << box;
        TitleToolWindow::SaveRequest request;
        request.name = QStringLiteral("Lower Third");
        request.bin = binId;
        request.drive = drive;
        request.resolution = QStringLiteral("dnx_hq");
        QString error;
        const QString titleId = window.saveClassicTitle(title, request, QString(), &error);
        QVERIFY2(!titleId.isEmpty(), qPrintable(error));
        QCOMPARE(composer.state(MonitorId::Source).current()->item.id, titleId);
        // Patched to V2 (p.366), marked, overwritten.
        QVERIFY(timeline.patchTracks(QStringLiteral("V1"), QStringLiteral("V2")));
        model::MonitorState& source = composer.state(MonitorId::Source);
        composer.moveTo(MonitorId::Source, 100);
        source.markIn();
        model::MonitorState& rec = composer.state(MonitorId::Record);
        composer.moveTo(MonitorId::Record, 5);
        rec.markIn();
        composer.moveTo(MonitorId::Record, 14);
        rec.markOut();
        QVERIFY2(timeline.perform(QStringLiteral("edit.overwrite")), qPrintable(timeline.lastMessage()));
        model::Sequence seq = record();
        const auto placed = model::segmentFor(seq, model::SegmentRef{QStringLiteral("V2"), 5});
        QVERIFY(placed && placed->length == 10 && placed->effect && model::isClassicTitleEffect(*placed->effect));
        QVERIFY(placed->hasNest());
        QVERIFY(model::segmentFor(seq, model::SegmentRef{QStringLiteral("V1"), 0})); // the video stays
        // "Media Composer keys the title over the underlying video layers" (p.488).
        bool offline = false;
        QImage shown = composer.pictureAt(MonitorId::Record, 8, QSize(320, 180), &offline);
        QVERIFY(!offline);
        QVERIFY2(isRed(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        // Patching to V2 moved the monitor there (Auto-Monitoring, p.629); V1 below it still plays (p.627).
        QVERIFY(timeline.isMonitored(QStringLiteral("V2")));
        QVERIFY2(isBlue(shown.pixelColor(280, 150)), qPrintable(shown.pixelColor(280, 150).name()));
        // Fading a title (p.370): it starts from nothing.
        timeline.setSelection({model::SegmentRef{QStringLiteral("V2"), 5}});
        QCOMPARE(timeline.fadeEffects(4, 4), 1);
        shown = composer.pictureAt(MonitorId::Record, 5, QSize(320, 180));
        QVERIFY2(isBlue(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        shown = composer.pictureAt(MonitorId::Record, 9, QSize(320, 180));
        QVERIFY(isRed(shown.pixelColor(40, 40)));
        timeline.setSelection({});

        // The segment knows the clip it came from, for Edit Title (6.8e2).
        QCOMPARE(model::titleSourceOf(*placed->effect).clip, titleId);
        QCOMPARE(model::titleSourceOf(*placed->effect).bin, binId);

        // The Title effect's Position (FX Guide p.489, p.498): half a frame to the right moves the box to the top right.
        const model::SegmentRef titleRef{QStringLiteral("V2"), 5};
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Position"), [&](model::Sequence& s) {
            model::Segment& t = s.track(QStringLiteral("V2"))->segments[1];
            QJsonObject position;
            position.insert(QStringLiteral("X"), 960);
            t.effect->parameters.insert(QStringLiteral("Position"), position);
            return true;
        }));
        shown = composer.pictureAt(MonitorId::Record, 9, QSize(320, 180));
        QVERIFY2(isBlue(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        QVERIFY2(isRed(shown.pixelColor(200, 40)), qPrintable(shown.pixelColor(200, 40).name()));

        // Edit Title from the Timeline's context menu and the Effect Editor (p.372).
        QMenu* menu = timeline.contextMenu(QString(), titleRef);
        QAction* editAction = menu->findChild<QAction*>(QStringLiteral("timeline:editTitle"));
        QVERIFY(editAction && editAction->isEnabled());
        QSignalSpy asked(&timeline, &Timeline::editTitleRequested);
        editAction->trigger();
        QCOMPARE(asked.count(), 1);
        delete menu;
        TitleToolWindow* tool = window.titleTool();
        QVERIFY(tool && tool->isVisible() && tool->revisingInSequence());
        QCOMPARE(tool->clipId(), titleId);
        QCOMPARE(tool->title().objects.size(), 1);
        // Save Title asks to save and exit (p.373): Cancel keeps the tool open and the sequence as it was.
        int exits = 0;
        bool exitAnswer = false;
        tool->setExitPrompt([&] {
            ++exits;
            return exitAnswer;
        });
        model::ClassicTitle green = tool->title();
        green.objects[0].fill.color = QStringLiteral("#00ff00");
        tool->setTitle(green);
        QVERIFY(!tool->saveTitle());
        QCOMPARE(exits, 1);
        QVERIFY(tool->isVisible());
        // Save: the clip is revised in its bin and replaces the segment's title, keeping its place, length, Position
        // and fades; the tool closes.
        exitAnswer = true;
        QVERIFY(tool->saveTitle());
        QVERIFY(!tool->isVisible() && !tool->revisingInSequence());
        seq = record();
        auto revised = model::segmentFor(seq, titleRef);
        QVERIFY(revised && revised->length == 10 && revised->effect && model::isClassicTitleEffect(*revised->effect));
        QCOMPARE(model::classicTitleOf(*revised->effect)->objects.at(0).fill.color, QStringLiteral("#00ff00"));
        QCOMPARE(model::classicTitleOf(*bin->bin().item(titleId))->objects.at(0).fill.color, QStringLiteral("#00ff00"));
        QCOMPARE(model::titleSourceOf(*revised->effect).clip, titleId);
        QCOMPARE(revised->effect->parameters.value(QStringLiteral("Position")).toObject().value(QStringLiteral("X")).toInt(), 960);
        QVERIFY(!model::keyframesOf(*revised->effect, QStringLiteral("Foreground"), QStringLiteral("Level")).keys.isEmpty());
        shown = composer.pictureAt(MonitorId::Record, 9, QSize(320, 180));
        QVERIFY2(shown.pixelColor(200, 40).green() > 200 && shown.pixelColor(200, 40).red() < 60, qPrintable(shown.pixelColor(200, 40).name()));
        // One undo step brings the red title back.
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(model::classicTitleOf(*model::segmentFor(record(), titleRef)->effect)->objects.at(0).fill.color, QStringLiteral("#ff0000"));
        QVERIFY(timeline.perform(QStringLiteral("edit.redo")));

        // The Effect Editor's Edit Title button; Save Title As makes a new clip (no Fast Save, p.373), and the old one
        // stays in the bin.
        timeline.setSelection({titleRef});
        composer.moveTo(MonitorId::Record, 9);
        timeline.setEffectMode(true);
        settle();
        auto* editButton = window.effectEditor().findChild<QPushButton*>(QStringLiteral("effectEditor:editTitle"));
        QVERIFY(editButton);
        editButton->click();
        QVERIFY(tool->isVisible() && tool->revisingInSequence());
        tool->setSaveDialog([&](const TitleToolWindow::SaveRequest& suggestion) {
            TitleToolWindow::SaveRequest r = suggestion;
            r.bin = binId;
            r.drive = drive;
            r.resolution = QStringLiteral("dnx_hq");
            r.fastSave = true; // ignored
            return std::optional<TitleToolWindow::SaveRequest>(r);
        });
        QVERIFY(tool->saveTitleAs());
        QCOMPARE(exits, 3);
        seq = record();
        revised = model::segmentFor(seq, titleRef);
        QVERIFY(revised && revised->effect);
        const QString newId = model::titleSourceOf(*revised->effect).clip;
        QVERIFY(!newId.isEmpty() && newId != titleId);
        QVERIFY(bin->bin().item(titleId));
        QCOMPARE(bin->bin().item(newId)->name, QStringLiteral("Lower Third.01"));
        QVERIFY(!model::isUnrenderedTitle(*bin->bin().item(newId)));
        timeline.setEffectMode(false);
        timeline.setSelection({});

        // Replacing the fill track (p.373): the nest's V1 holds filler instead of the fill precompute, so the box shows
        // that (black) keyed over the video.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Fill"), [&](model::Sequence& s) {
            model::Segment& t = s.track(QStringLiteral("V2"))->segments[1];
            model::Sequence nest = model::nestOf(t);
            model::Track* fill = nest.track(QStringLiteral("V1"));
            if (!fill) return false;
            const std::int64_t length = fill->segments.at(0).length;
            fill->segments = {model::Segment::filler(length)};
            model::setNest(t, nest);
            return true;
        }));
        shown = composer.pictureAt(MonitorId::Record, 9, QSize(320, 180));
        const QColor replaced = shown.pixelColor(200, 40);
        QVERIFY2(replaced.red() < 40 && replaced.green() < 40 && replaced.blue() < 40, qPrintable(replaced.name()));
        QVERIFY2(isBlue(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));

        // Place in Timeline (In -> Out) on V3 (p.359).
        composer.moveTo(MonitorId::Record, 20);
        rec.markIn();
        composer.moveTo(MonitorId::Record, 29);
        rec.markOut();
        request.placeTrack = QStringLiteral("V3");
        request.name = QStringLiteral("Placed");
        QVERIFY(!window.saveClassicTitle(title, request, QString(), &error).isEmpty());
        seq = record();
        const auto onV3 = model::segmentFor(seq, model::SegmentRef{QStringLiteral("V3"), 20});
        QVERIFY(onV3 && onV3->length == 10 && onV3->effect && model::isClassicTitleEffect(*onV3->effect));

        // A rolling title marked halfway in the Source monitor starts halfway through its roll (p.370).
        model::ClassicTitle roll = title;
        roll.motion = model::ClassicMotion::Rolling;
        roll.pages = 2;
        request.placeTrack.clear();
        request.name = QStringLiteral("Roll");
        const QString rollId = window.saveClassicTitle(roll, request, QString(), &error);
        QVERIFY2(!rollId.isEmpty(), qPrintable(error));
        const std::int64_t clipLength = bin->bin().item(rollId)->durationFrames;
        composer.moveTo(MonitorId::Source, clipLength / 2);
        composer.state(MonitorId::Source).markIn();
        composer.moveTo(MonitorId::Record, 40);
        rec.clearBoth();
        rec.markIn();
        composer.moveTo(MonitorId::Record, 49);
        rec.markOut();
        QVERIFY(timeline.patchTracks(QStringLiteral("V1"), QStringLiteral("V2")));
        QVERIFY2(timeline.perform(QStringLiteral("edit.overwrite")), qPrintable(timeline.lastMessage()));
        const auto rolled = model::segmentFor(record(), model::SegmentRef{QStringLiteral("V2"), 40});
        QVERIFY(rolled && rolled->effect);
        QVERIFY(std::abs(model::titleRollStart(*rolled->effect) - 0.5) < 0.01);

        // Dragged from a bin (p.367-368).
        auto* mime = new QMimeData;
        mime->setData(binItemsMime(), (binId + QLatin1Char('\n') + titleId + QLatin1Char('\n')).toUtf8());
        const auto dropped = timeline.droppedClip(mime);
        QVERIFY(dropped && dropped->second.kind == model::BinItemKind::Title);
        delete mime;
        media::setExtraMediaDrives({});
    }

    // 6.8e3: rendering titles, Create Unrendered Title Media, Re-create Title Media, Wrong Format and HDTitleFilter
    // (FX Guide p.373-376).
    void titleMedia() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
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
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus();
        settle();
        Composer& composer = window.composer();
        Timeline& timeline = window.timeline();

        // A fast-saved title keeps the resolution it was saved with (p.375).
        model::ClassicTitle title;
        model::ClassicObject box;
        box.id = QStringLiteral("Box 1");
        box.kind = model::ClassicKind::Box;
        box.rect = QRectF(0, 0, 960, 540);
        box.fill.color = QStringLiteral("#ff0000");
        title.objects << box;
        TitleToolWindow::SaveRequest request;
        request.name = QStringLiteral("Card");
        request.bin = binId;
        request.drive = drive;
        request.resolution = QStringLiteral("dnx_hq");
        request.fastSave = true;
        QString error;
        const QString id = window.saveClassicTitle(title, request, QString(), &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        QVERIFY(model::isUnrenderedTitle(*bin->bin().item(id)));
        QCOMPARE(model::titleMediaOf(*model::titleSegmentOf(*bin->bin().item(id))).resolution, QStringLiteral("dnx_hq"));

        // Timeline > Create Unrendered Title Media on the selected titles (p.375): the saved resolution first, then
        // media, and the name loses "unrendered.".
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({id});
        TitleMediaChoice suggested;
        QString caption;
        window.setTitleMediaDialog([&](const QString& c, const TitleMediaChoice& s) {
            caption = c;
            suggested = s;
            return std::optional<TitleMediaChoice>(TitleMediaChoice{drive, s.resolution});
        });
        QVERIFY(commands.action(QStringLiteral("timeline.create_unrendered_title_media")) && commands.action(QStringLiteral("timeline.create_unrendered_title_media"))->isEnabled());
        commands.action(QStringLiteral("timeline.create_unrendered_title_media"))->trigger();
        QCOMPARE(caption, QStringLiteral("Create Unrendered Title Media"));
        QCOMPARE(suggested.resolution, QStringLiteral("dnx_hq"));
        const model::BinItem* rendered = bin->bin().item(id);
        QVERIFY(rendered && !model::isUnrenderedTitle(*rendered));
        QCOMPARE(rendered->name, QStringLiteral("Card"));
        const auto segment = model::titleSegmentOf(*rendered);
        QVERIFY(segment);
        const model::TitleMedia first = model::titleMediaOf(*segment);
        QVERIFY(!first.isEmpty());
        QCOMPARE(first.raster, QSize(320, 180));

        // The title over filler in a sequence.
        model::BinItem sequenceItem = model::newSequence(bin->bin(), Rational(25), QDateTime::currentDateTimeUtc());
        model::Sequence seq = model::Sequence::withDefaultTracks();
        seq.ensureTrack(QStringLiteral("V2"));
        seq.track(QStringLiteral("V1"))->segments = {model::Segment::filler(50)};
        model::Segment placed = *segment;
        placed.length = 50;
        model::setTitleSource(*placed.effect, {binId, id});
        seq.track(QStringLiteral("V2"))->segments = {placed};
        seq.storeIn(sequenceItem);
        bin->addItem(sequenceItem);
        composer.load(MonitorId::Record, binId, sequenceItem);
        settle();
        auto record = [&] { return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item); };
        const model::SegmentRef ref{QStringLiteral("V2"), 0};
        QImage shown = composer.pictureAt(MonitorId::Record, 10, QSize(320, 180));
        QVERIFY2(isRed(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        // Rendering titles (p.374): the title segment is an effect to render like any other.
        QVERIFY(!model::renderTargets(record(), {QStringLiteral("V2")}, 0, 50).isEmpty());

        // Promoted to 3D (p.371): a 3D Warp that keeps the title (still a title, not promoted again); its Position (3D)
        // moves the keyed title.
        QVERIFY(model::canPromoteTo3D(*model::segmentFor(record(), ref)->effect));
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("3D Promote"), [](model::Sequence& s) {
            model::Segment& t = s.track(QStringLiteral("V2"))->segments[0];
            const auto promoted = model::promoteTo3D(*t.effect);
            if (!promoted) return false;
            t.effect = *promoted;
            return true;
        }));
        auto promoted = model::segmentFor(record(), ref);
        QVERIFY(promoted && promoted->effect);
        QCOMPARE(promoted->effect->name, QStringLiteral("3D Warp"));
        QVERIFY(model::isClassicTitleEffect(*promoted->effect) && model::is3DClassicTitle(*promoted->effect));
        QVERIFY(!model::canPromoteTo3D(*promoted->effect));
        QVERIFY(model::classicTitleOf(*promoted->effect));
        shown = composer.pictureAt(MonitorId::Record, 10, QSize(320, 180));
        QVERIFY2(isRed(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        QVERIFY2(!isRed(shown.pixelColor(280, 150)), qPrintable(shown.pixelColor(280, 150).name()));
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Position"), [](model::Sequence& s) {
            model::Segment& t = s.track(QStringLiteral("V2"))->segments[0];
            QJsonObject position = t.effect->parameters.value(QStringLiteral("Position (3D)")).toObject();
            position.insert(QStringLiteral("X"), 960);
            t.effect->parameters.insert(QStringLiteral("Position (3D)"), position);
            return true;
        }));
        shown = composer.pictureAt(MonitorId::Record, 10, QSize(320, 180));
        QVERIFY2(!isRed(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        QVERIFY2(isRed(shown.pixelColor(200, 40)), qPrintable(shown.pixelColor(200, 40).name()));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(model::segmentFor(record(), ref)->effect->name, QStringLiteral("Title"));

        // Wrong Format (p.376): media made at another raster.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Raster"), [](model::Sequence& s) {
            model::Segment& t = s.track(QStringLiteral("V2"))->segments[0];
            QJsonObject tool = t.effect->parameters.value(QStringLiteral("Title Tool")).toObject();
            tool.insert(QStringLiteral("Raster"), QStringLiteral("640x360"));
            t.effect->parameters.insert(QStringLiteral("Title Tool"), tool);
            return true;
        }));
        QCOMPARE(model::titleMediaOf(*model::segmentFor(record(), ref)).raster, QSize(640, 360));
        shown = composer.pictureAt(MonitorId::Record, 10, QSize(320, 180));
        QVERIFY2(!isRed(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        bool text = false;
        for (int x = 100; x < 220 && !text; ++x) {
            for (int y = 80; y < 100 && !text; ++y) text = shown.pixelColor(x, y).lightness() > 128;
        }
        QVERIFY(text);

        // Clip > Re-create Title Media (p.374): new precomputes at the project's raster, nested edits gone; one undo.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Nested edit"), [](model::Sequence& s) {
            model::Segment& t = s.track(QStringLiteral("V2"))->segments[0];
            model::Sequence nest = model::nestOf(t);
            nest.track(QStringLiteral("V1"))->segments = {model::Segment::filler(50)};
            model::setNest(t, nest);
            return true;
        }));
        QVERIFY(commands.action(QStringLiteral("clip.recreate_title_media")) && commands.action(QStringLiteral("clip.recreate_title_media"))->isEnabled());
        commands.action(QStringLiteral("clip.recreate_title_media"))->trigger();
        QCOMPARE(caption, QStringLiteral("Re-create Title Media"));
        const auto recreated = model::segmentFor(record(), ref);
        QVERIFY(recreated && recreated->effect && model::isClassicTitleEffect(*recreated->effect));
        const model::TitleMedia again = model::titleMediaOf(*recreated);
        QVERIFY(!again.isEmpty() && again.fill != first.fill);
        QCOMPARE(again.raster, QSize(320, 180));
        QCOMPARE(recreated->length, 50);
        QCOMPARE(model::nestOf(*recreated).track(QStringLiteral("V1"))->segments.at(0).clipId, again.fill);
        QCOMPARE(model::titleSourceOf(*recreated->effect).clip, id);
        shown = composer.pictureAt(MonitorId::Record, 10, QSize(320, 180));
        QVERIFY2(isRed(shown.pixelColor(40, 40)), qPrintable(shown.pixelColor(40, 40).name()));
        QVERIFY(bin->bin().item(again.fill) && bin->bin().item(again.matte));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        QCOMPARE(model::titleMediaOf(*model::segmentFor(record(), ref)).raster, QSize(640, 360));

        // HDTitleFilter (p.376): a console toggle; the filter softens a sharp edge between lines.
        ConsoleWindow console;
        QVERIFY(!media::hdTitleFilter());
        console.run(QStringLiteral("HDTitleFilter"));
        QVERIFY(media::hdTitleFilter());
        QImage edge(4, 4, QImage::Format_RGB32);
        edge.fill(Qt::black);
        for (int x = 0; x < 4; ++x) {
            edge.setPixel(x, 2, qRgb(255, 255, 255));
            edge.setPixel(x, 3, qRgb(255, 255, 255));
        }
        const QImage soft = media::hdTitleFiltered(edge);
        QCOMPARE(qRed(soft.pixel(0, 0)), 0);
        QCOMPARE(qRed(soft.pixel(0, 1)), 64);
        QCOMPARE(qRed(soft.pixel(0, 2)), 191);
        QCOMPARE(qRed(soft.pixel(0, 3)), 255);
        console.run(QStringLiteral("hdtitlefilter"));
        QVERIFY(!media::hdTitleFilter());
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestTitleClips)
#include "tst_title_clips.moc"

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
#include "lumenis/model/sequence.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/titler.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/title_tool.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
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
};

QTEST_MAIN(TestTitleClips)
#include "tst_title_clips.moc"

// Reformatting and FrameFlex in the application (Editing Guide p.475-478, p.527-535; 8.5a, docs/spec/frameflex.md):
// linked clips take the Link setting's Reformat attribute, the Source monitor and edited segments show a clip's
// FrameFlex box, Refresh Sequence brings changed attributes into a sequence, the FrameFlex effect pans a shot (8.5b3,
// p.533-534); and a failed Export to File says why
// (owner's report, 2026-11-08).
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/color_adapter.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/frame_flex.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/source_settings_dialog.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QDir>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 64 x 32 movie, the left half red and the right half blue.
bool writeHalves(const QString& path, int frames) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 32;
    s.frameRate = Rational(25);
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    QImage image(64, 32, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    for (int y = 0; y < 32; ++y) {
        for (int x = 32; x < 64; ++x) image.setPixelColor(x, y, Qt::blue);
    }
    for (int i = 0; i < frames; ++i) {
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

bool isRed(QColor c) { return c.red() > 180 && c.blue() < 80; }
bool isBlue(QColor c) { return c.blue() > 180 && c.red() < 80; }
bool isBlack(QColor c) { return c.red() < 40 && c.green() < 40 && c.blue() < 40; }

} // namespace

class TestFrameFlexUi : public QObject {
    Q_OBJECT

private slots:
    void reformatAndFrameFlex() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("halves.mov"));
        QVERIFY(writeHalves(movie, 10));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 128;
        project.settings.format.height = 128;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // "If you are linking to clips, then the default is set to Center Keep Size" (p.475): the 64 x 32 clip sits in
        // the middle of the 128 x 128 frame.
        QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        model::BinItem clip = *bin->bin().item(ids.first());
        QCOMPARE(model::cellText(clip, QLatin1String(model::kReformatHeading)), QStringLiteral("Center Keep Size"));
        Composer& composer = window.composer();
        composer.loadDefault(binId, clip);
        QImage shown = composer.pictureAt(MonitorId::Source, 0, QSize(128, 128));
        QVERIFY(isBlack(shown.pixelColor(5, 64)));
        QVERIFY(isRed(shown.pixelColor(40, 64)));
        QVERIFY(isBlue(shown.pixelColor(88, 64)));
        QVERIFY(isBlack(shown.pixelColor(64, 30)));

        // The Reformatting Options column changed in the bin: Stretch fills the frame.
        QVERIFY(bin->setField(clip.id, QLatin1String(model::kReformatHeading), QStringLiteral("Stretch")));
        clip = *bin->bin().item(clip.id);
        composer.loadDefault(binId, clip);
        shown = composer.pictureAt(MonitorId::Source, 0, QSize(128, 128));
        QVERIFY(isRed(shown.pixelColor(5, 5)));
        QVERIFY(isBlue(shown.pixelColor(122, 122)));

        // Edited into a sequence, the segment carries the adapter.
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        QVERIFY(composer.state(MonitorId::Record).current());
        auto segment = [&] {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
        };
        QVERIFY(segment().spatial);
        QCOMPARE(segment().spatial->reformat, model::Reformat::Stretch);

        // A FrameFlex box on the right half, set on the clip after the edit: the sequence keeps the old framing until
        // Refresh Sequence > Aspect Ratio and Reformatting Options (p.531).
        model::FrameFlex flex;
        flex.x = 16;
        flex.size = 0.5;
        flex.frameAspect = 1.0;
        model::setFrameFlex(clip, flex);
        bin->replaceItem(clip);
        shown = composer.pictureAt(MonitorId::Record, 0, QSize(128, 128));
        QVERIFY(isRed(shown.pixelColor(5, 64)));
        composer.setActiveMonitor(MonitorId::Record);
        commands.action(QStringLiteral("clip.refresh_sequence.aspect_ratio_reformatting"))->trigger();
        settle();
        QVERIFY(segment().spatial && segment().spatial->frameFlex);
        shown = composer.pictureAt(MonitorId::Record, 0, QSize(128, 128));
        QVERIFY(isBlue(shown.pixelColor(5, 64)));
        QVERIFY(isBlue(shown.pixelColor(122, 64)));
        // The Source monitor shows the clip's new framing too.
        composer.loadDefault(binId, clip);
        QVERIFY(isBlue(composer.pictureAt(MonitorId::Source, 0, QSize(128, 128)).pixelColor(5, 64)));
    }

    // Panning a shot (p.533-534): the FrameFlex effect in the Effect Editor, the box dragged in the Record monitor making
    // keyframes, the Layout button's bypass and Alt+Layout's reset, and Refresh Sequence's creative framing (p.478).
    void panningShot() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("halves.mov"));
        QVERIFY(writeHalves(movie, 10));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 128;
        project.settings.format.height = 128;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        // "you need to apply a FrameFlex source adapter to the clip in the bin" (p.533): a square box on the right half.
        model::BinItem clip = *bin->bin().item(ids.first());
        QVERIFY(bin->setField(clip.id, QLatin1String(model::kReformatHeading), QStringLiteral("Stretch")));
        clip = *bin->bin().item(clip.id);
        model::FrameFlex flex;
        flex.x = 16;
        flex.size = 0.5;
        flex.frameAspect = 1.0;
        model::setFrameFlex(clip, flex);
        bin->replaceItem(clip);
        Composer& composer = window.composer();
        composer.loadDefault(binId, clip);
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        QVERIFY(composer.state(MonitorId::Record).current());
        auto segment = [&] {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
        };
        auto picture = [&](std::int64_t frame) { return composer.pictureAt(MonitorId::Record, frame, QSize(128, 128)); };
        QVERIFY(isBlue(picture(0).pixelColor(5, 64)));

        // "Select the clip on the Timeline and click the Effect Mode button ... Select and expand the FrameFlex effect."
        Timeline& timeline = window.timeline();
        EffectEditor& editor = window.effectEditor();
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, 0);
        timeline.setEffectMode(true);
        settle();
        editor.refresh();
        QCOMPARE(editor.effectName(), QStringLiteral("FrameFlex"));
        QVERIFY(editor.categories().contains(QStringLiteral("FrameFlex")));
        QVERIFY(editor.layoutButton() && editor.layoutButton()->isChecked());
        // "The record viewer displays the frame and the framing box": the whole image, letterboxed, and the box's outline
        // and handles over its right half.
        QImage shown = picture(0);
        QVERIFY(isRed(shown.pixelColor(5, 64)));
        QVERIFY(isBlue(shown.pixelColor(122, 64)));
        QVERIFY(isBlack(shown.pixelColor(64, 10)));
        const Composer::EffectPreview& preview = composer.effectPreview();
        QCOMPARE(preview.outline.size(), 4);
        QVERIFY(preview.outline.at(0).x() > 0.45 && preview.outline.at(1).x() > 0.95);
        QVERIFY(std::any_of(preview.handles.begin(), preview.handles.end(), [](const auto& h) { return h.id == QLatin1String("rotZ"); }));

        // "Adjust the size and/or position of the framing box ... Media Composer automatically creates a new keyframe on this
        // frame": a little to the right at the first frame, then to the left half at the last.
        emit composer.effectHandleDragged(QStringLiteral("move"), QPointF(0.75, 0.5), QPointF(0.76, 0.5), true, false);
        settle();
        QVERIFY(segment().spatial->creative);
        composer.moveTo(MonitorId::Record, 9);
        settle();
        editor.refresh();
        emit composer.effectHandleDragged(QStringLiteral("move"), QPointF(0.75, 0.5), QPointF(0.25, 0.5), false, false);
        emit composer.effectHandleDragged(QStringLiteral("move"), QPointF(0.75, 0.5), QPointF(0.25, 0.5), true, false);
        settle();
        const model::Effect framing = segment().spatial->framing;
        QCOMPARE(model::keyframesOf(framing, QStringLiteral("FrameFlex"), QStringLiteral("X")).keys.size(), 2);
        QVERIFY(model::keyframesOf(framing, QStringLiteral("FrameFlex"), QStringLiteral("Y")).keys.isEmpty());
        // "Click the play button to see the results of your animation": the box pans from the blue half to the red one.
        timeline.setEffectMode(false);
        settle();
        QVERIFY(isBlue(picture(0).pixelColor(5, 64)));
        QVERIFY(isRed(picture(9).pixelColor(122, 64)));

        // "click the Layout button. When the button is gray, the effect is bypassed": the whole image, stretched.
        timeline.setEffectMode(true);
        composer.moveTo(MonitorId::Record, 9);
        settle();
        editor.refresh();
        QVERIFY(editor.layoutButton());
        editor.layoutButton()->click();
        settle();
        QVERIFY(model::framingBypassed(segment().spatial->framing));
        timeline.setEffectMode(false);
        settle();
        QVERIFY(isRed(picture(9).pixelColor(5, 64)) && isBlue(picture(9).pixelColor(122, 64)));
        QVERIFY(timeline.perform(QStringLiteral("edit.undo")));
        settle();
        QVERIFY(!model::framingBypassed(segment().spatial->framing));

        // "press ALT and click the Layout button": the keyframe at the position goes back to the clip's box.
        timeline.setEffectMode(true);
        composer.moveTo(MonitorId::Record, 9);
        settle();
        editor.refresh();
        QVERIFY(editor.resetFraming());
        settle();
        timeline.setEffectMode(false);
        settle();
        QVERIFY(isBlue(picture(9).pixelColor(5, 64)));
        QVERIFY(!editor.resetFraming());

        // Creative framing stays through Refresh Sequence > Aspect Ratio and Reformatting Options, and goes with All
        // FrameFlex including creative framing (p.478).
        composer.setActiveMonitor(MonitorId::Record);
        commands.action(QStringLiteral("clip.refresh_sequence.aspect_ratio_reformatting"))->trigger();
        settle();
        QCOMPARE(model::keyframesOf(segment().spatial->framing, QStringLiteral("FrameFlex"), QStringLiteral("X")).keys.size(), 2);
        commands.action(QStringLiteral("clip.refresh_sequence.all_frameflex"))->trigger();
        settle();
        QVERIFY(model::keyframesOf(segment().spatial->framing, QStringLiteral("FrameFlex"), QStringLiteral("X")).keys.isEmpty());
        QVERIFY(!segment().spatial->creative);
        // Remove Effect leaves the adapter.
        QVERIFY(!timeline.removeEffectAtPosition());
        QVERIFY(segment().spatial && segment().spatial->frameFlex);
    }

    // Colour adapters (p.536-541, p.544; 8.5c1): a clip's colour transformations in the Source monitor and in the segments
    // edited from it, Refresh Sequence > Color Adapters and Remove Color Adapters.
    void colorAdapters() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("halves.mov"));
        QVERIFY(writeHalves(movie, 10));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 128;
        project.settings.format.height = 128;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        QVERIFY(bin->setField(ids.first(), QLatin1String(model::kReformatHeading), QStringLiteral("Stretch")));
        model::BinItem clip = *bin->bin().item(ids.first());
        model::ColorEncoding e;
        e.transformations = {QStringLiteral("Levels scaling (full range to video levels)")};
        model::setColorEncoding(clip, e);
        bin->replaceItem(clip);
        // "When clips are viewed in the Source monitor or dropped on the Timeline ... the color transformation is applied"
        // (p.540): full red becomes video-level red.
        Composer& composer = window.composer();
        composer.loadDefault(binId, clip);
        QColor c = composer.pictureAt(MonitorId::Source, 0, QSize(128, 128)).pixelColor(5, 64);
        QVERIFY2(std::abs(c.red() - 235) <= 3 && std::abs(c.blue() - 16) <= 3, qPrintable(c.name()));
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        QVERIFY(composer.state(MonitorId::Record).current());
        auto segment = [&] {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
        };
        QVERIFY(segment().color == e);
        auto recordRed = [&] { return composer.pictureAt(MonitorId::Record, 0, QSize(128, 128)).pixelColor(5, 64).red(); };
        QVERIFY(std::abs(recordRed() - 235) <= 3);
        // The clip changed afterwards: the sequence keeps its adapter until Refresh Sequence > Color Adapters.
        e.transformations = {QStringLiteral("Gamma 2.6 to Linear")};
        model::setColorEncoding(clip, e);
        bin->replaceItem(clip);
        QVERIFY(std::abs(recordRed() - 235) <= 3);
        composer.setActiveMonitor(MonitorId::Record);
        commands.action(QStringLiteral("clip.refresh_sequence.color_adapters"))->trigger();
        settle();
        QVERIFY(segment().color == e);
        QVERIFY(recordRed() > 245); // 1.0 ^ 2.6 stays 1
        // Remove Color Adapters: the sequence only; the clip keeps its encoding (p.544).
        commands.action(QStringLiteral("clip.refresh_sequence.remove_color_adapters"))->trigger();
        settle();
        QVERIFY(!segment().color);
        QVERIFY(model::colorEncodingOf(*bin->bin().item(clip.id)));
        QVERIFY(window.timeline().perform(QStringLiteral("edit.undo")));
        settle();
        QVERIFY(segment().color == e);
    }

    // The Source Settings dialog's FrameFlex tab (p.529-535): fields, buttons and the top viewer's drags change the box,
    // the bottom viewer shows the result, Revert goes back to what was applied, Apply sets every selected clip.
    void sourceSettingsDialog() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("halves.mov"));
        QVERIFY(writeHalves(movie, 10));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 128;
        project.settings.format.height = 128;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        const QStringList ids = window.linkFiles(binId, {movie, movie});
        QCOMPARE(ids.size(), 2);
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds(ids);

        bool checked = false;
        window.setSourceSettingsHook([&](SourceSettingsDialog& d) {
            checked = true;
            // Opened with the clip's settings: Center Keep Size, no box; the raster shown.
            if (d.adapter().reformat != model::Reformat::CenterKeepSize || d.adapter().frameFlex) return false;
            if (d.findChild<QLabel*>(QStringLiteral("rasterDimension"))->text() != QLatin1String("64 x 32")) return false;
            if (!d.viewersShown()) return false;
            // Size matches project raster: the box is 128 wide in a 64-wide image, square.
            d.sizeMatchesRaster();
            if (!d.adapter().frameFlex || std::abs(d.adapter().frameFlex->size - 2.0) > 1e-6) return false;
            d.reset();
            if (d.adapter().frameFlex) return false;
            // The fields: a square box on the right half, Stretch.
            d.findChild<QComboBox*>(QStringLiteral("reformat"))->setCurrentIndex(0);
            d.findChild<QDoubleSpinBox*>(QStringLiteral("frameAspect"))->setValue(1.0);
            d.findChild<QDoubleSpinBox*>(QStringLiteral("framingSize"))->setValue(0.5);
            d.findChild<QDoubleSpinBox*>(QStringLiteral("framingX"))->setValue(16);
            const QImage result = d.resultImage();
            if (!isBlue(result.pixelColor(5, result.height() / 2)) || !isBlue(result.pixelColor(result.width() - 5, result.height() / 2)))
                return false;
            // Rotate left and right by 90 degrees.
            d.rotate(90);
            if (std::abs(d.adapter().frameFlex->rotation - 90) > 1e-6) return false;
            d.rotate(-90);
            // Dragging inside the box in the top viewer moves it left.
            FramingViewer* v = d.framingViewer();
            v->resize(400, 240);
            const QPoint from = v->toWidget(QPointF(48, 16)).toPoint();
            const QPoint to = v->toWidget(QPointF(40, 16)).toPoint();
            QTest::mousePress(v, Qt::LeftButton, {}, from);
            QTest::mouseMove(v, to);
            QTest::mouseRelease(v, Qt::LeftButton, {}, to);
            if (std::abs(d.adapter().frameFlex->x - 8) > 1.5) return false;
            // Revert: back to the clip's settings (nothing applied yet), then the box again, applied.
            d.revert();
            if (d.adapter().frameFlex) return false;
            d.findChild<QComboBox*>(QStringLiteral("reformat"))->setCurrentIndex(0);
            d.findChild<QDoubleSpinBox*>(QStringLiteral("frameAspect"))->setValue(1.0);
            d.findChild<QDoubleSpinBox*>(QStringLiteral("framingSize"))->setValue(0.5);
            d.findChild<QDoubleSpinBox*>(QStringLiteral("framingX"))->setValue(16);
            d.apply();
            return true;
        });
        // The bin's right-click Source Settings.
        commands.action(QStringLiteral("bin.source_settings"))->trigger();
        settle();
        QVERIFY(checked);
        for (const QString& id : ids) {
            const model::BinItem* clip = bin->bin().item(id);
            QCOMPARE(model::cellText(*clip, QLatin1String(model::kReformatHeading)), QStringLiteral("Stretch"));
            const auto flex = model::frameFlexOf(*clip);
            QVERIFY(flex && std::abs(flex->x - 16) < 1e-6 && std::abs(flex->size - 0.5) < 1e-6);
        }
        // The Source monitor shows the new framing.
        window.composer().loadDefault(binId, *bin->bin().item(ids.first()));
        QVERIFY(isBlue(window.composer().pictureAt(MonitorId::Source, 0, QSize(128, 128)).pixelColor(5, 64)));
        // A cancelled dialog changes nothing.
        window.setSourceSettingsHook([](SourceSettingsDialog& d) {
            d.reset();
            return false;
        });
        QVERIFY(!window.sourceSettings());
        QVERIFY(model::frameFlexOf(*bin->bin().item(ids.first())));
    }

    // The Playback Rate tab (p.545): a 25 fps clip in a 30 fps project plays its frames one by one at 30 fps once set to
    // the project's rate, from the Timeline's right-click Source Settings too; Refresh Sequence > Motion Adapters/Timewarps
    // then drops the segment's adapter.
    void playbackRate() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("halves.mov"));
        QVERIFY(writeHalves(movie, 10));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(30);
        project.settings.format.width = 128;
        project.settings.format.height = 128;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        model::BinItem clip = *bin->bin().item(ids.first());
        QCOMPARE(clip.durationFrames, std::int64_t{12}); // 10 frames at 25 fps = 0.4 s = 12 frames at 30
        // Edited into a sequence with its motion adapter.
        Composer& composer = window.composer();
        composer.loadDefault(binId, clip);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        auto segment = [&] {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
        };
        QVERIFY(segment().adapter);

        // The Timeline's right-click Source Settings on the segment: the Playback Rate menu, set to the project's.
        bool asked = false;
        window.setSourceSettingsHook([&](SourceSettingsDialog& d) {
            auto* rate = d.findChild<QComboBox*>(QStringLiteral("playbackRate"));
            if (!rate || !rate->isEnabled() || rate->count() != 2 || d.playbackAtProjectRate()) return false;
            if (!rate->itemText(0).contains(QStringLiteral("25")) || !rate->itemText(1).contains(QStringLiteral("30"))) return false;
            rate->setCurrentIndex(1);
            d.apply();
            asked = true;
            return true;
        });
        QVERIFY(window.sourceSettingsForSegment(model::SegmentRef{QStringLiteral("V1"), 0}));
        QVERIFY(asked);
        clip = *bin->bin().item(ids.first());
        QVERIFY(media::playsAtProjectRate(clip));
        QCOMPARE(clip.durationFrames, std::int64_t{10});
        QVERIFY(!model::isMixedRate(clip, Rational(30)));
        QCOMPARE(media::linkOf(clip)->fileFrame(7, clip.editRate), std::int64_t{7});
        // "select Clip > Refresh Sequence > Motion Adapters/Timewarps".
        composer.setActiveMonitor(MonitorId::Record);
        commands.action(QStringLiteral("clip.refresh_sequence.motion_adapters_timewarps"))->trigger();
        settle();
        QVERIFY(!segment().adapter);

        // Back to the original rate.
        window.setSourceSettingsHook([](SourceSettingsDialog& d) {
            if (!d.playbackAtProjectRate()) return false;
            d.findChild<QComboBox*>(QStringLiteral("playbackRate"))->setCurrentIndex(0);
            d.apply();
            return true;
        });
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds(ids);
        QVERIFY(window.sourceSettings());
        clip = *bin->bin().item(ids.first());
        QVERIFY(!media::playsAtProjectRate(clip));
        QCOMPARE(clip.durationFrames, std::int64_t{12});

        // A clip at the project's rate has nothing to change.
        QString why;
        model::BinItem still = clip;
        still.link = QJsonObject{};
        QVERIFY(!media::setPlaybackAtProjectRate(still, true, &why));
        QVERIFY(!why.isEmpty());
    }

    // Export to File's failure goes into a message, not only the Console (owner's report, 2026-11-08).
    void failedExportSaysWhy() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        // An empty sequence as a movie: "There is nothing to export."
        model::BinItem seq;
        seq.id = model::newItemId();
        seq.kind = model::BinItemKind::Sequence;
        seq.name = QStringLiteral("Empty");
        model::Sequence::withDefaultTracks().storeIn(seq);
        bin->addItem(seq);
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({seq.id});
        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("mov"));
        window.setExportHook([&](ExportAsDialog& d) {
            d.setPath(dir.filePath(QStringLiteral("Empty.mov")));
            return true;
        });
        window.exportToFile();
        settle();
        auto* box = window.findChild<QMessageBox*>(QStringLiteral("exportFailed"));
        QVERIFY(box);
        QVERIFY2(box->informativeText().contains(QStringLiteral("Empty")), qPrintable(box->informativeText()));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Empty.mov"))));
        box->close();
    }
};

QTEST_MAIN(TestFrameFlexUi)
#include "tst_frameflex_ui.moc"

// Effect templates in the application (FX Guide p.37-38, p.52-55, p.182; docs/spec/effect-templates.md): the
// Effect Editor's effect icon dropped in a bin, the template in the Effect Palette under its bin, applied to
// a segment, a transition and one parameter group, Promote Effects, and the Motion Effect Editor's Timewarp
// icon.
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_editor.h"
#include "lumenis/ui/effect_palette.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/motion_effect_editor.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps file whose frame n is grey level 5n.
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
        image.fill(QColor(5 * i, 5 * i, 5 * i));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

model::Segment v1At(const model::BinItem& item, int index) {
    return model::Sequence::fromItem(item).track(QStringLiteral("V1"))->segments.at(index);
}

// The template items of a bin, in order.
QStringList templateIds(const BinDocument& bin) {
    QStringList out;
    for (const model::BinItem& item : bin.bin().items) {
        if (model::effectTemplateOf(item)) out << item.id;
    }
    return out;
}

} // namespace

class TestEffectTemplateUi : public QObject {
    Q_OBJECT

private slots:
    void templates() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("grey.mov"));
        QVERIFY(writeMovie(movie, 50));
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
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        Composer& composer = window.composer();
        Timeline& timeline = window.timeline();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        const QString sequenceId = composer.state(MonitorId::Record).current()->item.id;
        // Three segments of ten frames, the first with a Picture-in-Picture moved right, a dissolve on the
        // cut at 20.
        model::Effect pip{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
        pip.parameters.insert(QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), 120}});
        pip.parameters.insert(QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 60}});
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Set up"), [&](model::Sequence& s) {
            if (!s.addEdit(QStringLiteral("V1"), 10)) return false;
            if (!s.addEdit(QStringLiteral("V1"), 20)) return false;
            if (!s.addEdit(QStringLiteral("V1"), 30)) return false;
            s.track(QStringLiteral("V1"))->segments[0].effect = pip;
            const model::Transition dissolve{model::Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}}, 4,
                                             model::TransitionAlignment::StartingAtCut, 0};
            return model::applyTransition(s, QStringLiteral("V1"), 20, dissolve);
        }));
        settle();
        auto record = [&] { return *bin->bin().item(sequenceId); };

        // "Drag the effect icon from the Effect Editor to a bin" (p.53).
        EffectEditor& editor = window.effectEditor();
        composer.moveTo(MonitorId::Record, 3);
        editor.refresh();
        QVERIFY(editor.findChild<QLabel*>(QStringLiteral("effectIcon")));
        const auto shown = editor.shownTemplate(false);
        QVERIFY(shown);
        QCOMPARE(shown->effect, pip);
        QVERIFY(!shown->transition);
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        {
            std::unique_ptr<QMimeData> mime(editor.templateMime(false));
            QVERIFY(mime);
            QVERIFY(view->itemModel()->dropMimeData(mime.get(), Qt::CopyAction, -1, -1, {}));
        }
        settle();
        QCOMPARE(templateIds(*bin).size(), 1);
        const QString pipId = templateIds(*bin).first();
        QCOMPARE(bin->bin().item(pipId)->kind, model::BinItemKind::Effect);
        QCOMPARE(bin->bin().item(pipId)->name, QStringLiteral("Picture-in-Picture"));

        // "Effect icons for open bins also display in the Effect Palette" (p.53, p.38).
        EffectPalette& palette = window.effectPalette();
        QCOMPARE(palette.templateBins().size(), 1);
        QVERIFY(!palette.templateBinList()->isHidden());
        palette.setTemplateBin(binId);
        QCOMPARE(palette.shownEffects(), QStringList{QStringLiteral("Picture-in-Picture")});
        QCOMPARE(palette.templateAt(0), (std::pair{binId, pipId}));
        QVERIFY(!palette.effectAt(0));
        palette.setTemplateBin(QString());
        QVERIFY(palette.shownEffects().size() > 1);

        // Dragged from the bin (or the palette) to the second segment: the effect with its parameters (p.53).
        QMimeData fromBin;
        fromBin.setData(binItemsMime(), (binId + QLatin1Char('\n') + pipId).toUtf8());
        const auto dropped = timeline.droppedTemplate(&fromBin);
        QVERIFY(dropped);
        QVERIFY(timeline.dropTemplate(QStringLiteral("V1"), 12, *dropped));
        settle();
        QCOMPARE(v1At(record(), 1).effect, pip);
        // A clip dragged from the bin is not a template.
        QMimeData clip;
        clip.setData(binItemsMime(), (binId + QLatin1Char('\n') + ids.first()).toUtf8());
        QVERIFY(!timeline.droppedTemplate(&clip));

        // "drag it to the specific parameter group in the Effect Editor" (p.54): Position alone comes back.
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Move"), [&](model::Sequence& s) {
            model::Effect& e = *s.track(QStringLiteral("V1"))->segments[1].effect;
            e.parameters.insert(QStringLiteral("Position"), QJsonObject{{QStringLiteral("X"), 0}});
            e.parameters.insert(QStringLiteral("Scaling"), QJsonObject{{QStringLiteral("X"), 90}});
            return true;
        }));
        settle();
        composer.moveTo(MonitorId::Record, 12);
        editor.refresh();
        QVERIFY(!editor.applyTemplateGroup(QStringLiteral("Background Color"), *dropped)); // not one of its groups
        QVERIFY(editor.applyTemplateGroup(QStringLiteral("Position"), *dropped));
        settle();
        const model::Effect grouped = *v1At(record(), 1).effect;
        QCOMPARE(grouped.parameters.value(QStringLiteral("Position")).toObject().value(QStringLiteral("X")).toInt(), 120);
        QCOMPARE(grouped.parameters.value(QStringLiteral("Scaling")).toObject().value(QStringLiteral("X")).toInt(), 90);

        // A transition's template goes on another cut with its duration and alignment.
        composer.moveTo(MonitorId::Record, 21);
        editor.refresh();
        QVERIFY(editor.shown());
        QCOMPARE(editor.shown()->target, model::EffectTarget::Transition);
        const auto dissolve = editor.shownTemplate(false);
        QVERIFY(dissolve && dissolve->transition);
        QCOMPARE(dissolve->duration, 4);
        QVERIFY(timeline.dropTemplate(QStringLiteral("V1"), 10, *dissolve, 10));
        settle();
        QVERIFY(v1At(record(), 1).transition);
        QCOMPARE(v1At(record(), 1).transition->duration, 4);
        QCOMPARE(v1At(record(), 1).transition->alignment, model::TransitionAlignment::StartingAtCut);

        // Alt: the segment with its source media (p.53).
        composer.moveTo(MonitorId::Record, 3);
        editor.refresh();
        const auto withSource = editor.shownTemplate(true);
        QVERIFY(withSource && withSource->hasSource());

        // Promote Effects (p.54-55): a copy named ".Promoted.01", listed in the palette too.
        const QStringList promoted = view->promoteEffects({pipId, ids.first()});
        QCOMPARE(promoted.size(), 1);
        QCOMPARE(bin->bin().item(promoted.first())->name, QStringLiteral("Picture-in-Picture.Promoted.01"));
        settle();
        QCOMPARE(palette.templateBins().first().templates.size(), 2);

        // The Motion Effect Editor's Timewarp icon (p.182).
        QVERIFY(timeline.changeRecordSequence(QStringLiteral("Timewarp"), [](model::Sequence& s) {
            return model::applySegmentEffect(s, QStringLiteral("V1"), 35,
                                             model::Effect{QLatin1String(model::kTimewarpCategory), QStringLiteral("Timewarp"), {}});
        }));
        settle();
        MotionEffectEditor& motion = window.motionEffectEditor();
        composer.moveTo(MonitorId::Record, 2);
        motion.refresh();
        QVERIFY(!motion.timewarpIcon()->isEnabled());
        QVERIFY(!motion.timewarpTemplateMime());
        composer.moveTo(MonitorId::Record, 35);
        motion.refresh();
        QCOMPARE(motion.shown(), MotionEffectEditor::Shown::Timewarp);
        QVERIFY(motion.timewarpIcon()->isEnabled());
        {
            std::unique_ptr<QMimeData> mime(motion.timewarpTemplateMime());
            QVERIFY(mime);
            QVERIFY(view->itemModel()->dropMimeData(mime.get(), Qt::CopyAction, -1, -1, {}));
        }
        QCOMPARE(templateIds(*bin).size(), 3);
        const auto warp = model::effectTemplateOf(*bin->bin().item(templateIds(*bin).last()));
        QVERIFY(warp && model::isTimewarp(warp->effect));
    }
};

QTEST_MAIN(TestEffectTemplateUi)
#include "tst_effect_template_ui.moc"

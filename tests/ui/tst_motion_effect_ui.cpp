// Traditional motion effects in the application (FX Guide p.165-173; docs/spec/motion-effects.md):
// Composer > Freeze Frame > Other, the Motion Effect dialog's Variable Speed, reverse, Strobe Motion and
// Create and Render, the new clips in the bin and the Source monitor, and motion effects played in a
// sequence: a motion effect clip edited in, and a segment's own Motion Effect speed.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/model/motion_effect.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/motion_effect_dialogs.h"
#include "lumenis/ui/motion_effect_editor.h"
#include "lumenis/ui/render_dialogs.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps QuickTime file whose frame n is grey level 5n, picture only.
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

// The Record monitor's V1 segment at its position.
model::Segment recordSegment(Composer& composer) {
    const model::Sequence seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
    const model::Track* v1 = seq.track(QStringLiteral("V1"));
    return v1->segments.at(v1->segmentAt(composer.state(MonitorId::Record).position()));
}

// The source frame a monitor shows at `frame`, from its grey level; -1 for none.
int shownFrame(Composer& composer, MonitorId monitor, std::int64_t frame) {
    const QImage image = composer.pictureAt(monitor, frame, QSize(64, 36));
    if (image.isNull()) return -1;
    return static_cast<int>(std::lround(qRed(image.pixel(32, 18)) / 5.0));
}

} // namespace

class TestMotionEffectUi : public QObject {
    Q_OBJECT

private slots:
    void motionEffects() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("grey.mov"));
        QVERIFY(writeMovie(movie, 40));
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
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("motionEffectsVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        const model::BinItem original = *bin->bin().item(ids.first());
        Composer& composer = window.composer();

        // Freeze Frame > Other (p.166-167): a clip named "... FF" of the cued frame, in the bin and the
        // Source monitor.
        composer.loadDefault(binId, original);
        composer.moveTo(MonitorId::Source, 7);
        window.setFreezeFrameHook([](FreezeFrameDialog& d) {
            d.durationField()->setText(QStringLiteral("10"));
            return true;
        });
        commands.action(QStringLiteral("composer.freeze_frame.other"))->trigger();
        settle();
        const model::LoadedClip* src = composer.state(MonitorId::Source).current();
        QVERIFY(src);
        QCOMPARE(src->item.name, original.name + QStringLiteral(" FF"));
        QCOMPARE(src->item.kind, model::BinItemKind::MotionEffect);
        QCOMPARE(src->item.durationFrames, 10);
        QVERIFY(bin->bin().item(src->item.id));
        QCOMPARE(shownFrame(composer, MonitorId::Source, 0), 7);
        QCOMPARE(shownFrame(composer, MonitorId::Source, 9), 7);

        // Variable Speed at 50% of frames 10-19 (p.168-170): twice as long, each frame twice.
        composer.loadDefault(binId, original);
        composer.moveTo(MonitorId::Source, 10);
        composer.state(MonitorId::Source).markIn();
        composer.moveTo(MonitorId::Source, 19);
        composer.state(MonitorId::Source).markOut();
        composer.setActiveMonitor(MonitorId::Source);
        bool fitOffered = true;
        window.setMotionEffectHook([&](MotionEffectDialog& d) {
            fitOffered = d.fitToFillBox()->isEnabled(); // no Record marks
            d.percentField()->setValue(50);
            QCOMPARE(d.framesField()->value(), 20);
            QCOMPARE(d.fpsField()->value(), 12.5);
            d.choose(MotionEffectDialog::Choice::Create);
        });
        commands.action(QStringLiteral("fx.motion_effect"))->trigger();
        settle();
        QVERIFY(!fitOffered);
        src = composer.state(MonitorId::Source).current();
        const model::BinItem slow = src->item;
        QCOMPARE(slow.name, original.name + QStringLiteral(" (12.5FPS)"));
        QCOMPARE(slow.durationFrames, 20);
        QCOMPARE(shownFrame(composer, MonitorId::Source, 0), 10);
        QCOMPARE(shownFrame(composer, MonitorId::Source, 5), 12);
        QCOMPARE(shownFrame(composer, MonitorId::Source, 19), 19);

        // Reverse with Strobe Motion: from the OUT back, each 3rd frame held.
        composer.loadDefault(binId, original);
        composer.moveTo(MonitorId::Source, 10);
        composer.state(MonitorId::Source).markIn();
        composer.moveTo(MonitorId::Source, 19);
        composer.state(MonitorId::Source).markOut();
        composer.setActiveMonitor(MonitorId::Source);
        window.setMotionEffectHook([](MotionEffectDialog& d) {
            d.percentField()->setValue(-100);
            d.strobeBox()->setChecked(true);
            d.strobeField()->setValue(3);
            d.choose(MotionEffectDialog::Choice::Create);
        });
        window.motionEffect();
        settle();
        src = composer.state(MonitorId::Source).current();
        QCOMPARE(src->item.name, original.name + QStringLiteral(" (-25FPS) (Strobe: 3)"));
        QCOMPARE(shownFrame(composer, MonitorId::Source, 0), 19);
        QCOMPARE(shownFrame(composer, MonitorId::Source, 2), 19);
        QCOMPARE(shownFrame(composer, MonitorId::Source, 3), 16);

        // Create and Render (p.169): its own media on the Target Drive, at double speed (the clip keeps
        // its marks 10-19 in the Source monitor).
        composer.loadDefault(binId, original);
        composer.setActiveMonitor(MonitorId::Source);
        window.setMotionEffectHook([](MotionEffectDialog& d) {
            d.percentField()->setValue(200);
            d.choose(MotionEffectDialog::Choice::CreateAndRender);
        });
        const auto fast = window.motionEffect();
        QVERIFY(fast);
        QCOMPARE(fast->durationFrames, 5);
        QVERIFY(fast->link.value(QStringLiteral("path")).toString().startsWith(QDir::cleanPath(drive)));
        const int rendered = shownFrame(composer, MonitorId::Source, 3);
        QVERIFY2(std::abs(rendered - 16) <= 1, qPrintable(QString::number(rendered))); // DNx is not lossless

        // With the Record monitor active, the button does not make a clip (the Motion Effect Editor is 6.5c).
        composer.setActiveMonitor(MonitorId::Record);
        QVERIFY(!window.motionEffect());

        // A motion effect clip edited into a sequence plays its frames there.
        composer.loadDefault(binId, slow);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        settle();
        QCOMPARE(shownFrame(composer, MonitorId::Record, 0), 10);
        QCOMPARE(shownFrame(composer, MonitorId::Record, 7), 13);

        // A segment's own Motion Effect (Fit to Fill's, an import's) plays at its speed too.
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Speed"), [&](model::Sequence& s) {
            model::Segment& seg = s.track(QStringLiteral("V1"))->segments[0];
            seg.clipId = original.id;
            seg.binId = binId;
            seg.sourceIn = 4;
            seg.effect = model::motionEffect(0.5);
            return true;
        }));
        settle();
        QCOMPARE(shownFrame(composer, MonitorId::Record, 6), 7);

        // Timewarp (p.174-196): Reverse Motion applied to the 20-frame segment plays it backwards ...
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Apply"), [&](model::Sequence& s) {
            return model::applySegmentEffect(s, QStringLiteral("V1"), 0,
                                             model::Effect{QLatin1String(model::kTimewarpCategory), QStringLiteral("Reverse Motion"), {}});
        }));
        settle();
        QCOMPARE(shownFrame(composer, MonitorId::Record, 0), 23);
        QCOMPARE(shownFrame(composer, MonitorId::Record, 19), 4);
        // ... and a blended Type at 50% shows in-between frames as a mix of the two around them (p.186).
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Type"), [&](model::Sequence& s) {
            model::Effect e = model::promotedTimewarp(0.5);
            model::Timewarp t = model::timewarpOf(e);
            t.type = model::TimewarpType::BlendedInterpolated;
            model::setTimewarp(e, t);
            s.track(QStringLiteral("V1"))->segments[0].effect = e;
            return true;
        }));
        settle();
        const QImage between = composer.pictureAt(MonitorId::Record, 3, QSize(64, 36)); // source 5.5
        QVERIFY(!between.isNull());
        QVERIFY2(std::abs(qRed(between.pixel(32, 18)) - 27) <= 2, qPrintable(QString::number(qRed(between.pixel(32, 18)))));
        QCOMPARE(shownFrame(composer, MonitorId::Record, 4), 6);
        // FluidMotion follows the motion between them (6.5d1); these frames are flat, so it is the mix too.
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Type"), [&](model::Sequence& s) {
            model::Effect e = *s.track(QStringLiteral("V1"))->segments[0].effect;
            model::Timewarp t = model::timewarpOf(e);
            t.type = model::TimewarpType::FluidMotion;
            model::setTimewarp(e, t);
            s.track(QStringLiteral("V1"))->segments[0].effect = e;
            return true;
        }));
        settle();
        const QImage fluid = composer.pictureAt(MonitorId::Record, 3, QSize(64, 36));
        QVERIFY2(std::abs(qRed(fluid.pixel(32, 18)) - 27) <= 2, qPrintable(QString::number(qRed(fluid.pixel(32, 18)))));

        // The Motion Effect Editor (p.178-189): the Motion Effect button with the Record monitor active
        // opens it on the Timewarp at the position.
        MotionEffectEditor& editor = window.motionEffectEditor();
        composer.moveTo(MonitorId::Record, 0);
        composer.setActiveMonitor(MonitorId::Record);
        QVERIFY(!window.motionEffect());
        settle();
        QCOMPARE(editor.shown(), MotionEffectEditor::Shown::Timewarp);
        QVERIFY(!editor.promoteButton()->isVisible());
        QCOMPARE(editor.speedGraph()->values().size(), 20);
        QCOMPARE(editor.speedGraph()->values().at(5), 50.0);
        QCOMPARE(editor.positionGraph()->values().at(4), 2.0);
        // Add Keyframe at 0 and at 10, then 200% at the second: frames after it go twice as fast.
        editor.addKeyframe();
        composer.moveTo(MonitorId::Record, 10);
        settle();
        editor.addKeyframe();
        QCOMPARE(editor.selectedKeyframe(), 1);
        editor.valueField()->setValue(200);
        emit editor.valueField()->editingFinished();
        settle();
        auto timewarp = [&] { return *recordSegment(composer).effect; };
        QCOMPARE(model::keyframesOf(timewarp(), QLatin1String(model::kSpeedCategory), QLatin1String(model::kSpeedParameter)).keys.size(), 2);
        // Shelf holds 50% until the second keyframe, then 200%.
        editor.setInterpolation(model::Interpolation::Shelf);
        settle();
        QCOMPARE(model::timewarpPosition(timewarp(), 10, 20), 5.0);
        QCOMPARE(model::timewarpPosition(timewarp(), 12, 20), 9.0);
        // Go to Previous Event; the Source Anchor Frame at the active keyframe keeps the picture.
        editor.goToEvent(false);
        settle();
        QCOMPARE(composer.state(MonitorId::Record).position(), 0);
        editor.selectKeyframe(1);
        editor.anchorAtActiveKeyframe();
        settle();
        QCOMPARE(model::timewarpOf(timewarp()).anchor, 5.0);
        QCOMPARE(model::timewarpPosition(timewarp(), 12, 20), 9.0);
        // Edit Graph: Position is kept apart; the effect now follows it (identity without keyframes).
        editor.editGraphMenu()->setCurrentIndex(1);
        settle();
        QVERIFY(model::timewarpOf(timewarp()).positionGraph);
        QCOMPARE(model::timewarpPosition(timewarp(), 12, 20), 12.0);
        editor.editGraphMenu()->setCurrentIndex(0);
        settle();
        // Strobe and Type.
        editor.strobeField()->setValue(2);
        emit editor.strobeField()->editingFinished();
        editor.typeMenu()->setCurrentIndex(editor.typeMenu()->findData(QStringLiteral("both")));
        settle();
        QCOMPARE(model::timewarpOf(timewarp()).strobe, 2);
        QCOMPARE(model::timewarpOf(timewarp()).type, model::TimewarpType::BothFields);
        // The Render setting's Timewarps Render Using dims the Type and offers Ignore Render Setting
        // (p.183); a render then changes the Type (p.173).
        window.settings().setValue(QStringLiteral("Render"), QStringLiteral("timewarpsRenderUsing"), QStringLiteral("blendedVtr"));
        settle();
        QVERIFY(!editor.typeMenu()->isEnabled());
        QVERIFY(editor.ignoreRenderSettingBox()->isVisibleTo(&editor));
        QCOMPARE(model::renderTypeFor(timewarp(), QStringLiteral("blendedVtr")), model::TimewarpType::BlendedVtr);
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("renderVideoDrive"), drive);
        window.setRenderHook([](RenderEffectDialog&) { return true; });
        window.renderEffects(true);
        settle();
        QCOMPARE(model::timewarpOf(timewarp()).type, model::TimewarpType::BlendedVtr);
        QVERIFY(recordSegment(composer).render);
        window.settings().setValue(QStringLiteral("Render"), QStringLiteral("timewarpsRenderUsing"), QStringLiteral("original"));
        settle();
        QVERIFY(editor.typeMenu()->isEnabled());

        // A traditional motion effect shows its speed and the Promote button (p.177).
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Speed"), [&](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect = model::motionEffect(0.5);
            return true;
        }));
        settle();
        QCOMPARE(editor.shown(), MotionEffectEditor::Shown::Traditional);
        QVERIFY(editor.promoteButton()->isVisibleTo(&editor));
        QCOMPARE(editor.speedLabel()->text(), QStringLiteral("50%"));
        editor.promote();
        settle();
        QCOMPARE(editor.shown(), MotionEffectEditor::Shown::Timewarp);
        QVERIFY(model::isTimewarp(timewarp()));
        QCOMPARE(shownFrame(composer, MonitorId::Record, 6), 7);
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestMotionEffectUi)
#include "tst_motion_effect_ui.moc"

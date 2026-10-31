// Mixed rate clips in the application (Editing Guide p.468-478; docs/spec/motion-effects.md, 6.5d2): a
// 25 fps file linked into a 30 fps project plays at the project's rate in the Source monitor and in a
// sequence, its in-between frames blended by the Render setting's Use frame blending in Motion Adapters;
// the "(25 fps)" name in the Timeline, the adapter in the Motion Effect Editor (Type, Promote), markers
// stored back in the clip's own frames, and Refresh Sequence after a Field Motion change.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/motion_effect_editor.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps QuickTime file whose frame n is grey level 10n, picture only.
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
        image.fill(QColor(10 * i, 10 * i, 10 * i));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

// The grey level a monitor shows at `frame`; -1 for none.
int grey(Composer& composer, MonitorId monitor, std::int64_t frame) {
    const QImage image = composer.pictureAt(monitor, frame, QSize(64, 36));
    if (image.isNull()) return -1;
    return qRed(image.pixel(32, 18));
}

model::Segment firstV1(Composer& composer) {
    return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
}

} // namespace

class TestMotionAdapterUi : public QObject {
    Q_OBJECT

private slots:
    void mixedRateClip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString movie = dir.filePath(QStringLiteral("grey.mov"));
        QVERIFY(writeMovie(movie, 20));
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
        project.settings.format.editRate = Rational(30);
        project.settings.format.width = 320;
        project.settings.format.height = 180;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        const model::BinItem original = *bin->bin().item(ids.first());
        // Linked at the project's rate, its own frames the file's (p.469: a mixed rate clip).
        QCOMPARE(original.editRate, Rational(30));
        QVERIFY(model::isMixedRate(original, Rational(30)));
        QCOMPARE(model::cellText(original, QLatin1String(model::kFieldMotionHeading)), QStringLiteral("Progressive"));
        Composer& composer = window.composer();

        // The Source monitor plays it at the project's rate with an adapter; frame 3 (0.1 s) is half way
        // between the file's frames 2 and 3: Blended Interpolated mixes them (p.1215).
        composer.loadDefault(binId, original);
        const model::LoadedClip* src = composer.state(MonitorId::Source).current();
        QVERIFY(src && model::isAdapted(src->item));
        QCOMPARE(src->item.durationFrames, 24);
        QVERIFY(std::abs(grey(composer, MonitorId::Source, 3) - 25) <= 2);
        QCOMPARE(grey(composer, MonitorId::Source, 6), 50);
        // Without frame blending the file frame at the time shows (Both Fields for progressive clips).
        window.settings().setValue(QStringLiteral("Render"), QStringLiteral("motionAdapterBlending"), false);
        settle();
        composer.loadDefault(binId, original);
        QVERIFY(std::abs(grey(composer, MonitorId::Source, 3) - 20) <= 1);
        window.settings().setValue(QStringLiteral("Render"), QStringLiteral("motionAdapterBlending"), true);
        settle();
        composer.loadDefault(binId, original);

        // A marker added in the Source monitor goes back into the bin's clip, not its adapted copy; a
        // clip linked at the project's rate keeps the frame.
        composer.setActiveMonitor(MonitorId::Source);
        composer.moveTo(MonitorId::Source, 6);
        window.timeline().setMarkerDialogEnabled(false);
        QVERIFY(window.timeline().addMarker());
        settle();
        const model::BinItem stored = *bin->bin().item(original.id);
        QVERIFY(!model::isAdapted(stored));
        QCOMPARE(stored.editRate, Rational(30));
        QCOMPARE(model::markersOf(stored).size(), 1);
        QCOMPARE(model::markersOf(stored).at(0).frame, 6);

        // Edited into a sequence: the segment carries the adapter and plays the same frames.
        composer.state(MonitorId::Source).clearBoth();
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        QVERIFY(composer.state(MonitorId::Record).current());
        model::Segment seg = firstV1(composer);
        QVERIFY(seg.adapter);
        QCOMPARE(seg.adapter->clipRate, Rational(25));
        QCOMPARE(seg.length, 24);
        QVERIFY(std::abs(grey(composer, MonitorId::Record, 3) - 25) <= 2);
        // "playtime (59.94 fps)" (p.470).
        const model::Sequence seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QVERIFY(window.timeline().clipText(*seq.track(QStringLiteral("V1")), seg).contains(QStringLiteral("(25 fps)")));

        // The Motion Effect Editor shows the adapter: Type and Promote active (p.471).
        MotionEffectEditor& editor = window.motionEffectEditor();
        composer.moveTo(MonitorId::Record, 3);
        composer.setActiveMonitor(MonitorId::Record);
        QVERIFY(!window.motionEffect());
        settle();
        QCOMPARE(editor.shown(), MotionEffectEditor::Shown::Adapter);
        QVERIFY(editor.typeMenu()->isEnabled());
        QVERIFY(editor.promoteButton()->isVisible());
        QVERIFY(!editor.strobeField()->isEnabled());
        // Duplicated Field shows the file frame at the time.
        editor.typeMenu()->setCurrentIndex(editor.typeMenu()->findData(model::timewarpTypeName(model::TimewarpType::DuplicatedField)));
        settle();
        QCOMPARE(firstV1(composer).adapter->type, model::timewarpTypeName(model::TimewarpType::DuplicatedField));
        QVERIFY(std::abs(grey(composer, MonitorId::Record, 3) - 20) <= 1);

        // Refresh Sequence > Motion Adapters/Timewarps takes the clip's new Field Motion (p.478).
        QVERIFY(bin->setField(original.id, QLatin1String(model::kFieldMotionHeading), QStringLiteral("Interlaced")));
        QVERIFY(!bin->setField(original.id, QLatin1String(model::kFieldMotionHeading), QStringLiteral("Sideways"))); // not a choice
        window.timeline().setFocus();
        QCOMPARE(window.refreshMotionAdapters(), 1);
        QCOMPARE(firstV1(composer).adapter->fieldMotion, QStringLiteral("Interlaced"));

        // Promote: a Timewarp of the adapter's Type over it; Remove Effect leaves the adapter (p.469).
        settle();
        editor.promote();
        settle();
        seg = firstV1(composer);
        QVERIFY(seg.effect && model::isTimewarp(*seg.effect));
        QCOMPARE(model::timewarpOf(*seg.effect).type, model::TimewarpType::DuplicatedField);
        QVERIFY(seg.adapter);
        QVERIFY(std::abs(grey(composer, MonitorId::Record, 3) - 20) <= 1);
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("Remove Effect"), [](model::Sequence& s) {
            s.track(QStringLiteral("V1"))->segments[0].effect.reset();
            return true;
        }));
        settle();
        QVERIFY(firstV1(composer).adapter);
        QCOMPARE(editor.shown(), MotionEffectEditor::Shown::Adapter);
    }
};

QTEST_MAIN(TestMotionAdapterUi)
#include "tst_motion_adapter_ui.moc"

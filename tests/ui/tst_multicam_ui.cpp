// Group clips in the application (8.1a; Editing Guide p.1123-1131; docs/spec/multicamera.md): Clip > Group Clips on
// two linked movies synced by IN points, the group in the Source monitor full size and in Quad Split view, Previous
// In Group and Next In Group in the Source and Record monitors (and the Up and Down Arrow keys), Swap Cam Bank, and
// a group edited into a sequence with "(G)" in the Timeline. The Group Clips dialog's options too.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/sequence.h"
#include "lumenis/core/project_format.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/group_clips_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 25 fps QuickTime file whose frame n is `colour` scaled by n (red 5n for (1, 0), green 5n + 20 for (0, 1)).
bool writeMovie(const QString& path, int frames, bool green) {
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
        image.fill(green ? QColor(0, 5 * i + 20, 0) : QColor(5 * i, 0, 0));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

// The colour at a point of a monitor's picture at `frame`; invalid for no picture.
QColor colourAt(Composer& composer, MonitorId monitor, std::int64_t frame, QSize size = QSize(64, 36), QPoint at = QPoint(32, 18)) {
    const QImage image = composer.pictureAt(monitor, frame, size);
    if (image.isNull()) return {};
    return image.pixelColor(at);
}

bool near(int a, int b) {
    return std::abs(a - b) <= 2;
}

} // namespace

class TestMulticamUi : public QObject {
    Q_OBJECT

private slots:
    void dialog() {
        GroupClipsDialog d(false);
        QCOMPARE(d.sync(), model::GroupSync::Inpoints);
        // Dimmed outside a 24p or 25p project; the ink options are not shown there.
        QVERIFY(!d.button(model::GroupSync::FilmTc)->isEnabled());
        QVERIFY(d.button(model::GroupSync::Ink)->isHidden());
        QVERIFY(!d.button(model::GroupSync::Waveform)->isEnabled());
        QCOMPARE(d.auxMenu()->count(), 5);
        d.setSync(model::GroupSync::AuxTimecode, 3);
        QCOMPARE(d.sync(), model::GroupSync::AuxTimecode);
        QCOMPARE(d.auxTc(), 3);
        GroupClipsDialog film(true);
        QVERIFY(!film.button(model::GroupSync::Ink)->isHidden());
    }

    void groupClipsInTheApplication() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("cam-a.mov"));
        const QString b = dir.filePath(QStringLiteral("cam-b.mov"));
        QVERIFY(writeMovie(a, 40, false));
        QVERIFY(writeMovie(b, 40, true));
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
        const QStringList ids = window.linkFiles(binId, {a, b});
        QCOMPARE(ids.size(), 2);
        Composer& composer = window.composer();
        // The sync point: IN at frame 10 of A and frame 0 of B (p.1124, step 1).
        composer.loadDefault(binId, *bin->bin().item(ids[0]));
        composer.moveTo(MonitorId::Source, 10);
        composer.state(MonitorId::Source).markIn();
        composer.loadDefault(binId, *bin->bin().item(ids[1]));
        composer.moveTo(MonitorId::Source, 0);
        composer.state(MonitorId::Source).markIn();

        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds(ids);
        window.setGroupClipsHook([] { return std::optional<std::pair<model::GroupSync, int>>({model::GroupSync::Inpoints, 1}); });
        const auto groupId = window.groupClips();
        QVERIFY(groupId);
        const model::BinItem group = *bin->bin().item(*groupId);
        QCOMPARE(group.name, QStringLiteral("cam-a.Grp.1"));
        QCOMPARE(group.durationFrames, std::int64_t(50));
        QCOMPARE(view->selectedIds(), QStringList{*groupId});

        // Full-Monitor display: the selected angle (A) at group frame 15 is A's frame 15.
        composer.loadDefault(binId, group);
        composer.setActiveMonitor(MonitorId::Source);
        QVERIFY(near(colourAt(composer, MonitorId::Source, 15).red(), 75));
        // Next In Group: B, whose frame 5 is at group frame 15; the group in the bin follows (p.1131).
        commands.action(QStringLiteral("mcam.next_in_group"))->trigger();
        settle();
        QCOMPARE(model::groupOf(*bin->bin().item(*groupId))->selected, 1);
        QVERIFY(near(colourAt(composer, MonitorId::Source, 15).green(), 45));
        // Before B starts it shows nothing.
        const QColor before = colourAt(composer, MonitorId::Source, 5);
        QVERIFY(!before.isValid() || before.green() < 5);

        // Quad Split: A top left, B top right (selected), the rest black (p.1127).
        commands.action(QStringLiteral("mcam.quad_split"))->trigger();
        QCOMPARE(composer.groupSplit(), 4);
        const QSize quad(128, 72);
        QVERIFY(near(colourAt(composer, MonitorId::Source, 15, quad, QPoint(32, 18)).red(), 75));
        QVERIFY(near(colourAt(composer, MonitorId::Source, 15, quad, QPoint(96, 18)).green(), 45));
        QCOMPARE(colourAt(composer, MonitorId::Source, 15, quad, QPoint(32, 54)), QColor(Qt::black));
        // One bank of four: Swap Cam Bank stays on it.
        commands.action(QStringLiteral("mcam.swap_cam_bank"))->trigger();
        QCOMPARE(composer.groupBank(), 0);
        // The Quad Split button again: Full-Monitor display.
        commands.action(QStringLiteral("mcam.quad_split"))->trigger();
        QCOMPARE(composer.groupSplit(), 1);

        // Edited into a sequence: the selected angle (B), named for it with "(G)" in the Timeline (p.1131).
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
        QVERIFY(rec);
        model::Sequence seq = model::Sequence::fromItem(rec->item);
        const model::Track* v1 = seq.track(QStringLiteral("V1"));
        QVERIFY(v1 && !v1->segments.isEmpty());
        QCOMPARE(v1->segments.first().groupAngle, std::optional<int>(1));
        QCOMPARE(v1->segments.first().name, QStringLiteral("cam-b"));
        QVERIFY(window.timeline().clipText(*v1, v1->segments.first()).contains(QStringLiteral("cam-b (G)")));
        QVERIFY(near(colourAt(composer, MonitorId::Record, 15).green(), 45));

        // Previous In Group in the Record monitor switches the segment at the position, as an undo step.
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, 15);
        commands.action(QStringLiteral("mcam.previous_in_group"))->trigger();
        settle();
        seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.first().groupAngle, std::optional<int>(0));
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.first().name, QStringLiteral("cam-a"));
        QVERIFY(near(colourAt(composer, MonitorId::Record, 15).red(), 75));
        // The audio segments keep their angle (no Audio Follow Video yet).
        if (const model::Track* a1 = seq.track(QStringLiteral("A1")); a1 && !a1->segments.isEmpty()) {
            QCOMPARE(a1->segments.first().groupAngle, std::optional<int>(1));
        }
        // The Down Arrow (Move Clip Down's key) with no segments selected: Next In Group.
        QVERIFY(window.timeline().perform(QStringLiteral("edit.move_clip_down")));
        settle();
        seq = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QCOMPARE(seq.track(QStringLiteral("V1"))->segments.first().groupAngle, std::optional<int>(1));
        // In the Source monitor the arrows switch the loaded group.
        composer.setActiveMonitor(MonitorId::Source);
        QVERIFY(window.timeline().perform(QStringLiteral("edit.move_clip_up")));
        settle();
        QCOMPARE(model::groupOf(*bin->bin().item(*groupId))->selected, 0);
    }
};

QTEST_MAIN(TestMulticamUi)
#include "tst_multicam_ui.moc"

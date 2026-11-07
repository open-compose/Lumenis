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
#include <QMenu>
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

// Noise whose loudness changes every 20 ms by a fixed pattern, its first `delay` seconds silent: the same sound later.
std::vector<float> pattern(int samples, int rate, int delay) {
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    std::uint32_t level = 5;
    std::uint32_t noise = 12345;
    const auto block = static_cast<std::size_t>(rate / 50);
    const auto skip = static_cast<std::size_t>(delay);
    float amp = 0.0f;
    for (std::size_t i = skip; i < out.size(); ++i) {
        if ((i - skip) % block == 0) {
            level = level * 1664525u + 1013904223u;
            amp = static_cast<float>((level >> 24) & 0xff) / 255.0f;
        }
        noise = noise * 22695477u + 1u;
        out[i] = amp * (static_cast<float>((noise >> 16) & 0x7fff) / 16384.0f - 1.0f) * 0.5f;
    }
    return out;
}

// A 25 fps QuickTime file of `frames` grey frames with mono sound: the pattern, `delayFrames` frames late.
bool writeSoundMovie(const QString& path, int frames, int delayFrames) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.channels = 1;
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    const std::vector<float> sound = pattern(frames * 1920, 48000, delayFrames * 1920);
    for (int i = 0; i < frames; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(QColor(100, 100, 100));
        if (!w.writeVideo(image) || !w.writeAudio(sound.data() + static_cast<std::ptrdiff_t>(i) * 1920, 1920)) return false;
    }
    return w.finish();
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
        QVERIFY(d.button(model::GroupSync::Waveform)->isEnabled());
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

    // MultiCamera mode (p.1129-1132): the Source monitor's split follows the Record monitor's group segment, the
    // keypad and clicks choose its angle, live cuts while playing, and the Source monitor stays inactive.
    void multiCameraMode() {
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
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        const QStringList ids = window.linkFiles(binId, {a, b});
        QCOMPARE(ids.size(), 2);
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds(ids);
        // Synced by timecode: both start together.
        window.setGroupClipsHook([] { return std::optional<std::pair<model::GroupSync, int>>({model::GroupSync::SourceTimecode, 1}); });
        const auto groupId = window.groupClips();
        QVERIFY(groupId);
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(*groupId));
        composer.setActiveMonitor(MonitorId::Source);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        QVERIFY(composer.state(MonitorId::Record).current());

        QAction* mode = commands.action(QStringLiteral("composer.multicamera_mode"));
        QVERIFY(mode && mode->isCheckable());
        mode->setChecked(true);
        QVERIFY(composer.multiCameraMode());
        QCOMPARE(composer.groupSplit(), 4);
        QCOMPARE(composer.activeMonitor(), MonitorId::Record);
        composer.setActiveMonitor(MonitorId::Source);
        QCOMPARE(composer.activeMonitor(), MonitorId::Record);
        // The Source monitor shows the sequence's group at the Record monitor's position: A and B at frame 15.
        composer.moveTo(MonitorId::Record, 15);
        const QSize quad(128, 72);
        QVERIFY(near(colourAt(composer, MonitorId::Source, 0, quad, QPoint(32, 18)).red(), 75));
        QVERIFY(near(colourAt(composer, MonitorId::Source, 0, quad, QPoint(96, 18)).green(), 95));
        const auto angle = [&] {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.first().groupAngle;
        };
        QCOMPARE(angle(), std::optional<int>(0));
        // Keypad 9 (upper right): B; a click on the upper left: A again.
        QVERIFY(composer.keypadPressed(Qt::Key_9));
        QCOMPARE(angle(), std::optional<int>(1));
        QVERIFY(near(colourAt(composer, MonitorId::Record, 15).green(), 95));
        QVERIFY(composer.pictureClicked(MonitorId::Source, QPointF(0.2, 0.2), Qt::LeftButton));
        QCOMPARE(angle(), std::optional<int>(0));
        // The right button swaps banks (one bank of four here: it stays).
        QVERIFY(composer.pictureClicked(MonitorId::Source, QPointF(0.5, 0.5), Qt::RightButton));
        QCOMPARE(composer.groupBank(), 0);
        // A live cut: the angle changes from the position on.
        QVERIFY(window.timeline().setGroupAngle(1, true));
        const model::Sequence cut = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QCOMPARE(cut.track(QStringLiteral("V1"))->segments.size(), qsizetype(2));
        QCOMPARE(cut.track(QStringLiteral("V1"))->segments.at(0).length, std::int64_t(15));
        QCOMPARE(cut.track(QStringLiteral("V1"))->segments.at(1).groupAngle, std::optional<int>(1));
        // Audio Follow Video: A1 switches with the picture (p.1133); the Group menu lists the angles.
        QMenu* menu = composer.groupMenu();
        QCOMPARE(menu->actions().size(), 4); // two angles, a separator, Audio Follow Video
        QVERIFY(menu->actions().at(1)->isChecked()); // the segment at 15 shows B since the live cut
        menu->findChild<QAction*>(QStringLiteral("audioFollowVideo"))->setChecked(true);
        delete menu;
        QVERIFY(composer.audioFollowVideo());
        composer.moveTo(MonitorId::Record, 5);
        QVERIFY(composer.keypadPressed(Qt::Key_8)); // upper right: B, on V1 and A1
        model::Sequence afv = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QCOMPARE(afv.track(QStringLiteral("V1"))->segments.at(0).groupAngle, std::optional<int>(1));
        // (These movies have no sound, so the sequence has no A1; scenario 46-audio-follow-video.json covers A1.)
        if (const model::Track* a1 = afv.track(QStringLiteral("A1")); a1 && !a1->segments.isEmpty()) {
            QCOMPARE(a1->segments.at(0).groupAngle, std::optional<int>(1));
        }
        composer.setAudioFollowVideo(false);
        // A Multi-angle View menu: the lower left shows B, then the sequence (p.1134).
        QMenu* angles = window.multiAngleMenu(2);
        QVERIFY(angles);
        angles->actions().at(1)->trigger();
        QCOMPARE(composer.splitCells().value(2), 1);
        QVERIFY(near(colourAt(composer, MonitorId::Source, 0, quad, QPoint(32, 54)).green(), 45)); // B at frame 5
        angles->actions().last()->trigger();
        QCOMPARE(composer.splitCells().value(2), Composer::kSplitSequence);
        delete angles;
        // Match Frame from the group segment: the group in the Source monitor, the segment's angle selected (p.1134).
        mode->setChecked(false);
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, 5);
        QVERIFY(window.timeline().perform(QStringLiteral("other.match_frame")));
        QCOMPARE(composer.state(MonitorId::Source).current()->item.id, *groupId);
        QCOMPARE(model::groupOf(*bin->bin().item(*groupId))->selected, 1);
        // And from the group in the Source monitor: its selected clip (B) at the same frame.
        QVERIFY(window.timeline().perform(QStringLiteral("other.match_frame")));
        QCOMPARE(composer.state(MonitorId::Source).current()->item.id, ids[1]);
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(5));
        // Commit Multicam Edits: a duplicate whose segments are the angles' clips (p.1135).
        const QString cutId = composer.state(MonitorId::Record).current()->item.id;
        view->selectIds({cutId});
        const QStringList committed = window.commitMulticamEdits();
        QCOMPARE(committed.size(), 1);
        const model::Sequence plain = model::Sequence::fromItem(*bin->bin().item(committed.first()));
        for (const model::Segment& seg : plain.track(QStringLiteral("V1"))->segments) {
            QVERIFY(!seg.groupAngle);
            QVERIFY(seg.clipId == ids[0] || seg.clipId == ids[1]);
        }
        QVERIFY(model::Sequence::fromItem(*bin->bin().item(cutId)).track(QStringLiteral("V1"))->segments.first().groupAngle);
        mode->setChecked(true);
        // Leaving the mode.
        mode->setChecked(false);
        QVERIFY(!composer.multiCameraMode());
        composer.setActiveMonitor(MonitorId::Source);
        QCOMPARE(composer.activeMonitor(), MonitorId::Source);
    }

    // Waveform Analysis grouping, Edit Group, Update Group, Create Group, Refresh Sequence > Group Clips and Waveform
    // Sync (p.478, p.1124-1126) on two cameras whose sounds are the same, the second 10 frames later.
    void soundAndGroupEdits() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(QStringLiteral("cam-a.mov"));
        const QString b = dir.filePath(QStringLiteral("cam-b.mov"));
        QVERIFY(writeSoundMovie(a, 100, 0));
        QVERIFY(writeSoundMovie(b, 100, 10));
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
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {a, b});
        QCOMPARE(ids.size(), 2);
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds(ids);
        // Waveform Analysis: A's frame 0 sounds with B's frame 10.
        window.setGroupClipsHook([] { return std::optional<std::pair<model::GroupSync, int>>({model::GroupSync::Waveform, 1}); });
        const auto groupId = window.groupClips();
        QVERIFY(groupId);
        QCOMPARE(model::groupOf(*bin->bin().item(*groupId))->angles[0].offset, std::int64_t(-10));
        QCOMPARE(model::groupOf(*bin->bin().item(*groupId))->angles[1].offset, std::int64_t(0));

        // An edit of the group into a sequence, for the refresh below.
        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(*groupId));
        composer.setActiveMonitor(MonitorId::Source);
        composer.state(MonitorId::Source).setPosition(20);
        composer.state(MonitorId::Source).markIn();
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        const QString cutId = composer.state(MonitorId::Record).current()->item.id;

        // Edit Group: the angles on V1 and V2; B moved 5 frames later; Update Group keeps the group, A selected.
        view->selectIds({*groupId});
        const auto editId = window.editGroup();
        QVERIFY(editId);
        model::Sequence edit = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        QCOMPARE(edit.editedGroup(), (QStringList{binId, *groupId}));
        QCOMPARE(edit.track(QStringLiteral("V1"))->segments.at(1).clipId, ids[0]); // A after 10 frames of filler
        QVERIFY(window.timeline().changeRecordSequence(QStringLiteral("slide"), [](model::Sequence& seq) {
            seq.track(QStringLiteral("V2"))->segments.prepend(model::Segment::filler(5));
            return true;
        }));
        window.setUpdateGroupHook([] { return 0; });
        QCOMPARE(window.updateGroup(), std::optional<QString>(*groupId));
        const auto updated = model::groupOf(*bin->bin().item(*groupId));
        QCOMPARE(updated->angles[1].offset, std::int64_t(-5));
        QCOMPARE(updated->selected, 0);
        // Create Group from the same sequence: a new group in the bin.
        const auto created = window.createGroupFromSequence();
        QVERIFY(created && *created != *groupId);
        QCOMPARE(bin->bin().item(*created)->name, QStringLiteral("cam-a.Grp.2"));

        // Refresh Sequence > Group Clips on the cut: A's segment keeps showing the same frames of A.
        const model::BinItem cutBefore = *bin->bin().item(cutId);
        const model::Segment before = model::Sequence::fromItem(cutBefore).track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(before.groupOffsets, (QList<std::int64_t>{-10, 0}));
        composer.load(MonitorId::Record, binId, cutBefore);
        window.timeline().setFocus();
        QCOMPARE(window.refreshGroupEdits(), 2); // V1 and A1
        const model::Segment after =
            model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(after.groupOffsets, (QList<std::int64_t>{-10, -5}));
        QCOMPARE(after.sourceIn, before.sourceIn); // A, the shown angle, did not move
        QCOMPARE(window.refreshGroupEdits(), 0);

        // Waveform Sync: A's picture on V1 at the start, B's sound on A1; A moves to where its sound matches B's.
        model::BinItem syncItem = model::newSequence(bin->bin(), Rational(25), QDateTime::currentDateTimeUtc());
        model::Sequence sync = model::Sequence::withDefaultTracks();
        model::Segment picture;
        picture.clipId = ids[0];
        picture.binId = binId;
        picture.name = QStringLiteral("cam-a");
        picture.sourceTrack = QStringLiteral("V1");
        picture.length = 50;
        model::Segment sound = picture;
        sound.clipId = ids[1];
        sound.name = QStringLiteral("cam-b");
        sound.sourceTrack = QStringLiteral("A1");
        sound.length = 60;
        sync.track(QStringLiteral("V1"))->segments = {picture};
        sync.track(QStringLiteral("A1"))->segments = {sound};
        sync.storeIn(syncItem);
        bin->addItem(syncItem);
        composer.load(MonitorId::Record, binId, syncItem);
        window.timeline().setSelection({{QStringLiteral("V1"), 0}, {QStringLiteral("A1"), 0}});
        QVERIFY2(window.timeline().waveformSync(), qPrintable(window.timeline().lastMessage()));
        const model::Sequence synced = model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item);
        const model::Track* v1 = synced.track(QStringLiteral("V1"));
        QCOMPARE(v1->segments.size(), qsizetype(2));
        QVERIFY(v1->segments.at(0).isFiller());
        QCOMPARE(v1->segments.at(0).length, std::int64_t(10));
        QCOMPARE(v1->segments.at(1).clipId, ids[0]);
    }
};

QTEST_MAIN(TestMulticamUi)
#include "tst_multicam_ui.moc"

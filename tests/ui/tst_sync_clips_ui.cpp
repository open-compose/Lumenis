// Sync Clips in the application (9.3, the guide's AutoSync; Editing Guide p.496-498; docs/spec/sync-clips.md): the Sync
// Selection dialog's options and defaults, Clip > Sync Clips on a bin's picture and sound clips, the synced subclip in
// the bin and its picture in the Source monitor, and the message when the selection cannot be synced.
#include "lumenis/ext/provider.h"
#include "lumenis/media/audio_cd.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/sync_clips.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/sync_clips_dialog.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

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
        image.fill(QColor(200, 5 * i, 0));
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

// `seconds` of quiet stereo 48 kHz sound.
QByteArray silence(double seconds) { return QByteArray(static_cast<int>(seconds * 48000) * 4, '\0'); }

} // namespace

class TestSyncClipsUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void dialog() {
        SyncClipsDialog video(false);
        // "Source Timecode ... The default option"; Film TC/Sound TC "appears dimmed if you are not in a 24p or 25p
        // project" (p.497).
        QVERIFY(video.button(model::GroupSync::SourceTimecode)->isChecked());
        QVERIFY(!video.button(model::GroupSync::FilmTc)->isEnabled());
        const model::SyncClipsOptions defaults = video.options();
        QCOMPARE(defaults.sync.sync, model::GroupSync::SourceTimecode);
        QVERIFY(!defaults.keepVideoAudio && !defaults.audioOnlyTracks && !defaults.collapseAudio);
        // The track ranges follow their check boxes ("Off by default").
        auto* keep = video.findChild<QCheckBox*>(QStringLiteral("keepVideoAudio"));
        auto* start = video.findChild<QSpinBox*>(QStringLiteral("keepStart"));
        QVERIFY(keep && start);
        QVERIFY(!start->isEnabled());
        keep->setChecked(true);
        QVERIFY(start->isEnabled());
        start->setValue(3);
        video.findChild<QSpinBox*>(QStringLiteral("keepEnd"))->setValue(4);
        video.button(model::GroupSync::AuxTimecode)->setChecked(true);
        video.findChild<QComboBox*>(QStringLiteral("auxTcMenu"))->setCurrentIndex(4);
        const model::SyncClipsOptions chosen = video.options();
        QCOMPARE(chosen.sync.sync, model::GroupSync::AuxTimecode);
        QCOMPARE(chosen.sync.auxTc, 5);
        QCOMPARE(chosen.keepVideoAudio, (std::optional<std::pair<int, int>>({3, 4})));
        // Shown again as left.
        SyncClipsDialog film(true);
        QVERIFY(film.button(model::GroupSync::FilmTc)->isEnabled());
        film.setOptions(chosen);
        QCOMPARE(film.options().keepVideoAudio, chosen.keepVideoAudio);
        QCOMPARE(film.options().sync.auxTc, 5);
        // Not built yet: the group clip for overlapping video.
        QVERIFY(!film.findChild<QCheckBox*>(QStringLiteral("createGroupClip"))->isEnabled());
    }

    void inTheApplication() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("Take1.mov"));
        QVERIFY(writeMovie(movie, 50));
        const QString wav = dir.filePath(QStringLiteral("Sound1.wav"));
        QVERIFY(media::writePcmWav(wav, silence(3.0), 48000));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        // Our name for the guide's AutoSync (ROADMAP 9.8).
        QCOMPARE(commands.text(QStringLiteral("clip.autosync")), QStringLiteral("Sync Clips"));
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Dailies"));
        project.settings.name = QStringLiteral("Dailies");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie, wav});
        QCOMPARE(ids.size(), 2);
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);

        int asked = 0;
        window.setSyncClipsHook([&] {
            ++asked;
            return std::optional<model::SyncClipsOptions>(model::SyncClipsOptions{});
        });
        // One clip is not a selection to sync: nothing asked.
        view->selectIds({ids.first()});
        QVERIFY(window.syncClips().isEmpty());
        QCOMPARE(asked, 0);
        view->selectIds(ids);
        QVERIFY(commands.hasHandler(QStringLiteral("clip.autosync")));
        commands.action(QStringLiteral("clip.autosync"))->trigger();
        QCOMPARE(asked, 1);
        const QStringList selected = view->selectedIds();
        QCOMPARE(selected.size(), 1);
        const model::BinItem synced = *bin->bin().item(selected.first());
        QVERIFY(model::isSyncedSubclip(synced));
        QCOMPARE(synced.name, QStringLiteral("Take1.sync.1"));
        QCOMPARE(synced.durationFrames, std::int64_t(50));
        QVERIFY(model::sourceTracksOf(synced).contains(QStringLiteral("A1")));
        // In the Source monitor it shows the video clip's picture.
        Composer& composer = window.composer();
        composer.loadDefault(binId, synced);
        const QImage shown = composer.pictureAt(MonitorId::Source, 4, QSize(64, 36));
        QVERIFY(!shown.isNull());
        QVERIFY2(shown.pixelColor(32, 18).red() > 150, qPrintable(shown.pixelColor(32, 18).name()));
        // Two picture clips cannot be synced: nothing made.
        const QStringList pictures = window.linkFiles(binId, {movie});
        view->selectIds({ids.first(), pictures.first()});
        QVERIFY(window.syncClips().isEmpty());
    }
};

QTEST_MAIN(TestSyncClipsUi)
#include "tst_sync_clips_ui.moc"

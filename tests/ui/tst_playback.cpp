#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/full_screen.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

QColor colourOf(int n) {
    return QColor(40 + (n * 23) % 176, 40 + (175 - n * 11 % 176), 40 + (n * 57) % 176);
}

// Two seconds at 25 fps with a stereo tone: a QuickTime file with picture
// and sound.
bool writeClip(const QString& path) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.channels = 2;
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    std::vector<float> second(2 * 1920, 0.25f); // one frame's samples
    for (int i = 0; i < 50; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(colourOf(i));
        if (!w.writeVideo(image) || !w.writeAudio(second.data(), 1920)) return false;
    }
    return w.finish();
}

} // namespace

class TestPlayback : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }
    // A font family asked for by a name the system lacks makes Qt build its family aliases (a quarter
    // of a second on macOS), which stalls the window: use QFontDatabase::systemFont (list_tool.cpp).
    void init() { QTest::failOnWarning(QRegularExpression(QStringLiteral("Populating font family aliases"))); }

    void playsWithSoundAndQuality() {
        // Play at normal speed plays the clip's sound and the picture keeps
        // pace with it (2.3); shuttling is silent. The Video Quality Menu
        // button cycles Full, Draft and Best Performance (p.403-404).
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("Tone.mov"));
        QVERIFY(writeClip(movie));
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
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        const QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        QCOMPARE(bin->bin().item(ids.first())->fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));

        Composer& composer = window.composer();
        composer.loadDefault(binId, *bin->bin().item(ids.first()));
        composer.setActiveMonitor(MonitorId::Source);
        // The window drawn and the clip's frame shown before Play, as an
        // editor plays: a first paint in the first ticks holds them back
        // (on the Mac, 100 ms and more), which is not what this measures.
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(500);
        composer.play(1);
        QVERIFY(composer.isPlaying());
        if (!window.isPlayingSound()) QSKIP("no audio output, not even the null device");
        QTest::qWait(300);
        // The picture follows the sound: the position is where the clock
        // put it at the last tick (every 20 ms). A busy machine can hold a
        // tick back, so the closest of several looks counts.
        double closest = 1e9;
        std::int64_t position = 0;
        for (int i = 0; i < 10; ++i) {
            QTest::qWait(25);
            position = composer.state(MonitorId::Source).position();
            const double heard = media::samplesToFrames(window.soundClock(), Rational(25));
            closest = std::min(closest, std::abs(static_cast<double>(position) - heard));
        }
        QVERIFY2(window.soundClock() > 0, "no sound was heard");
        QVERIFY2(closest <= 2.0, qPrintable(QStringLiteral("the picture was %1 frames from the sound (%2 frames shown, %3 dropped)")
                                                .arg(closest)
                                                .arg(composer.playStats().shown)
                                                .arg(composer.playStats().dropped)));
        QVERIFY(position > 0);
        QVERIFY(composer.playStats().shown > 0);
        // Smooth audio scrub (p.675): sound up to three times normal speed,
        // either way, and the picture follows it backwards too.
        composer.play(-1);
        QVERIFY(window.isPlayingSound());
        const std::int64_t from = composer.state(MonitorId::Source).position();
        QTRY_VERIFY_WITH_TIMEOUT(composer.state(MonitorId::Source).position() < from - 2, 2000);
        composer.play(2);
        QVERIFY(window.isPlayingSound());
        // Faster than three times normal speed, it cuts out.
        composer.play(5);
        QVERIFY(!window.isPlayingSound());
        composer.stop();
        QVERIFY(!window.isPlayingSound());

        // Digital audio scrub (p.676): with the button on, moving while
        // stopped plays the frames around the position: none before and one
        // after by default (Audio settings, p.1147).
        QAction* scrub = commands.action(QStringLiteral("play.digital_audio_scrub"));
        QVERIFY(scrub && scrub->isCheckable());
        composer.moveTo(MonitorId::Source, 20);
        QVERIFY(!window.isScrubbing()); // the button is off
        scrub->setChecked(true);
        composer.moveTo(MonitorId::Source, 30);
        QVERIFY(window.isScrubbing());
        QCOMPARE(window.scrubStart(), media::framesToSamples(30, Rational(25)));
        QTRY_VERIFY_WITH_TIMEOUT(!window.isScrubbing(), 2000);
        window.settings().setValue(QStringLiteral("Audio"), QStringLiteral("sourceScrubOut"), 3);
        composer.moveTo(MonitorId::Source, 10);
        QCOMPARE(window.scrubStart(), media::framesToSamples(7, Rational(25)));
        scrub->setChecked(false);

        // Composer > Full Screen Playback (p.391): the picture on a whole
        // screen, reformatted as the setting says (p.1190).
        commands.action(QStringLiteral("composer.full_screen_playback"))->trigger();
        FullScreenView* full = window.fullScreenView();
        QVERIFY(full && full->isVisible());
        QCOMPARE(full->reformat(), FullScreenView::Reformat::Letterbox);
        const QSize screen(1920, 1200);
        QCOMPARE(full->pictureRect(screen, QSize(64, 36)), QRect(0, 60, 1920, 1080));
        full->setReformat(FullScreenView::Reformat::Crop);
        QCOMPARE(full->pictureRect(screen, QSize(64, 36)).height(), 1200);
        full->setReformat(FullScreenView::Reformat::KeepSize);
        QCOMPARE(full->pictureRect(screen, QSize(64, 36)), QRect(928, 582, 64, 36));
        full->setReformat(FullScreenView::Reformat::Stretch);
        QCOMPARE(full->pictureRect(screen, QSize(64, 36)), QRect(QPoint(0, 0), screen));
        window.settings().setValue(QStringLiteral("Full Screen Playback"), QStringLiteral("reformat"),
                                   QStringLiteral("crop"));
        QTRY_COMPARE(full->reformat(), FullScreenView::Reformat::Crop);
        full->grab(); // paints the frame
        commands.action(QStringLiteral("composer.full_screen_playback"))->trigger();
        QVERIFY(!full->isVisible());
        commands.action(QStringLiteral("composer.full_screen_playback"))->trigger();
        QVERIFY(full->isVisible());
        QTest::keyClick(full, Qt::Key_Escape);
        QVERIFY(!full->isVisible());

        // Video quality.
        auto* quality = window.timeline().findChild<QToolButton*>(QStringLiteral("videoQuality"));
        QVERIFY(quality && quality->isEnabled());
        QCOMPARE(composer.videoQuality(), Composer::VideoQuality::Full);
        quality->click();
        QCOMPARE(composer.videoQuality(), Composer::VideoQuality::Draft);
        QCOMPARE(composer.qualityScale(), 0.5);
        quality->click();
        QCOMPARE(composer.videoQuality(), Composer::VideoQuality::Best);
        QCOMPARE(composer.qualityScale(), 0.25);
        quality->click();
        QCOMPARE(composer.videoQuality(), Composer::VideoQuality::Full);
    }
};

QTEST_MAIN(TestPlayback)
#include "tst_playback.moc"

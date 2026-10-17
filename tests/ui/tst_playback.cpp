#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

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
        composer.play(1);
        QVERIFY(composer.isPlaying());
        if (!window.isPlayingSound()) QSKIP("no audio output, not even the null device");
        QTest::qWait(400);
        // The picture follows the sound: the position is where the samples
        // played put it (within the ticks between).
        const auto played = window.soundPlayed();
        const std::int64_t position = composer.state(MonitorId::Source).position();
        QVERIFY2(played > 0, "no sound was taken");
        const double expected = media::samplesToFrames(played, Rational(25));
        QVERIFY2(std::abs(static_cast<double>(position) - expected) <= 3.0,
                 qPrintable(QStringLiteral("position %1, sound at %2").arg(position).arg(expected)));
        QVERIFY(position > 0);
        // Shuttling faster is silent; stopping stops the sound.
        composer.play(2);
        QVERIFY(!window.isPlayingSound());
        composer.stop();
        QVERIFY(!window.isPlayingSound());

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

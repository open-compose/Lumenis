// Audio CDs on import (9.2d; docs/spec/audio-cd.md; Editing Guide p.215, p.1198): Windows' ".cda" track files, which
// files are on a disc, WAV writing, a ".cda" track read by an audio CD provider and imported as a clip named after it,
// and the Import setting's Apply attenuation/gain effect on import with CD only.
#include "lumenis/ext/audio_cd.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/audio_cd.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <cmath>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// `seconds` of a 440 Hz tone, 44.1 kHz stereo 16-bit.
QByteArray tone(double seconds) {
    const int frames = static_cast<int>(seconds * 44100);
    QByteArray pcm(frames * 4, Qt::Uninitialized);
    for (int i = 0; i < frames; ++i) {
        const auto v = static_cast<std::int16_t>(8000 * std::sin(2 * 3.14159265358979 * 440 * i / 44100.0));
        qToLittleEndian(v, pcm.data() + 4 * i);
        qToLittleEndian(v, pcm.data() + 4 * i + 2);
    }
    return pcm;
}

// A drive that reads any ".cda" file as the tone, as long as the track says.
class FakeCdProvider : public ext::AudioCdProvider {
public:
    ext::ProviderInfo info() const override {
        return {"test.audio-cd", "Test audio CD", ext::Category::AudioCd, ext::LicenseTier::OpenSource, "test", "1"};
    }
    bool readsTrack(const std::string& path) const override { return media::readCdaFile(QString::fromStdString(path)).has_value(); }
    bool readTrack(const std::string& path, const std::string& wav, const std::function<bool(double)>& progress,
                   std::string*) override {
        const auto track = media::readCdaFile(QString::fromStdString(path));
        if (progress) progress(1.0);
        ++reads;
        return track && media::writePcmWav(QString::fromStdString(wav), tone(track->seconds()));
    }
    int reads = 0;
};

} // namespace

class TestAudioCd : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void files() {
        media::CdaTrack t;
        t.track = 3;
        t.serial = 0x1234abcd;
        t.startSector = 22500;
        t.sectors = 150; // two seconds
        const QByteArray data = media::cdaFileData(t);
        QCOMPARE(data.size(), 44);
        const auto back = media::parseCda(data);
        QVERIFY(back);
        QCOMPARE(back->track, 3);
        QCOMPARE(back->serial, std::uint32_t(0x1234abcd));
        QCOMPARE(back->startSector, std::uint32_t(22500));
        QCOMPARE(back->sectors, std::uint32_t(150));
        QCOMPARE(back->seconds(), 2.0);
        QVERIFY(!media::parseCda(QByteArray("RIFF....WAVEfmt ")));
        QVERIFY(!media::parseCda(data.left(40)));
        QVERIFY(media::isCdaFile(QStringLiteral("D:/Track03.CDA")));
        QVERIFY(!media::isCdaFile(QStringLiteral("D:/Track03.wav")));

        QTemporaryDir dir;
        // On a disc: ".cda" files and gvfs's audio CD tracks; an ordinary folder is not.
        QVERIFY(media::isOnDisc(QStringLiteral("D:/Track03.cda")));
        QVERIFY(media::isOnDisc(QStringLiteral("/run/user/1000/gvfs/cdda:host=sr0/Track 1.wav")));
        QVERIFY(!media::isOnDisc(dir.filePath(QStringLiteral("song.wav"))));

        // A WAV file FFmpeg reads as 44.1 kHz stereo.
        const QString wav = dir.filePath(QStringLiteral("tone.wav"));
        QVERIFY(media::writePcmWav(wav, tone(1.0)));
        const auto info = media::probe(wav);
        QVERIFY(info);
        const auto audio = info->audio();
        QCOMPARE(audio.size(), std::size_t(1));
        QCOMPARE(audio.front()->sampleRate, 44100);
        QCOMPARE(audio.front()->channels, 2);
    }

    void importing() {
        QTemporaryDir dir;
        const QString disc = dir.filePath(QStringLiteral("CD"));
        QVERIFY(QDir().mkpath(disc));
        media::CdaTrack t;
        t.track = 1;
        t.sectors = 150;
        const QString cda = QDir(disc).filePath(QStringLiteral("Track01.cda"));
        {
            QFile f(cda);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(media::cdaFileData(t));
        }
        const QString song = dir.filePath(QStringLiteral("song.wav"));
        QVERIFY(media::writePcmWav(song, tone(1.0)));
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
        project.folder = dir.filePath(QStringLiteral("Music"));
        project.settings.name = QStringLiteral("Music");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // Without a reader of audio CDs the track is left out.
        QVERIFY(window.importFiles(binId, {cda}).isEmpty());
        // With one, a clip of the track, named after it and imported from the ".cda" file as far as the bin knows.
        auto provider = std::make_shared<FakeCdProvider>();
        QVERIFY(providers.add(provider));
        const QString imp = QStringLiteral("Import");
        window.settings().setValue(imp, QStringLiteral("applyGain"), true);
        window.settings().setValue(imp, QStringLiteral("importGain"), -6);
        window.settings().setValue(imp, QStringLiteral("cdOnly"), true);
        QStringList ids = window.importFiles(binId, {cda, song});
        QCOMPARE(ids.size(), 2);
        QCOMPARE(provider->reads, 1);
        const model::BinItem* track = nullptr;
        const model::BinItem* other = nullptr;
        for (const QString& id : ids) {
            const model::BinItem* i = bin->bin().item(id);
            (i->name == QLatin1String("Track01") ? track : other) = i;
        }
        QVERIFY(track && other);
        QCOMPARE(track->kind, model::BinItemKind::AudioClip);
        QCOMPARE(track->fields.value(QStringLiteral("Source File")), QStringLiteral("Track01.cda"));
        QCOMPARE(track->fields.value(QStringLiteral("Source Path")), disc);
        QCOMPARE(track->durationFrames, std::int64_t(50));
        // CD only: the gain on the CD's track, not on the other file.
        QCOMPARE(track->audioGain, -6.0);
        QCOMPARE(other->audioGain, 0.0);
        // Without CD only, every imported file.
        window.settings().setValue(imp, QStringLiteral("cdOnly"), false);
        ids = window.importFiles(binId, {song});
        QCOMPARE(bin->bin().item(ids.first())->audioGain, -6.0);
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestAudioCd)
#include "tst_audio_cd.moc"

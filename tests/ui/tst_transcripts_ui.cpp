// Transcription in the application (8.2a; Editing Guide p.446-448, docs/spec/transcripts.md): master clips in the bins
// included in transcription transcribed on the Background Queue through the engine, Include/Exclude Bin in
// Transcription with the Sidebar's status, and "Include all new bins in transcription".
#include "lumenis/ext/provider.h"
#include "lumenis/ext/transcription.h"
#include "lumenis/media/media.h"
#include "lumenis/model/transcript.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include <atomic>
#include <cmath>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

bool writeTone(const QString& path, double seconds) {
    std::vector<float> samples(static_cast<std::size_t>(seconds * 48000));
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = 0.3f * static_cast<float>(std::sin(static_cast<double>(i) * 0.05));
    media::WriterSettings ws;
    ws.format = QStringLiteral("wav");
    ws.audioCodec = QStringLiteral("pcm_f32le");
    ws.channels = 1;
    ws.sampleRate = 48000;
    media::MediaWriter w;
    return w.open(path, ws) && w.writeAudio(samples.data(), static_cast<int>(samples.size())) && w.finish();
}

// Hears "Hello world." in anything, counting its calls.
class FakeProvider : public ext::TranscriptionProvider {
public:
    ext::ProviderInfo info() const override {
        return {"test.fake", "Fake", ext::Category::Transcription, ext::LicenseTier::OpenSource, "", ""};
    }
    ext::Transcript transcribe(std::span<const float>, int, const ext::TranscriptionOptions& options,
                               const ext::TranscriptionProgress&) override {
        ++calls;
        language = options.language;
        ext::Transcript t;
        t.words = {{"Hello", 0.1, 0.4, 1.0f, false}, {"world.", 0.5, 0.9, 1.0f, false}};
        return t;
    }
    std::atomic<int> calls{0};
    std::string language;
};

QString statusOf(BinContainer& bins, const QString& id) {
    const QModelIndex name = bins.treeModel()->indexOf(id);
    return bins.treeModel()->index(name.row(), BinTreeModel::kTranscriptionColumn, name.parent()).data().toString();
}

} // namespace

class TestTranscriptsUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void transcribesIncludedBins() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString wav = dir.filePath(QStringLiteral("talk.wav"));
        const QString music = dir.filePath(QStringLiteral("music.wav"));
        QVERIFY(writeTone(wav, 2.0));
        QVERIFY(writeTone(music, 2.0));
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
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        auto provider = std::make_shared<FakeProvider>();
        window.setTranscriber(provider);
        window.settings().setValue(QStringLiteral("Transcript"), QStringLiteral("languageHint"), QStringLiteral("de"));
        BinContainer& bins = window.binContainer();
        const QString talkBin = bins.newBin();
        const QString musicBin = bins.newBin();
        bins.openBin(talkBin);
        bins.openBin(musicBin);
        QCOMPARE(window.linkFiles(talkBin, {wav}).size(), qsizetype(1));
        QCOMPARE(window.linkFiles(musicBin, {music}).size(), qsizetype(1));
        QCOMPARE(statusOf(bins, musicBin), QStringLiteral("Transcription On"));
        // Exclude Bin from Transcription on the selected bin (p.447).
        bins.tree()->setCurrentIndex(bins.treeModel()->indexOf(musicBin));
        commands.action(QStringLiteral("bin.exclude_from_transcription"))->trigger();
        QVERIFY(!window.isBinTranscribed(musicBin));
        QCOMPARE(statusOf(bins, musicBin), QStringLiteral("Transcription Off"));
        QVERIFY(!model::TranscriptionBins(project.folder).isIncluded(musicBin)); // kept in the project
        // Only the included bin's clip is queued, once.
        QCOMPARE(window.queueTranscriptions(), 1);
        QCOMPARE(window.queueTranscriptions(), 0);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(provider->calls.load(), 1);
        QCOMPARE(provider->language, std::string("de"));
        const model::TranscriptStore store;
        const QStringList mobs = store.mobs();
        QCOMPARE(mobs.size(), qsizetype(1));
        QCOMPARE(store.load(mobs.first())->lines.first().text(), QStringLiteral("Hello world."));
        QCOMPARE(window.queueTranscriptions(), 0); // transcribed already
        // Include it again: its clip is queued.
        commands.action(QStringLiteral("bin.include_in_transcription"))->trigger();
        QCOMPARE(statusOf(bins, musicBin), QStringLiteral("Transcription On"));
        QCOMPARE(window.queueTranscriptions(), 1);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(store.mobs().size(), qsizetype(2));
        // "Include all new bins in transcription" off: new bins are excluded.
        window.settings().setValue(QStringLiteral("Transcript"), QStringLiteral("includeNewBins"), false);
        const QString later = bins.newBin();
        QVERIFY(!window.isBinTranscribed(later));
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }
};

QTEST_MAIN(TestTranscriptsUi)
#include "tst_transcripts_ui.moc"

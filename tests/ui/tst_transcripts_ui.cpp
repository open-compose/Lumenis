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
#include "lumenis/ui/composer.h"
#include "lumenis/ui/transcript_tool.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QMenu>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QRegularExpression>
#include <QTextEdit>
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

    // The Transcript Tool (p.442-444): a clip in the Source monitor and a sequence in the Record monitor shown, clicks
    // and selections moving the position and setting marks, search, Transcribe for a clip not transcribed.
    void transcriptTool() {
        QTemporaryDir dir;
        qputenv("LUMENIS_TRANSCRIPTS", dir.filePath(QStringLiteral("Transcripts")).toLocal8Bit());
        const QString wav = dir.filePath(QStringLiteral("talk.wav"));
        const QString other = dir.filePath(QStringLiteral("other.wav"));
        QVERIFY(writeTone(wav, 4.0));
        QVERIFY(writeTone(other, 2.0));
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
        auto provider = std::make_shared<FakeProvider>();
        window.setTranscriber(provider);
        BinContainer& bins = window.binContainer();
        const QString binId = bins.newBin();
        BinDocument* bin = bins.openBin(binId);
        const QStringList ids = window.linkFiles(binId, {wav, other});
        QCOMPARE(ids.size(), qsizetype(2));
        const model::BinItem talk = *bin->bin().item(ids[0]);
        const model::BinItem quiet = *bin->bin().item(ids[1]);
        model::Transcript t;
        t.mob = QString::fromStdString(model::mobIdOf(talk).toString());
        t.lines = {model::TranscriptLine{1, {{QStringLiteral("Good"), 0.0, 0.4}, {QStringLiteral("morning,"), 0.4, 0.8},
                                             {QStringLiteral("everyone."), 0.8, 1.2}}},
                   model::TranscriptLine{2, {{QStringLiteral("Morning!"), 2.0, 2.6}}}};
        QVERIFY(model::TranscriptStore().save(t));
        commands.action(QStringLiteral("tools.transcript_tool"))->trigger();
        TranscriptTool& tool = window.transcriptTool();
        Composer& composer = window.composer();
        composer.loadDefault(binId, talk);
        QCoreApplication::processEvents();
        QVERIFY(tool.sourceSide());
        QCOMPARE(tool.transcript().lines.size(), qsizetype(2));
        QVERIFY(tool.plainText().contains(QStringLiteral("Speaker 1: Good morning, everyone.")));
        QVERIFY(QRegularExpression(QStringLiteral("^\\d\\d:\\d\\d:\\d\\d[:;]\\d\\d ")).match(tool.plainText()).hasMatch()); // Show Timecode
        // The playhead word follows the position.
        composer.moveTo(MonitorId::Source, 52);
        QCOMPARE(tool.playheadWord(), (std::optional<std::pair<int, int>>{{1, 0}}));
        // A click moves the position; a selection sets the marks (p.442).
        tool.clickWord(0, 2);
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(20));
        tool.selectWords(0, 0, 2);
        QCOMPARE(composer.state(MonitorId::Source).in(), std::optional<std::int64_t>(0));
        QCOMPARE(composer.state(MonitorId::Source).out(), std::optional<std::int64_t>(19));
        // Search ignores case and punctuation; the arrows go to the next match.
        QCOMPARE(tool.search(QStringLiteral("morning")), 2);
        composer.moveTo(MonitorId::Source, 0);
        QVERIFY(tool.next());
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(10));
        QVERIFY(tool.next());
        QCOMPARE(composer.state(MonitorId::Source).position(), std::int64_t(50));
        // Editing (p.445-446; 2026.8 guide p.446-447): text, a speaker's name, Correct Speaker ID with Group Speaker Lines,
        // the right-click menus, Revert Transcript.
        tool.editWords(0, 1, 2, QStringLiteral("afternoon,"));
        QVERIFY(tool.plainText().contains(QStringLiteral("Good afternoon, everyone.")));
        QCOMPARE(model::TranscriptStore().load(t.mob)->lines[0].words[1].start, 0.4); // timing kept
        tool.renameSpeaker(1, QStringLiteral("Ann"));
        QVERIFY(tool.plainText().contains(QStringLiteral("Ann: Morning!")));
        const int speakerPos = static_cast<int>(tool.plainText().indexOf(QStringLiteral("Ann")));
        QMenu* menu = tool.contextMenuAt(speakerPos + 1, &tool);
        QVERIFY(menu->findChild<QMenu*>(QStringLiteral("identifySpeaker")));
        QVERIFY(menu->findChild<QMenu*>(QStringLiteral("correctSpeaker")));
        delete menu;
        menu = tool.contextMenuAt(static_cast<int>(tool.plainText().indexOf(QStringLiteral("everyone"))) + 2, &tool);
        QStringList items;
        for (QAction* a : menu->actions()) items << a->text();
        QCOMPARE(items, (QStringList{QStringLiteral("Edit Text"), QStringLiteral("Start New Line"), QStringLiteral("Revert")}));
        delete menu;
        tool.correctSpeaker(1, 1); // Morning! is Speaker 1's too
        tool.setGroupSpeakers(true);
        QCOMPARE(tool.displayed().lines.size(), qsizetype(1));
        tool.setGroupSpeakers(false);
        QCOMPARE(tool.displayed().lines.size(), qsizetype(2));
        tool.startNewLine(0, 2);
        QCOMPARE(tool.displayed().lines.size(), qsizetype(3));
        tool.revertTranscript();
        QVERIFY(tool.plainText().contains(QStringLiteral("Good morning, everyone.")));
        QVERIFY(!model::TranscriptStore().load(t.mob)->edited());
        tool.replaceAll(QStringLiteral("everyone"), QStringLiteral("all"));
        QVERIFY(tool.plainText().contains(QStringLiteral("Good morning, all")));
        tool.revertTranscript();
        // A sequence in the Record monitor: the Record side, with the track button.
        model::BinItem seqItem;
        seqItem.id = model::newItemId();
        seqItem.name = QStringLiteral("Cut");
        seqItem.kind = model::BinItemKind::Sequence;
        seqItem.editRate = Rational(25);
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = talk.id;
        seg.binId = binId;
        seg.name = talk.name;
        seg.sourceIn = 40;
        seg.length = 40;
        seg.sourceTrack = QStringLiteral("A1");
        seq.ensureTrack(QStringLiteral("A1"));
        seq.track(QStringLiteral("A1"))->segments = {seg};
        seq.storeIn(seqItem);
        bin->addItem(seqItem);
        composer.load(MonitorId::Record, binId, seqItem);
        QCoreApplication::processEvents();
        QVERIFY(!tool.sourceSide());
        QCOMPARE(tool.transcript().lines.size(), qsizetype(1));
        QCOMPARE(tool.transcript().lines[0].words[0].frame, std::int64_t(10));
        QVERIFY(tool.transcript().availableTracks.contains(QStringLiteral("A1")));
        // A clip not transcribed: the banner's Transcribe makes its transcript.
        composer.loadDefault(binId, quiet);
        QCoreApplication::processEvents();
        QVERIFY(tool.sourceSide());
        QCOMPARE(tool.transcript().notTranscribed.size(), qsizetype(1));
        QVERIFY(tool.findChild<QWidget*>(QStringLiteral("transcribe"))->isVisibleTo(&tool));
        QCOMPARE(window.transcribeNow(tool.transcript().notTranscribedMobs), 1);
        window.backgroundQueue().waitForIdle();
        QCoreApplication::processEvents();
        QCOMPARE(tool.transcript().lines.size(), qsizetype(1));
        QVERIFY(tool.transcript().notTranscribed.isEmpty());
        qunsetenv("LUMENIS_TRANSCRIPTS");
    }
};

QTEST_MAIN(TestTranscriptsUi)
#include "tst_transcripts_ui.moc"

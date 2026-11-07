// Transcripts (Editing Guide ch.14; lumenis/model/transcript.h): lines from an engine's words, JSON round trips, the
// transcript store and which bins are transcribed.
#include "lumenis/model/group_clip.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/transcript.h"
#include "lumenis/model/transcript_view.h"

#include <QTemporaryDir>
#include <QTest>

using namespace lumenis::model;

class TestTranscript : public QObject {
    Q_OBJECT

private slots:
    // Lines end at speaker turns and long pauses; turns alternate between two speakers, a third turn after a new voice.
    void linesFromWords() {
        const QList<EngineWord> words{
            {QStringLiteral("Hello"), 0.0, 0.4},   {QStringLiteral("there."), 0.5, 0.9, true},
            {QStringLiteral("Hi"), 1.2, 1.4},      {QStringLiteral("back."), 1.5, 1.8, true},
            {QStringLiteral("So"), 2.0, 2.2},      {QStringLiteral(""), 2.3, 2.4},
            {QStringLiteral("anyway."), 6.0, 6.5}, // after a 3.8 s pause: a new line, the same speaker
        };
        const QList<TranscriptLine> lines = transcriptLines(words);
        QCOMPARE(lines.size(), qsizetype(4));
        QCOMPARE(lines[0].speaker, 1);
        QCOMPARE(lines[0].text(), QStringLiteral("Hello there."));
        QCOMPARE(lines[1].speaker, 2);
        QCOMPARE(lines[2].speaker, 1);
        QCOMPARE(lines[2].text(), QStringLiteral("So"));
        QCOMPARE(lines[3].speaker, 1);
        QCOMPARE(lines[3].start(), 6.0);
    }

    void jsonRoundTripAndStore() {
        Transcript t;
        t.mob = QStringLiteral("060a2b34.0101.0105.01010f20-13-00-00-00-abcdef");
        t.clipName = QStringLiteral("Interview");
        t.language = QStringLiteral("en");
        t.created = QDateTime::fromString(QStringLiteral("2026-11-07T12:00:00Z"), Qt::ISODate);
        t.lines = {TranscriptLine{1, {{QStringLiteral("Hello"), 0.1, 0.5}}}, TranscriptLine{2, {{QStringLiteral("Hi"), 1.0, 1.2}}}};
        t.original = t.lines;
        t.speakers.insert(2, QStringLiteral("Ann"));
        QCOMPARE(t.speakerName(1), QStringLiteral("Speaker 1"));
        QCOMPARE(t.plainText(), QStringLiteral("Speaker 1: Hello\nAnn: Hi"));
        const auto back = Transcript::fromJson(t.toJson());
        QVERIFY(back);
        QCOMPARE(back->lines, t.lines);
        QVERIFY(back->edited());
        QCOMPARE(back->speakers.value(2), QStringLiteral("Ann"));
        QCOMPARE(back->created, t.created);

        QTemporaryDir dir;
        const TranscriptStore store(dir.filePath(QStringLiteral("Transcripts")));
        QVERIFY(!store.has(t.mob));
        QVERIFY(store.save(t));
        QVERIFY(store.has(t.mob));
        QCOMPARE(store.mobs(), QStringList{t.mob});
        QCOMPARE(store.load(t.mob)->clipName, QStringLiteral("Interview"));
        QVERIFY(store.remove(t.mob));
        QVERIFY(!store.has(t.mob));
        QVERIFY(!store.remove(t.mob));
    }

    // Include/Exclude Bin in Transcription, kept in the project folder.
    void transcriptionBins() {
        QTemporaryDir dir;
        TranscriptionBins bins(dir.path());
        QVERIFY(bins.isIncluded(QStringLiteral("b1")));
        QVERIFY(bins.setIncluded(QStringLiteral("b1"), false));
        QVERIFY(!TranscriptionBins(dir.path()).isIncluded(QStringLiteral("b1")));
        QVERIFY(bins.setIncluded(QStringLiteral("b1"), true));
        QVERIFY(TranscriptionBins(dir.path()).isIncluded(QStringLiteral("b1")));
    }

    // What the Transcript Tool shows (p.448-449): a master clip's words at its frames, a subclip's from where it starts,
    // a group's selected angle's, a sequence's chosen audio tracks' without repeating a clip on two tracks.
    void shownTranscripts() {
        BinItem master;
        master.id = QStringLiteral("m");
        master.name = QStringLiteral("Interview");
        master.kind = BinItemKind::MasterClip;
        master.editRate = lumenis::Rational(25);
        master.durationFrames = 250;
        master.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1-2"));
        const QString mob = QString::fromStdString(mobIdOf(master).toString());
        Transcript t;
        t.mob = mob;
        t.lines = {TranscriptLine{1, {{QStringLiteral("Hello"), 0.0, 0.4}, {QStringLiteral("there."), 0.4, 0.8}}},
                   TranscriptLine{2, {{QStringLiteral("Hi"), 4.0, 4.4}}}};
        t.speakers.insert(2, QStringLiteral("Ann"));
        const TranscriptFor transcriptFor = [&](const QString& m) -> std::optional<Transcript> {
            return m == mob ? std::optional(t) : std::nullopt;
        };
        ShownTranscript s = shownTranscript(master, transcriptFor);
        QCOMPARE(s.lines.size(), qsizetype(2));
        QCOMPARE(s.lines[0].words[1].frame, std::int64_t(10));
        QCOMPARE(s.lines[1].speaker, QStringLiteral("Ann"));
        QCOMPARE(s.lines[1].frame, std::int64_t(100));
        QCOMPARE(wordAt(s, 12), (std::optional<std::pair<int, int>>{{0, 1}}));
        QCOMPARE(wordAt(s, 150), (std::optional<std::pair<int, int>>{{1, 0}}));
        // A subclip from frame 50: only "Hi", at its frame 50.
        BinItem sub = master;
        sub.id = QStringLiteral("s");
        sub.kind = BinItemKind::Subclip;
        sub.durationFrames = 100;
        ClipSource src;
        src.kind = ClipSource::Kind::Clip;
        src.mob = mobIdOf(master);
        src.offset = 50;
        sub.sources = {src};
        s = shownTranscript(sub, transcriptFor);
        QCOMPARE(s.lines.size(), qsizetype(1));
        QCOMPARE(s.lines[0].words[0].frame, std::int64_t(50));
        // Not transcribed.
        BinItem other = master;
        other.id = QStringLiteral("o");
        other.name = QStringLiteral("Other");
        s = shownTranscript(other, transcriptFor);
        QVERIFY(s.lines.isEmpty());
        QCOMPARE(s.notTranscribed, QStringList{QStringLiteral("Other")});
        // A sequence: the master on A1 and A2 from frame 90 at sequence frame 0 shows "Hi" once, at frame 10.
        Sequence seq = Sequence::withDefaultTracks();
        Segment seg;
        seg.clipId = master.id;
        seg.binId = QStringLiteral("bin");
        seg.sourceIn = 90;
        seg.length = 40;
        seg.sourceTrack = QStringLiteral("A1");
        seq.ensureTrack(QStringLiteral("A1"));
        seq.ensureTrack(QStringLiteral("A2"));
        seq.track(QStringLiteral("A1"))->segments = {seg};
        seq.track(QStringLiteral("A2"))->segments = {seg};
        const ShownClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<BinItem> {
            return id == master.id ? std::optional(master) : std::nullopt;
        };
        s = shownTranscript(seq, {}, transcriptFor, lookup);
        QCOMPARE(s.availableTracks, (QStringList{QStringLiteral("A1"), QStringLiteral("A2")}));
        QCOMPARE(s.lines.size(), qsizetype(1));
        QCOMPARE(s.lines[0].words.size(), qsizetype(1));
        QCOMPARE(s.lines[0].words[0].frame, std::int64_t(10));
        // Only A2 chosen; none.
        s = shownTranscript(seq, {QStringLiteral("A3")}, transcriptFor, lookup);
        QVERIFY(s.lines.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTranscript)
#include "tst_transcript.moc"

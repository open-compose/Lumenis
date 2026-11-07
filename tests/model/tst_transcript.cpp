// Transcripts (Editing Guide ch.14; lumenis/model/transcript.h): lines from an engine's words, JSON round trips, the
// transcript store and which bins are transcribed.
#include "lumenis/model/group_clip.h"
#include "lumenis/model/phrase_find.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/transcript.h"
#include "lumenis/model/transcript_export.h"
#include "lumenis/model/transcript_view.h"

#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

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

    // Editing with word timing kept (p.446), Find and Replace, Speaker IDs (p.445), Start New Line and Revert (2026.8 guide
    // p.447).
    void editing() {
        Transcript t;
        t.mob = QStringLiteral("m");
        t.lines = {TranscriptLine{1, {{QStringLiteral("the"), 0.0, 1.0}, {QStringLiteral("quick"), 1.0, 2.0},
                                      {QStringLiteral("brwn"), 2.0, 3.0}, {QStringLiteral("fox"), 3.0, 4.0}}},
                   TranscriptLine{2, {{QStringLiteral("jumps."), 5.0, 6.0}}}};
        const QList<TranscriptLine> before = t.lines;
        // A single word: its timing kept.
        QVERIFY(editWords(t, 0, 2, 3, QStringLiteral("brown")));
        QCOMPARE(t.lines[0].words[2], (TranscriptWord{QStringLiteral("brown"), 2.0, 3.0}));
        QCOMPARE(t.original, before); // the transcribed lines kept
        QVERIFY(!editWords(t, 0, 2, 3, QStringLiteral("brown"))); // no change
        // Several words in one word's space: its time split evenly.
        QVERIFY(editWords(t, 0, 3, 4, QStringLiteral("red fox")));
        QCOMPARE(t.lines[0].words[3], (TranscriptWord{QStringLiteral("red"), 3.0, 3.5}));
        QCOMPARE(t.lines[0].words[4], (TranscriptWord{QStringLiteral("fox"), 3.5, 4.0}));
        // Several words: unchanged ones keep their times, the new ones between share the time between.
        QVERIFY(editWords(t, 0, 0, 5, QStringLiteral("The very quick brown fox")));
        QCOMPARE(t.lines[0].words.size(), qsizetype(5));
        QCOMPARE(t.lines[0].words[0].start, 0.0); // "The" matches "the" (case ignored)
        QCOMPARE(t.lines[0].words[2], (TranscriptWord{QStringLiteral("quick"), 1.0, 2.0}));
        QCOMPARE(t.lines[0].words[1].start, 0.5); // "very" shares "The"'s time: nothing was between the and quick
        QCOMPARE(t.lines[0].words[3], (TranscriptWord{QStringLiteral("brown"), 2.0, 3.0}));
        // Find and Replace.
        QCOMPARE(replaceWords(t, QStringLiteral("quick brown"), QStringLiteral("slow")), 1);
        QCOMPARE(t.lines[0].text(), QStringLiteral("The very slow fox"));
        QCOMPARE(t.lines[0].words[2].start, 1.0);
        QCOMPARE(t.lines[0].words[2].end, 3.0);
        // Speaker IDs.
        QVERIFY(renameSpeaker(t, 2, QStringLiteral("Ann")));
        QCOMPARE(t.speakerName(2), QStringLiteral("Ann"));
        QVERIFY(identifySpeaker(t, 0, QStringLiteral("Ann")));
        QCOMPARE(t.lines[0].speaker, 2);
        QVERIFY(correctSpeaker(t, 0, 0)); // Add New Speaker
        QCOMPARE(t.lines[0].speaker, 3);
        QCOMPARE(t.speakerName(3), QStringLiteral("Speaker 3"));
        QVERIFY(identifySpeaker(t, 0, QStringLiteral("Bob"))); // alone and unnamed: the ID takes the name
        QCOMPARE(t.lines[0].speaker, 3);
        QCOMPARE(t.speakerName(3), QStringLiteral("Bob"));
        QVERIFY(renameSpeaker(t, 3, QString())); // Clear Name
        QCOMPARE(t.speakerName(3), QStringLiteral("Speaker 3"));
        // Start New Line.
        QVERIFY(startNewLine(t, 0, 2));
        QCOMPARE(t.lines.size(), qsizetype(3));
        QCOMPARE(t.lines[1].text(), QStringLiteral("slow fox"));
        QCOMPARE(t.lines[1].speaker, t.lines[0].speaker);
        QVERIFY(!startNewLine(t, 0, 0));
        // Revert a line: the original lines in its time.
        QVERIFY(revertLine(t, 1));
        QCOMPARE(t.lines.size(), qsizetype(2));
        QCOMPARE(t.lines[0].text(), QStringLiteral("the quick brwn fox"));
        // Revert Transcript.
        QVERIFY(editWords(t, 1, 0, 1, QStringLiteral("leaps.")));
        QVERIFY(revertTranscript(t));
        QCOMPARE(t.lines, before);
        QVERIFY(!t.edited());
        QVERIFY(!revertTranscript(t));
        // Removing every word of a line removes it.
        QVERIFY(editWords(t, 1, 0, 1, QString()));
        QCOMPARE(t.lines.size(), qsizetype(1));
    }

    // Group Speaker Lines and New Line After (2026.8 guide p.446).
    void groupedSpeakerLines() {
        ShownTranscript s;
        auto line = [](const QString& speaker, std::int64_t frame, int words) {
            ShownLine l;
            l.speaker = speaker;
            l.frame = frame;
            for (int i = 0; i < words; ++i) l.words << ShownWord{QStringLiteral("w"), frame + i * 10, frame + i * 10 + 10, {}, 0, i};
            return l;
        };
        s.lines = {line(QStringLiteral("A"), 0, 2), line(QStringLiteral("A"), 30, 2), line(QStringLiteral("B"), 60, 1),
                   line(QStringLiteral("A"), 80, 1), line(QStringLiteral("A"), 500, 1)};
        QCOMPARE(groupedLines(s, false, 750).lines.size(), qsizetype(5));
        const ShownTranscript g = groupedLines(s, true, 300);
        QCOMPARE(g.lines.size(), qsizetype(4)); // A A | B | A | A (past 300 frames)
        QCOMPARE(g.lines[0].words.size(), qsizetype(4));
        QCOMPARE(groupedLines(s, true, 0).lines.size(), qsizetype(3));
    }

    // PhraseFind AI (p.436-441): the words in order score 100, a line with most of them less; case and punctuation
    // ignored; subclips find what is said within them; filters on Score and Transcription.
    void phraseFind() {
        BinItem master;
        master.id = QStringLiteral("m");
        master.name = QStringLiteral("Interview");
        master.kind = BinItemKind::MasterClip;
        master.editRate = lumenis::Rational(25);
        master.durationFrames = 500;
        const QString mob = QString::fromStdString(mobIdOf(master).toString());
        Transcript t;
        t.mob = mob;
        t.lines = {TranscriptLine{1, {{QStringLiteral("Here's"), 1.0, 1.3}, {QStringLiteral("to"), 1.3, 1.4}, {QStringLiteral("you,"), 1.4, 1.6}}},
                   TranscriptLine{2, {{QStringLiteral("Mrs."), 1.8, 2.0}, {QStringLiteral("Robinson."), 2.0, 2.6}}},
                   TranscriptLine{1, {{QStringLiteral("You"), 10.0, 10.2}, {QStringLiteral("know"), 10.2, 10.4}, {QStringLiteral("Robinsons?"), 10.4, 11.0}}}};
        const TranscriptFor transcriptFor = [&](const QString& m) -> std::optional<Transcript> {
            return m == mob ? std::optional(t) : std::nullopt;
        };
        BinItem sub = master;
        sub.id = QStringLiteral("s");
        sub.name = QStringLiteral("Late");
        sub.kind = BinItemKind::Subclip;
        sub.durationFrames = 100;
        ClipSource src;
        src.kind = ClipSource::Kind::Clip;
        src.mob = mobIdOf(master);
        src.offset = 200;
        sub.sources = {src};
        BinItem seq;
        seq.id = QStringLiteral("q");
        seq.kind = BinItemKind::Sequence;
        Bin bin;
        bin.items = {master, sub, seq};
        const QList<FindBin> bins{{QStringLiteral("b"), QStringLiteral("Bin 1"), bin}};
        QCOMPARE(phraseWords(QStringLiteral("Here's to you, Mrs. Robinson.")), QStringList({"heres", "to", "you", "mrs", "robinson"}));
        // Across two lines, in order: 100, at "Here's" (frame 25), the IN on the frame before.
        auto hits = findPhrase(bins, transcriptFor, QStringLiteral("heres to you mrs robinson"), {}, {});
        QCOMPARE(hits.size(), qsizetype(1));
        QCOMPARE(hits[0].score, 100);
        QCOMPARE(hits[0].item.id, QStringLiteral("m"));
        QCOMPARE(hits[0].frame, std::int64_t(25));
        QCOMPARE(phraseMarkIn(hits[0]), std::int64_t(24));
        QCOMPARE(hits[0].text, QStringLiteral("Here's to you, Mrs. Robinson."));
        // "you robinson": the master's later line holds both (You, Robinsons? as half): below 100; the subclip from
        // frame 200 shows that line too, at its frame 50.
        hits = findPhrase(bins, transcriptFor, QStringLiteral("you robinson"), {}, {});
        QVERIFY(hits.size() >= 3);
        for (const PhraseHit& h : hits) QVERIFY(h.score >= 50 && h.score <= 100);
        QVERIFY(std::is_sorted(hits.cbegin(), hits.cend(), [](const PhraseHit& a, const PhraseHit& b) { return a.score > b.score; }));
        bool subclip = false;
        for (const PhraseHit& h : hits) {
            if (h.item.id == QStringLiteral("s")) {
                subclip = true;
                QCOMPARE(h.frame, std::int64_t(50));
                QVERIFY(h.score < 100);
            }
        }
        QVERIFY(subclip);
        // Filters.
        hits = findPhrase(bins, transcriptFor, QStringLiteral("you robinson"), {{QStringLiteral("Name"), QStringLiteral("Late")}}, {});
        QCOMPARE(hits.size(), qsizetype(1));
        hits = findPhrase(bins, transcriptFor, QStringLiteral("robinson"), {{QStringLiteral("Transcription"), QStringLiteral("mrs")}}, {});
        QCOMPARE(hits.size(), qsizetype(1));
        QCOMPARE(hits[0].score, 100);
        QVERIFY(findPhrase(bins, transcriptFor, QStringLiteral("elephant"), {}, {}).isEmpty());
        QVERIFY(findPhrase(bins, transcriptFor, QStringLiteral("  ...  "), {}, {}).isEmpty());
    }

    // Transcripts out (p.450-453): the exported text as an Avid DS caption file with the speaker, captions with a
    // character limit and marks, Create SubCap's new track, the shared transcripts file.
    void transcriptsOut() {
        ShownTranscript shown;
        auto word = [](const QString& text, std::int64_t frame, std::int64_t end) { return ShownWord{text, frame, end, QStringLiteral("m"), 0, 0}; };
        ShownLine a;
        a.speaker = QStringLiteral("Speaker 1");
        a.words = {word(QStringLiteral("Hello"), 25, 35), word(QStringLiteral("there,"), 35, 45), word(QStringLiteral("friend."), 45, 60)};
        ShownLine b;
        b.speaker = QStringLiteral("Ann");
        b.words = {word(QStringLiteral("Hi."), 100, 110)};
        shown.lines = {a, b};
        const lumenis::TimecodeFormat format = lumenis::TimecodeFormat::forRate(lumenis::Rational(25), false);
        const QString text = exportedTranscriptText(shown, format, 90000); // starts at 01:00:00:00
        QVERIFY2(text.contains(QStringLiteral("01:00:01:00 01:00:02:10")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Speaker 1:\nHello there, friend.")));
        QVERIFY(text.contains(QStringLiteral("Ann:\nHi.")));
        // It reads back as SubCap captions.
        const auto back = parseDsCaptions(text, format);
        QVERIFY(back);
        QCOMPARE(back->size(), qsizetype(2));
        QCOMPARE(back->at(0).start, std::int64_t(90025));

        QList<Caption> captions = transcriptCaptions(shown);
        QCOMPARE(captions.size(), qsizetype(2));
        QCOMPARE(captions[0].text, QStringLiteral("Hello there, friend."));
        QCOMPARE(captions[0].end, std::int64_t(60));
        // 12 characters a row, two rows a caption: "Hello there," / "friend." fits one caption.
        captions = transcriptCaptions(shown, {12, std::nullopt});
        QCOMPARE(captions[0].text, QStringLiteral("Hello there,\nfriend."));
        // 6 characters: three rows, so two captions, the second from "friend.".
        captions = transcriptCaptions(shown, {6, std::nullopt});
        QCOMPARE(captions.size(), qsizetype(3));
        QCOMPARE(captions[1].start, std::int64_t(45));
        // Use Marks: only [40, 105).
        captions = transcriptCaptions(shown, {0, std::pair<std::int64_t, std::int64_t>{40, 105}});
        QCOMPARE(captions.size(), qsizetype(2));
        QCOMPARE(captions[0].start, std::int64_t(40));
        QCOMPARE(captions[1].end, std::int64_t(105));

        // Create SubCap: a new V2 with the SubCap effect subdivided at the captions.
        Sequence seq = Sequence::withDefaultTracks();
        QString error;
        QVERIFY(!createSubCapTrack(seq, {}, &error));
        const auto track = createSubCapTrack(seq, transcriptCaptions(shown), &error);
        QVERIFY2(track, qPrintable(error));
        QCOMPARE(*track, QStringLiteral("V2"));
        const auto made = sequenceCaptions(seq, {*track});
        QCOMPARE(made.size(), qsizetype(2));
        QCOMPARE(made[0].start, std::int64_t(25));
        QCOMPARE(made[1].text, QStringLiteral("Hi."));

        // Sharing.
        Transcript t;
        t.mob = QStringLiteral("mob-1");
        t.lines = {TranscriptLine{1, {{QStringLiteral("Hey"), 0.0, 0.5}}}};
        const QByteArray file = writeSharedTranscripts({QStringLiteral("en"), {t}});
        const auto read = readSharedTranscripts(file, &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(read->languageHint, QStringLiteral("en"));
        QCOMPARE(read->transcripts.size(), qsizetype(1));
        QCOMPARE(read->transcripts[0].lines, t.lines);
        QVERIFY(!readSharedTranscripts("{}", &error));
    }

    // Manage Speaker Names (p.445): renaming, merging and clearing by name, and the project's list.
    void speakerNames() {
        Transcript t;
        t.lines = {TranscriptLine{1, {{QStringLiteral("a"), 0, 1}}}, TranscriptLine{2, {{QStringLiteral("b"), 1, 2}}},
                   TranscriptLine{3, {{QStringLiteral("c"), 2, 3}}}};
        t.speakers = {{1, QStringLiteral("Ann")}, {2, QStringLiteral("Anne")}, {3, QStringLiteral("Bob")}};
        QCOMPARE(lumenis::model::speakerNames(t), QStringList({"Ann", "Anne", "Bob"}));
        QVERIFY(!renameSpeakerName(t, QStringLiteral("Zed"), QStringLiteral("Z")));
        QVERIFY(renameSpeakerName(t, QStringLiteral("Bob"), QStringLiteral("Robert")));
        QCOMPARE(t.speakerName(3), QStringLiteral("Robert"));
        QVERIFY(t.edited());
        // Merge: one Speaker ID, the lowest.
        QVERIFY(mergeSpeakerNames(t, {QStringLiteral("Anne"), QStringLiteral("Ann")}, QStringLiteral("Anne")));
        QCOMPARE(t.lines[0].speaker, 1);
        QCOMPARE(t.lines[1].speaker, 1);
        QCOMPARE(t.speakerName(1), QStringLiteral("Anne"));
        QVERIFY(!t.speakers.contains(2));
        QVERIFY(!mergeSpeakerNames(t, {QStringLiteral("Anne")}, QStringLiteral("Anne")));
        // Renaming to a name another speaker has merges them.
        QVERIFY(renameSpeakerName(t, QStringLiteral("Robert"), QStringLiteral("Anne")));
        QCOMPARE(t.lines[2].speaker, 1);
        QCOMPARE(t.speakers.size(), qsizetype(1));
        QVERIFY(clearSpeakerName(t, QStringLiteral("Anne")));
        QCOMPARE(t.speakerName(1), QStringLiteral("Speaker 1"));
        QVERIFY(!clearSpeakerName(t, QStringLiteral("Anne")));

        QTemporaryDir dir;
        {
            ProjectSpeakers list(dir.path());
            QVERIFY(list.names().isEmpty());
            QVERIFY(list.add(QStringLiteral("Zoe")));
            QVERIFY(list.add(QStringLiteral("adam")));
            QVERIFY(!list.add(QStringLiteral("Zoe")));
            QVERIFY(list.rename(QStringLiteral("adam"), QStringLiteral("Adam")));
            QVERIFY(!list.rename(QStringLiteral("Carl"), QStringLiteral("Carl Jr"))); // not listed
        }
        ProjectSpeakers again(dir.path());
        QCOMPARE(again.names(), QStringList({"Adam", "Zoe"}));
        QVERIFY(again.remove(QStringLiteral("Zoe")));
        QVERIFY(!again.remove(QStringLiteral("Zoe")));
        QCOMPARE(ProjectSpeakers(dir.path()).names(), QStringList({"Adam"}));
    }
};

QTEST_GUILESS_MAIN(TestTranscript)
#include "tst_transcript.moc"

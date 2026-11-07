// Transcripts (Editing Guide ch.14; lumenis/model/transcript.h): lines from an engine's words, JSON round trips, the
// transcript store and which bins are transcribed.
#include "lumenis/model/transcript.h"

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
};

QTEST_GUILESS_MAIN(TestTranscript)
#include "tst_transcript.moc"

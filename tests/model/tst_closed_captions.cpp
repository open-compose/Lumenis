// Closed captions (lumenis/model/closed_captions.h; docs/spec/closed-captions.md): SCC files in and out, CEA-608
// decoding of Pop-on and Roll-up captions, our pop-on encoder, the sequence's D1 data, broken captions and TTML.
#include "lumenis/model/closed_captions.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

QByteArray words(const QString& hex) {
    QByteArray out;
    for (const QString& w : hex.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const uint v = w.toUInt(nullptr, 16);
        out += static_cast<char>(v >> 8);
        out += static_cast<char>(v & 0xff);
    }
    return out;
}

} // namespace

class TestClosedCaptions : public QObject {
    Q_OBJECT

private slots:
    void parity() {
        QCOMPARE(static_cast<unsigned char>(withParity(0x14)), 0x94u);
        QCOMPARE(static_cast<unsigned char>(withParity(0x20)), 0x20u);
        QCOMPARE(static_cast<unsigned char>(withParity(0x48)), 0xc8u);
        QCOMPARE(static_cast<unsigned char>(withParity(0x00)), 0x80u);
    }

    void sccPopOn() {
        // "The starting timecode and drop frame checkbox will be automatically populated" (p.232) from the first line.
        const QString scc = QStringLiteral("Scenarist_SCC V1.0\n\n"
                                           "01:00:00;00\t9420 9420 94ae 94ae 9470 9470 c8e5 ecec ef80 942f 942f\n\n"
                                           "01:00:02;00\t942c 942c\n");
        QString error;
        const auto file = parseScc(scc, &error);
        QVERIFY2(file, qPrintable(error));
        QVERIFY(file->dropFrame);
        QCOMPARE(file->lines.size(), 2);
        QCOMPARE(file->startFrame(), Timecode::parse("01:00:00;00", {30, true})->frameCount());
        const QByteArray data = sccCaptionData(*file, file->startFrame(), Rational(30000, 1001));
        QCOMPARE(data.size(), 62 * 2);
        const QList<ShownCaption> shown = decodeCaptions(data);
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown.first().start, std::int64_t(9)); // the first EOC; the second is the repeat, ignored
        QCOMPARE(shown.first().end, std::int64_t(60));
        QCOMPARE(shown.first().screen.rows, (QList<CaptionRow>{{15, 0, QStringLiteral("Hello")}}));
        QVERIFY(captionAt(shown, 30));
        QVERIFY(!captionAt(shown, 8));
        QVERIFY(!captionAt(shown, 60));
        QCOMPARE(captionLoadingSpans(data), (QList<std::pair<std::int64_t, std::int64_t>>{{0, 11}, {60, 62}}));

        // Out again: the same lines, timecode from the start.
        const QString back = writeScc(data, file->startFrame(), {30, true});
        const auto again = parseScc(back);
        QVERIFY(again);
        QCOMPARE(again->lines, file->lines);
        QVERIFY(!parseScc(QStringLiteral("00:00:00:00 9420")));
        QVERIFY(!parseScc(QStringLiteral("Scenarist_SCC V1.0\n00:00:00:00\t94")));

        // At 25 frames a second the lines land at their times.
        const QByteArray pal = sccCaptionData(*file, file->startFrame(), Rational(25));
        QCOMPARE(captionLoadingSpans(pal).at(1).first, std::int64_t(50));
    }

    void rollUpAndCharacters() {
        // RU2, CR, "AB", CR, "C" ® "D": two rows that roll.
        QByteArray data = words(QStringLiteral("9425 9425 94ad 94ad 9470 c1c2 94ad 9470 4380 9130 c480"));
        const QList<ShownCaption> shown = decodeCaptions(data);
        QVERIFY(!shown.isEmpty());
        QCOMPARE(shown.last().screen.rows,
                 (QList<CaptionRow>{{14, 0, QStringLiteral("AB")}, {15, 0, QStringLiteral("C®D")}}));
        // Extended characters replace the one before them; basic 0x7e is ñ.
        data = words(QStringLiteral("9429 9470 4180 1338 fe80"));
        QCOMPARE(decodeCaptions(data).last().screen.text(), QStringLiteral("Åñ"));
    }

    void popOnRoundTrip() {
        const QList<Caption> captions{{30, 60, QStringLiteral("Hello world")},
                                      {60, 90, QStringLiteral("Two\nrows")},
                                      {120, 150, QStringLiteral("Ñandú ® {x}")}};
        const QByteArray data = popOnCaptionData(captions, 200);
        QCOMPARE(data.size(), 400);
        const QList<ShownCaption> shown = decodeCaptions(data);
        QCOMPARE(shown.size(), 3);
        for (qsizetype i = 0; i < 3; ++i) {
            QCOMPARE(shown.at(i).start, captions.at(i).start);
            QCOMPARE(shown.at(i).end, captions.at(i).end);
            QCOMPARE(shown.at(i).screen.text(), captions.at(i).text);
        }
        // Centred at the bottom.
        QCOMPARE(shown.at(1).screen.rows.last().row, 15);
        QCOMPARE(shown.at(1).screen.rows.first().row, 14);
        QCOMPARE(shown.first().screen.rows.first().column, (32 - 11) / 2);
        // Captions too close together still show, a little late.
        const QList<ShownCaption> tight = decodeCaptions(popOnCaptionData({{2, 10, QStringLiteral("Late")}}, 20));
        QCOMPARE(tight.size(), 1);
        QVERIFY(tight.first().start >= 2);
        QCOMPARE(tight.first().end, std::int64_t(10));
    }

    void sequenceData() {
        // A caption clip of 100 frames: a caption loading over 20-30, shown 30-60.
        const QByteArray clip = popOnCaptionData({{30, 60, QStringLiteral("One")}, {80, 95, QStringLiteral("Two")}}, 100);
        const auto spans = captionLoadingSpans(clip);
        QVERIFY(spans.size() >= 2);
        Sequence seq;
        Track* d = seq.ensureTrack(QStringLiteral("D1"));
        QVERIFY(d);
        Segment a;
        a.clipId = QStringLiteral("cc");
        a.sourceIn = 0;
        a.length = 50;
        Segment gap;
        gap.length = 10;
        Segment b = a;
        b.sourceIn = 50;
        b.length = 50;
        d->segments = {a, gap, b};
        auto data = [&](const Segment&) { return clip; };
        const QByteArray seqData = sequenceCaptionData(seq, data);
        QCOMPARE(seqData.size(), 110 * 2);
        QCOMPARE(seqData.mid(0, 100), clip.mid(0, 100));
        QCOMPARE(seqData.mid(100, 20), QByteArray(kNullCaptionWord, 2).repeated(10));
        const QList<ShownCaption> shown = decodeCaptions(seqData);
        QCOMPARE(shown.first().screen.text(), QStringLiteral("One"));
        QCOMPARE(shown.last().screen.text(), QStringLiteral("Two"));
        QCOMPARE(shown.last().start, std::int64_t(90));
        QVERIFY(brokenCaptionCuts(seq, data).isEmpty());
        // A cut inside the second caption's loading span breaks it.
        // (The span after the first caption's EDM word.)
        const auto two = std::max_element(spans.begin() + 1, spans.end(), [](const auto& x, const auto& y) {
            return x.second - x.first < y.second - y.first;
        });
        const std::int64_t mid = (two->first + two->second) / 2;
        d->segments = {a, b};
        d->segments[0].length = mid;
        d->segments[1].sourceIn = mid;
        d->segments[1].length = 100 - mid;
        QCOMPARE(brokenCaptionCuts(seq, data), (QList<std::int64_t>{mid}));
        QVERIFY(sequenceCaptionData(Sequence{}, data).isEmpty());
    }

    void clipData() {
        BinItem item;
        QVERIFY(!hasCaptionData(item));
        setClipCaptionData(item, words(QStringLiteral("9420 8080")));
        QVERIFY(hasCaptionData(item));
        QCOMPARE(item.durationFrames, std::int64_t(2));
        QCOMPARE(clipCaptionData(item), words(QStringLiteral("9420 8080")));
    }

    void ttml() {
        const QList<ShownCaption> shown{{30, 60, {{{14, 4, QStringLiteral("A & B")}, {15, 4, QStringLiteral("<c>")}}}}};
        const QString t = writeTtml(shown, Rational(30000, 1001));
        QVERIFY(t.contains(QStringLiteral("xmlns=\"http://www.w3.org/ns/ttml\"")));
        QVERIFY2(t.contains(QStringLiteral("<p begin=\"00:00:01.001\" end=\"00:00:02.002\">A &amp; B<br/>&lt;c&gt;</p>")), qPrintable(t));
    }
};

QTEST_MAIN(TestClosedCaptions)
#include "tst_closed_captions.moc"

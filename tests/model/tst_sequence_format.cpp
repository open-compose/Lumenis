// Set Format on sequences (Editing Guide p.1294; 8.7c1, docs/spec/sequence-format.md): a sequence modified in place at
// the same edit rate, and a new sequence with its media unlinked when the edit rate changes.
#include "lumenis/model/marker.h"
#include "lumenis/model/output_format.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/sequence_format.h"
#include "lumenis/model/universal_mastering.h"

#include <QTest>

using namespace lumenis;
using namespace lumenis::model;

namespace {

Segment piece(const QString& clipId, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = clipId;
    s.name = clipId;
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

ProjectFormat formatNamed(const std::string& label) {
    for (const ProjectFormat& f : projectFormatPresets()) {
        if (f.label() == label) return f;
    }
    return {};
}

} // namespace

class TestSequenceFormat : public QObject {
    Q_OBJECT

private slots:
    // HD Universal Mastering's remastered rates (p.1297): the guide's table, from 48 kHz.
    void universalMastering() {
        const QList<MasteringConversion>& list = masteringConversions();
        QCOMPARE(list.size(), 6);
        QList<int> rates;
        for (const MasteringConversion& c : list) rates << c.sampleRate;
        QCOMPARE(rates, (QList<int>{47952, 46080, 48048, 50050, 50000, 46034}));
        QCOMPARE(list.first().label, QStringLiteral("23.97fps -> 24fps for Universal Mastering"));
        QCOMPARE(masteredSampleRate(Rational(24), Rational(25)), 46080);
        QCOMPARE(masteredSampleRate(Rational(25), Rational(24000, 1001)), 50050);
        QCOMPARE(masteredSampleRate(Rational(25), Rational(25)), 48000);
    }

    // The Digital Cut tool's Output Format menu (p.933-937, p.1299): the formats by project, the pulldown cadences, the
    // tape frames and the sound's speed.
    void outputFormatsAndPulldown() {
        auto labels = [](const ProjectFormat& p) {
            QStringList out;
            for (const OutputFormat& f : outputFormats(p)) out << f.label;
            return out;
        };
        auto preset = [](const std::string& group, int height, const Rational& rate, ScanMode scan) {
            for (const ProjectFormat& f : projectFormatPresets()) {
                if (f.group == group && f.height == height && f.editRate == rate && f.scan == scan) return f;
            }
            return ProjectFormat{};
        };
        const Rational film(24000, 1001);
        const Rational ntsc(30000, 1001);
        // "the system will only display output options for one format" (p.933): NTSC or PAL by the project.
        QCOMPARE(labels(preset("NTSC", 486, film, ScanMode::Progressive)),
                 (QStringList{QStringLiteral("23.976 (NTSC)"), QStringLiteral("24 (NTSC)"), QStringLiteral("29.97 (NTSC)")}));
        QCOMPARE(labels(preset("NTSC", 486, Rational(24), ScanMode::Progressive)).first(), QStringLiteral("24 (NTSC)"));
        QCOMPARE(labels(preset("PAL", 576, Rational(25), ScanMode::Progressive)),
                 (QStringList{QStringLiteral("25 (PAL)"), QStringLiteral("24 (PAL)")}));
        // The table on p.1299.
        QCOMPARE(labels(preset("HD 1080p", 1080, Rational(24), ScanMode::Progressive)),
                 (QStringList{QStringLiteral("1080p24"), QStringLiteral("1080p23.976"), QStringLiteral("1080p25/PAL"),
                              QStringLiteral("1080i59.94/NTSC")}));
        QCOMPARE(labels(preset("HD 1080p", 1080, Rational(25), ScanMode::Progressive)).last(), QStringLiteral("1080i59.94 NTSC"));
        const QList<OutputFormat> hd = outputFormats(preset("HD 1080i", 1080, ntsc, ScanMode::Interlaced));
        QCOMPARE(hd.size(), 2);
        QCOMPARE(hd.last().label, QStringLiteral("NTSC 30i"));
        QCOMPARE(hd.last().height, 486);
        QCOMPARE(labels(preset("HD 720p", 720, Rational(25), ScanMode::Progressive)), (QStringList{QStringLiteral("720p/25")}));

        const QList<OutputFormat> sd = outputFormats(preset("NTSC", 486, film, ScanMode::Progressive));
        const OutputFormat& telecine = sd.at(0);
        QVERIFY(telecine.cadence);
        QVERIFY(telecine.ntsc);
        QVERIFY(sd.at(1).destinationRate);
        auto fields = [](const OutputFormat& f, PulldownCadence c, bool fromB) {
            QList<int> out;
            for (int i = 0; i < 10; ++i) out << static_cast<int>(outputSourceFrame(f, i, c, fromB));
            return out;
        };
        // AA BBB CC DDD, AA BBB CCC DD (p.937), and from a B frame BBB CC DDD AA (p.938).
        QCOMPARE(fields(telecine, PulldownCadence::Standard, false), (QList<int>{0, 0, 1, 1, 1, 2, 2, 3, 3, 3}));
        QCOMPARE(fields(telecine, PulldownCadence::Advanced, false), (QList<int>{0, 0, 1, 1, 1, 2, 2, 2, 3, 3}));
        QCOMPARE(fields(telecine, PulldownCadence::Standard, true), (QList<int>{0, 0, 0, 1, 1, 2, 2, 2, 3, 3}));
        QCOMPARE(outputFrameCount(telecine, 4), 5);
        QCOMPARE(outputFrameCount(telecine, 24), 30);
        // 24 (PAL): "one extra pulldown field occurring every 12th and 24th frame" (p.934): 24 frames in 50 fields.
        const OutputFormat pal24 = outputFormats(preset("PAL", 576, Rational(24), ScanMode::Progressive)).first();
        QCOMPARE(pal24.label, QStringLiteral("24 (PAL)"));
        QCOMPARE(outputFrameCount(pal24, 24), 25);
        int threes = 0;
        for (int frame = 0, field = 0; frame < 24; ++frame) {
            int n = 0;
            while (outputSourceFrame(pal24, field) == frame) ++n, ++field;
            if (n == 3) ++threes;
        }
        QCOMPARE(threes, 2);
        // The sound's speed (p.935): 0.1% slower for 23.976 NTSC from 24p, 4.1% faster for 25 PAL.
        QVERIFY(qAbs(outputSpeed(telecine, Rational(24)) - 0.999) < 1e-6);
        QCOMPARE(outputSpeed(outputFormats(preset("PAL", 576, Rational(24), ScanMode::Progressive)).last(), Rational(24)), 25.0 / 24.0);
        // 01:00:00:00 at 24 fps is 01:00:00:00 at 29.97 and at 25.
        QCOMPARE(sameTimecodeFrames(86400, Rational(24), ntsc), 108000);
        QCOMPARE(sameTimecodeFrames(86400, Rational(24), Rational(25)), 90000);
    }

    void sameRateModifies() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice(QStringLiteral("V1"), 0, {piece(QStringLiteral("a"), 10, 48)});
        BinItem seq;
        seq.kind = BinItemKind::Sequence;
        seq.id = newItemId();
        seq.name = QStringLiteral("Cut");
        seq.editRate = Rational(30000, 1001);
        s.storeIn(seq);
        // "you can change an NTSC 30i sequence to 1080i/59.94" (p.1294): the same edit rate.
        const ProjectFormat hd = formatNamed("1080i/59.94");
        QCOMPARE(hd.editRate, seq.editRate);
        const auto change = setSequenceFormat(seq, hd);
        QVERIFY(change);
        QVERIFY(!change->duplicated);
        QCOMPARE(change->item.id, seq.id);
        QCOMPARE(change->item.fields.value(QStringLiteral("Format")), QStringLiteral("1080i/59.94"));
        QCOMPARE(Sequence::fromItem(change->item), s);
    }

    void otherRateDuplicates() {
        Sequence s = Sequence::withDefaultTracks();
        s.splice(QStringLiteral("V1"), 0, {piece(QStringLiteral("a"), 24, 48), piece(QStringLiteral("b"), 0, 24)});
        BinItem seq;
        seq.kind = BinItemKind::Sequence;
        seq.id = newItemId();
        seq.name = QStringLiteral("Cut");
        seq.editRate = Rational(24);
        s.storeIn(seq);
        Marker m;
        m.id = QStringLiteral("m1");
        m.frame = 48;
        m.track = QStringLiteral("V1");
        setMarkers(seq, {m});
        // 24 to 25 fps: "Media Composer creates a new sequence labeled with the extension ... fps" (p.1294).
        const ProjectFormat pal = formatNamed("1080p/25");
        const auto change = setSequenceFormat(seq, pal);
        QVERIFY(change);
        QVERIFY(change->duplicated);
        QVERIFY(change->item.id != seq.id);
        QVERIFY(mobIdOf(change->item) != mobIdOf(seq));
        QCOMPARE(change->item.name, QStringLiteral("Cut 25 fps"));
        QCOMPARE(change->original, QStringLiteral("Cut"));
        QCOMPARE(change->item.editRate, Rational(25));
        const Sequence out = Sequence::fromItem(change->item);
        const Track* v1 = out.track(QStringLiteral("V1"));
        QVERIFY(v1);
        QCOMPARE(v1->segments.size(), 2);
        // The same times at the new rate: 2 s and 1 s; the first's source starts 1 s in.
        QCOMPARE(v1->segments.at(0).length, 50);
        QCOMPARE(v1->segments.at(0).sourceIn, 25);
        QCOMPARE(v1->segments.at(1).length, 25);
        QCOMPARE(change->item.durationFrames, 75);
        // The media unlinked: clips no bin has, names kept.
        QVERIFY(!v1->segments.at(0).isFiller());
        QVERIFY(v1->segments.at(0).clipId.startsWith(QLatin1String("offline-")));
        QCOMPARE(v1->segments.at(0).name, QStringLiteral("a"));
        QCOMPARE(markersOf(change->item).first().frame, 50);
        // Not a sequence: refused.
        BinItem clip;
        clip.kind = BinItemKind::MasterClip;
        QString why;
        QVERIFY(!setSequenceFormat(clip, pal, &why));
        QVERIFY(!why.isEmpty());
        QCOMPARE(convertFrames(1000, Rational(30000, 1001), Rational(30)), 1001);
    }
};

QTEST_GUILESS_MAIN(TestSequenceFormat)
#include "tst_sequence_format.moc"

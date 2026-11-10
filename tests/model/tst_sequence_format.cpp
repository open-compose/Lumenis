// Set Format on sequences (Editing Guide p.1294; 8.7c1, docs/spec/sequence-format.md): a sequence modified in place at
// the same edit rate, and a new sequence with its media unlinked when the edit rate changes.
#include "lumenis/model/marker.h"
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

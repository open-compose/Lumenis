#include "lumenis/interop/edl.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/structure.h"
#include "lumenis/model/titler.h"

#include <QDir>
#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

// EDL import and export (src/interop/edl.*, docs/spec/edl.md): parsing edge cases, lines that cannot
// be read, round trips through the native model, and OpenTimelineIO's sample lists when the
// interchange corpus is at hand (scripts/interop-corpus.py).

using namespace lumenis;
using namespace lumenis::interop;

namespace {

QString corpusFolder() {
    const QString folder = qEnvironmentVariable("CUTTHULHU_INTEROP_CORPUS");
    return !folder.isEmpty() && QDir(folder).exists() ? folder : QString();
}

edl::ImportOptions at(const Rational& rate) {
    edl::ImportOptions o;
    o.rate = rate;
    o.binId = QStringLiteral("bin");
    return o;
}

const model::BinItem& sequenceOf(const edl::ImportResult& r) {
    const model::BinItem* s = r.sequence();
    if (!s) qFatal("no sequence");
    return *s;
}

// A copy of the named track (a copy, so that no reference outlives the sequence it came from).
model::Track trackOf(const model::Sequence& s, const QString& name) {
    const model::Track* t = s.track(name);
    if (!t) qFatal("no track %s", qPrintable(name));
    return *t;
}

// The items' structure dump without what differs between two imports of the same list: the
// sequence's MobID (a new sequence each time) and the bin's name.
QJsonObject structure(const QList<model::BinItem>& items) {
    model::Bin bin;
    bin.items = items;
    QJsonObject o = model::binStructure(bin);
    QJsonArray out;
    for (const QJsonValue v : o.value(QStringLiteral("items")).toArray()) {
        QJsonObject item = v.toObject();
        if (item.value(QStringLiteral("kind")).toString() == QLatin1String("sequence")) item.remove(QStringLiteral("mobId"));
        out.append(item);
    }
    // In name order: the reels' source items come in the order the list names them, which a list
    // written again in record order can change.
    QList<QJsonValue> sorted(out.begin(), out.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonValue& x, const QJsonValue& y) {
        return x.toObject().value(QStringLiteral("name")).toString() < y.toObject().value(QStringLiteral("name")).toString();
    });
    QJsonArray ordered;
    for (const QJsonValue& v : sorted) ordered.append(v);
    o.insert(QStringLiteral("items"), ordered);
    o.remove(QStringLiteral("name"));
    return o;
}

// An EDL's own export options for a round trip: case kept, clips found among the import's items.
edl::ExportOptions roundTripOptions(const QList<model::BinItem>& items) {
    edl::ExportOptions o;
    o.upperCase = false;
    o.resolve = [items](const MobId& mob, const QString& clipId) -> std::optional<model::BinItem> {
        for (const model::BinItem& i : items) {
            if (i.id == clipId || (i.kind != model::BinItemKind::Sequence && model::mobIdOf(i) == mob)) return i;
        }
        return std::nullopt;
    };
    return o;
}

} // namespace

class TestEdl : public QObject {
    Q_OBJECT

private slots:
    void cuts();
    void channels();
    void dropFrame();
    void dissolve();
    void wipeAndKey();
    void speed();
    void locators();
    void guessedRate();
    void malformed();
    void reelNames();
    void roundTrip();
    void centredTransition();
    void optimize();
    void masterClips();
    void reuseSources();
    void titles();
    void corpus();
};

void TestEdl::cuts() {
    const QString text = QStringLiteral(
        "TITLE:   Reel One\r\n"
        "FCM: NON-DROP FRAME\r\n"
        "001  TAPE01   V     C        01:00:10:00 01:00:12:00 10:00:00:00 10:00:02:00\r\n"
        "* FROM CLIP NAME:  Wide Shot\r\n"
        "002  TAPE02   AA/V  C        02:00:00:00 02:00:01:00 10:00:02:00 10:00:03:00\r\n"
        "* FROM CLIP NAME:  Close\r\n"
        "003  BL       V     C        00:00:00:00 00:00:00:10 10:00:03:00 10:00:03:10\r\n"
        "004  TAPE01   V     C        01:00:20:00 01:00:20:05 10:00:03:10 10:00:03:15\r\n");
    const auto r = edl::importText(text, at(Rational(25)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(r.items.size(), 3);
    // A source item for each reel, then the sequence.
    QCOMPARE(r.items.at(0).kind, model::BinItemKind::Source);
    QCOMPARE(r.items.at(0).name, QStringLiteral("TAPE01"));
    QCOMPARE(model::mobIdOf(r.items.at(0)), edl::tapeMobId(QStringLiteral("TAPE01")));
    QCOMPARE(r.items.at(0).fields.value(QStringLiteral("Tape")), QStringLiteral("TAPE01"));
    QCOMPARE(r.items.at(1).fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));
    const model::BinItem& item = sequenceOf(r);
    QCOMPARE(item.name, QStringLiteral("Reel One"));
    QCOMPARE(item.editRate, Rational(25));
    QCOMPARE(item.fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:00"));
    QCOMPARE(item.fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2 TC1"));
    const model::Sequence s = model::Sequence::fromItem(item);
    const model::Track& v1 = trackOf(s, QStringLiteral("V1"));
    QCOMPARE(v1.segments.size(), 4);
    QCOMPARE(v1.segments.at(0).name, QStringLiteral("Wide Shot"));
    QCOMPARE(v1.segments.at(0).sourceIn, (3600 + 10) * 25);
    QCOMPARE(v1.segments.at(0).length, 50);
    QCOMPARE(v1.segments.at(0).clipId, r.items.at(0).id);
    QCOMPARE(v1.segments.at(0).binId, QStringLiteral("bin"));
    QCOMPARE(v1.segments.at(1).name, QStringLiteral("Close"));
    // Black is filler.
    QVERIFY(v1.segments.at(2).isFiller());
    QCOMPARE(v1.segments.at(2).length, 10);
    QCOMPARE(v1.segments.at(3).sourceIn, (3600 + 20) * 25);
    const model::Track& a2 = trackOf(s, QStringLiteral("A2"));
    QCOMPARE(a2.segments.size(), 2);
    QVERIFY(a2.segments.at(0).isFiller());
    QCOMPARE(a2.segments.at(1).sourceTrack, QStringLiteral("A2"));
    QCOMPARE(item.durationFrames, 3 * 25 + 15);
    QCOMPARE(item.fields.value(QStringLiteral("End")), QStringLiteral("10:00:03:15"));
}

void TestEdl::channels() {
    // Every channel field: V, A, A2, AA, B, A/V, AA/V, A2/V, and NONE with AUD 3 4.
    const QString text = QStringLiteral(
        "001  R V     C 00:00:00:00 00:00:00:01 01:00:00:00 01:00:00:01\n"
        "002  R A     C 00:00:00:00 00:00:00:01 01:00:00:01 01:00:00:02\n"
        "003  R A2    C 00:00:00:00 00:00:00:01 01:00:00:02 01:00:00:03\n"
        "004  R AA    C 00:00:00:00 00:00:00:01 01:00:00:03 01:00:00:04\n"
        "005  R B     C 00:00:00:00 00:00:00:01 01:00:00:04 01:00:00:05\n"
        "006  R A/V   C 00:00:00:00 00:00:00:01 01:00:00:05 01:00:00:06\n"
        "007  R AA/V  C 00:00:00:00 00:00:00:01 01:00:00:06 01:00:00:07\n"
        "008  R A2/V  C 00:00:00:00 00:00:00:01 01:00:00:07 01:00:00:08\n"
        "009  R NONE  C 00:00:00:00 00:00:00:01 01:00:00:08 01:00:00:09\n"
        "AUD  3    4\n");
    const auto r = edl::importText(text, at(Rational(24)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    const model::Sequence s = model::Sequence::fromItem(sequenceOf(r));
    QCOMPARE(s.trackNames(), (QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3"),
                                          QStringLiteral("A4")}));
    // Which of the nine frames each track has material on.
    auto frames = [&](const QString& name) {
        QString out;
        for (int f = 0; f < 9; ++f) {
            const auto m = s.material(name, f, 1);
            out += m.isEmpty() || m.first().isFiller() ? QLatin1Char('.') : QLatin1Char('x');
        }
        return out;
    };
    QCOMPARE(frames(QStringLiteral("V1")), QStringLiteral("x...xxxx."));
    QCOMPARE(frames(QStringLiteral("A1")), QStringLiteral(".x.xxxx.."));
    QCOMPARE(frames(QStringLiteral("A2")), QStringLiteral("..xx..xx."));
    QCOMPARE(frames(QStringLiteral("A3")), QStringLiteral("........x"));
    QCOMPARE(frames(QStringLiteral("A4")), QStringLiteral("........x"));
}

void TestEdl::dropFrame() {
    const QString text = QStringLiteral(
        "TITLE: DF\n"
        "FCM: DROP FRAME\n"
        "001  CAM1     V     C        00:01:00;02 00:01:01;00 01:00:00;00 01:00:00;28\n");
    const auto r = edl::importText(text, at(Rational(30000, 1001)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    const model::BinItem& item = sequenceOf(r);
    QCOMPARE(item.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00;00"));
    QVERIFY(model::isDropFrame(item));
    const model::Sequence s = model::Sequence::fromItem(item);
    // 00:01:00;02 is the 1800th frame: ;00 and ;01 of minute one do not exist.
    QCOMPARE(trackOf(s, QStringLiteral("V1")).segments.at(0).sourceIn, 1800);
    QCOMPARE(trackOf(s, QStringLiteral("V1")).segments.at(0).length, 28);
    // Written back drop frame, with the same labels.
    const auto out = edl::exportText(item, roundTripOptions(r.items));
    QVERIFY(out.ok);
    QVERIFY(out.text.contains(QStringLiteral("FCM: DROP FRAME")));
    QVERIFY2(out.text.contains(QStringLiteral("00:01:00;02 00:01:01;00 01:00:00;00 01:00:00;28")), qPrintable(out.text));
    // Drop frame at a rate without it is read as non-drop, with a warning.
    const auto at25 = edl::importText(text, at(Rational(25)));
    QVERIFY(!at25.warnings.filter(QStringLiteral("drop frame")).isEmpty());
}

void TestEdl::dissolve() {
    // The dissolve of OpenTimelineIO's dissolve_test_2: the transition starts at the event's record
    // IN, where the incoming clip starts; the outgoing line names the clip before.
    const QString text = QStringLiteral(
        "TITLE:   dissolve_test_2\n"
        "FCM: NON-DROP FRAME\n"
        "001  TST V     C        01:00:04:05 01:00:04:10 01:00:00:00 01:00:00:05 \n"
        "* FROM CLIP NAME:  clip_A\n"
        "002  TST V     C        01:00:04:10 01:00:04:10 01:00:00:05 01:00:00:05 \n"
        "002  TST V     D    010 01:00:08:04 01:00:08:14 01:00:00:05 01:00:00:15 \n"
        "* BLEND, DISSOLVE \n"
        "* FROM CLIP NAME:  clip_A\n"
        "* TO CLIP NAME:  clip_B\n"
        "003  TST V     C        01:00:08:14 01:00:08:19 01:00:00:15 01:00:00:20 \n"
        "* FROM CLIP NAME:  clip_B\n");
    const auto r = edl::importText(text, at(Rational(24)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    const model::Sequence s = model::Sequence::fromItem(sequenceOf(r));
    const model::Track& v1 = trackOf(s, QStringLiteral("V1"));
    QCOMPARE(v1.segments.size(), 3);
    QCOMPARE(v1.segments.at(0).length, 5);
    QCOMPARE(v1.segments.at(1).name, QStringLiteral("clip_B"));
    QCOMPARE(v1.segments.at(1).length, 10);
    QVERIFY(v1.segments.at(1).transition);
    const model::Transition& t = *v1.segments.at(1).transition;
    QCOMPARE(t.effect.category, QStringLiteral("Blend"));
    QCOMPARE(t.effect.name, QStringLiteral("Dissolve"));
    QCOMPARE(t.duration, 10);
    QCOMPARE(t.alignment, model::TransitionAlignment::StartingAtCut);
    // Back out: the same two lines for event 2.
    const auto out = edl::exportText(sequenceOf(r), roundTripOptions(r.items));
    QVERIFY2(out.text.contains(QStringLiteral("002  TST      V     C        01:00:04:10 01:00:04:10 01:00:00:05 01:00:00:05")),
             qPrintable(out.text));
    QVERIFY2(out.text.contains(QStringLiteral("002  TST      V     D    010 01:00:08:04 01:00:08:14 01:00:00:05 01:00:00:15")),
             qPrintable(out.text));
    QVERIFY(out.text.contains(QStringLiteral("* TO CLIP NAME:  clip_B")));
    // A dissolve from black: the outgoing line is BL, and the track is empty before it.
    const auto fade = edl::importText(QStringLiteral("001  BL V C 00:00:00:00 00:00:00:00 01:00:00:00 01:00:00:00\n"
                                                     "001  T1 V D 012 01:00:00:00 01:00:01:00 01:00:00:00 01:00:01:00\n"),
                                      at(Rational(24)));
    QVERIFY2(fade.warnings.isEmpty(), qPrintable(fade.warnings.join(QLatin1Char('\n'))));
    const model::Sequence fs = model::Sequence::fromItem(sequenceOf(fade));
    QCOMPARE(trackOf(fs, QStringLiteral("V1")).segments.size(), 1);
    QCOMPARE(trackOf(fs, QStringLiteral("V1")).segments.at(0).transition->duration, 12);
    const auto fadeOut = edl::exportText(sequenceOf(fade), roundTripOptions(fade.items));
    QVERIFY2(fadeOut.text.contains(QStringLiteral("001  BL       V     C")), qPrintable(fadeOut.text));
    // A comment between a transition's two lines does not part them.
    const auto between = edl::importText(QStringLiteral("001  A V C 00:00:00:00 00:00:01:00 01:00:00:00 01:00:01:00\n"
                                                        "002  A V C 00:00:01:00 00:00:01:00 01:00:01:00 01:00:01:00\n"
                                                        "* FROM CLIP NAME:  first\n"
                                                        "002  B V D 010 00:00:05:00 00:00:06:00 01:00:01:00 01:00:02:00\n"
                                                        "* TO CLIP NAME:  second\n"),
                                         at(Rational(24)));
    QVERIFY2(between.warnings.isEmpty(), qPrintable(between.warnings.join(QLatin1Char('\n'))));
    const model::Track bv = trackOf(model::Sequence::fromItem(sequenceOf(between)), QStringLiteral("V1"));
    QCOMPARE(bv.segments.size(), 2);
    QCOMPARE(bv.segments.at(1).name, QStringLiteral("second"));
    QVERIFY(bv.segments.at(1).transition);
}

void TestEdl::wipeAndKey() {
    const QString text = QStringLiteral(
        "001  A V C        01:00:00:00 01:00:01:00 01:00:00:00 01:00:01:00\n"
        "002  A V C        01:00:01:00 01:00:01:00 01:00:01:00 01:00:01:00\n"
        "002  B V W1    010 02:00:00:00 02:00:01:00 01:00:01:00 01:00:02:00\n"
        "003  A V K B      01:00:05:00 01:00:06:00 01:00:02:00 01:00:03:00\n"
        "003  C V K     000 03:00:00:00 03:00:01:00 01:00:02:00 01:00:03:00\n");
    const auto r = edl::importText(text, at(Rational(24)));
    const model::Sequence s = model::Sequence::fromItem(sequenceOf(r));
    const model::Track& v1 = trackOf(s, QStringLiteral("V1"));
    QCOMPARE(v1.segments.size(), 3);
    QCOMPARE(v1.segments.at(1).transition->effect.category, QStringLiteral("EDL"));
    QCOMPARE(v1.segments.at(1).transition->effect.name, QStringLiteral("W001"));
    // The key's background is on V1, its foreground on V2 with an EDL Key effect.
    QCOMPARE(v1.segments.at(2).name, QStringLiteral("A"));
    const model::Track& v2 = trackOf(s, QStringLiteral("V2"));
    QCOMPARE(v2.segments.last().name, QStringLiteral("C"));
    QCOMPARE(v2.segments.last().effect->name, QStringLiteral("Key"));
    QVERIFY(!r.warnings.filter(QStringLiteral("keys")).isEmpty());
    QVERIFY(!r.warnings.filter(QStringLiteral("wipes")).isEmpty());
    // The wipe is written back as itself.
    const auto out = edl::exportText(sequenceOf(r), roundTripOptions(r.items));
    QVERIFY2(out.text.contains(QStringLiteral("W001 010 02:00:00:00")), qPrintable(out.text));
}

void TestEdl::speed() {
    // OpenTimelineIO's speed_effects_small: a freeze frame (M2 000.0) and 47.6 frames a second.
    const QString text = QStringLiteral(
        "002  Z682_156 V     C        01:00:10:21 01:00:10:22 01:08:30:00 01:08:30:17 \n"
        "M2   Z682_156       000.0                01:00:10:21 \n"
        "* FROM CLIP NAME:  Z682_156 (LAY3) FF \n"
        "* * FREEZE FRAME\n"
        "005  Z686_5A. V     C        01:00:06:00 01:00:08:22 01:08:30:17 01:08:32:05 \n"
        "M2   Z686_5A.       -047.6                01:00:06:00 \n");
    const auto r = edl::importText(text, at(Rational(24)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    const model::Sequence s = model::Sequence::fromItem(sequenceOf(r));
    const model::Track& v1 = trackOf(s, QStringLiteral("V1"));
    QCOMPARE(v1.segments.size(), 2);
    QCOMPARE(v1.segments.at(0).length, 17);
    QCOMPARE(v1.segments.at(0).effect->category, QStringLiteral("EDL"));
    QCOMPARE(v1.segments.at(0).effect->name, QStringLiteral("Motion Effect"));
    QCOMPARE(v1.segments.at(1).effect->parameters.value(QStringLiteral("M2")).toObject().value(QStringLiteral("Speed")).toDouble(), -47.6);
    const auto out = edl::exportText(sequenceOf(r), roundTripOptions(r.items));
    QVERIFY2(out.text.contains(QStringLiteral("M2   Z686_5A.       -047.6                01:00:06:00")), qPrintable(out.text));
    QVERIFY2(out.text.contains(QStringLiteral("01:00:10:21 01:00:10:22 01:08:30:00 01:08:30:17")), qPrintable(out.text));
    const auto again = edl::importText(out.text, at(Rational(24)));
    QCOMPARE(structure(again.items), structure(r.items));
}

void TestEdl::locators() {
    // Media Composer's LOC comments: record timecode, colour, text.
    const QString text = QStringLiteral(
        "004  ZZ100_50 V     C        01:00:10:01 01:00:14:20 00:59:58:00 01:00:02:19 \n"
        "* FROM CLIP NAME:  ZZ100_504C (LAY1) \n"
        "* LOC: 01:00:01:14 RED     ANIM FIX NEEDED\n"
        "* LOC: 01:00:02:14 PINK     ANIM  FIX\n"
        "* LOC: 00:59:59:00\n");
    const auto r = edl::importText(text, at(Rational(24)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    const QList<model::Marker> markers = model::markersOf(sequenceOf(r));
    QCOMPARE(markers.size(), 3);
    QCOMPARE(markers.at(0).frame, 24);
    QCOMPARE(markers.at(1).frame, 24 * 3 + 14);
    QCOMPARE(markers.at(1).color, QStringLiteral("Red"));
    QCOMPARE(markers.at(1).comment, QStringLiteral("ANIM FIX NEEDED"));
    QCOMPARE(markers.at(1).track, QStringLiteral("V1"));
    QCOMPARE(markers.at(2).comment, QStringLiteral("ANIM  FIX"));
    QCOMPARE(markers.at(2).color.toUpper(), QStringLiteral("PINK"));
    // Written back after their event.
    const auto out = edl::exportText(sequenceOf(r), roundTripOptions(r.items));
    QVERIFY2(out.text.contains(QStringLiteral("* LOC: 01:00:01:14 RED     ANIM FIX NEEDED")), qPrintable(out.text));
    QCOMPARE(model::markersOf(sequenceOf(edl::importText(out.text, at(Rational(24))))).size(), 3);
}

void TestEdl::guessedRate() {
    auto rateOf = [](const QString& text) { return sequenceOf(edl::importText(text)).editRate; };
    QCOMPARE(rateOf(QStringLiteral("001 A V C 00:00:00:00 00:00:00:23 01:00:00:00 01:00:00:23\n")), Rational(24000, 1001));
    QCOMPARE(rateOf(QStringLiteral("001 A V C 00:00:00:00 00:00:00:24 01:00:00:00 01:00:00:24\n")), Rational(25));
    QCOMPARE(rateOf(QStringLiteral("001 A V C 00:00:00:00 00:00:00:29 01:00:00:00 01:00:00:29\n")), Rational(30000, 1001));
    QCOMPARE(rateOf(QStringLiteral("FCM: DROP FRAME\n001 A V C 00:00:00:00 00:00:00:02 01:00:00:00 01:00:00:02\n")),
             Rational(30000, 1001));
    QVERIFY(!edl::importText(QStringLiteral("001 A V C 00:00:00:00 00:00:00:02 01:00:00:00 01:00:00:02\n")).warnings.isEmpty());
}

void TestEdl::malformed() {
    // Lines that are not events are warnings, never a crash; good lines around them are read.
    const QString text = QStringLiteral(
        "TITLE: broken\n"
        "this is not an EDL line\n"
        "001  A V C 01:00:00:00 01:00:01:00\n"
        "002  A V X 01:00:00:00 01:00:01:00 01:00:00:00 01:00:01:00\n"
        "003  A Q C 01:00:00:00 01:00:01:00 01:00:00:00 01:00:01:00\n"
        "004  A V C 01:00:00:00 01:00:01:00 01:00:00:00 01:00:99:00\n"
        "005  A V C 01:00:00:00 01:00:01:00 01:00:05:00 01:00:04:00\n"
        "M2 nothing\n"
        "006  A V C 01:00:00:00 01:00:01:00 01:00:00:00 01:00:01:00\n"
        "SPLIT:   AUDIO DELAY=  00:00:00:10\n");
    const auto r = edl::importText(text, at(Rational(24)));
    QVERIFY(r.warnings.size() >= 5);
    QVERIFY(!r.warnings.filter(QStringLiteral("line 2")).isEmpty());
    QVERIFY(!r.warnings.filter(QStringLiteral("line 3")).isEmpty());
    QVERIFY(!r.warnings.filter(QStringLiteral("channels Q")).isEmpty());
    QVERIFY(!r.warnings.filter(QStringLiteral("timecode 01:00:99:00")).isEmpty());
    QVERIFY(!r.warnings.filter(QStringLiteral("without length")).isEmpty());
    QVERIFY(!r.warnings.filter(QStringLiteral("SPLIT")).isEmpty());
    const model::Sequence s = model::Sequence::fromItem(sequenceOf(r));
    QCOMPARE(trackOf(s, QStringLiteral("V1")).segments.size(), 1);
    // An empty list is an empty sequence with the default tracks.
    const auto empty = edl::importText(QString());
    QCOMPARE(empty.items.size(), 1);
    QCOMPARE(model::Sequence::fromItem(sequenceOf(empty)).trackNames(),
             (QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}));
    // Random damage: lines cut, bytes changed. Never a crash.
    QRandomGenerator random(3600);
    const QByteArray good = text.toUtf8() + QByteArrayLiteral(
        "007  B AA/V D 030 01:00:00:00 01:00:01:00 01:00:01:00 01:00:02:00\n* LOC: 01:00:01:10 RED x\nAUD 3\n");
    for (int i = 0; i < 300; ++i) {
        QByteArray damaged = good;
        const int changes = 1 + random.bounded(8);
        for (int c = 0; c < changes; ++c) {
            damaged[random.bounded(static_cast<int>(damaged.size()))] = static_cast<char>(random.bounded(256));
        }
        if (random.bounded(2)) damaged.truncate(random.bounded(static_cast<int>(damaged.size())));
        const auto d = edl::importText(QString::fromLatin1(damaged));
        QVERIFY(d.sequence());
    }
}

void TestEdl::reelNames() {
    model::BinItem tape;
    tape.id = QStringLiteral("t");
    tape.kind = model::BinItemKind::MasterClip;
    tape.name = QStringLiteral("clip");
    tape.editRate = Rational(25);
    tape.durationFrames = 250;
    tape.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
    tape.fields.insert(QStringLiteral("Tape"), QStringLiteral("A001_C002 long name"));
    model::BinItem seq;
    seq.id = QStringLiteral("s");
    seq.kind = model::BinItemKind::Sequence;
    seq.name = QStringLiteral("Seq");
    seq.editRate = Rational(25);
    seq.fields.insert(QStringLiteral("Start"), QStringLiteral("01:00:00:00"));
    model::Sequence s;
    model::Segment g;
    g.clipId = tape.id;
    g.name = tape.name;
    g.sourceTrack = QStringLiteral("V1");
    g.sourceIn = 10;
    g.length = 25;
    s.ensureTrack(QStringLiteral("V1"));
    s.overwrite(QStringLiteral("V1"), 0, {g});
    s.storeIn(seq);
    edl::ExportOptions o;
    o.resolve = [&](const MobId&, const QString& id) -> std::optional<model::BinItem> {
        return id == tape.id ? std::optional<model::BinItem>(tape) : std::nullopt;
    };
    // CMX 3600: eight characters, capitals, no spaces; the end is cut (p.960).
    auto cmx = edl::exportText(seq, o);
    QVERIFY2(cmx.text.contains(QStringLiteral("001  A001_C00 V     C        01:00:00:10 01:00:01:10 01:00:00:00 01:00:01:00")),
             qPrintable(cmx.text));
    QVERIFY(!cmx.warnings.filter(QStringLiteral("cut short")).isEmpty());
    QVERIFY(cmx.text.startsWith(QStringLiteral("TITLE:   SEQ\r\nFCM: NON-DROP FRAME\r\n")));
    QVERIFY(cmx.text.contains(QStringLiteral("* FROM CLIP NAME:  CLIP")));
    // File_129: the whole name.
    o.format = edl::Format::File129;
    o.upperCase = false;
    const auto file129 = edl::exportText(seq, o);
    QVERIFY2(file129.text.contains(QStringLiteral("001  A001_C002_long_name V     C        01:00:00:10")), qPrintable(file129.text));
    QVERIFY(file129.warnings.isEmpty());
    // And read back with the reel whole.
    const auto back = edl::importText(file129.text, at(Rational(25)));
    QCOMPARE(back.items.first().name, QStringLiteral("A001_C002_long_name"));
    QCOMPARE(edl::formatFromName(QStringLiteral("File_129")), edl::Format::File129);
    QCOMPARE(edl::formatName(edl::Format::Cmx3600), QStringLiteral("CMX_3600"));
    // A clip that cannot be found: its name is the reel.
    o.resolve = nullptr;
    const auto lost = edl::exportText(seq, o);
    QVERIFY(lost.text.contains(QStringLiteral("001  clip ")));
    QVERIFY(!lost.warnings.filter(QStringLiteral("clips not found")).isEmpty());
    // Not a sequence.
    QVERIFY(!edl::exportText(tape).ok);
}

// A sequence of the import's own form (tape sources, transitions starting at their cut, a motion
// effect, gaps, a marker) written and read back is the same, by structure dump and markers.
void TestEdl::roundTrip() {
    const QString text = QStringLiteral(
        "TITLE:   Round Trip\n"
        "FCM: NON-DROP FRAME\n"
        "001  CAM_A    AA/V  C        10:00:00:00 10:00:04:00 01:00:00:00 01:00:04:00\n"
        "* FROM CLIP NAME:  Opening\n"
        "* LOC: 01:00:01:00 BLUE    check focus\n"
        "002  CAM_A    V     C        10:00:04:00 10:00:04:00 01:00:04:00 01:00:04:00\n"
        "002  CAM_B    V     D    012 20:00:00:00 20:00:03:00 01:00:04:00 01:00:07:00\n"
        "* FROM CLIP NAME:  Opening\n"
        "* TO CLIP NAME:  Reply\n"
        "003  CAM_B    AA    C        20:00:00:00 20:00:03:00 01:00:04:00 01:00:07:00\n"
        "* FROM CLIP NAME:  Reply\n"
        "004  CAM_C    V     C        05:00:00:00 05:00:01:00 01:00:08:00 01:00:10:00\n"
        "M2   CAM_C          012.0                05:00:00:00\n"
        "* FROM CLIP NAME:  Slow\n"
        "005  MUSIC    NONE  C        00:10:00:00 00:10:10:00 01:00:00:00 01:00:10:00\n"
        "AUD  3    4\n"
        "* FROM CLIP NAME:  Score\n");
    const auto r = edl::importText(text, at(Rational(24)));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    for (const bool optimize : {false, true}) {
        edl::ExportOptions o = roundTripOptions(r.items);
        o.optimize = optimize;
        const auto out = edl::exportText(sequenceOf(r), o);
        QVERIFY(out.ok);
        QVERIFY2(out.warnings.isEmpty(), qPrintable(out.warnings.join(QLatin1Char('\n'))));
        const auto again = edl::importText(out.text, at(Rational(24)));
        QVERIFY2(again.warnings.isEmpty(), qPrintable(again.warnings.join(QLatin1Char('\n'))));
        QCOMPARE(structure(again.items), structure(r.items));
        const QList<model::Marker> a = model::markersOf(sequenceOf(r));
        const QList<model::Marker> b = model::markersOf(sequenceOf(again));
        QCOMPARE(b.size(), a.size());
        QCOMPARE(b.first().frame, a.first().frame);
        QCOMPARE(b.first().color, a.first().color);
        QCOMPARE(b.first().comment, a.first().comment);
        if (optimize) {
            // Optimized, video and both audio channels of the first edit are one event.
            QVERIFY2(out.text.contains(QStringLiteral("001  CAM_A    AA/V  C")), qPrintable(out.text));
        } else {
            QVERIFY2(out.text.contains(QStringLiteral("001  CAM_A    V     C")), qPrintable(out.text));
        }
        // And through a file.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("list.edl"));
        QVERIFY(edl::exportFile(sequenceOf(r), path, o).ok);
        QString error;
        const auto read = edl::importFile(path, at(Rational(24)), &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(structure(read->items), structure(r.items));
    }
    // Starting Event Number (p.960).
    edl::ExportOptions o = roundTripOptions(r.items);
    o.startEvent = 101;
    QVERIFY(edl::exportText(sequenceOf(r), o).text.contains(QStringLiteral("\n101  CAM_A")));
    // Only the tracks listed (Input tab, p.948).
    o.tracks = {QStringLiteral("A3")};
    const auto a3 = edl::exportText(sequenceOf(r), o);
    QCOMPARE(a3.events, 1);
    QVERIFY(a3.text.contains(QStringLiteral("AUD  3")));
}

// A transition centred on its cut is written starting where it starts, and read back as one
// starting at the cut that far back: the same frames on the screen.
void TestEdl::centredTransition() {
    const auto r = edl::importText(QStringLiteral("001 A V C 00:00:10:00 00:00:12:00 01:00:00:00 01:00:02:00\n"
                                                  "002 B V C 00:00:20:00 00:00:22:00 01:00:02:00 01:00:04:00\n"),
                                   at(Rational(24)));
    model::BinItem item = sequenceOf(r);
    model::Sequence s = model::Sequence::fromItem(item);
    model::Transition t;
    t.effect.category = QStringLiteral("Blend");
    t.effect.name = QStringLiteral("Dissolve");
    t.duration = 10;
    t.alignment = model::TransitionAlignment::CenteredOnCut;
    s.track(QStringLiteral("V1"))->segments[1].transition = t;
    s.storeIn(item);
    const auto out = edl::exportText(item, roundTripOptions(r.items));
    QVERIFY2(out.text.contains(QStringLiteral("002  A        V     C        00:00:11:19 00:00:11:19 01:00:01:19 01:00:01:19")),
             qPrintable(out.text));
    QVERIFY2(out.text.contains(QStringLiteral("002  B        V     D    010 00:00:19:19 00:00:22:00 01:00:01:19 01:00:04:00")),
             qPrintable(out.text));
    const model::Sequence back = model::Sequence::fromItem(sequenceOf(edl::importText(out.text, at(Rational(24)))));
    const model::Track& v1 = trackOf(back, QStringLiteral("V1"));
    QCOMPARE(v1.segments.at(0).length, 43);
    QCOMPARE(v1.segments.at(1).sourceIn, 19 * 24 + 19);
    QCOMPARE(v1.segments.at(1).transition->alignment, model::TransitionAlignment::StartingAtCut);
    // Without Dissolves, a cut at the cut.
    edl::ExportOptions o = roundTripOptions(r.items);
    o.pictureDissolves = false;
    QVERIFY(!edl::exportText(item, o).text.contains(QStringLiteral(" D ")));
}

void TestEdl::optimize() {
    // Pieces of different tracks that are not the same edit stay apart, also when optimized.
    const auto r = edl::importText(QStringLiteral("001 A V C 00:00:10:00 00:00:12:00 01:00:00:00 01:00:02:00\n"
                                                  "002 A A C 00:00:10:01 00:00:12:01 01:00:00:00 01:00:02:00\n"
                                                  "003 A A2 C 00:00:10:00 00:00:12:00 01:00:00:00 01:00:02:00\n"),
                                   at(Rational(24)));
    edl::ExportOptions o = roundTripOptions(r.items);
    o.optimize = true;
    const auto out = edl::exportText(sequenceOf(r), o);
    QCOMPARE(out.events, 2);
    QVERIFY2(out.text.contains(QStringLiteral("001  A        A2/V  C")), qPrintable(out.text));
}

void TestEdl::masterClips() {
    // A sequence of master clips: reels from the Tape column or the tape source, timecodes from the
    // clips' Start; a clip without either is its own reel.
    model::BinItem a;
    a.id = QStringLiteral("a");
    a.kind = model::BinItemKind::MasterClip;
    a.name = QStringLiteral("Interview");
    a.editRate = Rational(25);
    a.durationFrames = 1000;
    a.fields.insert(QStringLiteral("Start"), QStringLiteral("14:00:00:00"));
    model::ClipSource tape;
    tape.kind = model::ClipSource::Kind::Tape;
    tape.name = QStringLiteral("DAY2");
    a.sources << tape;
    model::BinItem b = a;
    b.id = QStringLiteral("b");
    b.name = QStringLiteral("B-roll");
    b.sources.clear();
    b.fields.insert(QStringLiteral("Start"), QStringLiteral("00:00:00:00"));
    model::BinItem seq;
    seq.id = QStringLiteral("s");
    seq.kind = model::BinItemKind::Sequence;
    seq.name = QStringLiteral("Cut");
    seq.editRate = Rational(25);
    seq.fields.insert(QStringLiteral("Start"), QStringLiteral("10:00:00:00"));
    model::Sequence s;
    s.ensureTrack(QStringLiteral("V1"));
    model::Segment sa;
    sa.clipId = a.id;
    sa.name = a.name;
    sa.sourceTrack = QStringLiteral("V1");
    sa.sourceIn = 50;
    sa.length = 25;
    model::Segment sb = sa;
    sb.clipId = b.id;
    sb.name = b.name;
    sb.sourceIn = 0;
    s.overwrite(QStringLiteral("V1"), 0, {sa, model::Segment::filler(25), sb});
    s.storeIn(seq);
    edl::ExportOptions o;
    o.resolve = [&](const MobId&, const QString& id) -> std::optional<model::BinItem> {
        if (id == a.id) return a;
        if (id == b.id) return b;
        return std::nullopt;
    };
    o.blackEdits = true;
    const auto out = edl::exportText(seq, o);
    QVERIFY2(out.warnings.isEmpty(), qPrintable(out.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(out.events, 3);
    QVERIFY2(out.text.contains(QStringLiteral("001  DAY2     V     C        14:00:02:00 14:00:03:00 10:00:00:00 10:00:01:00")),
             qPrintable(out.text));
    // Black Edits (p.958): the gap is a BL event.
    QVERIFY2(out.text.contains(QStringLiteral("002  BL       V     C        00:00:00:00 00:00:01:00 10:00:01:00 10:00:02:00")),
             qPrintable(out.text));
    QVERIFY2(out.text.contains(QStringLiteral("003  B-ROLL   V     C        00:00:00:00 00:00:01:00 10:00:02:00 10:00:03:00")),
             qPrintable(out.text));
    QVERIFY(out.text.endsWith(QStringLiteral("\r\n")));
}

void TestEdl::reuseSources() {
    const QString text = QStringLiteral("001 A V C 00:00:10:00 00:00:12:00 01:00:00:00 01:00:02:00\n"
                                        "002 B V C 00:00:10:00 00:00:12:00 01:00:02:00 01:00:04:00\n");
    const auto first = edl::importText(text, at(Rational(24)));
    auto second = edl::importText(text, at(Rational(24)));
    // The same reels are the same MobIDs every time.
    QCOMPARE(model::mobIdOf(second.items.at(0)), model::mobIdOf(first.items.at(0)));
    edl::reuseSources(second, {first.items.at(0)});
    QCOMPARE(second.items.size(), 2); // B and the sequence
    const model::Sequence s = model::Sequence::fromItem(sequenceOf(second));
    QCOMPARE(trackOf(s, QStringLiteral("V1")).segments.at(0).clipId, first.items.at(0).id);
    QCOMPARE(trackOf(s, QStringLiteral("V1")).segments.at(1).clipId, second.items.at(0).id);
}

// "all titles used in the Timeline can be exported as part of an Edit Decision List (EDL). Each Titler+ effect is
// assigned a dedicated event timecode and a unique name (based on the Clip Text label)" (FX Guide p.327): titles on
// V2, over the picture of V1, are events of their own with their names.
void TestEdl::titles() {
    model::BinItem a;
    a.id = QStringLiteral("a");
    a.kind = model::BinItemKind::MasterClip;
    a.name = QStringLiteral("Interview");
    a.editRate = Rational(25);
    a.durationFrames = 1000;
    a.fields.insert(QStringLiteral("Start"), QStringLiteral("14:00:00:00"));
    model::BinItem seq;
    seq.id = QStringLiteral("s");
    seq.kind = model::BinItemKind::Sequence;
    seq.name = QStringLiteral("Cut");
    seq.editRate = Rational(25);
    seq.fields.insert(QStringLiteral("Start"), QStringLiteral("10:00:00:00"));
    model::Sequence s;
    s.ensureTrack(QStringLiteral("V1"));
    s.ensureTrack(QStringLiteral("V2"));
    model::Segment sa;
    sa.clipId = a.id;
    sa.name = a.name;
    sa.sourceTrack = QStringLiteral("V1");
    sa.length = 100;
    s.overwrite(QStringLiteral("V1"), 0, {sa});
    auto title = [](const QString& text, std::int64_t length) {
        model::Effect e = model::titlerEffect();
        model::Title t;
        t.objects << model::textObject(t, text, QPointF());
        model::setTitle(e, t);
        model::Segment f = model::Segment::filler(length);
        f.effect = e;
        return f;
    };
    s.overwrite(QStringLiteral("V2"), 0, {model::Segment::filler(25), title(QStringLiteral("Jane Doe"), 25), model::Segment::filler(10), title(QStringLiteral("Jane Doe"), 15)});
    s.storeIn(seq);
    edl::ExportOptions o;
    o.resolve = [&](const MobId&, const QString& id) -> std::optional<model::BinItem> {
        if (id == a.id) return a;
        return std::nullopt;
    };
    o.clipNames = false;
    const auto out = edl::exportText(seq, o);
    QCOMPARE(out.events, 3);
    QVERIFY2(out.text.contains(QStringLiteral("002  BL       V     C        00:00:00:00 00:00:01:00 10:00:01:00 10:00:02:00\r\n"
                                              "* FROM CLIP NAME:  JANE DOE")),
             qPrintable(out.text));
    QVERIFY2(out.text.contains(QStringLiteral("003  BL       V     C        00:00:00:00 00:00:00:15 10:00:02:10 10:00:03:00\r\n"
                                              "* FROM CLIP NAME:  JANE DOE 2")),
             qPrintable(out.text));
    // The picture's event keeps no clip name (Clip Names off); no note about a left-out effect.
    QVERIFY(!out.text.contains(QStringLiteral("INTERVIEW")));
    QVERIFY2(out.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("V2 left out")), qPrintable(out.warnings.join(QLatin1Char(' '))));
}

// OpenTimelineIO's sample lists: each is read, and what Cutthulhu writes from it reads back the same.
void TestEdl::corpus() {
    const QString folder = corpusFolder();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QDir dir(folder + QStringLiteral("/otio-cmx3600"));
    const QStringList files = dir.entryList({QStringLiteral("*.edl")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) QSKIP("the corpus has no EDLs");
    for (const QString& name : files) {
        QString error;
        const auto r = edl::importFile(dir.filePath(name), at(Rational(24000, 1001)), &error);
        QVERIFY2(r, qPrintable(error));
        QVERIFY2(r->sequence(), qPrintable(name));
        QVERIFY2(r->sequence()->durationFrames > 0, qPrintable(name));
        const auto out = edl::exportText(*r->sequence(), roundTripOptions(r->items));
        QVERIFY2(out.ok, qPrintable(name));
        const auto again = edl::importText(out.text, at(Rational(24000, 1001)));
        QJsonObject a = structure(r->items);
        QJsonObject b = structure(again.items);
        QVERIFY2(a == b, qPrintable(name));
    }
    // What the lists hold, against OpenTimelineIO's own tests of them.
    const auto speed = edl::importFile(dir.filePath(QStringLiteral("speed_effects.edl")), at(Rational(24000, 1001)));
    QVERIFY(speed);
    int motion = 0;
    for (const model::Segment& s : trackOf(model::Sequence::fromItem(*speed->sequence()), QStringLiteral("V1")).segments) {
        if (s.effect) ++motion;
    }
    QVERIFY(motion > 0);
    const auto multi = edl::importFile(dir.filePath(QStringLiteral("multi_audio.edl")), at(Rational(24000, 1001)));
    QVERIFY(multi);
    QCOMPARE(model::Sequence::fromItem(*multi->sequence()).trackNames(),
             (QStringList{QStringLiteral("A1"), QStringLiteral("A2"), QStringLiteral("A3")}));
}

QTEST_GUILESS_MAIN(TestEdl)
#include "tst_edl.moc"

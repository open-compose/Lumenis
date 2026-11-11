// Film cut lists (9.4a; Editing Guide p.939-944, p.960-966; docs/spec/cut-lists.md): key and ink numbers, the assemble
// list's footage, edge numbers and flags, the optical list's transitions, effects and key frames (film masks left out),
// the dupe list, the pull list's order, short and jump cuts, the scene assemble list's requirement, and TabbedLists.
#include "lumenis/interop/cut_list.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QHash>
#include <QTest>

using namespace lumenis;
using namespace lumenis::interop;
using namespace lumenis::model;

namespace {

BinItem clip(const QString& id, const QHash<QString, QString>& fields) {
    BinItem c;
    c.id = id;
    c.name = id;
    c.editRate = Rational(24);
    c.durationFrames = 2000;
    c.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1"));
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) c.fields.insert(it.key(), it.value());
    return c;
}

Segment piece(const QString& clipId, std::int64_t in, std::int64_t length) {
    Segment s;
    s.clipId = clipId;
    s.binId = QStringLiteral("bin");
    s.name = clipId;
    s.sourceTrack = QStringLiteral("V1");
    s.sourceIn = in;
    s.length = length;
    return s;
}

struct Fixture {
    QHash<QString, BinItem> clips;
    BinItem sequence;
    cutlist::Options options() const {
        cutlist::Options o;
        o.title = QStringLiteral("REEL 1");
        o.resolve = [this](const MobId&, const QString& id) -> std::optional<BinItem> {
            if (!clips.contains(id)) return std::nullopt;
            return clips.value(id);
        };
        return o;
    }
};

// V1: A (key numbers), B (ink numbers) after a dissolve, A again overlapping the first A's frames (a dupe), C (timecode
// only) with a Flop, and D with a standard film mask.
Fixture fixture() {
    Fixture f;
    f.clips.insert(QStringLiteral("A"), clip(QStringLiteral("A"), {{QStringLiteral("KN Start"), QStringLiteral("KZ 12 3456 1000+00")},
                                                                    {QStringLiteral("Camroll"), QStringLiteral("C1")}}));
    f.clips.insert(QStringLiteral("B"), clip(QStringLiteral("B"), {{QStringLiteral("Ink Number"), QStringLiteral("023 0500+08")},
                                                                    {QStringLiteral("Camroll"), QStringLiteral("C2")}}));
    f.clips.insert(QStringLiteral("C"), clip(QStringLiteral("C"), {{QStringLiteral("Start"), QStringLiteral("01:00:00:00")}}));
    f.clips.insert(QStringLiteral("D"), clip(QStringLiteral("D"), {{QStringLiteral("KN Start"), QStringLiteral("KZ 99 0001 0000+00")}}));
    Sequence seq;
    seq.setTimecodeTrack(false);
    Segment b = piece(QStringLiteral("B"), 0, 20);
    Transition t;
    t.effect = Effect{QStringLiteral("Blend"), QStringLiteral("Dissolve"), {}};
    t.duration = 10;
    b.transition = t;
    Segment c = piece(QStringLiteral("C"), 24, 5);
    c.effect = Effect{QStringLiteral("Image"), QStringLiteral("Flop"), {}};
    c.note = QStringLiteral("check focus");
    Segment d = piece(QStringLiteral("D"), 0, 16);
    d.effect = Effect{QStringLiteral("Film"), QStringLiteral("1.85 Mask"), {}};
    seq.ensureTrack(QStringLiteral("V1"))->segments = {piece(QStringLiteral("A"), 16, 32), b, piece(QStringLiteral("A"), 40, 16), c, d};
    f.sequence.id = QStringLiteral("seq");
    f.sequence.name = QStringLiteral("Reel 1");
    f.sequence.kind = BinItemKind::Sequence;
    f.sequence.editRate = Rational(24);
    seq.storeIn(f.sequence);
    return f;
}

// The rows of a list in a Columnar text: from its name to the next blank line, without the head and rule.
QStringList rowsOf(const QString& text, const QString& list) {
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int at = static_cast<int>(lines.indexOf(list));
    if (at < 0) return {};
    QStringList out;
    for (int i = at + 3; i < lines.size() && !lines[i].isEmpty(); ++i) out << lines[i];
    return out;
}

} // namespace

class TestCutList : public QObject {
    Q_OBJECT

private slots:
    void filmNumbers() {
        const auto kn = cutlist::parseFilmNumber(QStringLiteral("KZ 12 3456 1234+05"), 16);
        QVERIFY(kn);
        QCOMPARE(kn->prefix, QStringLiteral("KZ 12 3456"));
        QCOMPARE(kn->frames, std::int64_t(1234 * 16 + 5));
        QCOMPARE(cutlist::filmNumberText(*kn, 11, 16), QStringLiteral("KZ 12 3456 1235+00"));
        QVERIFY(!cutlist::parseFilmNumber(QStringLiteral("KZ 12 3456 1234+16"), 16));
        QVERIFY(!cutlist::parseFilmNumber(QStringLiteral("no numbers"), 16));
        // 16mm: 40 frames a foot.
        QCOMPARE(cutlist::parseFilmNumber(QStringLiteral("EK 1 12+39"), 40)->frames, std::int64_t(12 * 40 + 39));
        QCOMPARE(cutlist::footage(41, 40), QStringLiteral("1+01"));
        QCOMPARE(cutlist::footage(-17, 16), QStringLiteral("-1+01"));
        // Frame counts ("FXS32v01-000001", p.966) when frames are counted.
        const auto count = cutlist::parseFilmNumber(QStringLiteral("FXS32v01-000001"), 0);
        QVERIFY(count);
        QCOMPARE(count->prefix, QStringLiteral("FXS32v01"));
        QCOMPARE(cutlist::filmNumberText(*count, 9, 0), QStringLiteral("FXS32v01 000010"));
        QCOMPARE(cutlist::framesPerFootOf(QStringLiteral("16mm")), 40);
        QCOMPARE(cutlist::framesPerFootOf(QStringLiteral("35mm, 4 perf")), 16);
        QCOMPARE(cutlist::framesPerFootOf(QStringLiteral("35mm, 3 perf")), 0);
        QCOMPARE(cutlist::formatFromName(QStringLiteral("TabbedLists")), std::optional(cutlist::Format::TabbedLists));
        QVERIFY(!cutlist::formatFromName(QStringLiteral("XML")));
    }

    void assembleAndOptical() {
        const Fixture f = fixture();
        cutlist::Options o = f.options();
        o.shortCuts = 8;
        const cutlist::Result r = cutlist::exportText(f.sequence, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.text.startsWith(QStringLiteral("REEL 1\n")));
        const QStringList assemble = rowsOf(r.text, QStringLiteral("ASSEMBLE LIST"));
        QCOMPARE(assemble.size(), 5);
        // A from its frame 16: key number 1000+00 + 16 frames = 1001+00, for 2 feet.
        QVERIFY2(assemble[0].contains(QStringLiteral("0+00")) && assemble[0].contains(QStringLiteral("1+15")) &&
                     assemble[0].contains(QStringLiteral("KZ 12 3456 1001+00")) && assemble[0].contains(QStringLiteral("KZ 12 3456 1002+15")) &&
                     assemble[0].contains(QStringLiteral("C1")),
                 qPrintable(assemble[0]));
        // B by its ink numbers, an optical (the dissolve).
        QVERIFY2(assemble[1].contains(QStringLiteral("023 500+08")) && assemble[1].contains(QStringLiteral("Optical 1")), qPrintable(assemble[1]));
        // The second A repeats frames 40-47 of the first: a dupe.
        QVERIFY2(assemble[2].contains(QStringLiteral("Dupe 1")), qPrintable(assemble[2]));
        QVERIFY2(assemble[0].contains(QStringLiteral("Dupe 1")), qPrintable(assemble[0]));
        // C by timecode, short (5 frames < 8), an optical, with its Clip Note.
        QVERIFY2(assemble[3].contains(QStringLiteral("01:00:01:00")) && assemble[3].contains(QStringLiteral("Short cut")) &&
                     assemble[3].contains(QStringLiteral("Optical 2")) && assemble[3].contains(QStringLiteral("check focus")),
                 qPrintable(assemble[3]));
        // The film mask is no optical.
        QVERIFY2(!assemble[4].contains(QStringLiteral("Optical")), qPrintable(assemble[4]));

        const QStringList optical = rowsOf(r.text, QStringLiteral("OPTICAL LIST"));
        QCOMPARE(optical.size(), 3);
        // The dissolve, centred on the cut at 2+00: from 1+11 for 10 frames; A's tail from 1002+11, B's head before 500+08.
        QVERIFY2(optical[0].startsWith(QStringLiteral("1")) && optical[0].contains(QStringLiteral("Dissolve")) &&
                     optical[0].contains(QStringLiteral("1+11")) && optical[0].contains(QStringLiteral("KZ 12 3456 1002+11")),
                 qPrintable(optical[0]));
        QVERIFY2(optical[1].contains(QStringLiteral("023 500+03")), qPrintable(optical[1]));
        QVERIFY2(optical[2].startsWith(QStringLiteral("2")) && optical[2].contains(QStringLiteral("Flop")), qPrintable(optical[2]));
        QVERIFY(!r.text.contains(QStringLiteral("1.85 Mask")));

        const QStringList dupe = rowsOf(r.text, QStringLiteral("DUPE LIST"));
        QCOMPARE(dupe.size(), 2);
        QVERIFY2(dupe[0].contains(QStringLiteral("KZ 12 3456 1002+08")) && dupe[1].contains(QStringLiteral("KZ 12 3456 1002+08")),
                 qPrintable(dupe.join(QLatin1Char('\n'))));
        // No pull list unless chosen.
        QVERIFY(!r.text.contains(QStringLiteral("PULL LIST")));
    }

    void keyFrames() {
        Fixture f = fixture();
        Sequence seq = Sequence::fromItem(f.sequence);
        Segment& c = seq.track(QStringLiteral("V1"))->segments[3];
        const EffectSpan span(c.length);
        setKeyframe(*c.effect, QStringLiteral("Position"), QStringLiteral("X"), 0.0, span, 0.0);
        setKeyframe(*c.effect, QStringLiteral("Position"), QStringLiteral("X"), 1.0, span, 100.0);
        seq.storeIn(f.sequence);
        cutlist::Options o = f.options();
        QVERIFY(cutlist::exportText(f.sequence, o).text.contains(QStringLiteral("Key frames Position/X")));
        o.keyFrames = false;
        QVERIFY(!cutlist::exportText(f.sequence, o).text.contains(QStringLiteral("Key frames")));
    }

    void pullAndJumpCuts() {
        Fixture f = fixture();
        cutlist::Options o = f.options();
        o.pull = true;
        o.assemble = false;
        o.optical = false;
        o.dupe = false;
        cutlist::Result r = cutlist::exportText(f.sequence, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QStringList pull = rowsOf(r.text, QStringLiteral("PULL LIST"));
        QCOMPARE(pull.size(), 5);
        // By Camroll: the two clips without one, then C1's two A events in edge order, then C2's B.
        QVERIFY2(pull[2].contains(QStringLiteral("KZ 12 3456 1001+00")) && pull[3].contains(QStringLiteral("KZ 12 3456 1002+08")) &&
                     pull[4].contains(QStringLiteral("C2")),
                 qPrintable(pull.join(QLatin1Char('\n'))));
        // Tails Out: descending edge numbers within a roll.
        o.headsOut = false;
        pull = rowsOf(cutlist::exportText(f.sequence, o).text, QStringLiteral("PULL LIST"));
        QVERIFY2(pull[2].contains(QStringLiteral("KZ 12 3456 1002+08")), qPrintable(pull.join(QLatin1Char('\n'))));

        // A jump cut: A, then A again with three frames missing.
        Sequence seq;
        seq.setTimecodeTrack(false);
        seq.ensureTrack(QStringLiteral("V1"))->segments = {piece(QStringLiteral("A"), 0, 20), piece(QStringLiteral("A"), 23, 20)};
        seq.storeIn(f.sequence);
        o = f.options();
        o.jumpCuts = 8;
        const QStringList assemble = rowsOf(cutlist::exportText(f.sequence, o).text, QStringLiteral("ASSEMBLE LIST"));
        QVERIFY2(assemble[1].contains(QStringLiteral("Jump cut (0+03 missing)")), qPrintable(assemble[1]));
    }

    void sceneAssembleAndTabs() {
        Fixture f = fixture();
        cutlist::Options o = f.options();
        o.sceneAssemble = true;
        // "The Scene/Take column must be filled in within the bin to generate a scene assemble list" (p.942).
        cutlist::Result r = cutlist::exportText(f.sequence, o);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QLatin1String("Scene")));
        int take = 1;
        for (auto it = f.clips.begin(); it != f.clips.end(); ++it) {
            it->fields.insert(QStringLiteral("Scene"), it.key() == QLatin1String("A") ? QStringLiteral("2") : QStringLiteral("1"));
            it->fields.insert(QStringLiteral("Take"), QString::number(take++));
        }
        r = cutlist::exportText(f.sequence, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        const QStringList scene = rowsOf(r.text, QStringLiteral("SCENE ASSEMBLE LIST"));
        QCOMPARE(scene.size(), 5);
        // Scene 1 first; the A events (scene 2) last, in assembly order.
        QVERIFY2(scene[3].contains(QStringLiteral(" 2/")) && scene[4].contains(QStringLiteral(" 2/")), qPrintable(scene.join(QLatin1Char('\n'))));
        // TabbedLists: the same rows, tab-separated.
        o.format = cutlist::Format::TabbedLists;
        r = cutlist::exportText(f.sequence, o);
        QVERIFY(r.text.contains(QStringLiteral("#\tTrack\tFootage In\tFootage Out")));
        QVERIFY(r.text.contains(QStringLiteral("1\tV1\t0+00\t1+15\t2+00\tKZ 12 3456 1001+00")));
        // Nothing chosen: a message.
        o.assemble = o.sceneAssemble = o.optical = o.dupe = o.pull = false;
        QVERIFY(!cutlist::exportText(f.sequence, o).ok);
    }
};

QTEST_MAIN(TestCutList)
#include "tst_cut_list.moc"

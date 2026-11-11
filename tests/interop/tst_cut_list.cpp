// Film cut lists (9.4a; Editing Guide p.939-944, p.960-966; docs/spec/cut-lists.md): key and ink numbers, the assemble
// list's footage, edge numbers and flags, the optical list's transitions, effects and key frames (film masks left out),
// the dupe list, the pull list's order, short and jump cuts, the scene assemble list's requirement, and TabbedLists.
#include "lumenis/interop/cut_list.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QFile>
#include <QHash>
#include <QTemporaryDir>
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
        QCOMPARE(cutlist::formatFromName(QStringLiteral("XML")), std::optional(cutlist::Format::Xml));
        QVERIFY(!cutlist::formatFromName(QStringLiteral("CMX_3600")));
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

    // XML (p.957): every column heading, and the clips' columns; Mark Short Cuts "Not included in XML output" (p.961).
    void xmlTemplate() {
        const Fixture f = fixture();
        cutlist::Options o = f.options();
        o.format = cutlist::Format::Xml;
        o.shortCuts = 8;
        const cutlist::Result r = cutlist::exportText(f.sequence, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.text.startsWith(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<CutList title=\"REEL 1\"")));
        QVERIFY(r.text.contains(QStringLiteral("<List type=\"assemble\" name=\"ASSEMBLE LIST\">")));
        QVERIFY(r.text.contains(QStringLiteral("<Cell heading=\"First Edge\">KZ 12 3456 1001+00</Cell>")));
        QVERIFY(r.text.contains(QStringLiteral("<Column heading=\"Camroll\">C1</Column>")));
        QVERIFY(r.text.contains(QStringLiteral("<List type=\"optical\"")));
        QVERIFY(!r.text.contains(QStringLiteral("Short cut")));
        QVERIFY(r.files.isEmpty());
    }

    // WebLists (p.957-958): a page per list, linked between the lists, and the Files folder.
    void webLists() {
        const Fixture f = fixture();
        cutlist::Options o = f.options();
        o.format = cutlist::Format::WebLists;
        o.pull = true;
        const cutlist::Result r = cutlist::exportText(f.sequence, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.files.keys(), (QStringList{QStringLiteral("CutAsm.htm"), QStringLiteral("CutDupe.htm"), QStringLiteral("CutOpt.htm"),
                                              QStringLiteral("CutPull.htm"), QStringLiteral("Files/cutlist.css")}));
        const QString assemble = QString::fromUtf8(r.files.value(QStringLiteral("CutAsm.htm")));
        QCOMPARE(r.text, assemble);
        // "Opticals in the Assemble List that are linked to their description in the Optical List"; dupes likewise.
        QVERIFY(assemble.contains(QStringLiteral("<a href=\"CutOpt.htm#o1\">Optical 1</a>")));
        QVERIFY(assemble.contains(QStringLiteral("<a href=\"CutDupe.htm#d1\">Dupe 1</a>")));
        QVERIFY(assemble.contains(QStringLiteral("<tr id=\"e2\">")));
        QVERIFY(assemble.contains(QStringLiteral("href=\"Files/cutlist.css\"")));
        // "Items in the Optical List that are linked to the appropriate event in the Assemble List"; the pull list too.
        const QString optical = QString::fromUtf8(r.files.value(QStringLiteral("CutOpt.htm")));
        QVERIFY(optical.contains(QStringLiteral("<tr id=\"o1\">")));
        QVERIFY(optical.contains(QStringLiteral("<a href=\"CutAsm.htm#e2\">1</a>")));
        QVERIFY(QString::fromUtf8(r.files.value(QStringLiteral("CutPull.htm"))).contains(QStringLiteral("href=\"CutAsm.htm#e")));
        QVERIFY(QString::fromUtf8(r.files.value(QStringLiteral("CutDupe.htm"))).contains(QStringLiteral("<tr id=\"d1\">")));
        // Written into a folder.
        QTemporaryDir dir;
        QVERIFY(cutlist::writeFiles(r, dir.path()));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("Files/cutlist.css"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("CutAsm.htm"))));
    }

    // Change lists (9.4c; p.940, p.953-954, p.962-963).
    void changeList() {
        Fixture f = fixture();
        for (const char* id : {"E"}) {
            f.clips.insert(QLatin1String(id), clip(QLatin1String(id), {{QStringLiteral("KN Start"), QStringLiteral("KZ 77 0001 0200+00")},
                                                                        {QStringLiteral("Camroll"), QStringLiteral("C3")}}));
        }
        auto sequenceOf = [](const QString& name, const QList<Segment>& segments, const QString& reel = {}) {
            Sequence seq;
            seq.setTimecodeTrack(false);
            seq.ensureTrack(QStringLiteral("V1"))->segments = segments;
            BinItem item;
            item.id = name;
            item.name = name;
            item.kind = BinItemKind::Sequence;
            item.editRate = Rational(24);
            if (!reel.isEmpty()) item.fields.insert(QStringLiteral("Reel #"), reel);
            seq.storeIn(item);
            return item;
        };
        const BinItem before = sequenceOf(QStringLiteral("Cut 1"), {piece(QStringLiteral("A"), 0, 40), piece(QStringLiteral("B"), 0, 30),
                                                                     piece(QStringLiteral("C"), 0, 20), piece(QStringLiteral("D"), 0, 10)});
        // A trimmed at both ends, E inserted, D deleted.
        const BinItem after = sequenceOf(QStringLiteral("Cut 2"), {piece(QStringLiteral("A"), 8, 40), piece(QStringLiteral("B"), 0, 30),
                                                                    piece(QStringLiteral("E"), 0, 12), piece(QStringLiteral("C"), 0, 20)});
        cutlist::Options o = f.options();
        cutlist::ChangeOptions c;
        cutlist::Result r = cutlist::exportChangeList({before}, {after}, o, c);
        QVERIFY2(r.ok, qPrintable(r.error));
        QStringList changes = rowsOf(r.text, QStringLiteral("CHANGE LIST"));
        QCOMPARE(changes.size(), 4);
        QVERIFY2(changes[0].contains(QStringLiteral("Delete Head")) && changes[0].contains(QStringLiteral("0+08")) &&
                     changes[0].contains(QStringLiteral("KZ 12 3456 1000+00")),
                 qPrintable(changes.join(QLatin1Char('\n'))));
        // The tail: the new A runs 8 frames past the old one's end (source 40-47), at record frame 32 = 2+00.
        QVERIFY2(changes[1].contains(QStringLiteral("Insert Tail")) && changes[1].contains(QStringLiteral("2+00")) &&
                     changes[1].contains(QStringLiteral("KZ 12 3456 1002+08")),
                 qPrintable(changes[1]));
        QVERIFY2(changes[2].contains(QStringLiteral("Insert")) && changes[2].contains(QStringLiteral("KZ 77 0001 200+00")), qPrintable(changes[2]));
        // D, after C's new end at 102 frames = 6+06.
        QVERIFY2(changes[3].contains(QStringLiteral("Delete")) && changes[3].contains(QStringLiteral("6+06")) && changes[3].contains(QStringLiteral("D")),
                 qPrintable(changes[3]));
        // The change pull list holds what goes in, the discard list what comes out.
        QCOMPARE(rowsOf(r.text, QStringLiteral("CHANGE PULL LIST")).size(), 2);
        const QStringList discard = rowsOf(r.text, QStringLiteral("CHANGE DISCARD LIST"));
        QCOMPARE(discard.size(), 2);
        QVERIFY(discard[0].contains(QStringLiteral("Delete Head")) || discard[1].contains(QStringLiteral("Delete Head")));
        // Without Show only changes, the unchanged shots too.
        c.showOnlyChanges = false;
        changes = rowsOf(cutlist::exportChangeList({before}, {after}, o, c).text, QStringLiteral("CHANGE LIST"));
        QCOMPARE(changes.filter(QStringLiteral("No change")).size(), 2);

        // A move: C goes first.
        const BinItem moved = sequenceOf(QStringLiteral("Cut 3"), {piece(QStringLiteral("C"), 0, 20), piece(QStringLiteral("A"), 0, 40),
                                                                    piece(QStringLiteral("B"), 0, 30), piece(QStringLiteral("D"), 0, 10)});
        c = {};
        changes = rowsOf(cutlist::exportChangeList({before}, {moved}, o, c).text, QStringLiteral("CHANGE LIST"));
        QCOMPARE(changes.size(), 1);
        QVERIFY2(changes[0].contains(QStringLiteral("Move")) && changes[0].contains(QStringLiteral("C")), qPrintable(changes[0]));
        // Combine Deletions: B, C and D as one event.
        const BinItem cut = sequenceOf(QStringLiteral("Cut 4"), {piece(QStringLiteral("A"), 0, 40)});
        QCOMPARE(rowsOf(cutlist::exportChangeList({before}, {cut}, o, c).text, QStringLiteral("CHANGE LIST")).size(), 3);
        c.combineDeletions = true;
        changes = rowsOf(cutlist::exportChangeList({before}, {cut}, o, c).text, QStringLiteral("CHANGE LIST"));
        QCOMPARE(changes.size(), 1);
        QVERIFY2(changes[0].contains(QStringLiteral("B, C, D")) && changes[0].contains(QStringLiteral("3+12")), qPrintable(changes[0]));
        // Preview Code numbers the changes.
        c.previewCode = true;
        QVERIFY(cutlist::exportChangeList({before}, {cut}, o, c).text.contains(QStringLiteral("Code")));
    }

    void changeListReels() {
        const Fixture f = fixture();
        auto sequenceOf = [](const QString& name, const QString& clipId, std::int64_t length, const QString& reel) {
            Sequence seq;
            seq.setTimecodeTrack(false);
            seq.ensureTrack(QStringLiteral("V1"))->segments = {piece(clipId, 0, length)};
            BinItem item;
            item.id = name;
            item.name = name;
            item.kind = BinItemKind::Sequence;
            item.editRate = Rational(24);
            item.fields.insert(QStringLiteral("Reel #"), reel);
            seq.storeIn(item);
            return item;
        };
        const cutlist::Options o = f.options();
        // "For each sequence, the old version and the new version must have exactly the same reel number" (p.953): paired
        // by it, not by order.
        const QList<BinItem> olds{sequenceOf(QStringLiteral("R1 old"), QStringLiteral("A"), 20, QStringLiteral("1")),
                                  sequenceOf(QStringLiteral("R2 old"), QStringLiteral("B"), 20, QStringLiteral("2"))};
        const QList<BinItem> news{sequenceOf(QStringLiteral("R2 new"), QStringLiteral("B"), 24, QStringLiteral("2")),
                                  sequenceOf(QStringLiteral("R1 new"), QStringLiteral("A"), 20, QStringLiteral("1"))};
        cutlist::Result r = cutlist::exportChangeList(olds, news, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QStringList reel2 = rowsOf(r.text, QStringLiteral("CHANGE LIST (REEL 2)"));
        QCOMPARE(reel2.size(), 1);
        QVERIFY(reel2[0].contains(QStringLiteral("Insert Tail")));
        QVERIFY(rowsOf(r.text, QStringLiteral("CHANGE LIST (REEL 1)")).isEmpty());
        // "You must have an equal number of old and new sequences."
        QVERIFY(!cutlist::exportChangeList(olds, {news.first()}, o).ok);
        QVERIFY(!cutlist::exportChangeList({olds.first()}, {news.first()}, o).ok);
        // WebLists: the changes link to the new and old assemble lists.
        cutlist::Options web = o;
        web.format = cutlist::Format::WebLists;
        const BinItem one = sequenceOf(QStringLiteral("Old"), QStringLiteral("A"), 20, QStringLiteral("1"));
        const BinItem two = sequenceOf(QStringLiteral("New"), QStringLiteral("C"), 5, QStringLiteral("1"));
        r = cutlist::exportChangeList({one}, {two}, web);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.files.contains(QStringLiteral("CutChg.htm")));
        QVERIFY(r.files.contains(QStringLiteral("CutAsmOld.htm")));
        const QString page = QString::fromUtf8(r.files.value(QStringLiteral("CutChg.htm")));
        QVERIFY2(page.contains(QStringLiteral("href=\"CutAsm.htm#e1\"")) && page.contains(QStringLiteral("href=\"CutAsmOld.htm#e1\"")), qPrintable(page));
    }
};

QTEST_MAIN(TestCutList)
#include "tst_cut_list.moc"

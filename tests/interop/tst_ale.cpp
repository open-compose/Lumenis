#include "corpus.h"
#include "lumenis/interop/ale.h"
#include "lumenis/model/shot_log.h"
#include "lumenis/model/structure.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

// Avid Log Exchange shot logs (src/interop/ale.*, docs/spec/ale.md): the guide's sample log
// (Editing Guide p.112), the specification's edge cases (p.106-114), round trips bin -> ALE -> bin,
// and OpenTimelineIO's sample logs when the interchange corpus is at hand.

using namespace lumenis;
using namespace lumenis::interop;

namespace {

QString corpusFolder() { return lumenis::test::interopCorpus(); }

// The bin's structure dump without MobIDs and sources (a log's clips are new clips each time).
QJsonObject structure(const QList<model::BinItem>& items, const QStringList& custom) {
    model::Bin bin;
    bin.items = items;
    bin.display.customColumns = custom;
    QJsonObject o = model::binStructure(bin);
    QJsonArray out;
    for (const QJsonValue v : o.value(QStringLiteral("items")).toArray()) {
        QJsonObject item = v.toObject();
        item.remove(QStringLiteral("mobId"));
        item.remove(QStringLiteral("sources"));
        out.append(item);
    }
    o.insert(QStringLiteral("items"), out);
    return o;
}

model::BinItem clip(const QString& name, const QString& start, std::int64_t frames, const QString& tracks) {
    model::BinItem c;
    c.id = model::newItemId();
    c.kind = model::BinItemKind::MasterClip;
    c.name = name;
    c.editRate = Rational(24000, 1001);
    c.durationFrames = frames;
    c.fields.insert(QStringLiteral("Start"), start);
    c.fields.insert(QStringLiteral("Tracks"), tracks);
    return c;
}

} // namespace

class TestAle : public QObject {
    Q_OBJECT

private slots:
    void guideSample();
    void columns();
    void durations();
    void leniency();
    void rates();
    void roundTrip();
    void exportChoices();
    void malformed();
    void shotLogFiles();
    void shotLogEvents();
    void corpus();
};

// The sample Avid log on p.112, as its picture shows it.
void TestAle::guideSample() {
    const QString text = QStringLiteral(
        "Heading\r\nFIELD_DELIM\tTABS\r\nVIDEO_FORMAT\tNTSC\r\nAUDIO_FORMAT\t44kHz\r\nTAPE\t001\r\nFPS\t29.97\r\n\r\n"
        "Column\r\nName\tTracks\tStart\tEnd\r\n\r\nData\r\n"
        "CU Josh & Mary\tV\t01:00:00:00\t01:15:05:00\r\n"
        "CU Josh\tVA1\t01:15:06:00\t01:20:00:00\r\n");
    const auto r = ale::importText(text);
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(r.items.size(), 2);
    QCOMPARE(r.heading.size(), 5);
    QCOMPARE(r.heading.at(1), (QPair<QString, QString>(QStringLiteral("VIDEO_FORMAT"), QStringLiteral("NTSC"))));
    const model::BinItem& a = r.items.at(0);
    QCOMPARE(a.kind, model::BinItemKind::MasterClip);
    QCOMPARE(a.name, QStringLiteral("CU Josh & Mary"));
    QCOMPARE(a.editRate, Rational(30000, 1001));
    QCOMPARE(a.fields.value(QStringLiteral("Tracks")), QStringLiteral("V1"));
    QCOMPARE(a.fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
    QCOMPARE(a.fields.value(QStringLiteral("End")), QStringLiteral("01:15:05:00"));
    QCOMPARE(a.durationFrames, (15 * 60 + 5) * 30);
    // The global TAPE (p.107) is each clip's tape, and its source.
    QCOMPARE(a.fields.value(QStringLiteral("Tape")), QStringLiteral("001"));
    QCOMPARE(a.sources.size(), 1);
    QCOMPARE(a.sources.first().kind, model::ClipSource::Kind::Tape);
    QCOMPARE(a.sources.first().name, QStringLiteral("001"));
    QCOMPARE(a.sources.first().startTimecode, QStringLiteral("01:00:00:00"));
    QVERIFY(!a.mobId.isNull());
    QCOMPARE(r.items.at(1).fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1"));
    QVERIFY(r.customColumns.isEmpty());
}

void TestAle::columns() {
    const QString text = QStringLiteral(
        "Heading\nFIELD_DELIM\tTABS\nVIDEO_FORMAT\t1080\nFPS\t25\n\n"
        "Column\nName\tTracks\tStart\tEnd\tDuration\tTape\tScene\tTake\tCOMMENTS\tDESCRIPT\tCircled\tCreation Date\tColor\n\nData\n"
        "S1 T1\tVA1A2\t10:00:00:00\t10:00:10:00\t00:00:10:00\tA001\t1\t1\tnice\tWide\tyes\t1/1/24 10:00:00 AM\t\n"
        "Wild\tA1A2\t11:00:00:00\t\t00:00:02:00\t\t2\t\t\t\t\t\t\n");
    ale::ImportOptions o;
    o.tape = QStringLiteral("LOGFILE");
    const auto r = ale::importText(text, o);
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(r.items.size(), 2);
    const model::BinItem& a = r.items.at(0);
    QCOMPARE(a.fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));
    QCOMPARE(a.fields.value(QStringLiteral("Tape")), QStringLiteral("A001"));
    QCOMPARE(a.fields.value(QStringLiteral("Scene")), QStringLiteral("1"));
    // Standard headings are matched without regard to case; the rest are custom columns.
    QCOMPARE(a.fields.value(QStringLiteral("Comments")), QStringLiteral("nice"));
    QCOMPARE(a.fields.value(QStringLiteral("DESCRIPT")), QStringLiteral("Wide"));
    QCOMPARE(r.customColumns, (QStringList{QStringLiteral("DESCRIPT"), QStringLiteral("Circled")}));
    QVERIFY(!a.fields.contains(QStringLiteral("Creation Date")));
    QVERIFY(!a.fields.contains(QStringLiteral("Duration")));
    // No video: an audio clip; no Tape and no global TAPE: the log's name (p.107).
    const model::BinItem& b = r.items.at(1);
    QCOMPARE(b.kind, model::BinItemKind::AudioClip);
    QCOMPARE(b.durationFrames, 50);
    QCOMPARE(b.fields.value(QStringLiteral("End")), QStringLiteral("11:00:02:00"));
    QCOMPARE(b.fields.value(QStringLiteral("Tape")), QStringLiteral("LOGFILE"));
    QVERIFY(!b.fields.contains(QStringLiteral("Take")));
}

void TestAle::durations() {
    const QString head = QStringLiteral("Heading\nFPS\t29.97\n\nColumn\nName\tTracks\tStart\tEnd\tDuration\n\nData\n");
    // Drop frame: semicolons (p.110).
    auto r = ale::importText(head + QStringLiteral("df\tV\t01;00;00;00\t01;01;00;02\t00;01;00;02\n"));
    QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(r.items.first().durationFrames, 1800);
    QCOMPARE(r.items.first().fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00;00"));
    QVERIFY(model::isDropFrame(r.items.first()));
    // Start, End and Duration that disagree: the clip is left out (p.114).
    r = ale::importText(head + QStringLiteral("bad\tV\t01:00:00:00\t01:00:10:00\t00:00:09:00\ngood\tV\t01:00:00:00\t01:00:10:00\t\n"));
    QCOMPARE(r.items.size(), 1);
    QCOMPARE(r.items.first().name, QStringLiteral("good"));
    QVERIFY(!r.warnings.filter(QStringLiteral("disagree")).isEmpty());
    // Past midnight.
    r = ale::importText(head + QStringLiteral("night\tV\t23:59:59:00\t00:00:01:00\t\n"));
    QCOMPARE(r.items.first().durationFrames, 60);
    // Without Start, or without End and Duration: left out.
    r = ale::importText(head + QStringLiteral("x\tV\t\t01:00:00:00\t\ny\tV\t01:00:00:00\t\t\n"));
    QVERIFY(r.items.isEmpty());
    QCOMPARE(r.warnings.size(), 2);
}

void TestAle::leniency() {
    // Lines ending in CR (classic Mac OS), the heading on one line, spaces instead of a tab in the
    // heading, fewer cells than titles, a Name-less row.
    const QString text = QStringLiteral(
        "Heading\rFIELD_DELIM\tTABS\tVIDEO_FORMAT\t1080\tFPS\t23.976\rTAPE R1\r\rColumn\rName\tTracks\tStart\tEnd\tScene\r\rData\r"
        "one\tV\t01:00:00:00\t01:00:01:00\r"
        "\tV\t01:00:00:00\t01:00:01:00\tx\r"
        "two\tV\t01:00:00:00\t01:00:01:00\t\t\t\t\r");
    const auto r = ale::importText(text);
    QCOMPARE(r.items.size(), 2);
    QCOMPARE(r.items.first().editRate, Rational(24000, 1001));
    QCOMPARE(r.items.first().fields.value(QStringLiteral("Tape")), QStringLiteral("R1"));
    QCOMPARE(r.items.first().durationFrames, 24);
    QCOMPARE(r.warnings, QStringList{QStringLiteral("lines without a Name left out")});
}

void TestAle::rates() {
    QCOMPARE(ale::rateFromFps(QStringLiteral("23.98")), Rational(24000, 1001));
    QCOMPARE(ale::rateFromFps(QStringLiteral("23.976")), Rational(24000, 1001));
    QCOMPARE(ale::rateFromFps(QStringLiteral("29.97")), Rational(30000, 1001));
    QCOMPARE(ale::rateFromFps(QStringLiteral("59.94")), Rational(60000, 1001));
    QCOMPARE(ale::rateFromFps(QStringLiteral("24.00")), Rational(24));
    QCOMPARE(ale::rateFromFps(QStringLiteral("25")), Rational(25));
    QVERIFY(!ale::rateFromFps(QStringLiteral("fast")));
    QVERIFY(!ale::rateFromFps(QStringLiteral("-25")));
    QCOMPARE(ale::fpsText(Rational(24000, 1001)), QStringLiteral("23.98"));
    QCOMPARE(ale::fpsText(Rational(30000, 1001)), QStringLiteral("29.97"));
    QCOMPARE(ale::fpsText(Rational(25)), QStringLiteral("25"));
}

// A bin's clips written as a log and read back are the same clips: names, rates, Start, End,
// durations, tracks, tapes, standard and custom columns.
void TestAle::roundTrip() {
    QList<model::BinItem> items;
    model::BinItem a = clip(QStringLiteral("A001_C001"), QStringLiteral("01:00:00:00"), 240, QStringLiteral("V1 A1-2"));
    a.fields.insert(QStringLiteral("Tape"), QStringLiteral("A001"));
    a.fields.insert(QStringLiteral("Scene"), QStringLiteral("12"));
    a.fields.insert(QStringLiteral("Circled"), QStringLiteral("yes"));
    items << a;
    model::BinItem b = clip(QStringLiteral("Room tone"), QStringLiteral("02:00:00:00"), 48, QStringLiteral("A1-4"));
    b.kind = model::BinItemKind::AudioClip;
    model::ClipSource tape;
    tape.kind = model::ClipSource::Kind::Tape;
    tape.name = QStringLiteral("SND1");
    b.sources << tape;
    b.fields.insert(QStringLiteral("Tape"), QStringLiteral("SND1"));
    items << b;
    model::BinItem df = clip(QStringLiteral("News"), QStringLiteral("00:59:59;28"), 100, QStringLiteral("V1 A1"));
    df.editRate = Rational(30000, 1001);
    df.fields.insert(QStringLiteral("Tape"), QStringLiteral("N1"));
    items << df;
    model::BinItem seq;
    seq.id = model::newItemId();
    seq.kind = model::BinItemKind::Sequence;
    seq.name = QStringLiteral("Not in a log");
    items << seq;
    const auto out = ale::exportText(items);
    QVERIFY(out.ok);
    QCOMPARE(out.clips, 3);
    QCOMPARE(out.warnings.size(), 1); // the sequence
    QVERIFY(out.text.startsWith(QStringLiteral("Heading\r\nFIELD_DELIM\tTABS\r\nVIDEO_FORMAT\t1080\r\nAUDIO_FORMAT\t48kHz\r\nFPS\t23.98\r\n\r\nColumn\r\n"
                                               "Name\tTracks\tStart\tEnd\tDuration\tTape\tFPS\t")));
    QVERIFY2(out.text.contains(QStringLiteral("A001_C001\tVA1A2\t01:00:00:00\t01:00:10:00\t00:00:10:00\tA001\t23.98\t")), qPrintable(out.text));
    QVERIFY2(out.text.contains(QStringLiteral("Room tone\tA1A2A3A4\t")), qPrintable(out.text));
    const auto back = ale::importText(out.text);
    QVERIFY2(back.warnings.isEmpty(), qPrintable(back.warnings.join(QLatin1Char('\n'))));
    // The import fills in End (Start + duration); the originals had none.
    QList<model::BinItem> clips = items.mid(0, 3);
    clips[0].fields.insert(QStringLiteral("End"), QStringLiteral("01:00:10:00"));
    clips[1].fields.insert(QStringLiteral("End"), QStringLiteral("02:00:02:00"));
    clips[2].fields.insert(QStringLiteral("End"), QStringLiteral("01:00:03;08"));
    QCOMPARE(structure(back.items, back.customColumns), structure(clips, {QStringLiteral("Circled")}));
    // And through a file.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("log.ale"));
    QVERIFY(ale::exportFile(items, path).ok);
    QString error;
    const auto read = ale::importFile(path, {}, &error);
    QVERIFY2(read, qPrintable(error));
    QCOMPARE(structure(read->items, read->customColumns), structure(clips, {QStringLiteral("Circled")}));
}

void TestAle::exportChoices() {
    model::BinItem master = clip(QStringLiteral("M"), QStringLiteral("01:00:00:00"), 24, QStringLiteral("V1"));
    master.fields.insert(QStringLiteral("Format"), QStringLiteral("720p/59.94"));
    master.fields.insert(QStringLiteral("Scene"), QStringLiteral("4"));
    master.fields.insert(QStringLiteral("Take"), QStringLiteral("2"));
    model::BinItem sub = clip(QStringLiteral("M.Sub.01"), QStringLiteral("01:00:00:10"), 5, QStringLiteral("V1"));
    sub.kind = model::BinItemKind::Subclip;
    // Tab Delimited (p.1174): the column titles, then a line per clip; no Heading, Column or Data sections.
    {
        ale::ExportOptions tab;
        tab.tabDelimited = true;
        tab.columns = {QStringLiteral("Scene"), QStringLiteral("Take")};
        const auto text = ale::exportText({master, sub}, tab);
        QVERIFY(text.ok);
        const QStringList lines = text.text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.first(), QStringLiteral("Name\tTracks\tStart\tEnd\tDuration\tScene\tTake"));
        QVERIFY(lines.at(1).startsWith(QStringLiteral("M\tV\t01:00:00:00\t01:00:01:00")));
        QVERIFY(lines.at(1).endsWith(QStringLiteral("\t4\t2")));
        QVERIFY(!text.text.contains(QStringLiteral("Heading")));
    }
    // Export Bin: master clips only (p.130).
    ale::ExportOptions o;
    o.subclips = false;
    auto out = ale::exportText({master, sub}, o);
    QCOMPARE(out.clips, 1);
    QVERIFY(out.text.contains(QStringLiteral("VIDEO_FORMAT\t720")));
    // Columns in the order given (a bin's Text view).
    o.subclips = true;
    o.columns = {QStringLiteral("Take"), QStringLiteral("Scene")};
    out = ale::exportText({master, sub}, o);
    QCOMPARE(out.clips, 2);
    QVERIFY2(out.text.contains(QStringLiteral("Name\tTracks\tStart\tEnd\tDuration\tTake\tScene\r\n")), qPrintable(out.text));
    // Nothing to write.
    QVERIFY(!ale::exportText({}).ok);
    // A tab in a value would start a new column: it is written as a space.
    master.fields.insert(QStringLiteral("Scene"), QStringLiteral("a\tb"));
    out = ale::exportText({master});
    QVERIFY(out.text.contains(QStringLiteral("a b")));
    QCOMPARE(out.warnings.size(), 1);
}

void TestAle::malformed() {
    const auto none = ale::importText(QStringLiteral("not a log at all\n"));
    QVERIFY(none.items.isEmpty());
    QVERIFY(!none.warnings.isEmpty());
    const QByteArray good = QByteArrayLiteral(
        "Heading\nFIELD_DELIM\tTABS\nFPS\t25\n\nColumn\nName\tTracks\tStart\tEnd\tCustom\n\nData\n"
        "a\tVA1\t01:00:00:00\t01:00:01:00\tx\nb\tV\t01:00:00:00\t01:00:02:00\ty\n");
    QRandomGenerator random(1999);
    for (int i = 0; i < 300; ++i) {
        QByteArray damaged = good;
        const int changes = 1 + random.bounded(8);
        for (int c = 0; c < changes; ++c) damaged[random.bounded(static_cast<int>(damaged.size()))] = static_cast<char>(random.bounded(256));
        if (random.bounded(2)) damaged.truncate(random.bounded(static_cast<int>(damaged.size())));
        const auto r = ale::importText(QString::fromLatin1(damaged));
        QVERIFY(r.items.size() <= 2);
    }
}

void TestAle::shotLogFiles() {
    QTemporaryDir dir;
    auto write = [&](const QString& name, const QByteArray& bytes) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
    };
    write(QStringLiteral("a.ALE"), "anything");
    write(QStringLiteral("log.txt"), "\r\nHeading\r\nFPS\t25\r\n");
    write(QStringLiteral("notes.txt"), "Shopping list\n");
    write(QStringLiteral("clip.mov"), "Heading\n");
    QVERIFY(ale::isShotLog(dir.filePath(QStringLiteral("a.ALE"))));
    QVERIFY(ale::isShotLog(dir.filePath(QStringLiteral("log.txt"))));
    QVERIFY(!ale::isShotLog(dir.filePath(QStringLiteral("notes.txt"))));
    QVERIFY(!ale::isShotLog(dir.filePath(QStringLiteral("clip.mov"))));
    // A log's name is the tape of clips without one (p.107).
    write(QStringLiteral("Reel 7.ale"), "Heading\nFPS\t25\n\nColumn\nName\tTracks\tStart\tEnd\n\nData\nc\tV\t01:00:00:00\t01:00:01:00\n");
    const auto r = ale::importFile(dir.filePath(QStringLiteral("Reel 7.ale")));
    QVERIFY(r);
    QCOMPARE(r->items.first().fields.value(QStringLiteral("Tape")), QStringLiteral("Reel 7"));
    QString error;
    QVERIFY(!ale::importFile(dir.filePath(QStringLiteral("missing.ale")), {}, &error));
    QVERIFY(!error.isEmpty());
}

// OpenTimelineIO's sample logs: each reads, and what Cutthulhu writes from it reads back the same.
// The Import setting's Shot Log tab (p.1197-1198): a log's events kept, combined by scene or camera roll into master
// clips with subclips, or merged with the selected clips.
void TestAle::shotLogEvents() {
    const QString log = QStringLiteral(
        "Heading\nFIELD_DELIM\tTABS\nVIDEO_FORMAT\t1080\nFPS\t25\n\n"
        "Column\nName\tTracks\tStart\tEnd\tTape\tScene\tCamroll\tComments\n\n"
        "Data\n"
        "12-1\tVA1A2\t01:00:00:00\t01:00:10:00\tT1\t12\tR1\tfirst\n"
        "13-1\tVA1A2\t01:00:20:00\t01:00:30:00\tT1\t13\tR1\t\n"
        "12-2\tVA1\t01:00:40:00\t01:00:50:00\tT1\t12\tR2\tbest\n"
        "x\tV\t01:01:00:00\t01:01:05:00\tT1\t\t\t\n");
    const QDateTime when = QDateTime::currentDateTimeUtc();
    const QList<model::BinItem> events = ale::importText(log).items;
    QCOMPARE(events.size(), 4);
    const model::Bin bin;

    // Maintain events as logged: the events themselves.
    auto r = model::shotLogEvents(model::ShotLogEvents::AsLogged, events, {}, bin, when);
    QCOMPARE(r.added.size(), 4);
    QCOMPARE(model::shotLogEventsOf(QStringLiteral("camroll")), model::ShotLogEvents::CameraRoll);
    QCOMPARE(model::shotLogEventsOf(QStringLiteral("nonsense")), model::ShotLogEvents::AsLogged);

    // By scene: scene 12's master clip over its two takes, with a subclip for each, then scene 13's; the event
    // without a scene kept as logged, with a warning.
    r = model::shotLogEvents(model::ShotLogEvents::Scene, events, {}, bin, when);
    QStringList names;
    for (const model::BinItem& i : r.added) names << i.name;
    QCOMPARE(names, (QStringList{"12", "12-1", "12-2", "13", "13-1", "x"}));
    const model::BinItem& scene12 = r.added[0];
    QCOMPARE(scene12.kind, model::BinItemKind::MasterClip);
    QCOMPARE(scene12.fields.value("Start"), QStringLiteral("01:00:00:00"));
    QCOMPARE(scene12.fields.value("End"), QStringLiteral("01:00:50:00"));
    QCOMPARE(scene12.durationFrames, 50 * 25);
    QCOMPARE(scene12.fields.value("Tracks"), QStringLiteral("V1 A1-2"));
    const model::BinItem& take2 = r.added[2];
    QCOMPARE(take2.kind, model::BinItemKind::Subclip);
    QCOMPARE(take2.fields.value("Start"), QStringLiteral("01:00:40:00"));
    QCOMPARE(take2.durationFrames, 10 * 25);
    QCOMPARE(take2.fields.value("Comments"), QStringLiteral("best"));
    QCOMPARE(take2.fields.value("Tracks"), QStringLiteral("V1 A1"));
    QCOMPARE(take2.sources.size(), 1);
    QCOMPARE(take2.sources[0].mob, model::mobIdOf(scene12));
    QCOMPARE(take2.sources[0].offset, 40 * 25);
    QVERIFY(r.warnings.join(' ').contains("without a Scene"));

    // By camera roll: R1's two events, R2's one.
    r = model::shotLogEvents(model::ShotLogEvents::CameraRoll, events, {}, bin, when);
    names.clear();
    for (const model::BinItem& i : r.added) names << i.name;
    QCOMPARE(names, (QStringList{"R1", "12-1", "13-1", "R2", "12-2", "x"}));
    QCOMPARE(r.added[0].fields.value("End"), QStringLiteral("01:00:30:00"));

    // Known sources: a selected clip of tape T1 from 01:00:00:00 to 01:00:35:00 takes the two events inside it as
    // subclips; the others are kept as logged. A clip of another tape takes nothing.
    model::BinItem source = clip(QStringLiteral("Roll"), QStringLiteral("01:00:00:00"), 35 * 25, QStringLiteral("V1 A1-2"));
    source.editRate = Rational(25);
    source.fields.insert("Tape", "T1");
    model::BinItem other = source;
    other.id = model::newItemId();
    other.fields.insert("Tape", "T2");
    r = model::shotLogEvents(model::ShotLogEvents::KnownSources, events, {other, source}, bin, when);
    QCOMPARE(r.added.size(), 4);
    QCOMPARE(r.added[0].kind, model::BinItemKind::Subclip);
    QCOMPARE(r.added[0].sources[0].mob, model::mobIdOf(source));
    QCOMPARE(r.added[1].sources[0].offset, 20 * 25);
    QCOMPARE(r.added[2].kind, model::BinItemKind::MasterClip);
    QVERIFY(r.updated.isEmpty());
    QVERIFY(r.warnings.join(' ').contains("outside the selected clips"));

    // Known master clips: the selected clip with the same tape, Start and End takes the log's columns, keeping its
    // name and id; a clip with another End takes nothing.
    model::BinItem master = clip(QStringLiteral("Take one"), QStringLiteral("01:00:00:00"), 10 * 25, QStringLiteral("V1 A1-2"));
    master.editRate = Rational(25);
    master.fields.insert("Tape", "T1");
    model::BinItem longer = master;
    longer.id = model::newItemId();
    longer.durationFrames += 1;
    r = model::shotLogEvents(model::ShotLogEvents::KnownMasterClips, events, {longer, master}, bin, when);
    QCOMPARE(r.updated.size(), 1);
    QCOMPARE(r.updated[0].id, master.id);
    QCOMPARE(r.updated[0].name, QStringLiteral("Take one"));
    QCOMPARE(r.updated[0].fields.value("Scene"), QStringLiteral("12"));
    QCOMPARE(r.updated[0].fields.value("Comments"), QStringLiteral("first"));
    QCOMPARE(r.added.size(), 3);
    QVERIFY(r.warnings.join(' ').contains("no selected master clip"));
}

void TestAle::corpus() {
    const QString folder = corpusFolder();
    if (folder.isEmpty()) QSKIP("no interchange corpus (scripts/interop-corpus.py fetch)");
    const QDir dir(folder + QStringLiteral("/otio-ale"));
    const QStringList files = dir.entryList({QStringLiteral("*.ale")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) QSKIP("the corpus has no logs");
    for (const QString& name : files) {
        const auto r = ale::importFile(dir.filePath(name));
        QVERIFY2(r, qPrintable(name));
        QVERIFY2(!r->items.isEmpty(), qPrintable(name));
        const auto out = ale::exportText(r->items);
        QVERIFY2(out.ok, qPrintable(name));
        const auto back = ale::importText(out.text, {QStringLiteral("x"), {}, {}});
        // Columns no clip has a value in are not written, so the clips are compared with the
        // custom columns that have values.
        QVERIFY2(structure(back.items, back.customColumns) == structure(r->items, back.customColumns), qPrintable(name));
        for (const QString& c : back.customColumns) QVERIFY2(r->customColumns.contains(c), qPrintable(c));
    }
    // sample.ale, as OpenTimelineIO's tests read it: four clips at 24 fps.
    const auto sample = ale::importFile(dir.filePath(QStringLiteral("sample.ale")));
    QCOMPARE(sample->items.size(), 4);
    QCOMPARE(sample->items.first().editRate, Rational(24));
    QCOMPARE(sample->items.first().durationFrames, 99);
}

QTEST_GUILESS_MAIN(TestAle)
#include "tst_ale.moc"

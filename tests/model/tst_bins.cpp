#include "lumenis/model/bin.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/structure.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace lumenis;
using namespace lumenis::model;

namespace {

const QDateTime kWhen(QDate(2026, 1, 15), QTime(10, 30), QTimeZone::utc());

QStringList names(const QList<BinNode>& nodes) {
    QStringList out;
    for (const BinNode& n : nodes) out << n.name;
    return out;
}

} // namespace

class TestBins : public QObject {
    Q_OBJECT

private slots:
    void binFilesRoundTrip() {
        QTemporaryDir dir;
        Bin bin;
        bin.name = QStringLiteral("Selects");
        BinItem a = newSequence(bin, Rational(25), kWhen);
        a.durationFrames = 1500;
        a.color = QStringLiteral("#20a040");
        a.locked = true;
        a.fields.insert(QStringLiteral("Comments"), QStringLiteral("opening"));
        bin.items << a;
        bin.display.mode = BinViewMode::Frame;
        bin.display.columns = {QStringLiteral("Duration"), QStringLiteral("Comments")};
        bin.display.widths.insert(QStringLiteral("Duration"), 80);
        bin.display.background = QStringLiteral("#303050");
        bin.display.frameSize = 3;
        bin.display.viewName = QStringLiteral("Capture");
        bin.display.viewModified = true;
        bin.display.framePositions.insert(a.id, QPoint(120, 40));
        bin.display.snap = SnapToGrid::Invisible;
        bin.display.sort = {{QStringLiteral("Duration"), false}, {QStringLiteral("Name"), true}};
        bin.display.sift.find = {{QStringLiteral("close"), QStringLiteral("Name")}};
        bin.display.sift.also = {{QStringLiteral("Red"), QStringLiteral("Color")}};
        bin.display.sifted = true;
        bin.items.first().script = QStringLiteral("INT. HOUSE - NIGHT");
        const QString path = dir.filePath(QStringLiteral("Selects.cthbin"));
        QString error;
        QVERIFY2(BinFile::write(path, bin, &error), qPrintable(error));

        const auto back = BinFile::read(path, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->name, bin.name);
        QCOMPARE(back->items.size(), 1);
        const BinItem& b = back->items.first();
        QCOMPARE(b.id, a.id);
        QCOMPARE(b.kind, BinItemKind::Sequence);
        QCOMPARE(b.name, QStringLiteral("Untitled Sequence.01"));
        QCOMPARE(b.created, kWhen);
        QCOMPARE(b.durationFrames, 1500);
        QCOMPARE(b.editRate, Rational(25));
        QCOMPARE(b.color, a.color);
        QVERIFY(b.locked);
        QCOMPARE(b.fields.value(QStringLiteral("Comments")), QStringLiteral("opening"));
        QCOMPARE(back->display.mode, BinViewMode::Frame);
        QCOMPARE(back->display.columns, bin.display.columns);
        QCOMPARE(back->display.widths.value(QStringLiteral("Duration")), 80);
        QCOMPARE(back->display.background, bin.display.background);
        QCOMPARE(back->display.frameSize, 3);
        QCOMPARE(back->display.viewName, QStringLiteral("Capture"));
        QVERIFY(back->display.viewModified);
        QCOMPARE(back->display.framePositions.value(a.id), QPoint(120, 40));
        QCOMPARE(back->display.snap, SnapToGrid::Invisible);
        QCOMPARE(back->display.sort, bin.display.sort);
        QCOMPARE(back->display.sift, bin.display.sift);
        QVERIFY(back->display.sifted);
        QVERIFY(back->isSiftShown());
        QCOMPARE(b.script, QStringLiteral("INT. HOUSE - NIGHT"));
    }

    void mobIds() {
        // Every clip and sequence has a MobID (docs/spec/native-model.md):
        // its own when it came from elsewhere, else one derived from its id.
        QTemporaryDir dir;
        Bin bin;
        bin.name = QStringLiteral("Dailies");
        BinItem made = newSequence(bin, Rational(25), kWhen);
        QVERIFY(made.mobId.isNull());
        const MobId derived = mobIdOf(made);
        QVERIFY(!derived.isNull());
        QCOMPARE(derivedMobId(made.id), derived);
        QVERIFY(derivedMobId(newItemId()) != derived);
        QVERIFY(QString::fromStdString(derived.toString()).startsWith(QStringLiteral("urn:smpte:umid:060a2b34.01010101.01010f10.13000000.")));
        // One from an AAF or a bin keeps its MobID.
        BinItem imported;
        imported.id = newItemId();
        imported.kind = BinItemKind::MasterClip;
        imported.name = QStringLiteral("A001_C002");
        imported.mobId = MobId::generate();
        // Its source chain: a file per track from a tape (docs/spec/native-model.md).
        ClipSource tape;
        tape.kind = ClipSource::Kind::Tape;
        tape.mob = MobId::generate();
        tape.name = QStringLiteral("A001");
        tape.startTimecode = QStringLiteral("01:00:00:00");
        for (const QString& track : {QStringLiteral("V1"), QStringLiteral("A1")}) {
            ClipSource file;
            file.kind = ClipSource::Kind::File;
            file.mob = MobId::generate();
            file.track = track;
            file.name = QStringLiteral("A001_C002.%1.mxf").arg(track);
            file.path = QStringLiteral("/Volumes/Media/Avid MediaFiles/MXF/1/") + file.name;
            file.from = tape.mob;
            imported.sources << file;
        }
        imported.sources << tape;
        ClipSource master;
        master.kind = ClipSource::Kind::Clip;
        master.mob = MobId::generate();
        master.name = QStringLiteral("A001_C001");
        master.offset = 48;
        master.startTimecode = QStringLiteral("01:00:02:00");
        imported.sources << master;
        // A sequence that uses it, and the clip without a MobID of its own.
        BinItem clip;
        clip.id = newItemId();
        clip.name = QStringLiteral("B001_C001");
        Sequence s;
        Segment a;
        a.clipId = imported.id;
        a.sourceMob = imported.mobId;
        a.length = 25;
        Segment b;
        b.clipId = clip.id;
        b.length = 50;
        s.ensureTrack(QStringLiteral("V1"))->segments = {a, b};
        s.storeIn(made);
        bin.items << made << imported << clip;
        // A duplicate is a new clip with a MobID of its own.
        const BinItem copy = duplicateItem(bin, imported, kWhen);
        QVERIFY(copy.mobId.isNull());
        QVERIFY(mobIdOf(copy) != mobIdOf(imported));
        const QString path = dir.filePath(QStringLiteral("Dailies.cthbin"));
        QString error;
        QVERIFY2(BinFile::write(path, bin, &error), qPrintable(error));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("\"version\": 2"));
        QVERIFY(text.contains(QByteArray::fromStdString(imported.mobId.toString())));
        QVERIFY(text.contains(QByteArray::fromStdString(derived.toString()))); // written out, though derived
        const auto back = BinFile::read(path, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->items.size(), 3);
        QVERIFY(back->items[0].mobId.isNull()); // derived, so kept as such
        QCOMPARE(mobIdOf(back->items[0]), derived);
        QCOMPARE(back->items[1].mobId, imported.mobId);
        QCOMPARE(back->items[1].sources, imported.sources);
        QVERIFY(back->items[2].sources.isEmpty());
        const Sequence again = Sequence::fromItem(back->items[0]);
        const Track* v1 = again.track(QStringLiteral("V1"));
        QVERIFY(v1);
        QCOMPARE(v1->segments.size(), 2);
        QCOMPARE(v1->segments[0].sourceMobId(), imported.mobId);
        QVERIFY(v1->segments[1].sourceMob.isNull());
        QCOMPARE(v1->segments[1].sourceMobId(), mobIdOf(clip));
        QCOMPARE(v1->segments, s.track(QStringLiteral("V1"))->segments);
        // A version 1 bin (no MobIDs) opens, its clips' MobIDs derived from
        // their ids as the segments' are from theirs.
        QString v1Text = QString::fromUtf8(text);
        v1Text.replace(QStringLiteral("\"version\": 2"), QStringLiteral("\"version\": 1"));
        v1Text.remove(QRegularExpression(QStringLiteral("\\s*\"mob(Id)?\": \"[^\"]*\",?")));
        QVERIFY(!v1Text.contains(QStringLiteral("mob")));
        const QString oldPath = dir.filePath(QStringLiteral("Old.cthbin"));
        QFile old(oldPath);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write(v1Text.toUtf8());
        old.close();
        const auto opened = BinFile::read(oldPath, &error);
        QVERIFY2(opened, qPrintable(error));
        QCOMPARE(mobIdOf(opened->items[2]), mobIdOf(clip));
        QCOMPARE(Sequence::fromItem(opened->items[0]).track(QStringLiteral("V1"))->segments[1].sourceMobId(), mobIdOf(clip));
    }

    void structureDump() {
        // What Phase 3 gate 1 compares (docs/spec/structure.md).
        QTemporaryDir dir;
        ProjectSettings ps;
        ps.name = QStringLiteral("Harbor");
        ps.format = projectFormatPresets().front();
        const auto entry = ProjectStore(dir.path()).create(ps);
        QVERIFY(entry);
        ProjectBins project(entry->folder, ps.name);
        QVERIFY(project.load());
        const QString binPath = project.binPath(project.bins().first()->id);
        auto bin = BinFile::read(binPath);
        QVERIFY(bin);
        bin->display.customColumns = {QStringLiteral("Circled")};
        BinItem clip;
        clip.id = newItemId();
        clip.kind = BinItemKind::MasterClip;
        clip.name = QStringLiteral("A001_C001");
        clip.editRate = Rational(25);
        clip.durationFrames = 250;
        clip.fields = {{QStringLiteral("Start"), QStringLiteral("01:00:00:00")},
                       {QStringLiteral("End"), QStringLiteral("01:00:10:00")},
                       {QStringLiteral("Tracks"), QStringLiteral("V1")},
                       {QStringLiteral("Tape"), QStringLiteral("A001")},
                       {QStringLiteral("Circled"), QStringLiteral("yes")},
                       {QStringLiteral("Lens"), QStringLiteral("35mm")}};
        BinItem cut = newSequence(*bin, Rational(25), kWhen);
        Sequence s;
        Segment used;
        used.clipId = clip.id;
        used.name = clip.name;
        used.sourceTrack = QStringLiteral("V1");
        used.sourceIn = 10;
        used.length = 50;
        Effect dissolve;
        dissolve.category = QStringLiteral("Blend");
        dissolve.name = QStringLiteral("Dissolve");
        Segment gap = Segment::filler(25);
        gap.transition = Transition{dissolve};
        s.ensureTrack(QStringLiteral("V1"))->segments = {used, gap};
        s.storeIn(cut);
        bin->items << clip << cut;
        QVERIFY(BinFile::write(binPath, *bin));

        QString error;
        const QJsonObject doc = structureOf(entry->folder, &error);
        QVERIFY2(!doc.isEmpty(), qPrintable(error));
        QCOMPARE(doc.value(QStringLiteral("format")).toString(), QStringLiteral("cutthulhu-structure"));
        QCOMPARE(doc.value(QStringLiteral("project")).toString(), QStringLiteral("Harbor"));
        const QJsonObject b = doc.value(QStringLiteral("bins")).toArray().first().toObject();
        QCOMPARE(b.value(QStringLiteral("name")).toString(), QStringLiteral("Harbor"));
        QCOMPARE(b.value(QStringLiteral("customColumns")).toArray(), QJsonArray{QStringLiteral("Circled")});
        const QJsonArray items = b.value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 2);
        const QJsonObject c = items.at(0).toObject();
        QCOMPARE(c.value(QStringLiteral("kind")).toString(), QStringLiteral("masterClip"));
        QCOMPARE(c.value(QStringLiteral("mobId")).toString(), QString::fromStdString(mobIdOf(clip).toString()));
        QCOMPARE(c.value(QStringLiteral("start")).toString(), QStringLiteral("01:00:00:00"));
        QCOMPARE(c.value(QStringLiteral("duration")).toInteger(), 250);
        // Custom columns (and headings that are not the guide's) apart from
        // the standard ones.
        QCOMPARE(c.value(QStringLiteral("columns")).toObject(),
                 (QJsonObject{{QStringLiteral("Circled"), QStringLiteral("yes")}, {QStringLiteral("Lens"), QStringLiteral("35mm")}}));
        QCOMPARE(c.value(QStringLiteral("fields")).toObject(), (QJsonObject{{QStringLiteral("Tape"), QStringLiteral("A001")}}));
        const QJsonArray tracks =
            items.at(1).toObject().value(QStringLiteral("sequence")).toObject().value(QStringLiteral("tracks")).toArray();
        QCOMPARE(tracks.size(), 1);
        const QJsonArray segments = tracks.first().toObject().value(QStringLiteral("segments")).toArray();
        QCOMPARE(segments.at(0).toObject(),
                 (QJsonObject{{QStringLiteral("source"), QString::fromStdString(mobIdOf(clip).toString())},
                              {QStringLiteral("name"), QStringLiteral("A001_C001")},
                              {QStringLiteral("track"), QStringLiteral("V1")},
                              {QStringLiteral("in"), 10},
                              {QStringLiteral("length"), 50}}));
        QCOMPARE(segments.at(1).toObject(),
                 (QJsonObject{{QStringLiteral("filler"), 25}, {QStringLiteral("transition"), QStringLiteral("Blend/Dissolve")}}));
        // The bin file alone gives the same bin.
        QJsonObject alone = structureOf(binPath, &error).value(QStringLiteral("bins")).toArray().first().toObject();
        alone.insert(QStringLiteral("name"), QStringLiteral("Harbor"));
        QCOMPARE(alone, b);
        QVERIFY(structureOf(dir.filePath(QStringLiteral("nothing")), &error).isEmpty());
    }

    void headingsComeFromTheGuide() {
        const QStringList& h = binHeadings();
        QCOMPARE(h.first(), QStringLiteral("Name"));
        for (const char* name : {"Auxiliary TC5", "Color", "Creation Date", "KN Mark OUT", "TC 25PD", "VITC"}) {
            QVERIFY2(h.contains(QLatin1String(name)), name);
        }
        QCOMPARE(QStringList(h).removeDuplicates(), 0);
        for (const QString& c : Bin::defaultColumns()) QVERIFY(h.contains(c));
        for (const BinViewPreset& v : builtInBinViews()) {
            for (const QString& c : v.columns) QVERIFY2(h.contains(c), qPrintable(c));
        }
        QVERIFY(isEditableHeading(QStringLiteral("Comments")));
        QVERIFY(!isEditableHeading(QStringLiteral("Duration")));
    }

    void sortsByValueNaturallyAndOnSeveralColumns() {
        Bin bin;
        auto add = [&](const QString& name, std::int64_t frames, const QString& scene) {
            BinItem i;
            i.id = newItemId();
            i.name = name;
            i.durationFrames = frames;
            i.editRate = Rational(25);
            i.fields.insert(QStringLiteral("Scene"), scene);
            bin.items << i;
        };
        add(QStringLiteral("Clip 10"), 50, QStringLiteral("2"));
        add(QStringLiteral("clip 2"), 500, QStringLiteral("1"));
        add(QStringLiteral("Clip 1"), 100, QStringLiteral("2"));
        auto order = [&] {
            QStringList out;
            for (const BinItem& i : bin.items) out << i.name;
            return out;
        };
        sortItems(bin, {{QStringLiteral("Name"), true}});
        QCOMPARE(order(), (QStringList{"Clip 1", "clip 2", "Clip 10"}));
        // By value, not by the text "0:02" < "0:20" < "0:04".
        sortItems(bin, {{QStringLiteral("Duration"), false}});
        QCOMPARE(order(), (QStringList{"clip 2", "Clip 1", "Clip 10"}));
        // The leftmost column has priority (p.247).
        sortItems(bin, {{QStringLiteral("Scene"), true}, {QStringLiteral("Duration"), true}});
        QCOMPARE(order(), (QStringList{"clip 2", "Clip 10", "Clip 1"}));
    }

    void sortsColoursByHue() {
        Bin bin;
        for (const char* hex : {"#3264c8", "", "#c83232", "#32a046"}) {
            BinItem i;
            i.id = newItemId();
            i.color = QLatin1String(hex);
            bin.items << i;
        }
        sortItems(bin, {{QStringLiteral("Color"), true}});
        QStringList names;
        for (const BinItem& i : bin.items) names << clipColorName(i.color);
        QCOMPARE(names, (QStringList{"", "Red", "Green", "Blue"}));
    }

    void readsTimecodesForRangeSifts() {
        // Colons and leading zeros may be left out (p.290).
        const Rational fps25(25);
        QCOMPARE(timecodeFrames(QStringLiteral("3172000"), fps25), std::optional<std::int64_t>(((3 * 60 + 17) * 60 + 20) * 25));
        QCOMPARE(timecodeFrames(QStringLiteral("03:17:20:00"), fps25), timecodeFrames(QStringLiteral("3172000"), fps25));
        QVERIFY(!timecodeFrames(QStringLiteral("abc"), fps25));
        QVERIFY(!timecodeFrames(QStringLiteral("00:00:00:30"), fps25)); // no frame 30 at 25 fps
        QVERIFY(!timecodeFrames({}, fps25));

        // An implicit range runs for the clip's Duration.
        BinItem item;
        item.editRate = fps25;
        item.durationFrames = 250;
        item.fields.insert(QStringLiteral("Sound TC"), QStringLiteral("10:00:00:00"));
        BinSift sift;
        sift.find = {{QStringLiteral("10000905"), QStringLiteral("Sound TC Range")}};
        QVERIFY(siftMatches(item, sift, {}));
        sift.find = {{QStringLiteral("10001000"), QStringLiteral("Sound TC Range")}};
        QVERIFY(!siftMatches(item, sift, {}));
        QCOMPARE(siftRangesFor({QStringLiteral("Start")}), QStringList());
        QCOMPARE(siftRangesFor({QStringLiteral("Sound TC"), QStringLiteral("Start"), QStringLiteral("End")}),
                 (QStringList{"Start to End Range", "Sound TC Range"}));
    }

    void siftsOnCriteria() {
        BinItem a;
        a.id = QStringLiteral("a");
        a.name = QStringLiteral("Interview close-up");
        a.fields.insert(QStringLiteral("Comments"), QStringLiteral("good take"));
        BinItem b;
        b.id = QStringLiteral("b");
        b.name = QStringLiteral("Wide shot");
        b.color = QStringLiteral("#c83232");
        const QStringList columns{QStringLiteral("Name"), QStringLiteral("Comments"), QStringLiteral("Color")};

        BinSift sift;
        QVERIFY(sift.isEmpty());
        sift.find = {{QStringLiteral("CLOSE"), {}}};
        QVERIFY(!sift.isEmpty());
        QVERIFY(siftMatches(a, sift, columns));
        QVERIFY(!siftMatches(b, sift, columns));
        // Every criterion of a group must match.
        sift.find << SiftRow{QStringLiteral("bad"), QStringLiteral("Comments")};
        QVERIFY(!siftMatches(a, sift, columns));
        // "Also find" adds items; a colour matches by its exact name (p.289).
        sift.find.removeLast();
        sift.also = {{QStringLiteral("Red"), QStringLiteral("Color")}};
        QVERIFY(siftMatches(b, sift, columns));
        sift.also = {{QStringLiteral("red"), QStringLiteral("Color")}};
        QVERIFY(!siftMatches(b, sift, columns));
        // Sift Selected Items.
        sift.ids = {QStringLiteral("b")};
        QVERIFY(!siftMatches(a, sift, columns));
        QVERIFY(siftMatches(b, sift, columns));
    }

    void rejectsOtherFiles() {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("x.cthbin")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"format":"something-else","version":1})");
        f.close();
        QString error;
        QVERIFY(!BinFile::read(f.fileName(), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!BinFile::read(dir.filePath(QStringLiteral("missing.cthbin"))));
    }

    void namesSequencesAndCopies() {
        Bin bin;
        bin.items << newSequence(bin, Rational(24), kWhen);
        bin.items << newSequence(bin, Rational(24), kWhen);
        QCOMPARE(bin.items[1].name, QStringLiteral("Untitled Sequence.02"));
        const BinItem copy = duplicateItem(bin, bin.items[1], kWhen);
        QCOMPARE(copy.name, QStringLiteral("Untitled Sequence.02.Copy.01"));
        QVERIFY(copy.id != bin.items[1].id);
        bin.items << copy;
        QCOMPARE(duplicateItem(bin, bin.items[1], kWhen).name, QStringLiteral("Untitled Sequence.02.Copy.02"));
    }

    void durationsDropLeadingZeroFields() {
        QCOMPARE(durationText(0, Rational(25)), QStringLiteral("0:00"));
        QCOMPARE(durationText(36, Rational(25)), QStringLiteral("1:11"));
        QCOMPARE(durationText((4 * 60 + 24) * 25 + 14, Rational(25)), QStringLiteral("4:24:14"));
        QCOMPARE(durationText(3600 * 24, Rational(24000, 1001)), QStringLiteral("1:00:00:00"));
    }

    void newProjectsGetABinNamedAfterThem() {
        QTemporaryDir dir;
        ProjectSettings s;
        s.name = QStringLiteral("Documentary");
        s.format = projectFormatPresets().front();
        const auto entry = ProjectStore(dir.path()).create(s);
        QVERIFY(entry);
        ProjectBins bins(entry->folder, s.name);
        QVERIFY(bins.load());
        QCOMPARE(names(bins.nodes()), (QStringList{"Documentary"}));
        QVERIFY(QFileInfo::exists(QDir(entry->folder).filePath(QStringLiteral("Documentary.cthbin"))));
        QVERIFY(QFileInfo::exists(QDir(entry->folder).filePath(QStringLiteral("Documentary.cthbins"))));
    }

    void createsRenamesAndMovesBinsAndFolders() {
        QTemporaryDir dir;
        ProjectBins bins(dir.path(), QStringLiteral("Doc"));
        QVERIFY(bins.load());
        const QString first = bins.createInitialBin();
        const QString second = bins.createBin();
        const QString third = bins.createBin();
        QCOMPARE(names(bins.nodes()), (QStringList{"Doc", "Doc.1", "Doc.2"}));
        QVERIFY(QFileInfo::exists(bins.binPath(third)));

        QString error;
        QVERIFY2(bins.rename(second, QStringLiteral("Interviews"), &error), qPrintable(error));
        QCOMPARE(bins.node(second)->name, QStringLiteral("Interviews"));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Interviews.cthbin"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Doc.1.cthbin"))));
        QCOMPARE(BinFile::read(bins.binPath(second))->name, QStringLiteral("Interviews"));
        // Only the case changes.
        QVERIFY2(bins.rename(second, QStringLiteral("INTERVIEWS"), &error), qPrintable(error));
        QCOMPARE(BinFile::read(bins.binPath(second))->name, QStringLiteral("INTERVIEWS"));
        QVERIFY(!bins.rename(third, QStringLiteral("doc"), &error)); // taken, ignoring case
        QVERIFY(!bins.rename(third, QStringLiteral("a/b"), &error)); // naming rules (p.295)
        QVERIFY(!bins.rename(third, QString(65, QLatin1Char('x')), &error));

        const QString folder = bins.createFolder();
        QCOMPARE(bins.node(folder)->name, QStringLiteral("Untitled Folder"));
        QVERIFY(bins.move(third, folder));
        QCOMPARE(bins.parentOf(third)->id, folder);
        const QString sub = bins.createFolder(folder);
        QVERIFY(!bins.move(folder, sub, -1, &error)); // not into itself
        QVERIFY(bins.move(first, sub));
        QCOMPARE(bins.bins().size(), 3);
        QVERIFY(bins.rename(folder, QStringLiteral("Selects")));

        // The tree survives saving and loading.
        QVERIFY(bins.save());
        ProjectBins again(dir.path(), QStringLiteral("Doc"));
        QVERIFY(again.load());
        QCOMPARE(names(again.nodes()), (QStringList{"INTERVIEWS", "Selects"}));
        QCOMPARE(again.parentOf(first)->id, sub);
    }

    void deletesToTheTrashAndEmptiesIt() {
        QTemporaryDir dir;
        ProjectBins bins(dir.path(), QStringLiteral("Doc"));
        bins.load();
        const QString a = bins.createInitialBin();
        const QString b = bins.createBin();
        const QString folder = bins.createFolder();
        bins.move(b, folder);
        bins.openBins = {a, b};
        QVERIFY(!bins.trash()); // only after the first delete (p.298)

        QVERIFY(bins.remove(folder));
        QVERIFY(bins.trash());
        QVERIFY(bins.isInTrash(b));
        QVERIFY(!bins.openBins.contains(b));
        QCOMPARE(bins.bins().size(), 1);
        QCOMPARE(bins.bins(true).size(), 2);
        QVERIFY(QFileInfo::exists(bins.binPath(b))); // still there
        // New bins go above the Trash.
        bins.createBin();
        QCOMPARE(bins.nodes().last().kind, BinNode::Kind::Trash);

        // Taken back out of the Trash.
        QVERIFY(bins.move(b, {}, 0));
        QVERIFY(!bins.isInTrash(b));
        QVERIFY(bins.remove(b));
        const QString path = bins.binPath(b);
        QVERIFY(bins.emptyTrash());
        QVERIFY(!bins.trash());
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!bins.node(b));
    }

    void opensBinsFromOtherProjects() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath(QStringLiteral("Doc"));
        QDir(dir.path()).mkpath(QStringLiteral("Other"));
        ProjectBins other(dir.filePath(QStringLiteral("Other")), QStringLiteral("Other"));
        other.load();
        const QString foreign = other.binPath(other.createInitialBin());

        ProjectBins bins(dir.filePath(QStringLiteral("Doc")), QStringLiteral("Doc"));
        bins.load();
        bins.createInitialBin();
        const QString id = bins.addOtherBin(foreign);
        QVERIFY(!id.isEmpty());
        QCOMPARE(bins.addOtherBin(foreign), id); // listed once
        QCOMPARE(bins.parentOf(id)->kind, BinNode::Kind::OtherBins);
        QCOMPARE(bins.parentOf(id)->name, QStringLiteral("Other Bins"));
        QCOMPARE(bins.binPath(id), QFileInfo(foreign).absoluteFilePath());

        // Deleting it only removes the pointer, and the folder goes with it.
        QVERIFY(bins.remove(id));
        QVERIFY(QFileInfo::exists(foreign));
        QVERIFY(!bins.trash());
        QCOMPARE(names(bins.nodes()), (QStringList{"Doc"}));
    }

    void adoptsUnlistedBinFilesAndDropsMissingOnes() {
        QTemporaryDir dir;
        ProjectBins bins(dir.path(), QStringLiteral("Doc"));
        bins.load();
        const QString a = bins.createInitialBin();
        const QString b = bins.createBin();
        bins.openBins = {a, b};
        QVERIFY(bins.save());
        QFile::remove(bins.binPath(b));
        Bin extra;
        extra.name = QStringLiteral("Copied In");
        QVERIFY(BinFile::write(dir.filePath(QStringLiteral("Copied In.cthbin")), extra));

        ProjectBins again(dir.path(), QStringLiteral("Doc"));
        QVERIFY(again.load());
        QCOMPARE(names(again.nodes()), (QStringList{"Doc", "Copied In"}));
        QCOMPARE(again.openBins, QStringList{a});
    }
};

QTEST_MAIN(TestBins)
#include "tst_bins.moc"

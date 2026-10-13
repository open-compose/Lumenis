#include "lumenis/model/bin.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/project_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

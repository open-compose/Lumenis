// The attic's copies and limits (model::saveToAttic; Editing Guide p.71, p.299, p.1153-1154;
// docs/spec/bin-attic.md).
#include "lumenis/model/bin_attic.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis::model;

namespace {

void writeBin(const QString& path, const QByteArray& text) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text);
}

QStringList names(const QStringList& paths) {
    QStringList out;
    for (const QString& p : paths) out << QFileInfo(p).fileName();
    return out;
}

void age(const QString& path, const QDateTime& when) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
}

} // namespace

class TestBinAttic : public QObject {
    Q_OBJECT

private slots:
    void copiesAreNumbered() {
        // "<bin>.<n>", the highest the newest, in <attic>/<project>/Bins/<bin> (p.71-72).
        QTemporaryDir dir;
        const QString bin = dir.filePath("Source Clips.cthbin");
        const QString attic = dir.filePath("Attic");
        writeBin(bin, "one");
        QVERIFY(saveToAttic(attic, "Film", "Source Clips", bin, {}));
        writeBin(bin, "two");
        QVERIFY(saveToAttic(attic, "Film", "Source Clips", bin, {}));
        const QStringList v = atticVersions(attic, "Film", "Source Clips");
        QCOMPARE(names(v), (QStringList{"Source Clips.1.cthbin", "Source Clips.2.cthbin"}));
        QCOMPARE(QFileInfo(v.first()).absolutePath(), QDir(attic).filePath("Film/Bins/Source Clips"));
        QFile f(v.last());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("two"));
        // No attic: nothing kept, no failure.
        QVERIFY(saveToAttic({}, "Film", "Source Clips", bin, {}));
    }

    void maxVersionsAndTheTwoHourRule() {
        QTemporaryDir dir;
        const QString bin = dir.filePath("B.cthbin");
        const QString attic = dir.filePath("Attic");
        writeBin(bin, "x");
        const AtticLimits limits{1000, 3, true};
        const QDateTime now = QDateTime::currentDateTime();
        for (int i = 0; i < 3; ++i) QVERIFY(saveToAttic(attic, "P", "B", bin, limits, now));
        // The second-oldest copy is recent: the newest makes way, the oldest stays.
        QVERIFY(saveToAttic(attic, "P", "B", bin, limits, now));
        QCOMPARE(names(atticVersions(attic, "P", "B")), (QStringList{"B.1.cthbin", "B.2.cthbin", "B.4.cthbin"}));
        // "The oldest backup file is overwritten only if the second-oldest backup file is more than 2 hours
        // old" (p.71).
        const QStringList v = atticVersions(attic, "P", "B");
        age(v.at(0), now.addSecs(-4 * 3600));
        age(v.at(1), now.addSecs(-3 * 3600));
        QVERIFY(saveToAttic(attic, "P", "B", bin, limits, now));
        QCOMPARE(names(atticVersions(attic, "P", "B")), (QStringList{"B.2.cthbin", "B.4.cthbin", "B.5.cthbin"}));
    }

    void maxFilesKeepsOneOfEachBin() {
        QTemporaryDir dir;
        const QString attic = dir.filePath("Attic");
        const QDateTime now = QDateTime::currentDateTime();
        for (const char* name : {"A", "B"}) writeBin(dir.filePath(QString::fromLatin1(name) + ".cthbin"), name);
        const AtticLimits loose{1000, 50, true};
        // A: one old copy; B: three newer ones.
        QVERIFY(saveToAttic(attic, "P", "A", dir.filePath("A.cthbin"), loose, now));
        age(atticVersions(attic, "P", "A").first(), now.addSecs(-3600));
        for (int i = 0; i < 3; ++i) QVERIFY(saveToAttic(attic, "P", "B", dir.filePath("B.cthbin"), loose, now));
        // A limit of 2 files: B's older copies go, never A's only one.
        QVERIFY(saveToAttic(attic, "P", "B", dir.filePath("B.cthbin"), {2, 50, true}, now));
        QCOMPARE(atticVersions(attic, "P", "A").size(), 1);
        QCOMPARE(names(atticVersions(attic, "P", "B")), QStringList{"B.4.cthbin"});
        // Without "Always keep at least one version", the oldest goes whatever its bin (p.1154).
        age(atticVersions(attic, "P", "A").first(), now.addSecs(-3600));
        QVERIFY(saveToAttic(attic, "P", "B", dir.filePath("B.cthbin"), {1, 50, false}, now));
        QCOMPARE(atticVersions(attic, "P", "A").size(), 0);
        QCOMPARE(names(atticVersions(attic, "P", "B")), QStringList{"B.5.cthbin"});
    }

    void zeroLimitsEmptyTheProject() {
        // Both limits at zero delete the project's attic and keep nothing (p.299).
        QTemporaryDir dir;
        const QString bin = dir.filePath("B.cthbin");
        const QString attic = dir.filePath("Attic");
        writeBin(bin, "x");
        QVERIFY(saveToAttic(attic, "P", "B", bin, {}));
        QVERIFY(saveToAttic(attic, "Other", "B", bin, {}));
        QVERIFY(saveToAttic(attic, "P", "B", bin, {0, 0, true}));
        QVERIFY(!QFileInfo::exists(QDir(attic).filePath("P")));
        QCOMPARE(atticVersions(attic, "Other", "B").size(), 1);
    }

    void namesAreSafe() {
        QTemporaryDir dir;
        const QString bin = dir.filePath("B.cthbin");
        writeBin(bin, "x");
        QVERIFY(saveToAttic(dir.filePath("Attic"), "P", "Act 1: Rough/Cut", bin, {}));
        QCOMPARE(names(atticVersions(dir.filePath("Attic"), "P", "Act 1: Rough/Cut")), QStringList{"Act 1_ Rough_Cut.1.cthbin"});
    }
};

QTEST_GUILESS_MAIN(TestBinAttic)
#include "tst_bin_attic.moc"

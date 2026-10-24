// Tests for shared-storage bin locks (src/model/bin_lock.*, docs/spec/bin-locking.md): the first to
// open a bin holds its lock and others are told who (p.302); a holder's own lock left behind is theirs
// again; Lock Project Bin keeps the lock after close and Protect Project Bin too, and only the holder
// unlocks them (p.302); the history lists who did what (p.301); damaged lock files are held, not
// free; and LUMENIS_SHARED_BINS overrides where locks are kept.
#include "lumenis/model/bin_lock.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace lumenis::model;

namespace {

BinLockHolder holder(const QString& user, const QString& machine, BinLockKind kind = BinLockKind::Open) {
    BinLockHolder h;
    h.user = user;
    h.machine = machine;
    h.since = QDateTime::currentDateTimeUtc();
    h.kind = kind;
    return h;
}

} // namespace

class TestBinLock : public QObject {
    Q_OBJECT

private slots:
    void firstOpenerHoldsTheLock() {
        QTemporaryDir dir;
        const QString bin = dir.filePath(QStringLiteral("Rushes.cthbin"));
        const BinLockHolder ed = holder(QStringLiteral("ed"), QStringLiteral("EDIT-1"));
        const BinLockHolder asst = holder(QStringLiteral("asst"), QStringLiteral("EDIT-2"));
        QVERIFY(!readBinLock(bin));
        BinLockHolder got;
        QCOMPARE(acquireBinLock(bin, ed, &got), BinLockResult::Acquired);
        QVERIFY(got.isSameAs(ed));
        QVERIFY(QFile::exists(binLockPath(bin)));
        QCOMPARE(acquireBinLock(bin, asst, &got), BinLockResult::Held);
        QVERIFY(got.isSameAs(ed));
        QCOMPARE(got.describe(), QStringLiteral("ed on EDIT-1"));
        // Others cannot release it; its holder can, and then the next one gets it.
        QString error;
        QVERIFY(!releaseBinLock(bin, asst, false, &error));
        QVERIFY(error.contains(QStringLiteral("ed on EDIT-1")));
        QVERIFY(releaseBinLock(bin, ed, false));
        QVERIFY(!readBinLock(bin));
        QCOMPARE(acquireBinLock(bin, asst, &got), BinLockResult::Acquired);
    }

    // A lock left by a session of the same user on the same machine (one that ended without closing
    // the bin) is theirs again; the machine name's case does not matter.
    void ownLockIsTakenBack() {
        QTemporaryDir dir;
        const QString bin = dir.filePath(QStringLiteral("Cut.cthbin"));
        QCOMPARE(acquireBinLock(bin, holder(QStringLiteral("ed"), QStringLiteral("EDIT-1"))), BinLockResult::Acquired);
        QCOMPARE(acquireBinLock(bin, holder(QStringLiteral("ed"), QStringLiteral("edit-1"))), BinLockResult::Acquired);
        // Another user on that machine is someone else.
        QCOMPARE(acquireBinLock(bin, holder(QStringLiteral("asst"), QStringLiteral("EDIT-1"))), BinLockResult::Held);
    }

    // Lock Project Bin and Protect Project Bin: kept after close, removed by Unlock (p.302), and a
    // stronger kind replaces a weaker one of the same holder.
    void lockedAndProtected() {
        QTemporaryDir dir;
        const QString bin = dir.filePath(QStringLiteral("Selects.cthbin"));
        const BinLockHolder ed = holder(QStringLiteral("ed"), QStringLiteral("EDIT-1"));
        QCOMPARE(acquireBinLock(bin, ed), BinLockResult::Acquired);
        BinLockHolder got;
        QCOMPARE(acquireBinLock(bin, holder(QStringLiteral("ed"), QStringLiteral("EDIT-1"), BinLockKind::Locked), &got),
                 BinLockResult::Acquired);
        QCOMPARE(got.kind, BinLockKind::Locked);
        QCOMPARE(got.describe(), QStringLiteral("ed* on EDIT-1"));
        // Closing keeps a Locked lock; opening again with Open keeps it Locked.
        QVERIFY(releaseBinLock(bin, ed, false));
        QCOMPARE(readBinLock(bin)->kind, BinLockKind::Locked);
        QCOMPARE(acquireBinLock(bin, ed, &got), BinLockResult::Acquired);
        QCOMPARE(got.kind, BinLockKind::Locked);
        QCOMPARE(acquireBinLock(bin, holder(QStringLiteral("ed"), QStringLiteral("EDIT-1"), BinLockKind::Protected), &got),
                 BinLockResult::Acquired);
        QCOMPARE(got.describe(), QStringLiteral("ed** on EDIT-1"));
        // Only its holder unlocks it.
        QVERIFY(!releaseBinLock(bin, holder(QStringLiteral("asst"), QStringLiteral("EDIT-2")), true));
        QVERIFY(releaseBinLock(bin, ed, true));
        QVERIFY(!readBinLock(bin));
    }

    // A lock file that cannot be read is held by someone unknown: never taken for free.
    void damagedLockIsHeld() {
        QTemporaryDir dir;
        const QString bin = dir.filePath(QStringLiteral("Odd.cthbin"));
        QFile f(binLockPath(bin));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{not json");
        f.close();
        const auto lock = readBinLock(bin);
        QVERIFY(lock);
        BinLockHolder got;
        QCOMPARE(acquireBinLock(bin, holder(QStringLiteral("ed"), QStringLiteral("EDIT-1")), &got), BinLockResult::Held);
        QCOMPARE(got.describe(), QStringLiteral("?"));
    }

    void history() {
        QTemporaryDir dir;
        const QString bin = dir.filePath(QStringLiteral("Rushes.cthbin"));
        QVERIFY(readBinHistory(bin).isEmpty());
        appendBinHistory(bin, holder(QStringLiteral("ed"), QStringLiteral("EDIT-1")), QStringLiteral("saved"));
        appendBinHistory(bin, holder(QStringLiteral("as\tst"), QStringLiteral("EDIT-2")), QStringLiteral("locked"));
        QFile f(binHistoryPath(bin));
        QVERIFY(f.open(QIODevice::Append));
        f.write("garbage line\n");
        f.close();
        const QList<BinHistoryEntry> h = readBinHistory(bin);
        QCOMPARE(h.size(), 2);
        QCOMPARE(h[0].user, QStringLiteral("ed"));
        QCOMPARE(h[0].action, QStringLiteral("saved"));
        QCOMPARE(h[1].user, QStringLiteral("as st")); // a tab in a name cannot split the line
        QCOMPARE(h[1].machine, QStringLiteral("EDIT-2"));
        QVERIFY(h[1].when.isValid());
    }

    void sharedLocations() {
        QTemporaryDir dir;
        qputenv("LUMENIS_SHARED_BINS", "all");
        QVERIFY(isSharedLocation(dir.path()));
        qputenv("LUMENIS_SHARED_BINS", "none");
        QVERIFY(!isSharedLocation(dir.path()));
        qunsetenv("LUMENIS_SHARED_BINS");
        // A temporary folder is local storage.
        QVERIFY(!isSharedLocation(dir.filePath(QStringLiteral("not/yet/made.cthbin"))));
    }
};

QTEST_GUILESS_MAIN(TestBinLock)
#include "tst_bin_lock.moc"

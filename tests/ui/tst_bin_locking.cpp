// Shared-storage bin locking in the Bin Container (Editing Guide p.300-304, docs/spec/bin-locking.md):
// two users' Bin Containers on one project, which LUMENIS_SHARED_BINS makes shared. The first to
// open a bin holds it (green) and the other reads it (red); the holder's saves turn it yellow, and a
// click reads it again; a released lock is blue, and a click takes it; Alt+click releases it. Bins
// someone else holds are bold, and refuse changes, drops, renames and moves; items dragged out of
// them are duplicated. Lock, Protect and Unlock Project Bin, the history, Alt+double-click and the
// shared bin list.
#include "lumenis/model/bin.h"
#include "lumenis/model/bin_lock.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace lumenis;
using namespace lumenis::ui;
using State = BinDocument::LockState;

namespace {

void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

// A save by the other system is seen by its file's time, which may not have moved on yet in a fast
// test: it is moved on by hand.
void ageAndTouch(const QString& path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime));
}

struct Shared {
    QTemporaryDir dir;
    model::ProjectEntry entry;
    BinContainer ed;
    BinContainer asst;

    Shared() {
        qputenv("LUMENIS_SHARED_BINS", "all");
        model::ProjectSettings s;
        s.name = QStringLiteral("Show");
        s.format = projectFormatPresets().front();
        entry = *model::ProjectStore(dir.path()).create(s);
        ed.setLockUser(QStringLiteral("ed"));
        asst.setLockUser(QStringLiteral("asst"));
        for (BinContainer* c : {&ed, &asst}) {
            c->resize(700, 400);
            c->show();
        }
        // The first to open the project opens its bin and holds it.
        ed.setProject(entry.folder, s.name, Rational(25));
        asst.setProject(entry.folder, s.name, Rational(25));
        // Both run on this machine, so asst reopened the bins ed has open (the open bins are each
        // machine's, p.301); asst starts with none here, as on a machine of its own.
        for (BinDocument* d : asst.openDocuments()) asst.closeBin(d);
        settle();
    }
    ~Shared() {
        ed.setProject({}, {}, Rational(25));
        asst.setProject({}, {}, Rational(25));
        qunsetenv("LUMENIS_SHARED_BINS");
    }

    QString binId() const { return ed.bins()->bins().first()->id; }
    static QToolButton* button(BinContainer& c, BinDocument* d) {
        BinView* v = c.viewFor(d);
        return v ? v->findChild<QToolButton*>(QStringLiteral("binLockButton")) : nullptr;
    }
};

} // namespace

class TestBinLocking : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void firstOpenerHoldsTheBin() {
        Shared s;
        BinDocument* mine = s.ed.document(s.binId());
        QVERIFY(mine);
        QCOMPARE(mine->lockState(), State::Mine);
        QVERIFY(!mine->isReadOnly());
        QToolButton* green = Shared::button(s.ed, mine);
        QVERIFY(green && green->isVisible());

        // Each machine reopens its own bins; asst opens it read only (red).
        BinDocument* theirs = s.asst.openBin(s.binId());
        QVERIFY(theirs);
        QCOMPARE(theirs->lockState(), State::Others);
        QCOMPARE(theirs->lockHolder()->user, QStringLiteral("ed"));
        QVERIFY(theirs->isReadOnly());

        // Bold in asst's Bin Container, with who holds it; not in ed's.
        s.asst.pollLocks();
        QVERIFY(s.asst.isLockedByOther(s.binId()));
        QVERIFY(!s.ed.isLockedByOther(s.binId()));
        const QModelIndex row = s.asst.treeModel()->indexOf(s.binId());
        QVERIFY(row.data(Qt::FontRole).value<QFont>().bold());
        QVERIFY(row.data(Qt::ToolTipRole).toString().contains(QStringLiteral("ed on")));

        // asst can neither change, save, rename nor move it.
        QSignalSpy refused(&s.asst, &BinContainer::lockRefused);
        QVERIFY(s.asst.newSequence().isEmpty());
        QCOMPARE(refused.count(), 1);
        QVERIFY(theirs->bin().items.isEmpty());
        QVERIFY(!theirs->isDirty());
        QString error;
        QVERIFY(!theirs->save(&error));
        QVERIFY(error.contains(QStringLiteral("ed on")));
        QVERIFY(!s.asst.renameNode(s.binId(), QStringLiteral("Mine now"), &error));
        QVERIFY(error.contains(QStringLiteral("locked by")));
        QVERIFY(!s.asst.deleteNode(s.binId(), &error));
        QVERIFY(QFileInfo::exists(theirs->path()));
    }

    // Yellow when the holder saved, red again after a click; blue when they let it go, green after
    // a click (p.302-303).
    void refreshAndTakeOver() {
        Shared s;
        BinDocument* mine = s.ed.document(s.binId());
        BinDocument* theirs = s.asst.openBin(s.binId());
        QVERIFY(mine && theirs);

        QVERIFY(!s.ed.newSequence().isEmpty());
        QVERIFY(s.ed.saveBin(mine));
        ageAndTouch(mine->path());
        s.asst.pollLocks();
        QCOMPARE(theirs->lockState(), State::Modified);
        QVERIFY(s.asst.tabs()->tabText(0).contains(QStringLiteral("(modified)")));
        s.asst.lockButtonClicked(theirs, false);
        QCOMPARE(theirs->lockState(), State::Others);
        QCOMPARE(theirs->bin().items.size(), 1);
        QVERIFY(!theirs->isDirty());

        // ed closes it: blue for asst, who takes it.
        QVERIFY(s.ed.closeBin(mine));
        settle();
        QVERIFY(!model::readBinLock(theirs->path()));
        s.asst.pollLocks();
        QCOMPARE(theirs->lockState(), State::Released);
        s.asst.lockButtonClicked(theirs, false);
        QCOMPARE(theirs->lockState(), State::Mine);
        QVERIFY(!s.asst.newSequence().isEmpty());
        QVERIFY(s.asst.saveBin(theirs));

        // ed opens it again: now red for ed.
        BinDocument* again = s.ed.openBin(s.binId());
        QCOMPARE(again->lockState(), State::Others);
        QCOMPARE(again->bin().items.size(), 2);

        // Alt+click on green releases it (p.303).
        s.asst.lockButtonClicked(theirs, true);
        QCOMPARE(theirs->lockState(), State::Released);
        QVERIFY(!model::readBinLock(theirs->path()));
        s.ed.pollLocks();
        QCOMPARE(again->lockState(), State::Released);

        // The history lists the saves (p.301).
        QWidget* history = s.asst.showLockHistory(theirs);
        QVERIFY(history);
        auto* text = history->findChild<QPlainTextEdit*>(QStringLiteral("binLockHistoryText"));
        QVERIFY(text);
        QVERIFY(text->toPlainText().contains(QStringLiteral("saved")));
        QVERIFY(text->toPlainText().contains(QStringLiteral("asst")));
        QVERIFY(text->toPlainText().contains(QStringLiteral("ed")));
        history->close();
    }

    // Lock Project Bin keeps the lock after close; Protect Project Bin makes it read only for
    // everyone; only its holder unlocks it (p.302).
    void lockProtectUnlock() {
        Shared s;
        const QString id = s.binId();
        BinDocument* mine = s.ed.document(id);
        QString error;
        QVERIFY(s.ed.lockProjectBins({id}, model::BinLockKind::Locked, &error));
        QCOMPARE(model::readBinLock(mine->path())->describe().left(3), QStringLiteral("ed*"));
        QVERIFY(s.ed.closeBin(mine));
        settle();
        QCOMPARE(model::readBinLock(s.ed.bins()->binPath(id))->kind, model::BinLockKind::Locked);
        BinDocument* theirs = s.asst.openBin(id);
        QCOMPARE(theirs->lockState(), State::Others);
        QVERIFY(!s.asst.unlockProjectBins({id}, &error));
        QVERIFY(error.contains(QStringLiteral("locked by")));

        mine = s.ed.openBin(id);
        QCOMPARE(mine->lockState(), State::Mine);
        QVERIFY(s.ed.lockProjectBins({id}, model::BinLockKind::Protected, &error));
        QCOMPARE(mine->lockState(), State::Protected);
        QVERIFY(mine->isReadOnly());
        QVERIFY(s.ed.newSequence().isEmpty());
        s.asst.pollLocks();
        QCOMPARE(theirs->lockState(), State::Protected);

        // Unlocking gives ed back an ordinary lock on the bin ed has open.
        QVERIFY(s.ed.unlockProjectBins({id}, &error));
        QCOMPARE(mine->lockState(), State::Mine);
        QCOMPARE(model::readBinLock(mine->path())->kind, model::BinLockKind::Open);
        s.asst.pollLocks();
        QCOMPARE(theirs->lockState(), State::Others);
    }

    // "You cannot drag an item to a locked bin"; dragged out of one, an item is duplicated (p.304).
    void dragsIntoAndOutOfLockedBins() {
        Shared s;
        const QString held = s.binId();
        BinDocument* edBin = s.ed.document(held);
        const QString item = s.ed.newSequence();
        QVERIFY(s.ed.saveBin(edBin));
        // asst's own bin, and ed's read only.
        const QString own = s.asst.newBin();
        BinDocument* asstBin = s.asst.document(own);
        QVERIFY(asstBin);
        QCOMPARE(asstBin->lockState(), State::Mine);
        BinDocument* readOnly = s.asst.openBin(held);
        QVERIFY(readOnly->bin().item(item));

        const QStringList copied = s.asst.transferItems(held, {item}, own, false);
        QCOMPARE(copied.size(), 1);
        QVERIFY(copied.first() != item); // a duplicate, not a copy
        QVERIFY(readOnly->bin().item(item));
        QVERIFY(asstBin->bin().item(copied.first()));

        QSignalSpy refused(&s.asst, &BinContainer::lockRefused);
        QVERIFY(s.asst.transferItems(own, {copied.first()}, held, false).isEmpty());
        QCOMPARE(refused.count(), 1);
        QVERIFY(asstBin->bin().item(copied.first()));
    }

    // Alt+double-click opens a bin "without controlling the lock" (p.302): blue while it is free.
    void opensWithoutTheLock() {
        Shared s;
        const QString id = s.binId();
        QVERIFY(s.ed.closeBin(s.ed.document(id)));
        settle();
        BinDocument* doc = s.asst.openBin(id, false);
        QCOMPARE(doc->lockState(), State::Released);
        QVERIFY(!model::readBinLock(doc->path()));
        // ed now takes it as usual.
        BinDocument* mine = s.ed.openBin(id);
        QCOMPARE(mine->lockState(), State::Mine);
        s.asst.pollLocks();
        QCOMPARE(doc->lockState(), State::Others);
    }

    // The bin list is shared; the open bins are each machine's own, beside it.
    void sharedBinList() {
        Shared s;
        const QString added = s.ed.newBin();
        QVERIFY(!added.isEmpty());
        QVERIFY(!s.asst.bins()->node(added));
        s.asst.pollLocks();
        QVERIFY(s.asst.bins()->node(added));
        QVERIFY(s.asst.treeModel()->indexOf(added).isValid());
        // asst opening a bin does not write ed's open bins, nor the shared list.
        const QDateTime treeAt = QFileInfo(s.ed.bins()->treeFilePath()).lastModified();
        BinDocument* doc = s.asst.openBin(added);
        QVERIFY(doc);
        QCOMPARE(QFileInfo(s.ed.bins()->treeFilePath()).lastModified(), treeAt);
        // A bin on storage that is not shared has no lock and no button.
        qputenv("LUMENIS_SHARED_BINS", "none");
        BinContainer local;
        local.show();
        local.setProject(s.entry.folder, QStringLiteral("Show"), Rational(25));
        BinDocument* plain = local.openBin(added);
        QVERIFY(plain);
        QCOMPARE(plain->lockState(), State::NotShared);
        QToolButton* none = Shared::button(local, plain);
        QVERIFY(none && !none->isVisible());
        local.setProject({}, {}, Rational(25));
        qputenv("LUMENIS_SHARED_BINS", "all");
    }
};

QTEST_MAIN(TestBinLocking)
#include "tst_bin_locking.moc"

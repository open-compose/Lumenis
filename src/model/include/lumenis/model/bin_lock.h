#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

// Shared-storage bin locking (Editing Guide p.300-304; docs/spec/bin-locking.md). When bins are on
// storage that several systems use, "the user who opens the bin first controls the lock and obtains
// write access to the bin"; the others can read it but not save it (p.302). Media Composer does this
// on Avid NEXIS; Cutthulhu does it on any shared folder (a NAS through SMB, NFS or AFP; ROADMAP §2):
// the lock is a small file beside the bin, "<bin>.cthbin.lck", made with an exclusive create so that
// two systems cannot both make it, and the bin's history, "<bin>.cthbin.history", lists who saved it
// and when (the history file of the Bin Lock Status button, p.301).
namespace lumenis::model {

// How a bin is held. Open: while its holder has it open, released when they close it. Locked (Lock
// Project Bin): kept after they close it. Protected (Protect Project Bin): kept, and read-only for
// everyone, its holder too, until its holder unlocks it (p.302).
enum class BinLockKind { Open, Locked, Protected };

// Who holds a lock, or who is asking for one: a user on a machine.
struct BinLockHolder {
    QString user;
    QString machine;
    QDateTime since;
    BinLockKind kind = BinLockKind::Open;

    // The same user on the same machine (machine names compared without case, as Windows does).
    bool isSameAs(const BinLockHolder& other) const;
    // "ed on EDIT-1", with one asterisk for Locked and two for Protected after the user's name, as
    // the guide shows them (p.302).
    QString describe() const;
};

// The lock and history files of a bin file.
QString binLockPath(const QString& binPath);
QString binHistoryPath(const QString& binPath);

// The current user on this machine (QSysInfo::machineHostName()), asking for `kind`.
BinLockHolder localBinLockHolder(const QString& user, BinLockKind kind = BinLockKind::Open);

// The bin's lock, or nothing when it has none. A lock file that cannot be read (being written, or
// damaged) is held by an unknown user, so that it is never taken for free.
std::optional<BinLockHolder> readBinLock(const QString& binPath);

enum class BinLockResult {
    Acquired, // `me` holds it now (its kind, or a stronger one it already had)
    Held,     // someone else holds it; `holder` says who
    Failed,   // the lock file could not be made; `error` says why
};

// Takes the bin's lock for `me` with me.kind. A lock `me` already holds (left by a session of theirs
// that ended without releasing it, or held from before) is theirs again, with the stronger of the two
// kinds. `holder` gets the lock's holder in every case but Failed.
BinLockResult acquireBinLock(const QString& binPath, const BinLockHolder& me, BinLockHolder* holder = nullptr,
                             QString* error = nullptr);

// Gives up `me`'s lock on the bin. On close (`unlock` false) an Open lock is removed and a Locked or
// Protected one stays; Unlock Project Bin (`unlock` true) removes any of them. True when the bin is
// left without a lock of `me`'s; false, with `error`, when someone else holds it ("it can be unlocked
// by the user who originated the command", p.302) or the file could not be removed.
bool releaseBinLock(const QString& binPath, const BinLockHolder& me, bool unlock, QString* error = nullptr);

// One line of a bin's history: who did what, and when.
struct BinHistoryEntry {
    QDateTime when;
    QString user;
    QString machine;
    QString action; // "saved", "locked", "protected", "unlocked"
};
void appendBinHistory(const QString& binPath, const BinLockHolder& who, const QString& action);
// Oldest first; lines that cannot be read are left out.
QList<BinHistoryEntry> readBinHistory(const QString& binPath);

// Whether `path` is on storage that other systems may share: a network file system (SMB, NFS, AFP,
// WebDAV and the like; on Windows a network drive or a UNC path). Locks are kept only there, as Media
// Composer shows the Bin Lock Status button only for bins on shared storage (p.302). The environment
// variable LUMENIS_SHARED_BINS overrides it: "all" (or 1) makes every location shared, "none" (or 0)
// none (tests; storage this check does not recognise).
bool isSharedLocation(const QString& path);

} // namespace lumenis::model

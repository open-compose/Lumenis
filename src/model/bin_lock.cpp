#include "lumenis/model/bin_lock.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>
#include <QSysInfo>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Bin lock and history files (bin_lock.h). The lock file is JSON: {"app", "user", "machine", "since",
// "kind"}. It is made with QIODevice::NewOnly, an exclusive create (O_EXCL, CREATE_NEW), which file
// servers carry out atomically, so two systems opening a bin at once cannot both make it; the one that
// fails reads who has it. The history is one tab-separated line per event, appended.
namespace lumenis::model {

namespace {

QString kindName(BinLockKind kind) {
    switch (kind) {
    case BinLockKind::Open: return QStringLiteral("open");
    case BinLockKind::Locked: return QStringLiteral("locked");
    case BinLockKind::Protected: return QStringLiteral("protected");
    }
    return QStringLiteral("open");
}

BinLockKind kindOf(const QString& name) {
    if (name == QLatin1String("locked")) return BinLockKind::Locked;
    if (name == QLatin1String("protected")) return BinLockKind::Protected;
    return BinLockKind::Open;
}

QByteArray lockJson(const BinLockHolder& h) {
    const QJsonObject o{{QStringLiteral("app"), QStringLiteral("Cutthulhu")},
                        {QStringLiteral("user"), h.user},
                        {QStringLiteral("machine"), h.machine},
                        {QStringLiteral("since"), h.since.toUTC().toString(Qt::ISODate)},
                        {QStringLiteral("kind"), kindName(h.kind)}};
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Writes a lock file in place of the one there (the holder's own, whose kind changes).
bool rewrite(const QString& path, const BinLockHolder& h, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(lockJson(h)) < 0 || !f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace

bool BinLockHolder::isSameAs(const BinLockHolder& other) const {
    return user == other.user && machine.compare(other.machine, Qt::CaseInsensitive) == 0;
}

QString BinLockHolder::describe() const {
    const QString stars = kind == BinLockKind::Protected ? QStringLiteral("**") : kind == BinLockKind::Locked ? QStringLiteral("*") : QString();
    const QString who = user.isEmpty() ? QStringLiteral("?") : user;
    return machine.isEmpty() ? who + stars : QStringLiteral("%1%2 on %3").arg(who, stars, machine);
}

QString binLockPath(const QString& binPath) {
    return binPath + QStringLiteral(".lck");
}

QString binHistoryPath(const QString& binPath) {
    return binPath + QStringLiteral(".history");
}

BinLockHolder localBinLockHolder(const QString& user, BinLockKind kind) {
    BinLockHolder h;
    h.user = user;
    h.machine = QSysInfo::machineHostName();
    h.since = QDateTime::currentDateTimeUtc();
    h.kind = kind;
    return h;
}

std::optional<BinLockHolder> readBinLock(const QString& binPath) {
    QFile f(binLockPath(binPath));
    if (!f.exists()) return std::nullopt;
    BinLockHolder h;
    if (!f.open(QIODevice::ReadOnly)) return h; // there, but unreadable: held by someone unknown
    const QJsonObject o = QJsonDocument::fromJson(f.read(64 * 1024)).object();
    h.user = o.value(QStringLiteral("user")).toString();
    h.machine = o.value(QStringLiteral("machine")).toString();
    h.since = QDateTime::fromString(o.value(QStringLiteral("since")).toString(), Qt::ISODate);
    h.kind = kindOf(o.value(QStringLiteral("kind")).toString());
    return h;
}

BinLockResult acquireBinLock(const QString& binPath, const BinLockHolder& me, BinLockHolder* holder, QString* error) {
    const QString path = binLockPath(binPath);
    for (int attempt = 0; attempt < 2; ++attempt) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            const QByteArray json = lockJson(me);
            const bool ok = f.write(json) == json.size();
            f.close();
            if (!ok) {
                if (error) *error = f.errorString();
                QFile::remove(path);
                return BinLockResult::Failed;
            }
            if (holder) *holder = me;
            return BinLockResult::Acquired;
        }
        const auto current = readBinLock(binPath);
        if (!current) continue; // released between the two calls: try once more
        if (!current->isSameAs(me)) {
            if (holder) *holder = *current;
            return BinLockResult::Held;
        }
        // Ours already: kept with the stronger kind.
        BinLockHolder mine = *current;
        if (static_cast<int>(me.kind) > static_cast<int>(mine.kind)) {
            mine.kind = me.kind;
            mine.since = me.since;
            if (!rewrite(path, mine, error)) return BinLockResult::Failed;
        }
        if (holder) *holder = mine;
        return BinLockResult::Acquired;
    }
    if (error) *error = QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(path));
    return BinLockResult::Failed;
}

bool releaseBinLock(const QString& binPath, const BinLockHolder& me, bool unlock, QString* error) {
    const auto current = readBinLock(binPath);
    if (!current) return true;
    if (!current->isSameAs(me)) {
        if (error) *error = QStringLiteral("the bin is locked by %1").arg(current->describe());
        return false;
    }
    if (!unlock && current->kind != BinLockKind::Open) return true;
    if (!QFile::remove(binLockPath(binPath)) && QFile::exists(binLockPath(binPath))) {
        if (error) *error = QStringLiteral("cannot remove %1").arg(QDir::toNativeSeparators(binLockPath(binPath)));
        return false;
    }
    return true;
}

void appendBinHistory(const QString& binPath, const BinLockHolder& who, const QString& action) {
    QFile f(binHistoryPath(binPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    auto clean = [](QString s) { return s.replace(QLatin1Char('\t'), QLatin1Char(' ')).replace(QLatin1Char('\n'), QLatin1Char(' ')); };
    const QString line = QStringLiteral("%1\t%2\t%3\t%4\n")
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate), clean(who.user), clean(who.machine), clean(action));
    f.write(line.toUtf8());
}

QList<BinHistoryEntry> readBinHistory(const QString& binPath) {
    QList<BinHistoryEntry> out;
    QFile f(binHistoryPath(binPath));
    if (!f.open(QIODevice::ReadOnly)) return out;
    // The newest few thousand lines are plenty to show; a history is never read whole into memory
    // past that.
    constexpr qint64 kTail = 4 * 1024 * 1024;
    if (f.size() > kTail) f.seek(f.size() - kTail);
    const QByteArray data = f.readAll();
    for (const QByteArray& raw : data.split('\n')) {
        const QStringList parts = QString::fromUtf8(raw).split(QLatin1Char('\t'));
        if (parts.size() != 4) continue;
        BinHistoryEntry e;
        e.when = QDateTime::fromString(parts[0], Qt::ISODate);
        if (!e.when.isValid()) continue;
        e.user = parts[1];
        e.machine = parts[2];
        e.action = parts[3];
        out << e;
    }
    return out;
}

bool isSharedLocation(const QString& path) {
    const QString force = qEnvironmentVariable("LUMENIS_SHARED_BINS").toLower();
    if (force == QLatin1String("all") || force == QLatin1String("1")) return true;
    if (force == QLatin1String("none") || force == QLatin1String("0")) return false;
    QString absolute = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(absolute);
    if (native.startsWith(QLatin1String("\\\\"))) return true; // \\server\share
    if (native.size() >= 2 && native[1] == QLatin1Char(':')) {
        const std::wstring root = native.left(2).toStdWString() + L"\\";
        return GetDriveTypeW(root.c_str()) == DRIVE_REMOTE;
    }
    return false;
#else
    // The storage of the nearest folder that exists (the bin may not be written yet).
    while (!absolute.isEmpty() && !QFileInfo::exists(absolute)) {
        const QString up = QFileInfo(absolute).absolutePath();
        if (up == absolute) break;
        absolute = up;
    }
    const QByteArray type = QStorageInfo(absolute).fileSystemType().toLower();
    static const char* const kNetwork[] = {"cifs", "smb3", "smbfs", "smb2", "nfs", "nfs4", "afpfs", "webdav", "davfs",
                                           "fuse.sshfs", "afs", "ceph", "glusterfs", "fuse.glusterfs", "lustre"};
    return std::any_of(std::begin(kNetwork), std::end(kNetwork), [&](const char* t) { return type == t; });
#endif
}

} // namespace lumenis::model

#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// The attic: backup copies of a project's bins, kept each time a bin is saved (Editing Guide p.71-72,
// p.298-299; the Bin settings' limits, p.1153-1154; docs/spec/bin-attic.md). The layout is the guide's:
//
//   <attic>/<project>/Bins/<bin>/<bin>.<n><suffix>
//
// where <n> counts up from 1 and the highest is the newest copy. Ours: the copy keeps the bin file's
// suffix (".cthbin"), so File > Open Bin opens it as it is. The functions here only copy and delete
// files; BinContainer calls them as bins are made and saved.
namespace lumenis::model {

struct AtticLimits {
    // "Maximum files in a project's attic" (default 1000) and "Max versions of a file in the attic"
    // (default 50), p.1153. Both 0: the project's attic is deleted and nothing is kept (p.299).
    int maxFiles = 1000;
    int maxVersions = 50;
    // "Always keep at least one version in Attic" (p.1154): the project limit never removes a bin's
    // last copy.
    bool keepOne = true;
};

// The folder that holds `project`'s copies of the bin named `binName`.
QString atticBinFolder(const QString& atticRoot, const QString& project, const QString& binName);

// The copies of one bin, oldest first (by number).
QStringList atticVersions(const QString& atticRoot, const QString& project, const QString& binName);

// Copies the bin file at `binFile` (the bin named `binName`) into the attic as its newest version,
// then keeps the limits:
//  * at "Max versions", a copy goes first: the oldest, but "only if the second-oldest backup file is
//    more than 2 hours old" (p.71); otherwise the newest is replaced, so a busy hour cannot push out
//    the older history;
//  * over "Maximum files", the project's oldest copies go, never a bin's last one with `keepOne`.
// `now` is the time the copies' ages are measured against. Returns false, with `error`, if the copy
// cannot be made; an empty `atticRoot` keeps nothing and succeeds.
bool saveToAttic(const QString& atticRoot, const QString& project, const QString& binName, const QString& binFile,
                 const AtticLimits& limits, const QDateTime& now = QDateTime::currentDateTime(), QString* error = nullptr);

} // namespace lumenis::model

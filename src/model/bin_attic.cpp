#include "lumenis/model/bin_attic.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>

// The attic's copies (bin_attic.h). A copy's number is in its file name, so the order of the copies does not
// depend on file times, which a copy across drives may not keep; the ages that the two-hour rule compares are
// the files' modification times.
namespace lumenis::model {

namespace {

// Characters a file name cannot hold on some platform become '_' (p.295 lists them).
QString safeName(QString name) {
    static const QRegularExpression bad(QStringLiteral(R"([/\\:*?"<>|])"));
    name.replace(bad, QStringLiteral("_"));
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("_") : name;
}

struct Version {
    QString path;
    int number = 0;
    QDateTime modified;
};

// The copies in one bin's folder, oldest first.
QList<Version> versionsIn(const QString& folder) {
    QList<Version> out;
    const QString base = QFileInfo(folder).fileName();
    static const QRegularExpression numbered(QStringLiteral(R"(^(.*)\.(\d+)(\.[^.]*)?$)"));
    for (const QFileInfo& f : QDir(folder).entryInfoList(QDir::Files)) {
        const QRegularExpressionMatch m = numbered.match(f.fileName());
        if (!m.hasMatch() || m.captured(1) != base) continue;
        out.append({f.absoluteFilePath(), m.captured(2).toInt(), f.lastModified()});
    }
    std::sort(out.begin(), out.end(), [](const Version& a, const Version& b) { return a.number < b.number; });
    return out;
}

} // namespace

QString atticBinFolder(const QString& atticRoot, const QString& project, const QString& binName) {
    return QDir(atticRoot).filePath(QStringLiteral("%1/Bins/%2").arg(safeName(project), safeName(binName)));
}

QStringList atticVersions(const QString& atticRoot, const QString& project, const QString& binName) {
    QStringList out;
    for (const Version& v : versionsIn(atticBinFolder(atticRoot, project, binName))) out << v.path;
    return out;
}

bool saveToAttic(const QString& atticRoot, const QString& project, const QString& binName, const QString& binFile,
                 const AtticLimits& limits, const QDateTime& now, QString* error) {
    if (atticRoot.isEmpty()) return true;
    const QString projectFolder = QDir(atticRoot).filePath(safeName(project));
    // "Setting to zero the maximum number of files ... as well as the maximum number of versions of a bin
    // deletes existing files in the project folder in the Avid Attic folder and prevents any backup bins
    // from being saved" (p.299).
    if (limits.maxFiles <= 0 && limits.maxVersions <= 0) {
        QDir(projectFolder).removeRecursively();
        return true;
    }
    // Either limit at zero alone keeps nothing new; the copies already there stay.
    if (limits.maxFiles <= 0 || limits.maxVersions <= 0) return true;

    const QString folder = atticBinFolder(atticRoot, project, binName);
    if (!QDir().mkpath(folder)) {
        if (error) *error = QObject::tr("cannot make the attic folder %1").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    QList<Version> versions = versionsIn(folder);
    const int next = versions.isEmpty() ? 1 : versions.last().number + 1;
    while (versions.size() >= limits.maxVersions) {
        // "The oldest backup file is overwritten only if the second-oldest backup file is more than 2 hours
        // old" (p.71); otherwise the newest copy makes way.
        const bool oldestGoes = versions.size() < 2 || versions.at(1).modified.secsTo(now) > 2 * 60 * 60;
        QFile::remove(oldestGoes ? versions.takeFirst().path : versions.takeLast().path);
    }
    const QString ext = QFileInfo(binFile).suffix();
    const QString suffix = ext.isEmpty() ? QString() : QLatin1Char('.') + ext;
    const QString target =
        QDir(folder).filePath(QStringLiteral("%1.%2%3").arg(QFileInfo(folder).fileName()).arg(next).arg(suffix));
    QFile::remove(target);
    if (!QFile::copy(binFile, target)) {
        if (error) *error = QObject::tr("cannot copy the bin into the attic (%1)").arg(QDir::toNativeSeparators(target));
        return false;
    }

    // The project's limit: its oldest copies go, by time, across all its bins.
    struct Entry {
        Version v;
        QString bin;
    };
    QList<Entry> all;
    QHash<QString, int> perBin;
    for (const QFileInfo& d : QDir(QDir(projectFolder).filePath(QStringLiteral("Bins"))).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        for (const Version& v : versionsIn(d.absoluteFilePath())) {
            all.append({v, d.fileName()});
            ++perBin[d.fileName()];
        }
    }
    if (all.size() <= limits.maxFiles) return true;
    std::stable_sort(all.begin(), all.end(), [](const Entry& a, const Entry& b) {
        return a.v.modified != b.v.modified ? a.v.modified < b.v.modified : a.v.number < b.v.number;
    });
    qsizetype count = all.size();
    for (const Entry& e : all) {
        if (count <= limits.maxFiles) break;
        if (e.v.path == QFileInfo(target).absoluteFilePath()) continue; // the copy just made stays
        if (limits.keepOne && perBin.value(e.bin) <= 1) continue;
        if (QFile::remove(e.v.path)) {
            --perBin[e.bin];
            --count;
        }
    }
    return true;
}

} // namespace lumenis::model

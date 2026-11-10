// Copy Media (copy_media.h; Editing Guide p.512-513).
#include "lumenis/media/copy_media.h"

#include "lumenis/media/managed.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>

namespace lumenis::media {

QString nextNumberedFolder(const QString& parent) {
    int highest = 0;
    const QDir dir(parent);
    for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const int n = name.toInt(&ok);
        if (ok && n > highest) highest = n;
    }
    return dir.filePath(QString::number(highest + 1));
}

namespace {

// A file's name and size as files already at the target are matched: the name without the " 2" a renamed copy took
// (planInto), so that a copy made under another name counts too.
QString fileKey(const QFileInfo& f) {
    static const QRegularExpression numbered(QStringLiteral(" \\d+$"));
    QString stem = f.completeBaseName();
    stem.remove(numbered);
    return stem.toLower() + QLatin1Char('.') + f.suffix().toLower() + QLatin1Char('\n') + QString::number(f.size());
}

// The keys (fileKey) of the files already under `folders`, each searched to any depth.
QSet<QString> filesAt(const QStringList& folders) {
    QSet<QString> out;
    for (const QString& folder : folders) {
        if (!QFileInfo(folder).isDir()) continue;
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            out.insert(fileKey(it.fileInfo()));
        }
    }
    return out;
}

// Plans the copies of `files` into `folder`: skipped when the target already has them, renamed "<name> 2.<suffix>" when
// another planned file there has the name.
void planInto(const QStringList& files, const QString& folder, const QSet<QString>& there, QSet<QString>& names,
              QList<CopyMediaPlan::Copy>& out, std::int64_t& bytes, CopyMediaPlan& plan) {
    for (const QString& file : files) {
        const QFileInfo f(file);
        if (!f.isFile()) {
            plan.missing << file;
            continue;
        }
        if (there.contains(fileKey(f))) {
            ++plan.existing;
            continue;
        }
        QString name = f.fileName();
        for (int n = 2; names.contains(QDir(folder).filePath(name).toLower()); ++n) {
            name = f.suffix().isEmpty() ? QStringLiteral("%1 %2").arg(f.completeBaseName()).arg(n)
                                        : QStringLiteral("%1 %2.%3").arg(f.completeBaseName()).arg(n).arg(f.suffix());
        }
        const QString to = QDir(folder).filePath(name);
        names.insert(to.toLower());
        out.append({f.absoluteFilePath(), to, f.size()});
        bytes += f.size();
    }
}

} // namespace

CopyMediaPlan planCopyMedia(const CopyMediaRequest& request) {
    CopyMediaPlan plan;
    QStringList mediaFiles;
    if (request.managed) mediaFiles << request.managedFiles;
    if (request.renders) mediaFiles << request.renderFiles;
    mediaFiles.removeDuplicates();
    QStringList proxyFiles = request.proxies ? request.proxyFiles : QStringList();
    proxyFiles.removeDuplicates();
    QSet<QString> names;
    if (!request.target.isEmpty()) {
        QStringList searched;
        if (request.toFolder) {
            // "If you selected a specific folder to copy the media to, the media appears in an 'Avid Copied Media' folder at
            // the target location" (p.513); ours: proxies too.
            plan.mediaFolder = QDir(request.target).filePath(QStringLiteral("Avid Copied Media"));
            plan.proxyFolder = plan.mediaFolder;
            searched << plan.mediaFolder;
        } else {
            // "If no Avid MediaFiles folder or MXF folder exists at the selected location, an Avid MediaFiles folder is
            // created and the media is placed inside the MXF '1' folder. Proxy media is placed in the Proxy folder based on
            // the next available number" (p.513).
            const QString root = mediaFilesFolder(request.target);
            plan.mediaFolder = nextNumberedFolder(QDir(root).filePath(QStringLiteral("MXF")));
            plan.proxyFolder = nextNumberedFolder(QDir(root).filePath(QStringLiteral("Proxy")));
            searched << QDir(root).filePath(QStringLiteral("MXF")) << QDir(root).filePath(QStringLiteral("Proxy"));
        }
        const QSet<QString> there = filesAt(searched);
        planInto(mediaFiles, plan.mediaFolder, there, names, plan.media, plan.mediaBytes, plan);
        planInto(proxyFiles, plan.proxyFolder, there, names, plan.media, plan.mediaBytes, plan);
    }
    if (request.bins && !request.binsTarget.isEmpty()) {
        // "If you chose to copy bins, the bins are placed in an 'Avid Copied Bins' folder at the target location" (p.513).
        plan.binFolder = QDir(request.binsTarget).filePath(QStringLiteral("Avid Copied Bins"));
        QStringList binFiles = request.binFiles;
        binFiles.removeDuplicates();
        planInto(binFiles, plan.binFolder, filesAt({plan.binFolder}), names, plan.bins, plan.binBytes, plan);
    }
    return plan;
}

std::int64_t availableSpace(const QString& path) {
    QFileInfo f(path);
    while (!f.exists() && !f.absolutePath().isEmpty() && f.absoluteFilePath() != f.absolutePath()) {
        f = QFileInfo(f.absolutePath());
    }
    if (!f.exists()) return -1;
    const QStorageInfo storage(f.absoluteFilePath());
    return storage.isValid() ? storage.bytesAvailable() : -1;
}

bool planFits(const CopyMediaPlan& plan, const CopyMediaRequest& request) {
    const std::int64_t mediaSpace = availableSpace(request.target);
    const std::int64_t binSpace = availableSpace(request.binsTarget);
    const bool sameVolume = !plan.bins.isEmpty() && !plan.media.isEmpty() &&
                            QStorageInfo(QFileInfo(request.target).absoluteFilePath()).rootPath() ==
                                QStorageInfo(QFileInfo(request.binsTarget).absoluteFilePath()).rootPath();
    if (sameVolume) return mediaSpace >= 0 && plan.mediaBytes + plan.binBytes <= mediaSpace;
    if (!plan.media.isEmpty() && (mediaSpace < 0 || plan.mediaBytes > mediaSpace)) return false;
    if (!plan.bins.isEmpty() && (binSpace < 0 || plan.binBytes > binSpace)) return false;
    return true;
}

int copyMedia(const CopyMediaPlan& plan, const std::function<bool(double)>& progress, QStringList* warnings) {
    const std::int64_t total = std::max<std::int64_t>(1, plan.mediaBytes + plan.binBytes);
    std::int64_t done = 0;
    int copied = 0;
    QList<CopyMediaPlan::Copy> all = plan.media;
    all << plan.bins;
    for (const CopyMediaPlan::Copy& c : all) {
        if (progress && !progress(double(done) / double(total))) break;
        if (!QDir().mkpath(QFileInfo(c.to).absolutePath())) {
            if (warnings) *warnings << QObject::tr("The folder %1 cannot be made.").arg(QDir::toNativeSeparators(QFileInfo(c.to).absolutePath()));
            continue;
        }
        if (QFile::copy(c.from, c.to)) {
            ++copied;
        } else if (warnings) {
            *warnings << QObject::tr("%1 could not be copied to %2.")
                             .arg(QDir::toNativeSeparators(c.from), QDir::toNativeSeparators(c.to));
        }
        done += c.bytes;
    }
    if (progress) progress(1.0);
    return copied;
}

QString megabytes(std::int64_t bytes) {
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

QString gigabytes(std::int64_t bytes) {
    return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace lumenis::media

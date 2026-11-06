// XDCAM disks as folders (lumenis/media/xdcam_disk.h).
#include "lumenis/media/xdcam_disk.h"

#include "lumenis/media/media.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>

#include <algorithm>

namespace lumenis::media {

namespace {

QStringList& extraDisks() {
    static QStringList folders;
    return folders;
}

// The child folder of `root` named `name`, ignoring case (XDCAM disks are FAT/UDF; their names are upper or mixed
// case), or an empty string.
QString childFolder(const QString& root, const QString& name) {
    const QDir dir(root);
    for (const QString& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.compare(name, Qt::CaseInsensitive) == 0) return dir.filePath(entry);
    }
    return {};
}

bool looksLikeXdcamDisk(const QString& root) {
    if (childFolder(root, QStringLiteral("Clip")).isEmpty()) return false;
    return !childFolder(root, QStringLiteral("General")).isEmpty() || !childFolder(root, QStringLiteral("Sub")).isEmpty() ||
           !childFolder(root, QStringLiteral("Edit")).isEmpty();
}

// The Clip folder of a disk, made when the disk has none yet.
QString clipFolder(const QString& disk) {
    QString clip = childFolder(disk, QStringLiteral("Clip"));
    if (clip.isEmpty()) {
        clip = QDir(disk).filePath(QStringLiteral("Clip"));
        QDir().mkpath(clip);
    }
    return clip;
}

} // namespace

void setXdcamDiskFolders(const QStringList& folders) { extraDisks() = folders; }

QStringList xdcamDiskFolders() {
    QStringList out = extraDisks();
    const QString env = qEnvironmentVariable("LUMENIS_XDCAM_DISKS");
    if (!env.isEmpty()) out << env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return out;
}

QStringList xdcamDisks() {
    QStringList out;
    for (const QString& f : xdcamDiskFolders()) {
        const QString path = QFileInfo(f).absoluteFilePath();
        if (QFileInfo(path).isDir() && !out.contains(path)) out << path;
    }
    if (qEnvironmentVariable("LUMENIS_NO_SYSTEM_DRIVES") != QLatin1String("1")) {
        for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
            if (!volume.isValid() || !volume.isReady() || volume.isReadOnly()) continue;
            const QString root = volume.rootPath();
            if (looksLikeXdcamDisk(root) && !out.contains(root)) out << root;
        }
    }
    return out;
}

QString nextXdcamClipName(const QString& disk) {
    static const QRegularExpression name(QStringLiteral("^C(\\d{4})\\.MXF$"), QRegularExpression::CaseInsensitiveOption);
    int highest = 0;
    const QString clip = childFolder(disk, QStringLiteral("Clip"));
    if (!clip.isEmpty()) {
        for (const QString& f : QDir(clip).entryList(QDir::Files)) {
            const QRegularExpressionMatch m = name.match(f);
            if (m.hasMatch()) highest = std::max(highest, m.captured(1).toInt());
        }
    }
    return QStringLiteral("C%1.MXF").arg(highest + 1, 4, 10, QLatin1Char('0'));
}

ext::ProviderInfo XdcamDiskProvider::info() const {
    return {"org.cutthulhu.xdcam-disk", "XDCAM disks as folders", ext::Category::ExportDevice, ext::LicenseTier::OpenSource,
            "Cutthulhu", "1"};
}

std::vector<ext::ExportDeviceTarget> XdcamDiskProvider::targets() const {
    std::vector<ext::ExportDeviceTarget> out;
    for (const QString& disk : xdcamDisks()) {
        ext::ExportDeviceTarget t;
        t.id = disk.toStdString();
        t.name = QDir::toNativeSeparators(disk).toStdString();
        // The audio channels of the first clip on it.
        const QString clip = childFolder(disk, QStringLiteral("Clip"));
        if (!clip.isEmpty()) {
            for (const QString& f : QDir(clip).entryList({QStringLiteral("*.mxf"), QStringLiteral("*.MXF")}, QDir::Files, QDir::Name)) {
                if (const auto probed = media::probe(QDir(clip).filePath(f))) {
                    int channels = 0;
                    for (const StreamInfo* a : probed->audio()) channels += a->channels;
                    t.audioChannels = channels;
                    break;
                }
            }
        }
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<std::string> XdcamDiskProvider::videoFormats() const { return {"xdcam_50", "xdcam_35", "xdcam_25", "xdcam_17"}; }

bool XdcamDiskProvider::deliver(const std::string& targetId, const std::string& file, std::string* clipName, std::string* error) {
    const QString disk = QString::fromStdString(targetId);
    if (!QFileInfo(disk).isDir()) {
        if (error) *error = QObject::tr("The XDCAM disk %1 is not connected.").arg(QDir::toNativeSeparators(disk)).toStdString();
        return false;
    }
    const QString name = nextXdcamClipName(disk);
    const QString to = QDir(clipFolder(disk)).filePath(name);
    const QString from = QString::fromStdString(file);
    // A rename on the same volume; otherwise a copy.
    if (!QFile::rename(from, to)) {
        if (!QFile::copy(from, to)) {
            if (error) *error = QObject::tr("%1 could not be written to the disk.").arg(name).toStdString();
            return false;
        }
        QFile::remove(from);
    }
    if (clipName) *clipName = name.toStdString();
    return true;
}

} // namespace lumenis::media

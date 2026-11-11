// Batch Import and Batch Re-import's clips and source files (batch_import.h; Editing Guide p.233-237, p.1097-1098).
#include "lumenis/media/batch_import.h"

#include "lumenis/media/link.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFileInfo>

namespace lumenis::media {

bool isReimportable(const model::BinItem& clip) {
    if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::AudioClip) return false;
    if (clip.fields.value(QStringLiteral("Source File")).isEmpty()) return false;
    const auto link = linkOf(clip);
    return !link || link->isManaged();
}

QString lastImportPath(const model::BinItem& clip) {
    const QString file = clip.fields.value(QStringLiteral("Source File"));
    if (file.isEmpty()) return {};
    const QString folder = clip.fields.value(QStringLiteral("Source Path"));
    return folder.isEmpty() ? file : QDir(folder).filePath(file);
}

QString relocatedSource(const QString& firstOld, const QString& firstNew, const QString& otherOld) {
    const QDir newFolder = QFileInfo(firstNew).absoluteDir();
    // In the same folder as the first clip's file.
    const QString beside = newFolder.filePath(QFileInfo(otherOld).fileName());
    if (QFileInfo(beside).isFile()) return QDir::cleanPath(beside);
    // In a folder that keeps the same relationship with the first clip's folder.
    const QDir oldFolder = QFileInfo(firstOld).absoluteDir();
    const QString relative = oldFolder.relativeFilePath(QFileInfo(otherOld).absoluteFilePath());
    const QString kept = QDir::cleanPath(newFolder.filePath(relative));
    return QFileInfo(kept).isFile() ? kept : QString();
}

bool isMixedRateSequence(const model::BinItem& sequence, const Rational& projectRate) {
    if (sequence.kind != model::BinItemKind::Sequence) return false;
    if (sequence.editRate.num() != 0 && sequence.editRate != projectRate) return true;
    const model::Sequence seq = model::Sequence::fromItem(sequence);
    for (const QString& name : seq.trackNames()) {
        const model::Track* track = seq.track(name);
        if (!track) continue;
        for (const model::Segment& s : track->segments) {
            if (s.adapter) return true;
        }
    }
    return false;
}

} // namespace lumenis::media

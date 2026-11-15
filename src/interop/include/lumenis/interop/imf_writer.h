#pragma once

#include "lumenis/interop/imf.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

// The documents of an IMF package written (11.5c2; docs/spec/imf.md, "Export"): the composition playlist (ST 2067-3,
// the 2016 namespaces), the packing list (ST 2067-2) and the asset map (ST 429-9), from the same structures
// interop::imf reads, so what is written reads back the same. Ours: not validated against SMPTE's schemas or a checker
// (Photon); on the watch list.
namespace lumenis::interop::imf {

// The composition playlist of `c` (its segments, sequences and resources).
QByteArray compositionXml(const Composition& c, const QDateTime& issueDate);
// The packing list of `p`: each asset's id, SHA-1 hash in base64, size, type and original file name.
QByteArray packingListXml(const PackingList& p, const QDateTime& issueDate);
// The asset map of a package: `assets` as (id, path relative to the root), `packingLists` the ids that are packing lists.
QByteArray assetMapXml(const QUuid& id, const QString& annotation, const QString& issuer, const QString& creator,
                       const QList<std::pair<QUuid, QString>>& assets, const QList<QUuid>& packingLists,
                       const QDateTime& issueDate);
// The SHA-1 of a file in base64 (the packing list's Hash); empty when it cannot be read.
QString fileHash(const QString& path);

// ---------------------------------------------------------------- a new package (the IMF window's Export)

// A track file written into the package's root: picture (one MainImageSequence) or sound (one MainAudioSequence,
// counted in samples at 48000 1).
struct TrackFile {
    QUuid id;
    QString path;              // absolute, inside the root
    bool picture = true;
    std::int64_t duration = 0; // in its edit rate: frames, or samples for sound
    Rational editRate{24};
};
// A composition to write: `playlist`'s metadata (id, Content Title, Content Kind, Issuer, Creator, Annotation, edit
// rate; its segments are made from `files`, one segment holding each track file whole).
struct NewComposition {
    Composition playlist;
    QList<TrackFile> files;
};
struct NewPackingList {
    QUuid id;
    QString annotation, issuer, creator;
    QList<NewComposition> compositions;
};
// An IMF Original to write: the root folder (made already, holding the track files) and its metadata.
struct NewPackage {
    QString root;
    QUuid assetMapId;
    QString annotation, issuer, creator;
    QList<NewPackingList> imps;
    QDateTime issueDate; // invalid: now
};
// Writes each composition as CPL_<id>.xml, each packing list as PKL_<id>.xml (its CPLs and their track files, hashed)
// and ASSETMAP.xml in `p.root`. False, with `error`, when a file cannot be read or written. The ids left null are made.
bool writePackageDocuments(NewPackage& p, QString* error = nullptr);

} // namespace lumenis::interop::imf

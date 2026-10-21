#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/model/bin.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Bins of the native model out as Media Composer bins (.avb) (Phase 3.4b, docs/spec/avb.md,
// "Export"): master and audio clips with their source chains, subclips and sequences, laid out as
// Media Composer's own bins lay them out, so that the import (avb_import.h) and pyavb read them back
// as the same bin. The objects are built with avb::Builder (avb_writer.h). The export is the
// import's inverse: what the import reads, this writes; what it does not read is not written.
namespace lumenis::interop::avb {

// How an export is written.
struct ExportOptions {
    QString productName;    // the creator in the header; the product's name when empty
    QString productVersion; // the product's version when empty
    QDateTime when;         // the last save and the dates of items without one; now when invalid
    bool littleEndian = true; // every Media Composer bin seen is little-endian
    // The Export setting's Include All Video/Data Tracks in Sequence and Include All Audio Tracks in
    // Sequence (p.1175): sequences' tracks of that kind are left out when off.
    bool includeVideo = true;
    bool includeAudio = true;
    // The clips the items refer to but do not include: a sequence's clips in other bins (by the
    // segment's MobID and clip id), a subclip's master (by MobID; the clip id is then empty). Each
    // found is written as a reference clip, listed in the bin but not placed by the user, as Media
    // Composer's bins hold the clips their sequences use (p.288, Show reference clips). Unset, or
    // nullopt: the segment names the clip by MobID and is offline where the bin is read.
    std::function<std::optional<model::BinItem>(const MobId& mob, const QString& clipId)> resolve;
};

// What an export did.
struct ExportResult {
    bool ok = false;      // whether the bin was made (and written, for exportBin)
    QString error;        // why not
    QStringList warnings; // what was left out or changed, once each, with a count when more than one
    int referenceClips = 0; // clips added through ExportOptions::resolve
};

// The bin's bytes: its items in its order, its custom columns (_USER), and its Text view's columns
// as the bin view (BVst). Empty, with result->error set, when it cannot be written.
QByteArray exportBytes(const model::Bin& bin, const ExportOptions& options = {}, ExportResult* result = nullptr);
// exportBytes() written to `path` (through QSaveFile).
ExportResult exportBin(const model::Bin& bin, const QString& path, const ExportOptions& options = {});

} // namespace lumenis::interop::avb

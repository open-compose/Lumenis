#pragma once

#include "lumenis/interop/aaf_essence.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Bins into AAF (docs/spec/aaf.md, "Export"): master clips with their
// source chains, subclips and sequences, laid out as Media Composer lays
// out its own exports, so that its AAF import reads them.
namespace lumenis::interop::aaf {

// How an export is written.
struct ExportOptions {
    QString productName;    // the product's name when empty
    QString productVersion; // the product's version when empty
    QDateTime when; // the file's dates; now when invalid
    // The Export setting's Include All Video/Data Tracks in Sequence and
    // Include All Audio Tracks in Sequence (p.1175): sequences' tracks of
    // that kind are left out when off.
    bool includeVideo = true;
    bool includeAudio = true;
    // Media Destinations' Embedded in AAF (p.871, p.1176): the media of a clip's File source to embed, or nothing
    // to link to it as usual. The essence names its bytes in a file (Essence::file: interleaved little-endian PCM,
    // or DNx frames one after another), copied into the AAF file as it is written; its kind, sound fields or DNx
    // header, rate and length describe it. Unset: nothing is embedded.
    std::function<std::optional<Essence>(const model::BinItem& clip, const model::ClipSource& file)> embed;
};

// What an export did.
struct ExportResult {
    // Whether the file was written; `error` says why not.
    bool ok = false;
    QString error;
    QStringList warnings; // what was left out, once each
};

// Writes `items` to `path`. A sequence's segments name their clips by
// MobID; clips that are not among the items are still named, and are
// offline where the file is read.
ExportResult exportItems(const QList<model::BinItem>& items, const QString& path, const ExportOptions& options = {});

} // namespace lumenis::interop::aaf

#pragma once

#include "lumenis/interop/aaf.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// AAF into bins (docs/spec/aaf.md, "Import"): master clips with their
// source chains, subclips and sequences, as Media Composer exports them.
namespace lumenis::interop::aaf {

struct ImportOptions {
    // The bin the items go into: the sequences' segments name it.
    QString binId;
    // Created and Modified for mobs that carry no times.
    QDateTime when;
};

struct ImportResult {
    QList<model::BinItem> items;
    // Headings of the clips' user comments that are not standard bin
    // headings: the bin's custom columns (p.269).
    QStringList customColumns;
    // What was read only in part, once each ("3 effects named ...").
    QStringList warnings;
};

ImportResult importMobs(const File& file, const ImportOptions& options = {});
// Opens the file and imports it; nothing, with `error` set, when it cannot
// be read.
std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options = {}, QString* error = nullptr);

} // namespace lumenis::interop::aaf

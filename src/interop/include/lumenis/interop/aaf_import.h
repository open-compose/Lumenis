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

// How the import fills in what the file does not say.
struct ImportOptions {
    // The bin the items go into: the sequences' segments name it.
    QString binId;
    // Created and Modified for mobs that carry no times.
    QDateTime when;
};

// What an import makes.
struct ImportResult {
    // Master and audio clips first, then subclips, then sequences, each in the file's mob order.
    QList<model::BinItem> items;
    // Headings of the clips' user comments that are not standard bin
    // headings: the bin's custom columns (p.269).
    QStringList customColumns;
    // What was read only in part, once each ("3 effects named ...").
    QStringList warnings;
};

// Reads the file's mobs into bin items (Created and Modified default to now when `options.when` is not
// valid).
// Throws std::overflow_error when the file's rates or lengths do not fit a Rational (a damaged
// file); importFile() turns that into an error.
ImportResult importMobs(const File& file, const ImportOptions& options = {});
// Opens the file and imports it; nothing, with `error` set, when it cannot
// be read.
std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options = {}, QString* error = nullptr);

} // namespace lumenis::interop::aaf

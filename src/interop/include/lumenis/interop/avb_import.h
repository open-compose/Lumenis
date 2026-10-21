#pragma once

#include "lumenis/interop/avb.h"
#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Media Composer bins (.avb) into bins of the native model (Phase 3.3b, docs/spec/avb.md, "Import"):
// master and audio clips with their source chains, subclips and sequences, read from the objects
// avb::File reads (avb.h). The same material read from a bin and from an AAF export of it
// (aaf_import.h) makes the same bin items.
namespace lumenis::interop::avb {

// How the import fills in what the bin does not say.
struct ImportOptions {
    // The bin the items go into: the sequences' segments name it (Segment::binId).
    QString binId;
    // Created and Modified for mobs that carry no times; now when not valid.
    QDateTime when;
};

// What an import makes.
struct ImportResult {
    // Master and audio clips first, then subclips, then sequences, each in the bin's item order.
    // Items the bin holds only because placed items refer to them have userPlaced false.
    QList<model::BinItem> items;
    // The clips' user columns (_USER) that are not standard bin headings: the bin's custom
    // columns (p.269).
    QStringList customColumns;
    // What was read only in part, once each, with a count when more than one ("3 × ...").
    QStringList warnings;
};

// Reads the bin's mobs into bin items. A file whose root is not a bin gives no items and a
// warning. Throws std::overflow_error when the bin's rates or lengths do not fit a Rational (a
// damaged bin); importFile() turns that into an error.
ImportResult importBin(const File& file, const ImportOptions& options = {});
// Opens the bin and imports it; nothing, with `error` set, when it cannot be read or its root is
// not a bin.
std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options = {}, QString* error = nullptr);

} // namespace lumenis::interop::avb

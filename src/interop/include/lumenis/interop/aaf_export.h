#pragma once

#include "lumenis/model/bin.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// Bins into AAF (docs/spec/aaf.md, "Export"): master clips with their
// source chains, subclips and sequences, laid out as Media Composer lays
// out its own exports, so that its AAF import reads them.
namespace lumenis::interop::aaf {

struct ExportOptions {
    QString productName;    // the product's name when empty
    QString productVersion; // the product's version when empty
    QDateTime when; // the file's dates; now when invalid
};

struct ExportResult {
    bool ok = false;
    QString error;
    QStringList warnings; // what was left out, once each
};

// Writes `items` to `path`. A sequence's segments name their clips by
// MobID; clips that are not among the items are still named, and are
// offline where the file is read.
ExportResult exportItems(const QList<model::BinItem>& items, const QString& path, const ExportOptions& options = {});

} // namespace lumenis::interop::aaf

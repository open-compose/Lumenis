#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QString>

namespace lumenis::model {

// The structure of bins and projects as Phase 3 gate 1 compares it:
// clips, their names, timecodes and custom columns, and the structure of
// sequences (docs/spec/structure.md). Any reader that fills the model (our
// bins, AAF, Media Composer's bins) can be dumped and diffed this way, with
// scripts/structdiff.py.
// One bin: {name, customColumns (sorted), items}. projectStructure() and structureOf() wrap bins in
// a document {format: "cutthulhu-structure", version: 1, bins[, project]}.
QJsonObject binStructure(const Bin& bin);
// Every bin of the project in `folder` (the one holding "<name>.cthproj"),
// depth first, without the Trash. Empty, with `error` set, when it cannot
// be read.
QJsonObject projectStructure(const QString& folder, const QString& name, QString* error = nullptr);
// Either: a bin file (.cthbin), a project file (.cthproj) or its folder.
QJsonObject structureOf(const QString& path, QString* error = nullptr);

} // namespace lumenis::model

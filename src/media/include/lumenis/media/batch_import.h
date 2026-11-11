#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"

#include <QString>

// Batch Import and Batch Re-import (9.2a; docs/spec/batch-import.md; Editing Guide p.233-237 "Reimporting Files" and
// "Batch Import Dialog Box", p.1097-1098 "Batch Importing File-Based Media at Different Resolutions"): the clips that
// can be imported again from their source files, where those files are, and the sequences that cannot be.
namespace lumenis::media {

// A master clip (or audio clip) that was imported, so it can be imported again: it names the file it came from (its
// Source File and Source Path columns) and its media are managed or missing. A clip linked to its file (p.315) has no
// imported media to replace.
bool isReimportable(const model::BinItem& clip);

// The file the clip was last imported from: Source Path / Source File. Empty when it names none.
QString lastImportPath(const model::BinItem& clip);

// Set File Location for several clips (p.235: "If you select more than one clip displayed in red, the system first
// attempts to find the rest of the clips in the same folder as the first clip and then in folders that maintain the
// same relationship with the first clip's folder"). The first clip's file was at `firstOld` and was found at `firstNew`;
// where another clip's file, last at `otherOld`, is now: in firstNew's folder by its name, or at the same path relative
// to firstNew's folder as otherOld is to firstOld's. Empty when neither exists.
QString relocatedSource(const QString& firstOld, const QString& firstNew, const QString& otherOld);

// Whether a sequence has material at another rate than `projectRate` (p.234: "You cannot reimport a mixed-rate sequence
// because you cannot batch import material in formats other than the project format"): its own edit rate differs, or a
// segment plays a clip through a motion adapter (a mixed rate clip, motion_adapter.h).
bool isMixedRateSequence(const model::BinItem& sequence, const Rational& projectRate);

} // namespace lumenis::media

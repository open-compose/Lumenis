#pragma once

#include "lumenis/model/bin.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

// How the objects in open bins refer to each other, for Bin > Select's Media Relatives, Sources and
// Unreferenced Clips (Editing Guide p.266-267; docs/spec/bin-select.md). A master clip (or audio clip)
// stands for its own media; a subclip refers to its master clip; a sequence refers to the clips of its
// segments, by MobID or by clip id. Results are item ids, keyed by the open bin they are in.
namespace lumenis::model {

struct OpenBin {
    QString id; // the bin's Sidebar id
    const Bin* bin = nullptr;
};

using BinSelection = QHash<QString, QStringList>; // bin id -> item ids

// "highlights all other clips linked to the selected clip, such as subclips or other sequences"
// (p.266): every object in the open bins that uses any of the media the selected objects use, the
// selected ones included.
BinSelection mediaRelatives(const QList<OpenBin>& bins, const BinSelection& selected);

// "identifies every master clip, subclip ... that is a source for that sequence" (p.267): the clips the
// selected objects use, directly (a sequence's segments) or through a subclip (its master clip). Tapes
// and media files are not bin objects here.
BinSelection sources(const QList<OpenBin>& bins, const BinSelection& selected);

// "all clips not currently referenced by clips or sequences that are in the open bins. Any master clips,
// subclips, or effect clips you edited into sequences in the bins do not highlight" (p.267).
BinSelection unreferencedClips(const QList<OpenBin>& bins);

} // namespace lumenis::model

#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>

// AutoSequence (Bin > AutoSequence, Clip > AutoSequence; Editing Guide p.499-500;
// docs/spec/autosequence.md): a sequence built from clips by their source timecode, "with filler
// where gaps in audio or video exist", or without filler in ascending timecode order (Alt). Clips with
// different Camera values go on separate tracks (p.499).
namespace lumenis::model {

struct AutoSequenceResult {
    Sequence sequence;
    std::int64_t start = 0; // the sequence's starting timecode, in frames: the earliest clip's
    QString name;           // "Synchronized sequences are named from the Tape Name column ... and the
                            // Source File column for file-based media" (p.499), without the number
    QStringList dropped;    // related clips left out (a subclip of another clip chosen)
    QString error;          // why there is no sequence
    bool ok() const { return error.isEmpty(); }
};

// The clips are master clips, subclips and audio clips (others are left out). `binId` is where they
// are, for the segments. With `noFiller` the clips of each track follow one another without gaps.
// "If you select two unrelated clips with overlapping timecodes, a message box indicates you cannot do
// this operation. If the clips are related (for example, one clip is a subclip of the other master
// clip), then one of the clips is selected automatically" (p.499): the master, or of two subclips of
// one master the longer.
AutoSequenceResult autoSequence(const QList<BinItem>& clips, const QString& binId, bool noFiller);

} // namespace lumenis::model

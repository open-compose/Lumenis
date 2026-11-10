#pragma once

#include "lumenis/core/project_format.h"
#include "lumenis/model/bin.h"

#include <QString>

#include <optional>

// Clip > Modify > Set Format on sequences (Editing Guide p.1294; 8.7c1, docs/spec/sequence-format.md): "When Media
// Composer creates a sequence, it uses the format of the current project ... you can change an NTSC 30i sequence to
// 1080i/59.94 or to 720p/59.94. In other cases, you do change the edit rate ... In cases where timecode needs to be
// converted, Media Composer creates a new sequence and unlinks the media. No media is converted." The Format column
// shows the result.
namespace lumenis::model {

// What Set Format made: the sequence modified in place, or (`duplicated`) a new sequence at the new edit rate.
struct SequenceFormatChange {
    BinItem item;
    bool duplicated = false;
    QString original; // the sequence's name before
};

// The sequence in `format`.
// * The same edit rate: the sequence itself with its Format column set to the format's label.
// * Another edit rate: a new sequence (a new id and MobID) named "<name> <rate> fps" ("Media Composer creates a new
//   sequence labeled with the extension 23.976 fps", p.1294), every segment's length and source frame and every marker
//   moved to the same time at the new rate, and its media unlinked: "Media for the new ... sequences and clips is offline.
//   Sequences and clips cannot be linked to the original ... media" (p.1294); segments keep their clip names, tracks and
//   source frames for batch capture or relinking.
// Nothing, with `error`, for an item that is not a sequence.
std::optional<SequenceFormatChange> setSequenceFormat(const BinItem& sequence, const ProjectFormat& format,
                                                      QString* error = nullptr);

// A frame count at `from` as the frame count lasting as long at `to`, rounded to the nearest frame.
std::int64_t convertFrames(std::int64_t frames, const Rational& from, const Rational& to);

} // namespace lumenis::model

#pragma once

#include "lumenis/model/sequence.h"

#include <QList>
#include <QSet>
#include <QString>

#include <cstdint>

// Sync breaks and duplicate frames in a sequence (Editing Guide p.490-492, p.641-642;
// docs/spec/sync-dupes.md): what the Timeline's Sync Breaks and Dupe Detection options show. Only
// the sequence is looked at; the Timeline draws the results (timeline.cpp).
namespace lumenis::model {

// A run of frames of a segment out of sync with material of the same clip on a track of the other
// kind: "white numbers indicating negative or positive offset values relative to zero" (p.491).
struct SyncBreak {
    QString track;
    std::int64_t start = 0;  // record frames: where the out-of-sync run starts
    std::int64_t length = 0;
    // Source frames by which this track's material is ahead of its partner's: positive when it shows
    // a later part of the clip than the partner at the same record frame.
    std::int64_t offset = 0;
};

// "The Sync Breaks feature applies only to master clips in which audio and video tracks were
// captured simultaneously, to autosynced subclips, or to any other subclip with video and audio
// tracks" (p.491). Ours: a segment's partners are the segments of the same clip on tracks of the
// other kind (video or audio) that overlap it in the record. Where one runs in sync with it (the
// same source frame at the same record frame) there is no break; where only out-of-sync partners
// overlap, the break's offset is taken from the partner on the first track (V1 before V2, A1 before
// A2). Data tracks and filler have no sync. Both sides of a break are reported.
QList<SyncBreak> syncBreaks(const Sequence& sequence);

// Frames of a segment that repeat source frames used elsewhere in the sequence (Dupe Detection,
// p.641): "each set of duplicate frames is tagged with a different color ... Matching frames have
// matching colors". `set` counts from 0 in order of the sets' first record frames.
struct Dupe {
    QString track;
    std::int64_t start = 0; // record frames
    std::int64_t length = 0;
    int set = 0;
};

// Dupes among the segments on tracks of `kinds` ("You can choose to turn on Dupe Detection for Video,
// Audio, and Data tracks", p.641). Two segments duplicate each other where they use the same
// source track of the same clip and their source ranges overlap once `handles` frames are added at
// the beginning and the end of each ("Dupe Detection Handles", p.1220; a half frame counts: 0.5 is
// the typical 35mm safety frame, p.642). Each overlapping pair is a set; the frames marked are the
// segment's frames within the other's widened range, which takes the handles into the indicator
// (p.642).
QList<Dupe> dupes(const Sequence& sequence, const QSet<TrackKind>& kinds, double handles = 0);

// "Up to 10 color sets can be distinguished during a single detection process" (p.641): set n's
// colour ("#rrggbb"); orange first, then green (p.641), the rest ours. Sets past ten repeat them.
QString dupeColor(int set);

} // namespace lumenis::model

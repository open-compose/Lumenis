#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>

#include <optional>
#include <utility>

// Sync Clips (9.3; docs/spec/sync-clips.md; Editing Guide p.496-498, "Autosyncing Clips"): picture and sound captured
// apart made into one synced subclip ("Autosyncing creates a new subclip that displays sync breaks in the Timeline as
// though the audio and video were captured simultaneously", p.496). The guide's Clip > AutoSync, under our own name
// (Avid marks the name "autosync™", p.497; ROADMAP 9.8).
//
// Ours: the synced subclip is a Subclip item whose composition holds a sequence of the clips' segments, video from the
// video clip and sound from the audio clips, as a multigroup holds its sequence (group_clip.h). It plays, edits and
// matches frames as that sequence does (composedSequence()).
namespace lumenis::model {

struct SyncClipsOptions {
    // Film TC/Sound TC, Inpoints, Outpoints, Source Timecode (the default), Auxiliary TC1-TC5 or Waveform Analysis, with
    // the marks and the waveform analysis as Group Clips takes them (group_clip.h). Film TC lines up the video clip's
    // Film TC with the audio clips' Sound TC.
    GroupOptions sync = [] {
        GroupOptions o;
        o.sync = GroupSync::SourceTimecode;
        return o;
    }();
    // "Keep audio on clip with video": the video clip's audio tracks from first to last (1-based) are kept; nothing
    // (the default) leaves them out.
    std::optional<std::pair<int, int>> keepVideoAudio;
    // "Include audio from audio-only clips": only these audio tracks of the audio clips; nothing (the default) takes
    // them all (ours: the guide does not say what Off keeps; all of them).
    std::optional<std::pair<int, int>> audioOnlyTracks;
    // "Collapse Audio Tracks": tracks without sound are removed and the others move up.
    bool collapseAudio = false;
};

struct SyncClipsResult {
    QList<BinItem> subclips; // one for each video clip, in the order given
    QString error;           // why there is none
    bool ok() const { return error.isEmpty() && !subclips.isEmpty(); }
};

// Sync Clips on the selected `clips` of the bin `binId` (`bin` for names). The video clips are those with picture; the
// others with sound are the audio clips. Each video clip is synced with the audio clips whose material overlaps it,
// into a subclip "<video>.sync.n" (p.498: "named by default after the video clip with the file name extension
// .sync.n").
// * With timecode or the waveform, the subclip covers where the video clip and its audio clips overlap ("the later
//   starting timecode becomes the start of the new subclip"; "the longer clip is truncated to the length of the shorter
//   clip").
// * With In or Out points, the whole video clip ("When you autosync using this method, the whole clip is taken into the
//   subclip"), sound where the audio clips have it; one video clip only ("sync two clips at a time").
// "You can autosync audio clips with video clips only" (p.497): an error without both.
SyncClipsResult syncClips(const Bin& bin, const QString& binId, const QList<BinItem>& clips, const SyncClipsOptions& options);

// Whether an item is a synced subclip, and its sequence (as a Sequence item, as multigroupSequence gives one).
bool isSyncedSubclip(const BinItem& item);
std::optional<BinItem> syncedSequence(const BinItem& item);
// The sequence an item plays as: a sequence's or Title Effect clip's own, a synced subclip's; nothing for clips.
std::optional<Sequence> composedSequence(const BinItem& item);

} // namespace lumenis::model

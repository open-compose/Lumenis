#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/subcap.h"
#include "lumenis/model/transcript.h"
#include "lumenis/model/transcript_view.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>
#include <utility>

// Transcripts out of the application (Editing Guide p.450-453; 2026.8 guide p.455-456; docs/spec/transcripts.md, 8.2d):
// Export Transcript as a text file, captions for Create SubCap and Export SubCap, and the shared transcripts file of
// Share Transcripts and Manage Transcripts' Import.
namespace lumenis::model {

// Export Transcript (p.450): "a text file (.txt), containing timecode and unique speaker ID information ... Exported
// transcripts can also be imported back into Media Composer for use in the SubCap effect": ours is laid out as an Avid
// DS caption file (model::writeDsCaptions), one caption per shown line, its first row the speaker ("Speaker 1:"), so
// that the SubCap effect's Import Caption Data reads it. Frames are the clip's or sequence's, written as timecode in
// `format` from `startFrame`.
QString exportedTranscriptText(const ShownTranscript& shown, const TimecodeFormat& format, std::int64_t startFrame);

// Create SubCap and Export SubCap (p.452-453): the shown lines as captions (frames as shown), each from its first word to
// its last word's end and ending before the next one starts. With `maxCharacters` ("Limit characters per caption") a
// line is broken at words into rows of at most that many characters, two rows to a caption (ours). With `range` ("Use
// Marks") only [first, second) is captioned, captions cut to it.
struct TranscriptCaptionOptions {
    int maxCharacters = 0; // 0: no limit
    std::optional<std::pair<std::int64_t, std::int64_t>> range;
};
QList<Caption> transcriptCaptions(const ShownTranscript& shown, const TranscriptCaptionOptions& options = {});

// Create SubCap (p.452): a new video track above the others with a SubCap effect over the captions, subdivided at them
// (model::importCaptions). Returns the track's name; nothing, with `error`, when there is no caption or no track can be
// added.
std::optional<QString> createSubCapTrack(Sequence& sequence, const QList<Caption>& captions, QString* error = nullptr);

// Share Transcripts (p.451): the clips' transcripts in one file, with the project's Language Hint ("Project Transcript
// Language Hint settings need to match when exporting and importing"). Ours, JSON ("cutthulhu-transcripts", version 1;
// Avid's .avt is undocumented), suffix kSharedTranscriptsSuffix.
inline constexpr char kSharedTranscriptsSuffix[] = "cttr";
struct SharedTranscripts {
    QString languageHint;
    QList<Transcript> transcripts;
};
QByteArray writeSharedTranscripts(const SharedTranscripts& shared);
std::optional<SharedTranscripts> readSharedTranscripts(const QByteArray& data, QString* error = nullptr);

} // namespace lumenis::model

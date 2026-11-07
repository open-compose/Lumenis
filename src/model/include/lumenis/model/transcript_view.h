#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/transcript.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>

// What the Transcript Tool shows for a monitor's clip or sequence (Editing Guide p.442-450; docs/spec/transcripts.md,
// 8.2b): the words of the master clips' transcripts placed at the monitor's frames.
//
// "Only master clips are transcribed for use with PhraseFind AI and the Transcript Tool ... the Transcript Tool will
// always reference master clip transcripts" (p.448-449): a subclip shows its master clip's transcript from where it
// starts; a group shows its selected angle's; "Group clips, multigroup clips, and sequences will reference the
// transcript for the master clip associated with any selected audio tracks" (p.449).
namespace lumenis::model {

// A word where the monitor shows it: [frame, endFrame) in the clip's or sequence's frames.
struct ShownWord {
    QString text;
    std::int64_t frame = 0;
    std::int64_t endFrame = 0;
    QString mob;  // whose transcript it is from
    int line = 0; // the line and word in that transcript (for editing, 8.2b2)
    int word = 0;
};

struct ShownLine {
    QString speaker; // the Speaker ID's name ("Speaker 1" or the name given)
    int speakerId = 1;
    std::int64_t frame = 0; // its first word's
    QList<ShownWord> words;
    QString mob;
    int line = 0;
};

struct ShownTranscript {
    QList<ShownLine> lines;
    // Clips shown without a transcript ("Clip not transcribed", p.443), by name, and their master clips' MobIDs.
    QStringList notTranscribed;
    QStringList notTranscribedMobs;
    // The audio tracks the transcript is made from (the track button, p.449); empty for a master clip or subclip.
    QStringList tracks;
    // The tracks there are to choose from.
    QStringList availableTracks;
    // Each shown transcript's speakers, {Speaker ID, name as shown}, by MobID (Correct Speaker ID, p.445).
    QMap<QString, QList<std::pair<int, QString>>> speakers;
};

// A master clip's transcript by its MobID; nothing when it has none.
using TranscriptFor = std::function<std::optional<Transcript>(const QString& mob)>;
// A segment's clip by its bin and id (as model::GroupClipLookup); nothing when it cannot be found.
using ShownClipLookup = std::function<std::optional<BinItem>(const QString& binId, const QString& clipId)>;

// The transcript a master clip, subclip or group shows, at its frames.
ShownTranscript shownTranscript(const BinItem& item, const TranscriptFor& transcriptFor);
// A sequence's (or multigroup's) transcript from the segments on `tracks` (audio tracks; empty: all of them), in time
// order, a word shown when it starts within a segment.
ShownTranscript shownTranscript(const Sequence& sequence, const QStringList& tracks, const TranscriptFor& transcriptFor,
                                const ShownClipLookup& lookup);

// The master clip a clip's transcript comes from and the frame of the master clip at the clip's frame 0: itself for a
// master clip, its Clip source for a subclip. Nothing for other items.
std::optional<std::pair<QString, std::int64_t>> transcriptSource(const BinItem& clip);

// Improving Transcript Readability (2026.8 guide p.446): with `groupSpeakers` ("Group Speaker Lines") consecutive lines
// of one speaker are shown as one paragraph, broken again once a paragraph lasts `newLineAfter` frames ("New Line After",
// 30 seconds by default); without it the lines are as transcribed. A merged paragraph keeps its first line's mob and
// line; its words keep their own.
ShownTranscript groupedLines(const ShownTranscript& shown, bool groupSpeakers, std::int64_t newLineAfter);

// The index of the word at `frame` (the last one starting at or before it) as {line, word}; nothing before the first.
std::optional<std::pair<int, int>> wordAt(const ShownTranscript& shown, std::int64_t frame);

} // namespace lumenis::model

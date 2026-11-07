#pragma once

#include "lumenis/model/text_find.h"
#include "lumenis/model/transcript_view.h"

#include <QList>
#include <QString>

#include <cstdint>

// PhraseFind AI (Editing Guide p.436-441; docs/spec/transcripts.md, 8.2c): "searches and finds audible dialogue
// throughout bins in a single project based on indexed audio ... PhraseFind AI indexes and finds audio in master clips,
// subclips and group clips". Our index is the transcripts themselves (model::TranscriptStore): each clip's words as the
// clip shows them (model::shownTranscript), so a subclip finds what is said within it and a group what its selected
// angle says. The Find window (src/ui/find_window.*) lists the hits with their Score and Transcription.
namespace lumenis::model {

// A place in a clip where the dialogue matches.
struct PhraseHit {
    QString binId;
    QString binName;
    BinItem item;
    // "the Score column lists your results in order of importance (ranging from 100 to 50) ... the score with the higher
    // value is more probable the audio dialogue you are searching for" (p.438). Ours: 100 for the words in order, from 50
    // to 90 for a line holding half or more of them.
    int score = 0;
    std::int64_t frame = 0;    // the clip frame where the matched dialogue starts
    std::int64_t endFrame = 0; // and ends (exclusive)
    QString text;              // the Transcription column: the line it is in
};

// The items PhraseFind searches: master clips (with audio-only ones), subclips and groups.
bool phraseSearchable(const BinItem& item);

// The words of `text` as PhraseFind compares them: "case insensitive and ignores punctuation. Searching for 'heres to
// you mrs robinson' works the same as 'Here's to you, Mrs. Robinson.'" (p.441).
QStringList phraseWords(const QString& text);

// Every hit of `query` in the bins' searchable items whose transcripts `transcriptFor` gives, best first (then in bin
// order). The filters apply as in text find, with the columns "Score" and "Transcription" of the hit too.
QList<PhraseHit> findPhrase(const QList<FindBin>& bins, const TranscriptFor& transcriptFor, const QString& query,
                            const QList<FindFilter>& filters, const TextFindOptions& options);

// Where loading a hit puts the position and the IN mark: "the frame immediately before the dialogue starts" (p.437).
inline std::int64_t phraseMarkIn(const PhraseHit& hit) { return hit.frame > 0 ? hit.frame - 1 : 0; }

} // namespace lumenis::model

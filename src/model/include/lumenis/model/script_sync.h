#pragma once

// ScriptSync AI and Create Script from Clip (Editing Guide p.569-570, p.584-585; docs/spec/scriptsync.md, 8.3d).
//
// "ScriptSync AI uses the PhraseFind AI engine to analyze the audio portion of a clip and match it to lines of the script
// text" (p.584). Ours: the clip's transcript (8.2, model::ShownTranscript at the clip's frames) is aligned with the
// words of the take's script lines by their longest common sequence, compared as PhraseFind compares words (case and
// punctuation ignored, model::phraseWords); a line is marked at the frame of its first matched word when at least half
// its words (one at least) are matched.
//
// "With the 'Create Script from Clip' operation, transcripts are referenced to generate lined scripts" (p.569): a script
// of the clips' transcript lines, each clip a take over its own lines with every line marked.

#include "lumenis/model/script.h"
#include "lumenis/model/transcript_view.h"

#include <QList>
#include <QString>

#include <cstdint>

namespace lumenis::model {

// The ScriptSync AI dialog's options (p.585), the tracks aside.
struct ScriptSyncOptions {
    // "Skip lines that only contain CAPITAL letters ... Dramatic scripts often use all-capital letters to identify the
    // speaker or for scene descriptions."
    bool skipCapitals = false;
    // "Skip text in parentheses '()' or brackets '[]'".
    bool skipBracketed = false;
    // "Skip text before colon ':' ... the convention of placing a character's name before a colon".
    bool skipBeforeColon = false;
    // "Skip lines indented less than dialog ... type the number of characters that dialog is indented".
    bool skipIndented = false;
    int dialogIndent = 10; // ours: the box starts at 10
    // "Overwrite existing marks": else lines already marked keep their marks.
    bool overwrite = false;
    // "Sync between first and last mark": only the lines and frames between the take's first and last marks.
    bool betweenMarks = false;
};

// What of a script line is spoken under the options; empty for a skipped line.
QString spokenText(const QString& line, const ScriptSyncOptions& options);
// The Select Dialog button: how many characters a line is indented (tabs as 8 columns).
int indentOf(const QString& line);

// A transcript word at a clip frame.
struct SyncWord {
    QString text;
    std::int64_t frame = 0;
};
// The words of a clip's shown transcript in order.
QList<SyncWord> syncWords(const ShownTranscript& transcript);

// ScriptSync AI on one take: marks its lines from the clip's words. Returns how many marks were placed or moved.
int scriptSyncTake(Script& script, const QString& takeId, const QList<SyncWord>& words, const ScriptSyncOptions& options);

// Create Script from Clip (p.569-570): one take per clip, each over its transcript's lines ("Speaker: words", ours) in
// turn, a slate for each with every line marked at the line's first word. `clips` and `transcripts` pair up; clips
// without lines are left out. Empty lines between clips (ours).
Script scriptFromTranscripts(const QList<ScriptClip>& clips, const QList<ShownTranscript>& transcripts, const ScriptLayout& layout);

} // namespace lumenis::model

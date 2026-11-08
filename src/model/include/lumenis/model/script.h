#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

// Scripts for script-based editing (Editing Guide ch.18, p.566-590; docs/spec/scriptsync.md, 8.3): "You begin the script
// integration process by importing a script into a project. The script appears in a script bin, and opens in a Script
// window" (p.570). A script is lines of text (the imported file's rows) with scene and page numbers starting at lines,
// and slates of takes lined through it (8.3b).
// Ours: the script is kept in its bin item (BinItemKind::Script) under kScriptKey, so that it is saved, auto-saved and
// kept in the attic with its bin (p.570: "The Script window has the same auto-save functionality as bins ... saves
// Script window files in the project folder along with bins, and stores backup copies automatically in the Avid Attic
// folder"); Avid's .avc file is undocumented. Script marks come in 8.3c.
namespace lumenis::model {

inline constexpr char kScriptKey[] = "script";

// The Script Text Layout (p.571-572) and the font a script was made with (the Script setting, p.1217).
struct ScriptLayout {
    int leftMargin = 40;  // pixels; "The default is 40 pixels" (p.1217)
    int textWidth = 600;  // pixels, used with wordWrap (ours: the guide gives no default)
    bool wordWrap = true; // ours: on
    QString font;         // empty: the application's font
    int fontSize = 12;    // "The default is 12 points" (p.1217)
    bool operator==(const ScriptLayout&) const = default;
};

// Lines [first, last] of a take marked with a colour (p.582) or as off-screen dialog (p.581).
struct ScriptRange {
    int first = 0;
    int last = 0;
    QString color; // the colour; empty for an off-screen range
    bool operator==(const ScriptRange&) const = default;
};

// "Each vertical line drawn through the scene represents a single take" (p.567): a clip linked to lines [first, last].
struct ScriptTake {
    QString id;
    QString binId;   // where the clip was when it was linked (p.576)
    QString clipId;
    QString clipName;
    QString take;    // "Numbers in the Take column appear in the tabs for each take" (p.580)
    int first = 0;
    int last = 0;
    std::int64_t frame = 0; // the representative frame in the slate (p.581), a clip frame
    QList<ScriptRange> offscreen;
    QList<ScriptRange> colors;
    // Script marks (p.582-586): "When you place a mark in the script, an IN point also appears in the clip": the clip
    // frame synced to each marked line.
    QMap<int, std::int64_t> marks;
    bool operator==(const ScriptTake&) const = default;
};

// "Takes are organized into slates that display a representative frame and clip name for the take that is currently
// selected" (p.568). The slate stands at line `line` (its takes' first line unless moved alone, p.579) and `x` pixels
// to the right of where slates start (ours: halfway across the text column, lumenis/ui/script_window.h).
struct ScriptSlate {
    QString id;
    int line = 0;
    int x = 0;
    QList<ScriptTake> takes;
    int active = 0; // the take whose frame and name the slate shows
    bool operator==(const ScriptSlate&) const = default;
};

struct Script {
    QStringList lines;
    // "Each scene or page number continues throughout the script until you mark another line as the beginning of a
    // new scene or page" (p.574): the number by the line it starts at.
    QMap<int, QString> scenes;
    QMap<int, QString> pages;
    QList<ScriptSlate> slates;
    ScriptLayout layout;
    // Made by Create Script from Clip (p.569-570): "Just click a line of text to jump to its corresponding location".
    bool fromClips = false;

    // The scene or page a line is in; empty before the first.
    QString sceneAt(int line) const;
    QString pageAt(int line) const;
    bool operator==(const Script&) const = default;

    QJsonObject toJson() const;
    static Script fromJson(const QJsonObject& o);
};

// "The imported script must be in text format ... export the script from your word processor by using the 'Text Only
// with Line Breaks' option" (p.571): a line for each row of the text, line ends of any platform, tabs as 8 columns
// (ours).
Script scriptFromText(const QString& text, const ScriptLayout& layout = {});

// A script item and the script it holds.
bool isScript(const BinItem& item);
std::optional<Script> scriptOf(const BinItem& item);
void setScript(BinItem& item, const Script& script);
// A new script item named `name`.
BinItem scriptItem(const QString& name, const Script& script);

// Add Scene and Add Page (p.574): `number` begins at `line` (replacing any there); an empty number removes it.
void setScene(Script& script, int line, const QString& number);
void setPage(Script& script, int line, const QString& number);
// The Delete dialog's "Delete scene(s)" and "Delete page break(s)" (p.574) over lines [from, to]; how many went.
int deleteScenes(Script& script, int from, int to);
int deletePages(Script& script, int from, int to);

// Editing the text (p.573): lines [from, to) become `replacement` (rows split at line breaks). Scene and page numbers on
// the replaced lines move to the first new line (or are dropped with no lines left); later ones move with their lines.
void replaceLines(Script& script, int from, int to, const QStringList& replacement);

// Slates and takes (p.576-582). A clip to link: where it is, its name and Take column.
struct ScriptClip {
    QString binId;
    QString clipId;
    QString name;
    QString take;
};
// "Select the portion of the script that is covered by the first clip or clips ... and drag them to the highlighted text.
// A slate frame appears above the text, with one or more of the takes covering the scene as lines" (p.576): a new
// slate at `first` with a take for each clip over [first, last]. Returns its id; empty with no clips.
QString addSlate(Script& script, int first, int last, const QList<ScriptClip>& clips);
// "Drag the clip to the slate. The new take appears in the slate and is applied to the selected region" (p.580).
QString addTake(Script& script, const QString& slateId, int first, int last, const ScriptClip& clip);
// The slate holding a take, and the take; nullptr when there is none.
const ScriptSlate* slateOf(const Script& script, const QString& takeId);
const ScriptTake* takeOf(const Script& script, const QString& takeId);
// "Select Delete Takes ... The takes are deleted"; a slate left without takes goes too (p.579-580). How many went.
int deleteTakes(Script& script, const QStringList& takeIds);
// Moving a slate (p.579): by `lines` lines and `x` pixels; with `withTakes` (Ctrl+drag) "the slate and all its take lines
// ... to a new location", else only the slate ("The take lines remain fixed"). Lines are kept in the script.
void moveSlate(Script& script, const QString& slateId, int lines, int x, bool withTakes);
// "Click the end mark or beginning mark of a take, and drag it until you reach the correct line" (p.581).
void setTakeLines(Script& script, const QString& takeId, int first, int last);
// "Press the appropriate arrow keys or step keys ... to advance the footage displayed in the slate" (p.581): the takes'
// representative frames moved by `frames` (not before 0). The slate shows `takeId` from now on.
void stepTakeFrames(Script& script, const QStringList& takeIds, std::int64_t frames);
void setActiveTake(Script& script, const QString& takeId);
// "Click the Set Offscreen button ... You can switch the indicators on or off by clicking the button repeatedly"
// (p.581): over lines [first, last] of the takes, on unless the first take already has it there. Set Color the same
// with `color` (p.582). Return whether the indicators are now on.
bool toggleOffscreen(Script& script, const QStringList& takeIds, int first, int last);
bool toggleColor(Script& script, const QStringList& takeIds, int first, int last, const QString& color);
// Whether a take has an off-screen indicator, or a colour (which), on a line.
bool isOffscreen(const ScriptTake& take, int line);
QString colorAt(const ScriptTake& take, int line);
// Interpolate Position (p.577): "Media Composer matches the length of the take in the script to the length of the clip
// in the Source monitor": the clip frame for a point `fraction` (0-1) of the way down line `line` of the take, for a
// clip of `clipFrames` frames.
std::int64_t interpolatedFrame(const ScriptTake& take, int line, double fraction, std::int64_t clipFrames);

// Script marks (p.582-588). addScriptMark: line `line` of the take synced to clip frame `frame`; "Click a line that
// already contains a mark to replace the previous mark ... Click a line in the script before or after the range of the
// existing take line to add the mark and extend the take line to include the new line" (p.584). False without the take.
bool addScriptMark(Script& script, const QString& takeId, int line, std::int64_t frame);
// "When you move a script mark up or down, the mark in the source clip remains at the same frame but is resynced to a new
// line in the script" (p.586): kept within the take's lines; a mark already on `to` is replaced.
bool moveScriptMark(Script& script, const QString& takeId, int from, int to);
// Delete Mark(s) (p.586): the takes' marks on lines [first, last]; how many went.
int deleteScriptMarks(Script& script, const QStringList& takeIds, int first, int last);
int countScriptMarks(const Script& script, const QStringList& takeIds, int first, int last);
// Find Script (p.586): the lines of the take "that most closely matches the clip location" `frame`: from the last mark at
// or before it to the line before the next mark (the take's last line after the last mark); nothing before the first mark
// or without marks.
std::optional<std::pair<int, int>> scriptLinesAt(const ScriptTake& take, std::int64_t frame);
// The clip frames a script range covers (Splicing a Script Range, p.588): from the mark at or before `first` (or the first
// mark inside the range) to the frame before the first mark after `last` (or the clip's end, `clipFrames` - 1). Nothing
// when no mark is at or before the range's end.
std::optional<std::pair<std::int64_t, std::int64_t>> markedFrames(const ScriptTake& take, int first, int last,
                                                                  std::int64_t clipFrames);

// Find and Replace in the Script window (p.575-576).
struct ScriptMatch {
    int line = 0;
    int column = 0;
    int length = 0;
};
// The next match of `text` after (or with `backward` before) line `line`, column `column`, going round; nothing without
// one.
std::optional<ScriptMatch> findInScript(const Script& script, const QString& text, int line, int column, bool backward = false,
                                        bool ignoreCase = true);
// "replace all instances of the text": how many were replaced.
int replaceInScript(Script& script, const QString& text, const QString& replacement, bool ignoreCase = true);

} // namespace lumenis::model

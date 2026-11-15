#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/stereo_clip.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// Edit decision lists (Phase 3.6a, docs/spec/edl.md): CMX 3600 and Media Composer's File_129
// variant, read into a sequence of the native model and written from one. An EDL names its material
// by tape (reel) and timecode only, so the import makes one source item (BinItemKind::Source, a tape)
// for each reel, and the sequence's segments name those, as Media Composer's Import EDL makes a
// sequence that needs Decompose before it has master clips (Editing Guide p.951-952). Export is the
// List Tool's EDL output (p.947-949, p.958-960) for one sequence.
namespace lumenis::interop::edl {

// The EDL output types the List Tool offers that are written here (p.947: CMX_3600, CMX_DigitalCut,
// CMX_Transfer, Cuedos, File_16, File_32, File_129). They differ in how long a reel name may be:
// 8 characters in CMX 3600, 129 in File_129. The reader takes either.
enum class Format { Cmx3600, File129 };
// "CMX_3600", "File_129": the List Tool's names (p.947).
QString formatName(Format format);
std::optional<Format> formatFromName(const QString& name);
// The longest reel name the format holds.
int reelLength(Format format);

// The MobID of a tape named `reel`: the same for every import of the same name, so that a sequence
// imported again, or another EDL of the same tapes, names the same source items. A name-based UUID
// (model::derivedMobId) of "edl-reel:" and the name.
MobId tapeMobId(const QString& reel);

// How an import fills in what an EDL does not say.
struct ImportOptions {
    QString binId;  // the bin the items go into: the segments name it
    QDateTime when; // Created and Modified; now when invalid
    // The Frame Rate chosen in Import EDL's dialog (p.951). An EDL does not say it: unset, it is
    // guessed from FCM and the highest frame number (with a warning).
    std::optional<Rational> rate;
    // The sequence's name when the EDL has no TITLE (the file's base name, by importFile()).
    QString name;
};

// What an import makes.
struct ImportResult {
    // One source item for each reel, in the order they first appear, then the sequence.
    QList<model::BinItem> items;
    // What was read only in part or not at all, once each, with a count when more than one.
    QStringList warnings;
    // The sequence (the last item); null when there is none.
    const model::BinItem* sequence() const;
};

// Reads an EDL's text. Never fails: lines that cannot be read are warnings, and an EDL without
// events makes an empty sequence.
ImportResult importText(const QString& text, const ImportOptions& options = {});
// Reads a file (UTF-8, else Latin-1). Nothing, with `error` set, when it cannot be read.
std::optional<ImportResult> importFile(const QString& path, const ImportOptions& options = {}, QString* error = nullptr);

// Drops the source items of `result` whose MobID one of `existing` already has, and points the
// sequence's segments at those instead: a reel imported before is the same source item.
void reuseSources(ImportResult& result, const QList<model::BinItem>& existing);

// How a sequence is written: the List Tool's Input, List Options and Formatting tabs (p.945-946,
// p.958-960). Defaults the guide does not give are ours (docs/spec/edl.md).
struct ExportOptions {
    Format format = Format::Cmx3600;
    // The Title field (p.945); the sequence's name when empty (p.948 step 3).
    QString title;
    // The tracks listed (Input tab, p.948 step 4): all when empty. An EDL holds one video track
    // (the first listed) and up to four audio tracks; the rest are left out with a warning.
    QStringList tracks;
    // List Options (p.958): Picture Dissolves (video tracks) and Sound Dissolves (audio tracks),
    // Black Edits; Clip Names and Markers. Transitions left out are cuts at their cut.
    bool pictureDissolves = true;
    bool soundDissolves = true;
    bool blackEdits = false;
    bool clipNames = true;
    bool markers = true;
    // Formatting (p.958-960): Optimize EDL (one event for tracks that share their edit), Starting
    // Event Number, and Tape or File Name truncation (End: the end of a long reel name is cut).
    bool optimize = false;
    int startEvent = 1;
    // "an EDL produced by Media Composer contains text that is capitalized" (p.950); off keeps the
    // case, as toupper_conversion FALSE does.
    bool upperCase = true;
    // The clip a segment uses (by its MobID and clip id), for its reel and start timecode; unset or
    // nullopt: the segment's name is its reel and its IN point is counted from 00:00:00:00, with a
    // warning.
    std::function<std::optional<model::BinItem>(const MobId& mob, const QString& clipId)> resolve;
    // S3D Contributor (p.959): a stereoscopic clip's segment is listed as the contributing eye's master clip (its name,
    // reel and timecode, found through `resolve` by the eye's MobID and id; its name alone when it is gone).
    model::S3DContributor s3dContributor = model::S3DContributor::Monoscopic;

    // ---- 11.4c1: the rest of the List Options and Formatting that have a known form (docs/spec/edl.md, "Export").
    // List Options (p.958): Other Transitions, the transitions that are not dissolves (wipes as Wnnn, others as dissolves);
    // off, they are cuts. Source File Name, "* SOURCE FILE: <the clip's Source File>". Clip Comments, "* <the clip's
    // Comments>". Color Decision Lists, the clip's ASC_SOP and ASC_SAT columns as "*ASC_SOP (s s s) (o o o) (p p p)" and
    // "*ASC_SAT x". Other Segment Effects, "* <CATEGORY>, <NAME>" for each segment effect but motion effects (written
    // as M2). All Markers at End (p.954): the markers outside every event, at the end of the list.
    bool otherTransitions = true;
    bool sourceFileName = false;
    bool clipComments = false;
    bool colorDecisionLists = false;
    bool segmentEffects = false;
    bool allMarkersAtEnd = false;
    // Formatting (p.959-960). Sort Mode: A (Record In), B (Source, Record In), C (Source, Source In), D and E as B and C
    // with the effect events (transitions, motion effects) at the end, S (Source IN only).
    enum class SortMode { RecordIn, SourceRecordIn, SourceSourceIn, SourceRecordInEffectsAtEnd, SourceSourceInEffectsAtEnd, SourceStart };
    SortMode sortMode = SortMode::RecordIn;
    // Source TC: the column a clip's timecode comes from; empty for Start, else "Sound TC" or "Auxiliary TC1" to "TC5".
    QString sourceTimecode;
    // Reel ID Type: the column the reel comes from; empty for Tape (the reel as before), else "Soundroll", "Camroll",
    // "Labroll" or "Disk Label" (a clip without it falls back to its tape reel).
    QString reelColumn;
    // Tape or File Name truncation (p.960): End ("CHICAGO138101 becomes CHICAGO1"), Middle ("CHIC8101"), Beginning
    // ("GO138101") or First_N, which "keeps the first character and the last character and removes everything in between.
    // For example, CHICAGO138101 becomes CO138101": the first character and the end that fits.
    enum class Truncation { End, Middle, Beginning, FirstN };
    Truncation truncation = Truncation::End;
    // Convert tape names to numbers (p.960): every reel (but BL and AX) as a number, 001 up in the order the reels are
    // first used (ours).
    bool tapeNumbers = false;
    // Starting TC (p.960): the record timecode the sequence starts at; empty for the sequence's Start.
    QString startingTimecode;
};

// What an export made.
struct ExportResult {
    bool ok = false;
    QString error;
    QString text;          // the list
    QStringList warnings;  // what was left out or changed, once each
    int events = 0;        // how many events
};

// The sequence as an EDL. Fails (ok false) only when the item is not a sequence.
ExportResult exportText(const model::BinItem& sequence, const ExportOptions& options = {});
// exportText() written to `path` (through QSaveFile).
ExportResult exportFile(const model::BinItem& sequence, const QString& path, const ExportOptions& options = {});

} // namespace lumenis::interop::edl

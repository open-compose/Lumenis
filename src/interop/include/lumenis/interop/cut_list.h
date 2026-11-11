#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/model/bin.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>

// Film cut lists (9.4a; docs/spec/cut-lists.md; Editing Guide p.939-944, p.960-966; FX Guide p.79, p.412): the List
// Tool's Cut List output, "breakdowns of exactly which frames should be cut from the work print or original negative"
// (p.939), read "by assistant editors or negative cutters". A cut list is made of the lists the List Options choose:
// the assemble list ("the heart of a cut list", p.941), the scene assemble list, the optical list, the dupe list and
// the pull list. Each event names its source by its frame reference numbers: key numbers (KN Start), else ink numbers
// (Ink Number), else timecode (Start; ours, for clips logged without film numbers). Written in the Columnar layout (a
// page of aligned columns), as TabbedLists (tab-separated, "that you can open in a separate application such as
// Microsoft Excel", p.958), as XML (every column heading and every column of the clips, p.957) or as WebLists (HTML
// pages linked between the lists, p.957-958; 9.4b). Change lists come later (ROADMAP 9.4c).
namespace lumenis::interop::cutlist {

enum class Format { Columnar, TabbedLists, WebLists, Xml };
// "Columnar", "TabbedLists", "WebLists", "XML": the List Tool's names (p.947).
QString formatName(Format format);
std::optional<Format> formatFromName(const QString& name);

// A key number or ink number: the part that names the roll ("KZ 12 3456", "023"), then the footage counted along it
// ("1234+05"). `frames` is the footage in frames at the counting's frames per foot.
struct FilmNumber {
    QString prefix;
    std::int64_t frames = 0;
};
// "KZ 12 3456 1234+05" (feet+frames last; a space or nothing before the feet) as a film number; nothing when the text
// does not end in feet+frames.
std::optional<FilmNumber> parseFilmNumber(const QString& text, int framesPerFoot);
// Feet+frames, the frames two digits: "1234+05". With `framesPerFoot` 0, a frame count.
QString footage(std::int64_t frames, int framesPerFoot);
// The film number moved by `offset` frames, written as "<prefix> <feet+frames>".
QString filmNumberText(const FilmNumber& number, std::int64_t offset, int framesPerFoot);
// The frames per foot of a project's film type (p.107: "16mm" 40, "35mm, 4 perf" 16); 0 (frames counted) for the
// others, "35mm, 3 perf" among them (ours: its feet do not hold a whole number of frames).
int framesPerFootOf(const QString& filmType);

// The pull list's sort fields (p.961): None, Labroll, Camroll, Soundroll, Scene and Take, Name and Reel #.
enum class SortField { None, Labroll, Camroll, Soundroll, SceneTake, Name, Reel };
QString sortFieldName(SortField field);

struct Options {
    Format format = Format::Columnar;
    QString title;
    // The tracks listed ("V1", "A1", ...); empty: every track.
    QStringList tracks;
    // The film counting: frames per foot (framesPerFootOf), and where the master footage starts ("Start at", p.964).
    int framesPerFoot = 16;
    std::int64_t startAt = 0;
    // The lists (p.960-962).
    bool assemble = true;
    bool sceneAssemble = false;
    bool optical = true;
    bool dupe = true;
    bool pull = false;
    // Assemble: "Mark Short Cuts" and "Mark Jump Cuts" with their minimums in frames.
    std::optional<int> shortCuts;
    std::optional<int> jumpCuts;
    // Optical: "Key Frames" (each keyframe of the effects), "Optical Footage" (footage from each optical's start).
    bool keyFrames = true;
    bool opticalFootage = false;
    // Dupe: "Assume Handles" in frames for the check; "Print w/Handles" frames added to the entries listed.
    int assumeHandles = 0;
    int printHandles = 0;
    // Pull: the sort and the order ("Heads Out (ascending edge numbers) Tails Out (descending edge numbers)").
    SortField firstSort = SortField::Camroll;
    SortField secondSort = SortField::None;
    bool headsOut = true;
    // Both Picture and Sound: Clip Names, Reel Names, Clip Comments (the segments' Clip Notes).
    bool clipNames = true;
    bool reelNames = true;
    bool clipComments = true;
    // The picture's aspect, for the previsualization markers' Effect Grid coordinates (FX Guide p.412).
    double aspect = 16.0 / 9.0;
    // A segment's clip, by its MobID and id.
    std::function<std::optional<model::BinItem>(const MobId& mob, const QString& clipId)> resolve;
};

struct Result {
    bool ok = false;
    QString error;
    QString text; // the list; for WebLists, the first list's page
    QStringList warnings;
    // WebLists: every file by its path relative to the folder they are saved in ("CutAsm.htm", "CutOpt.htm", ...,
    // "Files/cutlist.css").
    QMap<QString, QByteArray> files;
};
// Writes a WebLists result's files into `folder`. False, with the reason, when one cannot be written.
bool writeFiles(const Result& result, const QString& folder, QString* error = nullptr);

// The cut list of `sequence` as `options` say.
Result exportText(const model::BinItem& sequence, const Options& options);

} // namespace lumenis::interop::cutlist

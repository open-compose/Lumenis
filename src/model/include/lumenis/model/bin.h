#pragma once

#include "lumenis/core/mob_id.h"
#include "lumenis/core/rational.h"
#include "lumenis/core/timecode.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <optional>

namespace lumenis::model {

// Object types a bin holds (guide p.243-244, "Object Icons in Bins").
enum class BinItemKind {
    MasterClip,
    Subclip,
    AudioClip,
    Sequence,
    Source,
    Effect,
    MotionEffect,
    RenderedEffect,
    Group,
    Title,
    Script, // a Script window's script (8.3; Editing Guide p.570: "The script appears in a script bin")
};

// The kind's name in bin files ("masterClip", "subclip", ...); kindFromName() reads it back and
// gives nullopt for an unknown name.
QString kindName(BinItemKind kind);
std::optional<BinItemKind> kindFromName(const QString& name);

// One step of a clip's source chain (docs/spec/native-model.md, AAF's
// source mobs): what the clip's material came from.
struct ClipSource {
    enum class Kind {
        File,   // a media file of one track (AAF file source mob)
        Tape,   // the tape it was captured or imported from (tape source mob)
        Import, // the file it was imported or linked from (import source mob)
        Clip,   // another clip: a subclip's master clip
    };
    Kind kind = Kind::File;
    MobId mob;             // this source's MobID; a Clip source's is the clip's
    QString track;         // the clip's track it carries ("V1", "A2"); empty: all
    QString name;          // the tape's name, the file's name, the clip's name
    QString path;          // File and Import: where the file is
    QString startTimecode; // Tape and Import: their own start; Clip: where it starts in the master
    std::int64_t offset = 0; // Clip: the first frame used of the master
    MobId from;            // the source before this one (a file's tape or import)
    bool operator==(const ClipSource&) const = default;
};
// "file", "tape", "import" or "clip", as bin files and structure dumps write them.
QString sourceKindName(ClipSource::Kind kind);

// One object in a bin. Headings that only some objects have (Tape, Drive,
// custom columns, ...) live in `fields`, keyed by heading name.
struct BinItem {
    QString id; // stable, unique within the project
    // Its MobID in AAF and Media Composer's bins (docs/spec/native-model.md).
    // Null: derived from `id` (mobIdOf), as for clips made before MobIDs;
    // set when it came from elsewhere (AAF, a bin) or was given one.
    MobId mobId;
    BinItemKind kind = BinItemKind::MasterClip;
    // Shown in the Name column; need not be unique.
    QString name;
    // Creation Date and Modified Date; written to bin files in UTC.
    QDateTime created;
    QDateTime modified;
    // Length in frames at editRate. For a sequence, the end of its last track's material.
    std::int64_t durationFrames = 0;
    // Frames per second of the clip's timecode and durations.
    Rational editRate{24000, 1001};
    QString color; // "#rrggbb", empty for none (p.264)
    bool locked = false; // Clip > Lock Bin Selection (p.265)
    // False for an item the bin holds only because something placed in it refers to it (a
    // "reference clip", shown with Set Bin Display's Show reference clips, p.288): Media Composer's
    // bins mark these (pyavb's user_placed), ours made here are all placed.
    bool userPlaced = true;
    QString script;      // the Script view's text box (p.253)
    // A master clip's or subclip's own gain in dB (Clip > Audio > Apply Gain, p.216) and pan (Center
    // Pan, p.714; -100 to 100), heard when it plays and copied into the segments edited from it
    // (p.711). Use model::clipGain/clipPan (clip_audio.h).
    double audioGain = 0.0;
    std::optional<int> audioPan;
    // Heading name -> cell text, e.g. "Start" and "End" as timecode text, "Tracks", "Tape", custom
    // columns. Name, Duration, the dates, Lock and Color are shown from the members instead.
    QHash<QString, QString> fields;
    // A sequence's tracks (lumenis/model/sequence.h); an AudioSuite effect template's effect
    // (lumenis/model/audio_suite.h).
    QJsonObject composition;
    QJsonArray markers;      // lumenis/model/marker.h
    // A linked clip's media file (Editing Guide ch.11): where it is and how
    // its frames map onto the clip's (lumenis/media/link.h). Empty for clips
    // without media.
    QJsonObject link;
    // Its source chain: a file source per track, then the tape or import
    // before them; a subclip's master clip. Empty for clips made before it.
    QList<ClipSource> sources;
};

// How a bin is shown (p.245-254). Saved with the bin.
// Text (columns), Frame (pictures) or Script (pictures with a text box).
enum class BinViewMode { Text, Frame, Script };

// Bin > Sort and Sort Again (p.247): headings, highest priority first.
// One sort key: a heading and its direction.
struct BinSortKey {
    QString heading;
    bool ascending = true;
    bool operator==(const BinSortKey&) const = default;
};

// One row of the Custom Sift dialog (p.288-289). The guide names only the
// "Contains" criterion. An empty column means "Any".
struct SiftRow {
    QString text;
    QString column;
    bool operator==(const SiftRow&) const = default;
};

// Bin > Sift Bin Contents and Sift Selected Items (p.288-289).
struct BinSift {
    QList<SiftRow> find; // "Find clips that meet these criteria": all of them
    QList<SiftRow> also; // "Also find clips that meet these criteria"
    QStringList ids;     // Sift Selected Items
    bool isEmpty() const;
    bool operator==(const BinSift&) const = default;
};

// Snap to Grid in Frame view (p.252).
// Disable, Temporary (while a key is held), Enable, or Enable without drawing the grid.
enum class SnapToGrid { Disable, Temporary, Enable, Invisible };

// Everything about how a bin is shown, saved in the bin file's "display" object.
struct BinDisplay {
    // "up to seven times" (p.250): eight frame sizes.
    static constexpr int kMaxFrameSize = 7;
    static constexpr int kDefaultFrameSize = 2;

    BinViewMode mode = BinViewMode::Text;
    QString viewName;          // the Bin View menu entry
    bool viewModified = false; // shown in italic as "<view>.1" (p.249)
    QStringList columns;       // Text view headings, left to right (Name is implied)
    QStringList customColumns; // user-made headings (p.269)
    // Text view column widths in pixels, by heading.
    QHash<QString, int> widths;
    QString background;                    // "#rrggbb", empty for the default (p.263)
    int frameSize = kDefaultFrameSize;     // Enlarge/Reduce Frame (p.250)
    // Keyed by BinItem::id.
    QHash<QString, QPoint> framePositions; // Frame view items placed by hand (p.250)
    SnapToGrid snap = SnapToGrid::Disable;
    QList<BinSortKey> sort; // the last sort, for Sort Again
    // The last sift's criteria, kept so that Show Sifted can show it again.
    BinSift sift;
    bool sifted = false;    // Show Sifted / Show Unsifted
    bool statusBar = false; // Show Status Bar (p.254)
    // Windows > Set Font (p.81-82): "Bin fonts are stored in each bin". Empty family and 0 size: the
    // application's.
    QString fontFamily;
    int fontSize = 0;
    // Set Bin Display (p.288): object types not shown. By default "all
    // existing media objects except source clips and rendered effects".
    QList<BinItemKind> hiddenKinds = defaultHiddenKinds();
    bool showUserClips = true;       // "Show clips created by user"
    bool showReferenceClips = false; // "Show reference clips"

    static QList<BinItemKind> defaultHiddenKinds() { return {BinItemKind::Source, BinItemKind::RenderedEffect}; }
};

// A bin: its objects and how it is displayed. Stored one per file.
struct Bin {
    QString name;
    QList<BinItem> items;
    BinDisplay display;
    // An Export Volume bin's volume (Phase 7.2d; lumenis/model/export_volume.h, docs/spec/as02.md); empty for an
    // ordinary bin.
    QJsonObject exportVolume;

    // The item with this BinItem::id, or null. The pointer is valid until `items` changes.
    const BinItem* item(const QString& id) const;
    BinItem* item(const QString& id);

    // Whether the bin is shown sifted ("(sifted)" after its name, p.289).
    bool isSiftShown() const { return display.sifted && !display.sift.isEmpty(); }

    // The default Text view headings: "Bin (basic default)" on p.248.
    static QStringList defaultColumns();
};

// Every standard heading (the table on p.274-278), Name first.
const QStringList& binHeadings();
// Headings that can be typed into (p.274-278, "Modifiable after capture
// with no restrictions"), besides custom columns.
bool isEditableHeading(const QString& heading);

// The Bin View menu's built-in views (p.248).
struct BinViewPreset {
    QString name;
    QStringList columns; // after Name
};
const QList<BinViewPreset>& builtInBinViews();

// A cell's text as the Text view shows it.
QString cellText(const BinItem& item, const QString& heading);
// Sort order for one heading: durations and dates by value, colours by hue,
// saturation and value (p.248), text naturally ("Clip 2" before "Clip 10").
int compareCells(const BinItem& a, const BinItem& b, const QString& heading);
// Bin > Sort (p.247). The bin's own order changes, so every view shows it
// (p.254). Stable: equal items keep their order.
void sortItems(Bin& bin, const QList<BinSortKey>& keys);
// Whether an item stays in the sifted bin. "Any" searches `columns`.
bool siftMatches(const BinItem& item, const BinSift& sift, const QStringList& columns);

// Timecode ranges a sift can search (p.290): explicit ones between two
// columns (Start to End Range), and implicit ones that run from a column for
// the clip's Duration (Sound TC Range). A range is offered when its columns
// are shown. Keycode and ink number ranges come with film support.
struct SiftRange {
    QString name;  // the Column or Range to Search menu item
    QString start; // heading
    QString end;   // heading, or empty: start + Duration
};
const QList<SiftRange>& siftRanges();
// The names of the ranges whose columns are all among `columns`.
QStringList siftRangesFor(const QStringList& columns);
// A timecode typed without colons or leading zeros ("3172000") or with
// them, as frames at `editRate`; nullopt if it is not a timecode.
std::optional<std::int64_t> timecodeFrames(const QString& text, const Rational& editRate);
// Whether an item's timecode is drop-frame: its Start is written with a ';'
// (as drop-frame timecode is, "01:00:00;00") and its edit rate allows it.
bool isDropFrame(const BinItem& item);
// An item's timecode format: its edit rate, drop-frame when isDropFrame().
TimecodeFormat timecodeFormatOf(const BinItem& item);
// Where an item's timecode starts, in frames: its Start column; else, for a sequence, the default
// start (kDefaultStartHours); else 0.
std::int64_t startFrameOf(const BinItem& item);

// Set Bin Display: the object types the dialog lists (p.243-244, p.288).
// The plural label of a kind in Set Bin Display ("Master Clips", ...), and every kind in its order.
QString kindLabel(BinItemKind kind);
const QList<BinItemKind>& allKinds();

// Clip colours (p.264). The guide does not list the palette: these names are
// ours, and a colour sift matches them (p.289).
struct ClipColor {
    QString name;
    QString hex;
};
const QList<ClipColor>& clipColors();
// The palette name for "#rrggbb" (any case), the hex text itself for a colour not in the palette,
// or empty for no colour.
QString clipColorName(const QString& hex);

// Bin files (".cthbin", JSON). Media Composer's .avb bins are read and
// written by the interchange layer (Phase 3), not here.
class BinFile {
public:
    static QString suffix() { return QStringLiteral(".cthbin"); }
    // The file is {format: "cutthulhu-bin", version, name, items: [...], display: {...}}. Version 2
    // adds MobIDs and source chains; version 1 files are read too (their MobIDs are derived from item
    // ids). read() fails on a newer version and skips items without an id or a known kind. write()
    // always writes the current version and replaces the file atomically.
    static std::optional<Bin> read(const QString& path, QString* error = nullptr);
    static bool write(const QString& path, const Bin& bin, QString* error = nullptr);
};

// The default starting timecode of a new sequence ("Default Starting TC",
// General settings, guide p.1191). The guide does not print the value; ours
// is one hour.
inline constexpr std::int64_t kDefaultStartHours = 1;

// A new, empty sequence named "Untitled Sequence.NN" (numbered after the
// highest such name already in the bin; p.457 and the picture on p.255).
// `start` is its starting timecode; empty, or not a timecode, means the
// default.
BinItem newSequence(const Bin& bin, const Rational& editRate, const QDateTime& when,
                    const QString& start = {});

// Edit > Duplicate (p.260): a copy named "<name>.Copy.NN".
BinItem duplicateItem(const Bin& bin, const BinItem& original, const QDateTime& when);
// A subclip of frames [in, out) of a master clip (p.407): named
// "<master>.Sub.NN", its Start and End moved by `in`, and a Clip source
// that names the master by its MobID, with `in` as the offset ("Subclips
// remain linked to the master clips from which they are created"). Its
// link is the master's; media::makeSubclip moves it to `in`. Empty when the
// range has no frames.
std::optional<BinItem> subclipItem(const Bin& bin, const BinItem& master, std::int64_t in, std::int64_t out,
                                   const QDateTime& when);

// A unique id for new objects.
QString newItemId();
// An item as bins keep it, and back (nothing for JSON that is not an item). Rendered effects keep
// their precompute clips this way (EffectRender::clip in sequence.h).
QJsonObject binItemToJson(const BinItem& item);
std::optional<BinItem> binItemFromJson(const QJsonObject& o);
// A clip's MobID: its own, else the one derived from its id.
MobId mobIdOf(const BinItem& item);
// The MobID for an item id that has none of its own: the same every time,
// and different for every id (a name-based UUID as the material number).
MobId derivedMobId(const QString& itemId);

// Durations as bins show them: leading zero fields are left out, e.g.
// "0:00", "1:11", "4:24:14", "1:00:00:00" (pictures on p.246-255). Hours
// do not wrap at 24 as timecode does: 25 hours is "25:00:00:00".
QString durationText(std::int64_t frames, const Rational& editRate);

} // namespace lumenis::model

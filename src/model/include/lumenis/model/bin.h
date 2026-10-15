#pragma once

#include "lumenis/core/rational.h"

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
};

QString kindName(BinItemKind kind);
std::optional<BinItemKind> kindFromName(const QString& name);

// One object in a bin. Headings that only some objects have (Tape, Drive,
// custom columns, ...) live in `fields`, keyed by heading name.
struct BinItem {
    QString id; // stable, unique within the project
    BinItemKind kind = BinItemKind::MasterClip;
    QString name;
    QDateTime created;
    QDateTime modified;
    std::int64_t durationFrames = 0;
    Rational editRate{24000, 1001};
    QString color; // "#rrggbb", empty for none (p.264)
    bool locked = false; // Clip > Lock Bin Selection (p.265)
    QString script;      // the Script view's text box (p.253)
    QHash<QString, QString> fields;
    QJsonObject composition; // a sequence's tracks (lumenis/model/sequence.h)
    QJsonArray markers;      // lumenis/model/marker.h
};

// How a bin is shown (p.245-254). Saved with the bin.
enum class BinViewMode { Text, Frame, Script };

// Bin > Sort and Sort Again (p.247): headings, highest priority first.
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
enum class SnapToGrid { Disable, Temporary, Enable, Invisible };

struct BinDisplay {
    // "up to seven times" (p.250): eight frame sizes.
    static constexpr int kMaxFrameSize = 7;
    static constexpr int kDefaultFrameSize = 2;

    BinViewMode mode = BinViewMode::Text;
    QString viewName;          // the Bin View menu entry
    bool viewModified = false; // shown in italic as "<view>.1" (p.249)
    QStringList columns;       // Text view headings, left to right (Name is implied)
    QStringList customColumns; // user-made headings (p.269)
    QHash<QString, int> widths;
    QString background;                    // "#rrggbb", empty for the default (p.263)
    int frameSize = kDefaultFrameSize;     // Enlarge/Reduce Frame (p.250)
    QHash<QString, QPoint> framePositions; // Frame view items placed by hand (p.250)
    SnapToGrid snap = SnapToGrid::Disable;
    QList<BinSortKey> sort; // the last sort, for Sort Again
    BinSift sift;
    bool sifted = false;    // Show Sifted / Show Unsifted
    bool statusBar = false; // Show Status Bar (p.254)
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
QStringList siftRangesFor(const QStringList& columns);
// A timecode typed without colons or leading zeros ("3172000") or with
// them, as frames at `editRate`; nullopt if it is not a timecode.
std::optional<std::int64_t> timecodeFrames(const QString& text, const Rational& editRate);

// Set Bin Display: the object types the dialog lists (p.243-244, p.288).
QString kindLabel(BinItemKind kind);
const QList<BinItemKind>& allKinds();

// Clip colours (p.264). The guide does not list the palette: these names are
// ours, and a colour sift matches them (p.289).
struct ClipColor {
    QString name;
    QString hex;
};
const QList<ClipColor>& clipColors();
QString clipColorName(const QString& hex);

// Bin files (".cthbin", JSON). Media Composer's .avb bins are read and
// written by the interchange layer (Phase 3), not here.
class BinFile {
public:
    static QString suffix() { return QStringLiteral(".cthbin"); }
    static std::optional<Bin> read(const QString& path, QString* error = nullptr);
    static bool write(const QString& path, const Bin& bin, QString* error = nullptr);
};

// The default starting timecode of a new sequence ("Default Starting TC",
// General settings, guide p.1191). The guide does not print the value; ours
// is one hour.
inline constexpr std::int64_t kDefaultStartHours = 1;

// A new, empty sequence named "Untitled Sequence.NN" (numbered after the
// highest such name already in the bin; p.457 and the picture on p.255).
BinItem newSequence(const Bin& bin, const Rational& editRate, const QDateTime& when);

// Edit > Duplicate (p.260): a copy named "<name>.Copy.NN".
BinItem duplicateItem(const Bin& bin, const BinItem& original, const QDateTime& when);

// A unique id for new objects.
QString newItemId();

// Durations as bins show them: leading zero fields are left out, e.g.
// "0:00", "1:11", "4:24:14", "1:00:00:00" (pictures on p.246-255).
QString durationText(std::int64_t frames, const Rational& editRate);

} // namespace lumenis::model

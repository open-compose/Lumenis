#pragma once

#include "lumenis/core/rational.h"

#include <QDateTime>
#include <QHash>
#include <QList>
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
    QHash<QString, QString> fields;
};

// How a bin is shown (p.245-254). Saved with the bin.
enum class BinViewMode { Text, Frame, Script };

struct BinDisplay {
    BinViewMode mode = BinViewMode::Text;
    QString viewName;          // the Bin View menu entry
    QStringList columns;       // Text view headings, left to right (Name is implied)
    QStringList customColumns; // user-made headings (p.269)
    QHash<QString, int> widths;
    QString background;        // "#rrggbb", empty for the default (p.263)
    int frameScale = 0;        // Enlarge/Reduce Frame steps, -7..7 (p.250)
    bool statusBar = false;    // Show Status Bar (p.254)
};

// A bin: its objects and how it is displayed. Stored one per file.
struct Bin {
    QString name;
    QList<BinItem> items;
    BinDisplay display;

    const BinItem* item(const QString& id) const;
    BinItem* item(const QString& id);

    // The default Text view headings: "Bin (basic default)" on p.248.
    static QStringList defaultColumns();
};

// Bin files (".cthbin", JSON). Media Composer's .avb bins are read and
// written by the interchange layer (Phase 3), not here.
class BinFile {
public:
    static QString suffix() { return QStringLiteral(".cthbin"); }
    static std::optional<Bin> read(const QString& path, QString* error = nullptr);
    static bool write(const QString& path, const Bin& bin, QString* error = nullptr);
};

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

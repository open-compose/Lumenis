#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/marker.h"

#include <QList>
#include <QString>

// Text find (Edit > Find; Editing Guide p.417, p.428-431; docs/spec/find.md): matching bin items and
// markers against the text typed in the Find window. Pure functions over bins; the window
// (src/ui/find_window.*) gathers the project's bins, open or not, and shows the results.
namespace lumenis::model {

struct TextFindOptions {
    bool ignoreCase = true;  // "Ignore Case" (p.430); ours: on by default
    bool wholeWords = false; // "Whole Words only" (p.430)
};

// Whether `text` contains `query` (whole words only: bounded by non-word characters). An empty query
// matches everything.
bool textMatches(const QString& text, const QString& query, const TextFindOptions& options);

// The columns an item has text in: every standard heading (binHeadings()) and its own fields (custom
// columns). "The system searches through all available columns in your bins including metadata
// columns, even if they are not visible in your current bin" (p.428).
QStringList searchColumns(const BinItem& item);

// A filter row of the Find window (p.430): a column and the text it must contain; an empty column is
// "Any Column".
struct FindFilter {
    QString column;
    QString text;
};

// Whether an item matches the query in any column and every filter.
bool itemMatches(const BinItem& item, const QString& query, const QList<FindFilter>& filters, const TextFindOptions& options);

// A bin to search: its id in the project, its name and its contents.
struct FindBin {
    QString id;
    QString name;
    Bin bin;
};

struct ItemFound {
    QString binId;
    QString binName;
    BinItem item;
};
// Clips and Sequences (p.429): the matching items of the bins, in bin order; effect templates and
// sources are left out (ours).
QList<ItemFound> findItems(const QList<FindBin>& bins, const QString& query, const QList<FindFilter>& filters,
                           const TextFindOptions& options);

struct MarkerFound {
    QString binId;
    QString binName;
    BinItem item;
    Marker marker;
};
// Markers (p.417): "text within the Marker Comment field across all sequences and master clips". The
// filters apply to the clip or sequence the marker is in.
QList<MarkerFound> findMarkers(const QList<FindBin>& bins, const QString& query, const QList<FindFilter>& filters,
                               const TextFindOptions& options);

} // namespace lumenis::model

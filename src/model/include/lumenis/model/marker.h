#pragma once

#include "lumenis/model/bin.h"

#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

namespace lumenis::model {

// A marker: an electronic bookmark on a frame of a clip or sequence, or on a
// span of frames (guide p.412-424).
struct Marker {
    // Unique within the item (newItemId()); markersOf() gives one to a stored marker that has none.
    QString id;
    QString user;    // "By default ... the user name logged onto your system" (p.413)
    QString name;
    QString color;   // a name from markerColors()
    // The marker's text, as typed in the Marker window.
    QString comment;
    QString track;   // "V1", "A2", or "TC1" for spanned markers in a sequence (p.415)
    std::int64_t frame = 0;  // from the clip's start
    std::int64_t length = 1; // more than one: a spanned marker (p.414)

    // end() is exclusive: the frame after the last one marked.
    bool isSpanned() const { return length > 1; }
    std::int64_t end() const { return frame + length; }
    bool operator==(const Marker&) const = default;
};

// The sixteen marker colours, highest priority first; Red is the highest
// (p.414). The guide names only Red and Blue: the rest are ours.
struct MarkerColor {
    QString name;
    std::uint32_t rgb = 0; // 0xRRGGBB
};
// The colours in priority order, and the RGB of a colour name (any case); an unknown name gives
// Red's.
const QList<MarkerColor>& markerColors();
std::uint32_t markerColor(const QString& name);

// The markers stored in a bin item, sorted by frame.
// Markers are kept as JSON objects in BinItem::markers (id, color, track, frame, and user, name,
// comment and length only when set). markersOf() reads them, clamping frame to >= 0 and length to
// >= 1; setMarkers() sorts and writes them back, replacing the item's list.
QList<Marker> markersOf(const BinItem& item);
void setMarkers(BinItem& item, QList<Marker> markers);

// Adds a marker; a marker already on that frame and track is replaced.
// Spanned markers cannot overlap (p.415): returns false then.
bool addMarker(BinItem& item, Marker marker);
// Remove or update the marker with the marker's id; false if there is none.
bool removeMarker(BinItem& item, const QString& id);
bool updateMarker(BinItem& item, const Marker& marker);

// Go to Previous/Next Marker (p.419).
// The frame of the nearest marker strictly before or after `frame` on any track, or nullopt.
std::optional<std::int64_t> previousMarker(const BinItem& item, std::int64_t frame);
std::optional<std::int64_t> nextMarker(const BinItem& item, std::int64_t frame);
// Mark Markers: the markers on either side of the position (p.419), as an
// IN and an OUT (the frame before the next marker).
// The marker before may be on `frame` itself. Nullopt unless there is a marker on both sides.
std::optional<std::pair<std::int64_t, std::int64_t>> markMarkers(const BinItem& item, std::int64_t frame);

} // namespace lumenis::model

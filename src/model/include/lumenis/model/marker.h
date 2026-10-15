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
    QString id;
    QString user;    // "By default ... the user name logged onto your system" (p.413)
    QString name;
    QString color;   // a name from markerColors()
    QString comment;
    QString track;   // "V1", "A2", or "TC1" for spanned markers in a sequence (p.415)
    std::int64_t frame = 0;  // from the clip's start
    std::int64_t length = 1; // more than one: a spanned marker (p.414)

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
const QList<MarkerColor>& markerColors();
std::uint32_t markerColor(const QString& name);

// The markers stored in a bin item, sorted by frame.
QList<Marker> markersOf(const BinItem& item);
void setMarkers(BinItem& item, QList<Marker> markers);

// Adds a marker; a marker already on that frame and track is replaced.
// Spanned markers cannot overlap (p.415): returns false then.
bool addMarker(BinItem& item, Marker marker);
bool removeMarker(BinItem& item, const QString& id);
bool updateMarker(BinItem& item, const Marker& marker);

// Go to Previous/Next Marker (p.419).
std::optional<std::int64_t> previousMarker(const BinItem& item, std::int64_t frame);
std::optional<std::int64_t> nextMarker(const BinItem& item, std::int64_t frame);
// Mark Markers: the markers on either side of the position (p.419), as an
// IN and an OUT (the frame before the next marker).
std::optional<std::pair<std::int64_t, std::int64_t>> markMarkers(const BinItem& item, std::int64_t frame);

} // namespace lumenis::model

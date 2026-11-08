#pragma once

#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QSize>
#include <QString>

#include <functional>
#include <optional>

// Reformatting and FrameFlex (Editing Guide p.475-478, p.527-535; docs/spec/frameflex.md, 8.5a): how a clip of another
// size or aspect ratio than the project fits the project frame. A clip's Reformat attribute (the "Reformatting Options"
// bin column) and its FrameFlex framing box (the Source Settings' FrameFlex tab, 8.5b) make a spatial adapter, which a
// segment takes when the clip is edited into a sequence; media::spatialAdaptedImage (lumenis/media/frame_flex.h) draws
// it.
namespace lumenis::model {

// The Reformatting Options column (p.277, p.475-477).
inline constexpr char kReformatHeading[] = "Reformatting Options";

// The Reformat attribute (p.476-477, p.534): Stretch, Pillarbox/Letterbox, Center Crop or Center Keep Size.
enum class Reformat { Stretch, Letterbox, CenterCrop, CenterKeepSize };
// The column's text: "Stretch", "Pillarbox/Letterbox", "Center Crop", "Center Keep Size".
QString reformatText(Reformat r);
// From the column's text, or the Link setting's keys ("stretch", "letterbox", "crop", "keepSize").
std::optional<Reformat> reformatFromText(const QString& text);
// The clip's attribute; nothing when its column is empty (ours: such a clip, made before 8.5a, shows pillarboxed or
// letterboxed as it did).
std::optional<Reformat> reformatOf(const BinItem& item);

// A FrameFlex framing box (p.529-531): the area of the image fit into the project frame. Sizes are in the image's own
// pixels (its Image Size), positions from the image's centre.
struct FrameFlex {
    // Image Size: how the media is interpreted (an empty size: its raster). A 4K clip transcoded to HD keeps its 4K
    // Image Size (p.529-530).
    QSize imageSize;
    // Pixel aspect ratio (1 = square) and Image aspect ratio (0: the Image Size's own), both changing how large the
    // image is shown; Frame aspect ratio (0: the image's) gives the framing box its shape.
    double pixelAspect = 1.0;
    double imageAspect = 0.0;
    double frameAspect = 0.0;
    // X, Y: the box's centre from the image's centre, in pixels; Size: the box's width over the image's (1 = all of
    // it); Z Rotation in degrees, anticlockwise.
    double x = 0.0;
    double y = 0.0;
    double size = 1.0;
    double rotation = 0.0;
    // Color: the box's outline in the viewers.
    QString color = QStringLiteral("#ffd200");

    // Whether the box is the whole image, unrotated ("Reset", p.530).
    bool isFullImage() const;
    QJsonObject toJson() const;
    static FrameFlex fromJson(const QJsonObject& json);
    bool operator==(const FrameFlex&) const = default;
};

// The clip's FrameFlex box, kept in its Source Settings (BinItem::sourceSettings "frameFlex"); nothing when it has none.
std::optional<FrameFlex> frameFlexOf(const BinItem& item);
// Sets (or, with nothing, removes) the clip's box.
void setFrameFlex(BinItem& item, const std::optional<FrameFlex>& flex);
// "Size matches project raster" (p.530): the box as large as the project frame is in the image's pixels.
FrameFlex sizeMatchingRaster(FrameFlex flex, QSize imageRaster, QSize projectRaster);

// What a segment shows of its clip's picture: the Reformat attribute and the FrameFlex box ("A Spatial Adapter effect is
// applied either when the clip is reframed, or reformatted to fit within the project frame size", p.528). `creative` is
// set once the box is changed in the sequence (8.5b: keyframes in the Effect Editor, p.533), so that only Refresh
// Sequence > All FrameFlex including creative framing (p.478) replaces it.
struct SpatialAdapter {
    Reformat reformat = Reformat::Letterbox;
    std::optional<FrameFlex> frameFlex;
    bool creative = false;
    QJsonObject toJson() const;
    static SpatialAdapter fromJson(const QJsonObject& json);
    bool operator==(const SpatialAdapter&) const = default;
};

// The adapter a segment from the clip takes: none when the clip has neither a Reformat attribute nor a box.
std::optional<SpatialAdapter> spatialAdapterOf(const BinItem& item);

// Clip > Refresh Sequence (p.478): segments whose clip's Reformat attribute or box changed take the clip's (Aspect Ratio
// and Reformatting Options); with `creative`, segments whose box was changed in the sequence too (All FrameFlex including
// creative framing). Returns how many segments changed. `resolve` finds a segment's clip.
class Sequence;
int refreshSpatialAdapters(Sequence& sequence, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve,
                           bool creative);

} // namespace lumenis::model

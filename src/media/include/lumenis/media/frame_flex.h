#pragma once

#include "lumenis/model/frame_flex.h"

#include <QImage>
#include <QList>
#include <QPolygonF>
#include <QSize>
#include <QTransform>

#include <optional>

// Drawing a spatial adapter (Editing Guide p.527-535; 8.5a, docs/spec/frameflex.md): the FrameFlex box cut from a
// clip's frame and fit into the project frame by the Reformat attribute. The Composer's frame source applies it to each
// clip frame before the compositor (MainWindow::fillComposition), and the Source monitor to a loaded clip.
namespace lumenis::media {

// The source frame's pixels -> the output's, for a frame of `source` pixels shown at `out` in a project of
// `projectRaster` pixels; the box, in output pixels, in `target`. Center Keep Size keeps one box pixel per project pixel.
QTransform spatialTransform(QSize source, const model::SpatialAdapter& adapter, QSize projectRaster, QSize out, QRectF* target = nullptr);
// The framing box's corners in the source frame's pixels (the Source Settings viewer draws them, 8.5b).
QPolygonF frameFlexBox(QSize source, const model::FrameFlex& flex);
// The frame as the segment shows it: `out` pixels, black outside the box's place.
QImage spatialAdaptedImage(const QImage& source, const model::SpatialAdapter& adapter, QSize projectRaster, QSize out);

// ---- The FrameFlex effect in the Effect Preview (p.533; 8.5b3)
// "The Record viewer becomes your workspace to adjust the framing parameters ... The record viewer displays the frame and
// the framing box": the whole image, fit into the project frame (Pillarbox/Letterbox, ours) with the box's Image Size and
// aspect ratios, and the box over it.
model::SpatialAdapter framingPreviewAdapter(const model::FrameFlex& flex);
// The box's corners (top left, top right, bottom right, bottom left) over that picture, `out` pixels in a project of
// `projectRaster`, for a source frame of `source` pixels.
QPolygonF framingOutline(QSize source, const model::FrameFlex& flex, QSize projectRaster, QSizeF out);
// The handles on it: "move" in the middle (dragging inside the box does the same), "corner1" to "corner4" ("drag the
// handles on the corners of the image to resize it", p.533) and "rotZ" above the top edge, drawn from its middle (ours, as
// the Source Settings' viewer's centre handlebar, p.532).
struct FramingHandle {
    QString id;
    QPointF at;
    std::optional<QPointF> from;
};
QList<FramingHandle> framingHandles(QSize source, const model::FrameFlex& flex, QSize projectRaster, QSizeF out);
// Dragging handle `id` of the box `start` from `from` to `to` (`out` pixels): the box that follows. A corner resizes about
// the centre, keeping the box's shape; "rotZ" turns it by the angle swept about its centre.
model::FrameFlex dragFraming(QSize source, const model::FrameFlex& start, const QString& id, QPointF from, QPointF to,
                             QSize projectRaster, QSizeF out);

} // namespace lumenis::media

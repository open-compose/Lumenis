#pragma once

#include "lumenis/model/frame_flex.h"

#include <QImage>
#include <QPolygonF>
#include <QSize>
#include <QTransform>

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

} // namespace lumenis::media

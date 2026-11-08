// The FrameFlex box and the Reformat attribute drawn (Editing Guide p.527-535; 8.5a, docs/spec/frameflex.md). Coordinates:
// the source frame's pixels are scaled to the Image Size, widened by the pixel aspect ratio (and the Image aspect ratio)
// into display units; the box is cut there around its centre and turned by its Z Rotation; the box then goes into the
// output by the Reformat attribute.
#include "lumenis/media/frame_flex.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

// The image in display units and the box there: its centre, size and the mapping from source pixels.
struct Geometry {
    QTransform toDisplay; // source pixels -> display units
    QPointF centre;       // the box's centre, display units
    QSizeF box;           // the box's size, display units
    double rotation = 0;
};

Geometry geometryOf(QSize source, const std::optional<model::FrameFlex>& flex) {
    Geometry g;
    const model::FrameFlex f = flex.value_or(model::FrameFlex{});
    const QSize image = f.imageSize.isEmpty() ? source : f.imageSize;
    const double sx = source.width() > 0 ? static_cast<double>(image.width()) / source.width() : 1.0;
    const double sy = source.height() > 0 ? static_cast<double>(image.height()) / source.height() : 1.0;
    // Display width: the pixels by the pixel aspect ratio, or as the Image aspect ratio says.
    double width = image.width() * f.pixelAspect;
    const double height = image.height();
    if (f.imageAspect > 0 && height > 0) width = height * f.imageAspect;
    const double px = image.width() > 0 ? width / image.width() : 1.0;
    g.toDisplay = QTransform::fromScale(sx * px, sy);
    g.centre = QPointF(width / 2 + f.x * px, height / 2 + f.y);
    const double shape = f.frameAspect > 0 ? f.frameAspect : (height > 0 ? width / height : 1.0);
    g.box = QSizeF(width * f.size, shape > 0 ? width * f.size / shape : height);
    g.rotation = f.rotation;
    return g;
}

} // namespace

QTransform spatialTransform(QSize source, const model::SpatialAdapter& adapter, QSize projectRaster, QSize out, QRectF* target) {
    const Geometry g = geometryOf(source, adapter.frameFlex);
    const double W = std::max(g.box.width(), 1e-6);
    const double H = std::max(g.box.height(), 1e-6);
    const double ow = out.width();
    const double oh = out.height();
    double kx = ow / W;
    double ky = oh / H;
    switch (adapter.reformat) {
    case model::Reformat::Stretch: break;
    case model::Reformat::Letterbox: kx = ky = std::min(ow / W, oh / H); break;
    case model::Reformat::CenterCrop: kx = ky = std::max(ow / W, oh / H); break;
    case model::Reformat::CenterKeepSize:
        // One box unit to one project pixel, the output being the project frame at its own size.
        kx = ky = projectRaster.width() > 0 ? ow / projectRaster.width() : 1.0;
        break;
    }
    if (target) *target = QRectF(ow / 2 - W * kx / 2, oh / 2 - H * ky / 2, W * kx, H * ky);
    // Qt applies the last operation first: source pixels -> display units -> about the box's centre -> unturned ->
    // scaled -> the output's centre.
    QTransform t;
    t.translate(ow / 2, oh / 2);
    t.scale(kx, ky);
    t.rotate(-g.rotation);
    t.translate(-g.centre.x(), -g.centre.y());
    return g.toDisplay * t;
}

QPolygonF frameFlexBox(QSize source, const model::FrameFlex& flex) {
    const Geometry g = geometryOf(source, flex);
    QTransform box;
    box.translate(g.centre.x(), g.centre.y());
    box.rotate(g.rotation);
    const QRectF r(-g.box.width() / 2, -g.box.height() / 2, g.box.width(), g.box.height());
    const QPolygonF display = box.map(QPolygonF(r));
    return g.toDisplay.inverted().map(display);
}

QImage spatialAdaptedImage(const QImage& source, const model::SpatialAdapter& adapter, QSize projectRaster, QSize out) {
    if (source.isNull() || out.isEmpty()) return source;
    QImage result(out, QImage::Format_RGB32);
    result.fill(Qt::black);
    QRectF target;
    const QTransform t = spatialTransform(source.size(), adapter, projectRaster, out, &target);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // "The application first crops out the unwanted area and then fits the image within the project frame size"
    // (p.535): only the box's place shows.
    p.setClipRect(target.intersected(QRectF(QPointF(0, 0), QSizeF(out))));
    p.setTransform(t);
    p.drawImage(QPointF(0, 0), source);
    return result;
}

} // namespace lumenis::media

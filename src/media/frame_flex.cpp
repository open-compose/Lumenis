// The FrameFlex box and the Reformat attribute drawn (Editing Guide p.527-535; 8.5a, docs/spec/frameflex.md). Coordinates:
// the source frame's pixels are scaled to the Image Size, widened by the pixel aspect ratio (and the Image aspect ratio)
// into display units; the box is cut there around its centre and turned by its Z Rotation; the box then goes into the
// output by the Reformat attribute.
#include "lumenis/media/frame_flex.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

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

model::SpatialAdapter framingPreviewAdapter(const model::FrameFlex& flex) {
    model::FrameFlex whole = flex;
    whole.x = 0;
    whole.y = 0;
    whole.size = 1;
    whole.rotation = 0;
    whole.frameAspect = 0;
    model::SpatialAdapter a;
    a.reformat = model::Reformat::Letterbox;
    a.frameFlex = whole;
    return a;
}

namespace {

QTransform previewTransform(QSize source, const model::FrameFlex& flex, QSize projectRaster, QSizeF out) {
    return spatialTransform(source, framingPreviewAdapter(flex), projectRaster, out.toSize());
}

QPointF centreOf(const QPolygonF& box) {
    QPointF c;
    const qsizetype n = std::min<qsizetype>(box.size(), 4);
    for (qsizetype i = 0; i < n; ++i) c += box.at(i);
    return n > 0 ? c / static_cast<double>(n) : c;
}

} // namespace

QPolygonF framingOutline(QSize source, const model::FrameFlex& flex, QSize projectRaster, QSizeF out) {
    if (source.isEmpty()) return {};
    const QPolygonF box = frameFlexBox(source, flex);
    const QTransform t = previewTransform(source, flex, projectRaster, out);
    QPolygonF shown;
    for (qsizetype i = 0; i < std::min<qsizetype>(box.size(), 4); ++i) shown << t.map(box.at(i));
    return shown;
}

QList<FramingHandle> framingHandles(QSize source, const model::FrameFlex& flex, QSize projectRaster, QSizeF out) {
    const QPolygonF shown = framingOutline(source, flex, projectRaster, out);
    if (shown.size() < 4) return {};
    QList<FramingHandle> handles;
    const QPointF centre = centreOf(shown);
    handles << FramingHandle{QStringLiteral("move"), centre, std::nullopt};
    for (int i = 0; i < 4; ++i) handles << FramingHandle{QStringLiteral("corner%1").arg(i + 1), shown.at(i), std::nullopt};
    // The rotation handle: out from the top edge's middle, 4% of the picture's width away.
    const QPointF top = (shown.at(0) + shown.at(1)) / 2;
    QPointF up = top - centre;
    const double length = std::hypot(up.x(), up.y());
    up = length > 1e-9 ? up / length : QPointF(0, -1);
    handles << FramingHandle{QStringLiteral("rotZ"), top + up * (out.width() * 0.04), top};
    return handles;
}

model::FrameFlex dragFraming(QSize source, const model::FrameFlex& start, const QString& id, QPointF from, QPointF to,
                             QSize projectRaster, QSizeF out) {
    if (source.isEmpty()) return start;
    bool invertible = false;
    const QTransform back = previewTransform(source, start, projectRaster, out).inverted(&invertible);
    if (!invertible) return start;
    const QPointF a = back.map(from);
    const QPointF b = back.map(to);
    const QSize image = start.imageSize.isEmpty() ? source : start.imageSize;
    const double sx = static_cast<double>(image.width()) / source.width();
    const double sy = static_cast<double>(image.height()) / source.height();
    const QPointF centre = centreOf(frameFlexBox(source, start));
    model::FrameFlex f = start;
    if (id == QLatin1String("move")) {
        f.x = start.x + (b.x() - a.x()) * sx;
        f.y = start.y + (b.y() - a.y()) * sy;
    } else if (id.startsWith(QLatin1String("corner"))) {
        const double before = std::hypot(a.x() - centre.x(), a.y() - centre.y());
        if (before > 1e-6) f.size = std::clamp(start.size * std::hypot(b.x() - centre.x(), b.y() - centre.y()) / before, 0.01, 10.0);
    } else if (id == QLatin1String("rotZ")) {
        // Screen y runs down, so an anticlockwise turn is a falling angle.
        const double a0 = std::atan2(a.y() - centre.y(), a.x() - centre.x());
        const double a1 = std::atan2(b.y() - centre.y(), b.x() - centre.x());
        const double degrees = start.rotation - (a1 - a0) * 180.0 / std::numbers::pi;
        f.rotation = std::fmod(degrees + 540.0, 360.0) - 180.0;
    }
    return f;
}

} // namespace lumenis::media

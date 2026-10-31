// The Pan and Scan effect (ROADMAP 6.7b1; FX Guide p.230-238, p.480, p.564; docs/spec/pan-scan.md): a Reformat
// effect that keeps the part of the picture inside a wire frame of the Target aspect ratio and fills the
// output with it. Like the other Reformat effects it works on its own segment and every track below it
// (compositor.cpp). The Effect Preview shows the original picture with the wire frame instead
// (Composition::panScanOriginal); direct manipulation of the wire frame is in effect_handles.cpp.
//
// Geometry (ours; the guide gives the ratios, not the arithmetic). The frame is taken to have square pixels,
// so its aspect ratio is its width over its height. The Source ratio says where the source picture lies in
// the frame: an anamorphic source fills it, squeezed; another is the largest centred rectangle of its ratio
// (letterboxed or pillarboxed). The wire frame has the Target's ratio as the source picture shows it: at
// Scaling 100 the largest such rectangle in the source picture, centred; Scaling shrinks or grows it about its
// centre and Position moves it, in pixels of a 1920-wide frame (Y down, as for Picture-in-Picture). The output
// is the wire frame's contents stretched over the Target picture: the whole frame for 16:9 anamorphic (squeezed
// when the frame is narrower), else the largest centred rectangle of the Target's ratio, with black around it.
#include "lumenis/media/compositor.h"

#include "effect_drawing.h"

#include <algorithm>
#include <cmath>

namespace lumenis::media {

namespace {

const QString kRatios = QStringLiteral("Pan and Scan Aspect Ratios and Actions Parameters");

// A menu entry's ratio and whether it is anamorphic (p.564).
std::pair<double, bool> ratioOf(const QString& entry) {
    if (entry.startsWith(QLatin1String("1.33"))) return {4.0 / 3.0, false};
    if (entry == QLatin1String("1.66")) return {1.66, false};
    if (entry.startsWith(QLatin1String("1.78"))) return {16.0 / 9.0, false};
    if (entry == QLatin1String("1.85")) return {1.85, false};
    if (entry == QLatin1String("2.35")) return {2.35, false};
    if (entry.startsWith(QLatin1String("15:9"))) return {15.0 / 9.0, true};
    if (entry.startsWith(QLatin1String("16:9"))) return {16.0 / 9.0, true};
    return {16.0 / 9.0, false};
}

QString entry(const model::Effect& e, const QString& name) {
    const QJsonValue v = e.parameters.value(kRatios).toObject().value(name);
    return v.isString() ? v.toString() : parameterDefault(e, kRatios, name).toString();
}

double number(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    return model::numberAt(e, category, name, time, span).value_or(parameterDefault(e, category, name).toDouble(0.0));
}

// The largest rectangle of display ratio `ratio` centred in a W x H frame of square pixels.
QRectF fitted(double ratio, double W, double H) {
    if (ratio >= W / H) {
        const double h = W / ratio;
        return {0.0, (H - h) / 2.0, W, h};
    }
    const double w = H * ratio;
    return {(W - w) / 2.0, 0.0, w, H};
}

} // namespace

bool isPanAndScan(const model::Effect& effect) {
    return effect.category == QLatin1String("Reformat") && effect.name == QLatin1String("Pan and Scan");
}

std::optional<PanScanFrames> panScanFrames(const model::Effect& e, double time, const model::EffectSpan& span, const QSizeF& size) {
    if (!isPanAndScan(e) || size.width() <= 0.0 || size.height() <= 0.0) return std::nullopt;
    const double W = size.width();
    const double H = size.height();
    const auto [rs, squeezedSource] = ratioOf(entry(e, QStringLiteral("Source")));
    const auto [rt, squeezedTarget] = ratioOf(entry(e, QStringLiteral("Target")));
    PanScanFrames f;
    f.source = squeezedSource ? QRectF(0.0, 0.0, W, H) : fitted(rs, W, H);
    f.target = squeezedTarget ? QRectF(0.0, 0.0, W, H) : fitted(rt, W, H);
    // The wire frame at Scaling 100, in the source picture's own proportions (height 1, width rs), then in
    // frame pixels.
    const double ww = rt <= rs ? rt : rs;
    const double wh = rt <= rs ? 1.0 : rs / rt;
    const double sx = std::max(number(e, QStringLiteral("Scaling"), QStringLiteral("X"), time, span), 0.0) / 100.0;
    const double sy = std::max(number(e, QStringLiteral("Scaling"), QStringLiteral("Y"), time, span), 0.0) / 100.0;
    const double w = ww / rs * f.source.width() * sx;
    const double h = wh * f.source.height() * sy;
    const QPointF centre = f.source.center() + QPointF(number(e, QStringLiteral("Position"), QStringLiteral("X"), time, span) * W / 1920.0,
                                                       number(e, QStringLiteral("Position"), QStringLiteral("Y"), time, span) * W / 1920.0);
    f.wire = QRectF(centre.x() - w / 2.0, centre.y() - h / 2.0, w, h);
    return f;
}

Picture panScanPicture(const model::Effect& e, const Picture& picture, double time, const model::EffectSpan& span) {
    if (picture.isNull()) return picture;
    const int width = picture.width();
    const int height = picture.height();
    const auto f = panScanFrames(e, time, span, QSizeF(width, height));
    if (!f || f->wire.width() <= 0.0 || f->wire.height() <= 0.0) return Picture::solid(width, height, Qt::black);
    Picture out = Picture::solid(width, height, Qt::black);
    float in[4];
    const detail::Border edge; // a pixel's soft edge at the Target picture's sides
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double a = (x + 0.5 - f->target.left()) / f->target.width();
            const double b = (y + 0.5 - f->target.top()) / f->target.height();
            detail::sample(picture, f->wire.left() + a * f->wire.width(), f->wire.top() + b * f->wire.height(), in);
            edge.draw(out.pixel(x, y),
                      detail::boxDistance(x + 0.5, y + 0.5, f->target.left(), f->target.top(), f->target.right(), f->target.bottom()), in);
        }
    }
    return out;
}

} // namespace lumenis::media

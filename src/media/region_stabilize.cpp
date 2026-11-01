// The Region Stabilize effect (ROADMAP 6.7d3; FX Guide p.279-281; docs/spec/tracking.md, "6.7d3"): the picture
// inside a region of interest is followed from frame to frame by the Correlation Tracker (tracking.h), and each frame
// is moved back by how far it went since the effect's first frame, on the axes the Model menu keeps steady, enlarged
// by Auto Zoom so that no edge shows. The motion is kept per segment and region, so a frame is only tracked once.
#include "lumenis/media/tracking.h"

#include "lumenis/model/effect_keyframes.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <mutex>
#include <vector>

namespace lumenis::media {

namespace {

const QString kRegion = QStringLiteral("Region of Interest");

double regionNumber(const model::Effect& e, const QString& name, double time, const model::EffectSpan& span) {
    return model::numberAt(e, kRegion, name, time, span).value_or(parameterDefault(e, kRegion, name).toDouble(0.0));
}

// The motion found so far for a segment and region, by frame of the effect (0 at its first).
std::mutex cacheMutex;
QHash<QString, std::vector<QPointF>>& motionCache() {
    static QHash<QString, std::vector<QPointF>> cache;
    return cache;
}

// What the motion depends on besides the segment's material: the region's values and keyframes.
QString regionKey(const model::Effect& e, const QString& material) {
    QJsonObject region;
    region.insert(QStringLiteral("values"), e.parameters.value(kRegion));
    for (const QString& name : {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Width"), QStringLiteral("Height")}) {
        region.insert(name, model::keyframesOf(e, kRegion, name).toJson());
    }
    return material + QLatin1Char('|') + QString::fromUtf8(QJsonDocument(region).toJson(QJsonDocument::Compact));
}

// The stabilizing move at `offset` (fractions of the frame): the motion undone on the Model's axes.
QPointF steadying(const model::Effect& e, const QPointF& motion) {
    const QString model = e.parameters.value(QStringLiteral("Model")).toObject().value(QStringLiteral("Model")).toString();
    // "Horizontal: Constrains horizontal motion in the region of interest. Features in the region of interest can
    // move vertically but not horizontally. Vertical: Constrains vertical motion" (p.280).
    if (model == QLatin1String("Horizontal")) return {-motion.x(), 0.0};
    if (model == QLatin1String("Vertical")) return {0.0, -motion.y()};
    return -motion; // Translational: "steady along both the horizontal and the vertical axes"
}

bool option(const model::Effect& e, const char* name) {
    return e.parameters.value(QStringLiteral("Options")).toObject().value(QLatin1String(name)).toBool();
}

} // namespace

bool isRegionStabilize(const model::Effect& effect) {
    return effect.category == QLatin1String("Image") && effect.name == QLatin1String("Region Stabilize");
}

QRectF regionOfInterest(const model::Effect& effect, double time, const model::EffectSpan& span) {
    const double w = std::clamp(regionNumber(effect, QStringLiteral("Width"), time, span), 1.0, 100.0) / 100.0;
    const double h = std::clamp(regionNumber(effect, QStringLiteral("Height"), time, span), 1.0, 100.0) / 100.0;
    const double cx = 0.5 + regionNumber(effect, QStringLiteral("X"), time, span) / 100.0;
    const double cy = 0.5 + regionNumber(effect, QStringLiteral("Y"), time, span) / 100.0;
    return {cx - w / 2.0, cy - h / 2.0, w, h};
}

QPointF regionMotion(const model::Effect& effect, const QString& material, std::int64_t offset, std::int64_t length, const TrackedFrame& frames) {
    if (offset <= 0 || length <= 1) return {};
    offset = std::min(offset, length - 1);
    const QString key = regionKey(effect, material);
    std::vector<QPointF> motion;
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        motion = motionCache().value(key);
    }
    if (motion.empty()) motion.push_back(QPointF());
    const model::EffectSpan span(length);
    // From the last frame known: the region's picture at the frame before, found again in this frame within a search
    // area of twice its size (ours), and its move added up.
    for (auto f = static_cast<std::int64_t>(motion.size()); f <= offset; ++f) {
        const QRectF region = regionOfInterest(effect, span.timeOf(static_cast<double>(f - 1)), span);
        const QRectF search = region.adjusted(-region.width() / 2.0, -region.height() / 2.0, region.width() / 2.0, region.height() / 2.0);
        const QList<model::TrackPoint> points = trackTarget(TrackingEngine::Correlation, frames, f - 1, f, region, search);
        QPointF step;
        if (points.size() >= 2) step = QPointF(points.last().x - points.first().x, points.last().y - points.first().y);
        motion.push_back(motion.back() + step);
    }
    {
        const std::lock_guard<std::mutex> lock(cacheMutex);
        QHash<QString, std::vector<QPointF>>& cache = motionCache();
        if (cache.size() > 32) cache.clear(); // ours: a few segments' worth
        if (cache.value(key).size() < motion.size()) cache.insert(key, motion);
    }
    return motion.at(static_cast<std::size_t>(offset));
}

Picture regionStabilizePicture(const model::Effect& effect, const Picture& layer, std::int64_t offset, std::int64_t length, const QString& material,
                               const TrackedFrame& frames) {
    if (layer.isNull()) return layer;
    QPointF shift = steadying(effect, regionMotion(effect, material, offset, length, frames));
    double zoom = 1.0;
    if (option(effect, "Auto Zoom")) {
        // "Auto Zoom compensates by resizing and, if necessary, repositioning the clip to remove the exposed edges"
        // (p.281): as the Stabilize effect's Auto-Zoom, from every frame's move.
        double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
        for (std::int64_t f = 0; f < length; ++f) {
            const QPointF s = steadying(effect, regionMotion(effect, material, f, length, frames));
            x0 = std::min(x0, s.x());
            x1 = std::max(x1, s.x());
            y0 = std::min(y0, s.y());
            y1 = std::max(y1, s.y());
        }
        zoom = 1.0 + std::max(x1 - x0, y1 - y0);
        shift += QPointF(-(x0 + x1) / 2.0, -(y0 + y1) / 2.0);
    }
    if (shift.isNull() && zoom == 1.0) return layer;
    // Fractions of the frame: the picture enlarged about the frame's centre, then moved.
    const std::array<double, 9> map{zoom, 0.0, 0.5 - 0.5 * zoom + shift.x(), 0.0, zoom, 0.5 - 0.5 * zoom + shift.y(), 0.0, 0.0, 1.0};
    return transformed(layer, map);
}

void clearRegionMotion() {
    const std::lock_guard<std::mutex> lock(cacheMutex);
    motionCache().clear();
}

} // namespace lumenis::media

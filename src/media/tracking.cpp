// The tracking engines and tracking data in the picture (FX Guide ch.10, p.248-275; docs/spec/tracking.md). See
// tracking.h.
#include "lumenis/media/tracking.h"

#include "lumenis/media/optical_flow.h"

#include "effect_drawing.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lumenis::media {

namespace {

// A frame's brightness, 0 to 1, row by row.
struct Grey {
    int width = 0, height = 0;
    std::vector<float> v;
    float at(int x, int y) const { return v[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
};

Grey greyOf(const QImage& image) {
    Grey g;
    if (image.isNull()) return g;
    const QImage grey = image.convertToFormat(QImage::Format_Grayscale8);
    g.width = grey.width();
    g.height = grey.height();
    g.v.resize(static_cast<std::size_t>(g.width) * static_cast<std::size_t>(g.height));
    for (int y = 0; y < g.height; ++y) {
        const uchar* row = grey.constScanLine(y);
        for (int x = 0; x < g.width; ++x) g.v[static_cast<std::size_t>(y) * static_cast<std::size_t>(g.width) + static_cast<std::size_t>(x)] = row[x] / 255.0f;
    }
    return g;
}

// The pattern of the target area: its pixels, their mean and spread.
struct Pattern {
    int width = 0, height = 0;
    std::vector<double> v;
    double mean = 0.0, norm = 0.0;
};

Pattern patternAt(const Grey& g, int left, int top, int width, int height) {
    Pattern p;
    p.width = width;
    p.height = height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) p.v.push_back(static_cast<double>(g.at(std::clamp(left + x, 0, g.width - 1), std::clamp(top + y, 0, g.height - 1))));
    }
    for (double d : p.v) p.mean += d;
    p.mean /= static_cast<double>(p.v.size());
    for (double& d : p.v) {
        d -= p.mean;
        p.norm += d * d;
    }
    p.norm = std::sqrt(p.norm);
    return p;
}

// The normalized cross-correlation of the pattern with the frame at top-left (left, top); -1 to 1.
double correlation(const Grey& g, const Pattern& p, int left, int top) {
    double mean = 0.0;
    for (int y = 0; y < p.height; ++y) {
        for (int x = 0; x < p.width; ++x) mean += static_cast<double>(g.at(left + x, top + y));
    }
    mean /= static_cast<double>(p.v.size());
    double cross = 0.0, norm = 0.0;
    for (int y = 0; y < p.height; ++y) {
        for (int x = 0; x < p.width; ++x) {
            const double d = static_cast<double>(g.at(left + x, top + y)) - mean;
            cross += d * p.v[static_cast<std::size_t>(y * p.width + x)];
            norm += d * d;
        }
    }
    const double denominator = std::sqrt(norm) * p.norm;
    return denominator > 1e-12 ? cross / denominator : 0.0;
}

// The offset (-0.5 to 0.5) of a parabola's peak through three samples.
double peakOffset(double before, double at, double after) {
    const double d = before - 2.0 * at + after;
    return std::abs(d) < 1e-12 ? 0.0 : std::clamp(0.5 * (before - after) / d, -0.5, 0.5);
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

} // namespace

TrackingEngine trackingEngine(const QString& name) {
    if (name == QLatin1String("FluidTracker")) return TrackingEngine::Fluid;
    if (name == QLatin1String("FluidStabilizer")) return TrackingEngine::FluidStabilizer;
    return TrackingEngine::Correlation;
}

QList<model::TrackPoint> trackTarget(TrackingEngine engine, const TrackedFrame& frames, std::int64_t from, std::int64_t to,
                                     const QRectF& target, const QRectF& search, const std::function<bool()>& stop,
                                     const std::function<void(const model::TrackPoint&)>& progress) {
    QList<model::TrackPoint> out;
    QImage previous = frames(from);
    if (previous.isNull()) return out;
    const double W = previous.width(), H = previous.height();
    QPointF centre(target.center().x() * W, target.center().y() * H);
    auto record = [&](std::int64_t f) {
        const model::TrackPoint p{f, centre.x() / W, centre.y() / H};
        out << p;
        if (progress) progress(p);
    };
    record(from);
    const int step = to >= from ? 1 : -1;
    Grey first = greyOf(previous);
    // The Correlation Tracker's pattern: the target area at the first frame (ours: never updated, so it does not
    // drift), at least 4 pixels each way.
    const int tw = std::max(4, static_cast<int>(std::lround(target.width() * W)));
    const int th = std::max(4, static_cast<int>(std::lround(target.height() * H)));
    const Pattern pattern = patternAt(first, static_cast<int>(std::lround(centre.x() - tw / 2.0)), static_cast<int>(std::lround(centre.y() - th / 2.0)), tw, th);
    const double sw = std::max(search.width() * W, static_cast<double>(tw));
    const double sh = std::max(search.height() * H, static_cast<double>(th));
    for (std::int64_t f = from + step; step > 0 ? f <= to : f >= to; f += step) {
        if (stop && stop()) break;
        const QImage image = frames(f);
        if (image.isNull() || image.size() != previous.size()) break;
        if (engine == TrackingEngine::Correlation) {
            const Grey g = greyOf(image);
            // Every top-left position that keeps the pattern inside the search area and the frame.
            const int x0 = std::max(0, static_cast<int>(std::floor(centre.x() - sw / 2.0)));
            const int y0 = std::max(0, static_cast<int>(std::floor(centre.y() - sh / 2.0)));
            const int x1 = std::min(g.width - tw, static_cast<int>(std::ceil(centre.x() + sw / 2.0)) - tw);
            const int y1 = std::min(g.height - th, static_cast<int>(std::ceil(centre.y() + sh / 2.0)) - th);
            if (x1 < x0 || y1 < y0) break;
            const int cols = x1 - x0 + 1;
            std::vector<double> score(static_cast<std::size_t>(cols * (y1 - y0 + 1)));
            int bestX = x0, bestY = y0;
            double best = -2.0;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const double s = correlation(g, pattern, x, y);
                    score[static_cast<std::size_t>((y - y0) * cols + (x - x0))] = s;
                    if (s > best) {
                        best = s;
                        bestX = x;
                        bestY = y;
                    }
                }
            }
            auto at = [&](int x, int y) {
                if (x < x0 || x > x1 || y < y0 || y > y1) return best;
                return score[static_cast<std::size_t>((y - y0) * cols + (x - x0))];
            };
            const double dx = peakOffset(at(bestX - 1, bestY), best, at(bestX + 1, bestY));
            const double dy = peakOffset(at(bestX, bestY - 1), best, at(bestX, bestY + 1));
            centre = QPointF(bestX + dx + tw / 2.0, bestY + dy + th / 2.0);
        } else {
            const FlowField flow = estimateFlow(previous, image);
            if (flow.isEmpty()) break;
            double mx = 0.0, my = 0.0;
            if (engine == TrackingEngine::Fluid) {
                flow.at(centre.x(), centre.y(), &mx, &my);
            } else {
                std::vector<double> xs, ys;
                for (int j = 0; j < 4; ++j) {
                    for (int i = 0; i < 4; ++i) {
                        double fx = 0.0, fy = 0.0;
                        flow.at((i + 0.5) * W / 4.0, (j + 0.5) * H / 4.0, &fx, &fy);
                        xs.push_back(fx);
                        ys.push_back(fy);
                    }
                }
                mx = median(xs);
                my = median(ys);
            }
            centre += QPointF(mx, my);
        }
        previous = image;
        record(f);
    }
    return out;
}

namespace {

void setPlain(model::Effect& e, const QString& category, const QString& name, double value) {
    model::setKeyframes(e, category, name, {});
    QJsonObject group = e.parameters.value(category).toObject();
    group.insert(name, value);
    e.parameters.insert(category, group);
}

double valueAt(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    return model::numberAt(e, category, name, time, span).value_or(parameterDefault(e, category, name).toDouble(0.0));
}

} // namespace

model::Effect trackedEffect(const model::Effect& effect, double time, const model::EffectSpan& span, double aspect) {
    if (!effect.parameters.contains(QStringLiteral("Trackers")) || aspect <= 0.0) return effect;
    const model::TrackedChange change = model::trackedChange(effect, model::trackingFrame(time, span), aspect);
    if (change.position.isNull() && change.scale == 1.0 && change.rotation == 0.0) return effect;
    model::Effect e = effect;
    const bool threeD = is3DEffect(effect);
    const QString position = threeD ? QStringLiteral("Position (3D)") : QStringLiteral("Position");
    const QString scaling = threeD ? QStringLiteral("Scaling (3D)") : QStringLiteral("Scaling");
    const QString X = QStringLiteral("X"), Y = QStringLiteral("Y");
    if (!change.position.isNull()) {
        // Pixels of a 1920-wide frame; 3D's Y is up (p.577).
        setPlain(e, position, X, valueAt(effect, position, X, time, span) + change.position.x() * 1920.0);
        const double dy = change.position.y() * 1920.0 / aspect;
        setPlain(e, position, Y, valueAt(effect, position, Y, time, span) + (threeD ? -dy : dy));
    }
    if (change.scale != 1.0) {
        setPlain(e, scaling, X, valueAt(effect, scaling, X, time, span) * change.scale);
        setPlain(e, scaling, Y, valueAt(effect, scaling, Y, time, span) * change.scale);
    }
    if (change.rotation != 0.0 && threeD) {
        const QString rotation = QStringLiteral("Rotation");
        setPlain(e, rotation, QStringLiteral("Z"), valueAt(effect, rotation, QStringLiteral("Z"), time, span) + change.rotation);
    }
    return e;
}

std::optional<std::array<double, 9>> trackedTransform(const model::Effect& effect, double time, const model::EffectSpan& span, double aspect) {
    if (!effect.parameters.contains(QStringLiteral("Trackers")) || aspect <= 0.0) return std::nullopt;
    return model::trackedChange(effect, model::trackingFrame(time, span), aspect).transform;
}

Picture transformed(const Picture& picture, const std::array<double, 9>& m) {
    const int width = picture.width(), height = picture.height();
    // The inverse map, output -> source.
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (width <= 0 || height <= 0 || std::abs(det) < 1e-15) return picture;
    const std::array<double, 9> inv{(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                                    (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                                    (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    Picture out(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double u = (x + 0.5) / width, v = (y + 0.5) / height;
            const double z = inv[6] * u + inv[7] * v + inv[8];
            if (std::abs(z) < 1e-12) continue;
            const double sx = (inv[0] * u + inv[1] * v + inv[2]) / z;
            const double sy = (inv[3] * u + inv[4] * v + inv[5]) / z;
            detail::sample(picture, sx * width, sy * height, out.pixel(x, y));
        }
    }
    return out;
}

} // namespace lumenis::media

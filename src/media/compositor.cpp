#include "lumenis/media/compositor.h"

#include "lumenis/model/effect_keyframes.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

// The compositor (compositor.h). Tracks are walked from the top down to find what hides the rest,
// then composited from the bottom up. Each effect is a function of the pictures it takes and its
// parameters (model::Effect::parameters, by category and name, as the Effect Editor stores them).
namespace lumenis::media {

namespace {

constexpr float kDensityFloor = 1e-3f; // the darkest light the film blend works with

bool parameterFlag(const model::Effect& effect, const QString& category, const QString& name) {
    return effect.parameters.value(category).toObject().value(name).toBool();
}

} // namespace

// ---------------------------------------------------------------- Picture

Picture::Picture(int width, int height)
    : width_(std::max(width, 0)), height_(std::max(height, 0)),
      data_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 0.0f) {}

Picture Picture::solid(int width, int height, const QColor& color) {
    Picture p(width, height);
    const float a = static_cast<float>(color.alphaF());
    const float rgba[4] = {static_cast<float>(color.redF()) * a, static_cast<float>(color.greenF()) * a,
                           static_cast<float>(color.blueF()) * a, a};
    for (std::size_t i = 0; i < p.data_.size(); i += 4) std::copy(rgba, rgba + 4, &p.data_[i]);
    return p;
}

Picture Picture::fromImage(const QImage& image, int width, int height) {
    Picture p(width, height);
    if (image.isNull() || p.isNull()) return p;
    QImage fitted = image;
    if (image.size() != QSize(width, height)) {
        fitted = image.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    fitted = fitted.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    const int ox = (width - fitted.width()) / 2;
    const int oy = (height - fitted.height()) / 2;
    for (int y = 0; y < fitted.height(); ++y) {
        const uchar* line = fitted.constScanLine(y);
        for (int x = 0; x < fitted.width(); ++x) {
            float* out = p.pixel(ox + x, oy + y);
            for (int c = 0; c < 4; ++c) out[c] = static_cast<float>(line[x * 4 + c]) / 255.0f;
        }
    }
    return p;
}

void Picture::quantize(EffectPrecision precision) {
    if (precision == EffectPrecision::Bits32) return;
    const float steps = precision == EffectPrecision::Bits8 ? 255.0f : 65535.0f;
    for (float& v : data_) v = std::round(std::clamp(v, 0.0f, 1.0f) * steps) / steps;
}

QImage Picture::toImage() const {
    QImage out(width_, height_, QImage::Format_RGB32);
    for (int y = 0; y < height_; ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width_; ++x) {
            const float* p = pixel(x, y);
            // Premultiplied over black: the colour channels as they are.
            auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            line[x] = qRgb(channel(p[0]), channel(p[1]), channel(p[2]));
        }
    }
    return out;
}

// ---------------------------------------------------------------- blends

Picture mix(const Picture& a, const Picture& b, double t) {
    Picture out(a.width(), a.height());
    const float f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    const auto& x = a.data();
    const auto& y = b.data();
    auto& o = out.data();
    for (std::size_t i = 0; i < o.size(); ++i) o[i] = x[i] + (y[i] - x[i]) * f;
    return out;
}

Picture over(const Picture& top, const Picture& bottom) {
    Picture out(top.width(), top.height());
    const auto& t = top.data();
    const auto& b = bottom.data();
    auto& o = out.data();
    for (std::size_t i = 0; i < o.size(); i += 4) {
        const float keep = 1.0f - t[i + 3];
        for (std::size_t c = 0; c < 4; ++c) o[i + c] = t[i + c] + b[i + c] * keep;
    }
    return out;
}

Picture faded(const Picture& picture, double level) {
    Picture out = picture;
    const float f = static_cast<float>(std::clamp(level, 0.0, 1.0));
    for (float& v : out.data()) v *= f;
    return out;
}

Picture filmMix(const Picture& a, const Picture& b, double t) {
    Picture out(a.width(), a.height());
    const float f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    const auto& x = a.data();
    const auto& y = b.data();
    auto& o = out.data();
    for (std::size_t i = 0; i < o.size(); i += 4) {
        const float alpha = x[i + 3] + (y[i + 3] - x[i + 3]) * f;
        for (std::size_t c = 0; c < 3; ++c) {
            const float da = -std::log10(std::max(x[i + c], kDensityFloor));
            const float db = -std::log10(std::max(y[i + c], kDensityFloor));
            const float v = std::pow(10.0f, -(da + (db - da) * f));
            // The floor stands for black: black ends stay black.
            o[i + c] = v <= kDensityFloor * 1.0001f ? 0.0f : std::min(v, alpha);
        }
        o[i + 3] = alpha;
    }
    return out;
}

// ---------------------------------------------------------------- tracks

std::optional<TrackFrame> trackFrame(const model::Sequence& sequence, const QString& name, std::int64_t frame) {
    const model::Track* track = sequence.track(name);
    if (!track || frame < 0) return std::nullopt;
    std::int64_t start = 0;
    const int i = track->segmentAt(frame, &start);
    if (i < 0) return std::nullopt;
    const auto& segs = track->segments;
    const model::Segment& seg = segs.at(i);
    TrackFrame out;
    out.segment = &seg;
    out.frame = seg.sourceIn + (frame - start);
    out.offset = frame - start;
    // A transition sits on the cut where its segment starts (model::Segment::transition), `before()`
    // frames on the outgoing side and `after()` on the incoming.
    auto inTransition = [&](int incoming, std::int64_t cut) {
        const model::Transition& t = *segs.at(incoming).transition;
        const std::int64_t begin = cut - t.before();
        if (t.duration <= 0 || frame < begin || frame >= cut + t.after()) return false;
        const model::Segment& a = segs.at(incoming - 1);
        const model::Segment& b = segs.at(incoming);
        const std::int64_t aStart = cut - a.length;
        out.outgoing = &a;
        out.outgoingFrame = a.sourceIn + (frame - aStart);
        out.segment = &b;
        out.frame = b.sourceIn + (frame - cut);
        out.transition = &t;
        out.offset = frame - begin;
        out.progress = static_cast<double>(frame - begin + 1) / static_cast<double>(t.duration + 1);
        return true;
    };
    if (i > 0 && seg.transition && inTransition(i, start)) return out;
    if (i + 1 < segs.size() && segs.at(i + 1).transition && inTransition(i + 1, start + seg.length)) return out;
    return out;
}

namespace {

// A segment's own picture: transparent for filler and for media that cannot be had.
Picture segmentPicture(const Composition& c, const model::Segment* segment, std::int64_t frame, bool* missing) {
    if (!segment || segment->isFiller() || !c.source) return Picture(c.size.width(), c.size.height());
    const std::optional<QImage> image = c.source(*segment, frame, c.size);
    if (!image) {
        if (missing) *missing = true;
        return Picture(c.size.width(), c.size.height());
    }
    return Picture::fromImage(*image, c.size.width(), c.size.height());
}

// The transitions of the dissolve family (FX Guide p.457, p.462) and the 2D transitions (wipes.cpp); any
// other is a cut at the cut point until its step comes.
Picture transitionPicture(const model::Transition& t, Picture outgoing, Picture incoming, double time, int width, int height) {
    const model::Effect& e = t.effect;
    const QString foreground = QStringLiteral("Foreground");
    // "Swaps the image source for the foreground and background layers of the effect" (p.495).
    if (parameterFlag(e, foreground, QStringLiteral("Swap Sources"))) std::swap(outgoing, incoming);
    // The Foreground Level keyframed, when it is, says how far through the blend the effect is; else
    // straight from 0 to 100 over its length (ch.4). Ours: a transition's keyframes are placed in its
    // progress, so Level keyed 0 and 100 at its ends is the same as no keyframes.
    double progress = time;
    if (!model::keyframesOf(e, foreground, QStringLiteral("Level")).keys.isEmpty()) {
        progress = std::clamp(model::numberAt(e, foreground, QStringLiteral("Level"), time, model::EffectSpan(t.duration, true)).value_or(time * 100.0) / 100.0, 0.0, 1.0);
    }
    QColor colour(Qt::black);
    if (const auto rgb = model::colorAt(e, QStringLiteral("Background Color"), QStringLiteral("Background Color"), time, model::EffectSpan(t.duration, true))) {
        colour = QColor::fromRgbF(static_cast<float>(rgb->red), static_cast<float>(rgb->green), static_cast<float>(rgb->blue));
    }
    if (isWipe(e)) return wipePicture(e, std::move(outgoing), std::move(incoming), progress, time, model::EffectSpan(t.duration, true));
    const QString& name = e.name;
    if (name == QLatin1String("Dissolve")) return mix(outgoing, incoming, progress);
    if (name == QLatin1String("Fade to Color")) return mix(outgoing, Picture::solid(width, height, colour), progress);
    if (name == QLatin1String("Fade from Color")) return mix(Picture::solid(width, height, colour), incoming, progress);
    if (name == QLatin1String("Dip to Color")) {
        // "fades from the outgoing video to any color and then fades up to the incoming video".
        const Picture solid = Picture::solid(width, height, colour);
        return progress < 0.5 ? mix(outgoing, solid, progress * 2.0) : mix(solid, incoming, progress * 2.0 - 1.0);
    }
    if (name == QLatin1String("Film Dissolve") || name == QLatin1String("Film Fade")) return filmMix(outgoing, incoming, progress);
    return {};
}

} // namespace

Picture composeFrame(const Composition& c, std::int64_t frame, bool* missing) {
    const int w = c.size.width();
    const int h = c.size.height();
    Picture result(w, h);
    if (!c.sequence || w <= 0 || h <= 0) return result;
    for (const QString& name : c.tracks) {
        const auto tf = trackFrame(*c.sequence, name, frame);
        if (!tf) continue;
        Picture layer;
        if (tf->transition) {
            Picture out = segmentPicture(c, tf->outgoing, tf->outgoingFrame, missing);
            Picture in = segmentPicture(c, tf->segment, tf->frame, missing);
            layer = transitionPicture(*tf->transition, out, in, tf->progress, w, h);
            if (layer.isNull()) {
                // Not built yet: the cut, which lies after the outgoing side's frames.
                const double cutAt = static_cast<double>(tf->transition->before() + 1) / static_cast<double>(tf->transition->duration + 1);
                layer = tf->progress < cutAt ? std::move(out) : std::move(in);
            }
            layer.quantize(c.precision);
        } else {
            layer = segmentPicture(c, tf->segment, tf->frame, missing);
        }
        if (tf->segment && tf->segment->effect && !tf->transition) {
            const model::Effect& e = *tf->segment->effect;
            const QString foreground = QStringLiteral("Foreground");
            const QString level = QStringLiteral("Level");
            // Where in the segment the frame is, 0 at its first frame and 1 at its last.
            const std::int64_t length = tf->segment->length;
            const double time = length > 1 ? static_cast<double>(tf->offset) / static_cast<double>(length - 1) : 0.0;
            const bool keyed = !model::keyframesOf(e, foreground, level).keys.isEmpty();
            if (isWipe(e)) {
                // A 2D transition on a segment ("multilayer segments", p.459) wipes from the tracks below
                // to the segment. Ours: over the segment's length unless its Level is keyframed, as a
                // transition's is.
                const double progress = keyed ? model::numberAt(e, foreground, level, time, length).value_or(100.0) / 100.0 : time;
                Picture below = std::move(result);
                if (parameterFlag(e, foreground, QStringLiteral("Swap Sources"))) std::swap(below, layer);
                result = wipePicture(e, std::move(below), std::move(layer), progress, time, length);
                result.quantize(c.precision);
                continue;
            }
            if (e.name == QLatin1String("Superimpose") || keyed) {
                // "The Foreground Level slider controls the blend percentage ... When Level=100, only the
                // top image is visible, and when Level=0, only the bottom image is visible" (p.458); the Fade
                // Effect button keyframes it (p.46-47). Ours: Superimpose is 50 until a level is set.
                const double fallback = e.name == QLatin1String("Superimpose") ? 50.0 : 100.0;
                layer = faded(layer, model::numberAt(e, foreground, level, time, length).value_or(fallback) / 100.0);
                layer.quantize(c.precision);
            }
        }
        result = over(layer, result);
        result.quantize(c.precision);
    }
    return result;
}

QImage composeImage(const Composition& c, std::int64_t frame, bool* missing) {
    if (!c.sequence) return {};
    // From the top down: the first track with a picture hides those below when it is a plain segment.
    for (auto it = c.tracks.crbegin(); it != c.tracks.crend(); ++it) {
        const auto tf = trackFrame(*c.sequence, *it, frame);
        if (!tf || (!tf->transition && tf->segment->isFiller() && !tf->segment->effect)) continue;
        if (tf->transition || tf->segment->effect || tf->segment->isFiller() || !c.source) break;
        const std::optional<QImage> image = c.source(*tf->segment, tf->frame, c.size);
        if (!image) {
            if (missing) *missing = true;
            return {};
        }
        return *image;
    }
    return composeFrame(c, frame, missing).toImage();
}

} // namespace lumenis::media

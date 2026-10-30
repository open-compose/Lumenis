#pragma once

#include "effect_drawing.h"

#include <QColor>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

// What the Illusion FX effects share (illusion_fx.cpp, the segment effects; illusion_transitions.cpp,
// the transitions; docs/spec/illusion-fx.md): reading parameters with their values until set, a
// random hash and generator that are the same on every platform, and the Illusion FX Border with the
// distortion loop. Internal to lumenis_media.
namespace lumenis::media::detail::illusion {

constexpr double kPi = std::numbers::pi;

// A number parameter at `time`, else its value until set (parameterDefault), else 0.
inline double num(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    const QJsonValue d = parameterDefault(e, category, name);
    return model::numberAt(e, category, name, time, span).value_or(d.isDouble() ? d.toDouble() : 0.0);
}

// A colour parameter at `time`, else its value until set, else white.
inline QColor colourOf(const model::Effect& e, const QString& category, const QString& name, double time, const model::EffectSpan& span) {
    if (const auto c = model::colorAt(e, category, name, time, span)) {
        return QColor::fromRgbF(static_cast<float>(c->red), static_cast<float>(c->green), static_cast<float>(c->blue));
    }
    const QColor d(parameterDefault(e, category, name).toString());
    return d.isValid() ? d : QColor(Qt::white);
}

// A menu's choice, else its value until set.
inline QString text(const model::Effect& e, const QString& category, const QString& name) {
    const QJsonValue v = e.parameters.value(category).toObject().value(name);
    return v.isString() ? v.toString() : parameterDefault(e, category, name).toString();
}

// The frame of the effect `time` is at, counted from its start.
inline std::int64_t frameOf(double time, const model::EffectSpan& span) {
    return span.length > 1 ? static_cast<std::int64_t>(std::lround(time * static_cast<double>(span.length - 1))) : 0;
}

// A hash of integers to 0..1, the same everywhere.
inline double hash01(std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t seed) {
    // (Written out, not looped over a braced list: MSVC 14.44 has failed on such loops; BUILDING.md.)
    std::uint64_t h = 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(seed);
    auto mixIn = [&h](std::int64_t v) {
        h ^= static_cast<std::uint64_t>(v) + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31;
    };
    mixIn(a);
    mixIn(b);
    mixIn(c);
    return static_cast<double>(h >> 11) / 9007199254740992.0; // 2^53
}

// A small random generator for the drawn effects (lightning, sparks), the same everywhere.
struct Random {
    std::uint64_t state;
    explicit Random(std::uint64_t seed) : state(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    double next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / 9007199254740992.0;
    }
};

// A checkbox, `fallback` until set.
inline bool flag(const model::Effect& e, const QString& category, const QString& name, bool fallback) {
    const QJsonValue v = e.parameters.value(category).toObject().value(name);
    return v.isBool() ? v.toBool() : fallback;
}

// The Illusion FX Border's Left, Right, Top and Bottom (p.508), "to prevent black edges of the source
// clip from warping into the effect": ours, each insets the source by percent of the picture (0 to 50),
// and a sample is held inside what is left.
struct SourceBox {
    double left, right, top, bottom;
    static SourceBox of(const model::Effect& e, double time, const model::EffectSpan& span, double W, double H) {
        const QString border = QStringLiteral("Border Parameter for Illusion FX Effects");
        auto inset = [&](const char* side) { return std::clamp(num(e, border, QLatin1String(side), time, span), 0.0, 50.0) / 100.0; };
        return {inset("Left") * W + 0.5, W - inset("Right") * W - 0.5, inset("Top") * H + 0.5, H - inset("Bottom") * H - 0.5};
    }
    double x(double v) const { return std::clamp(v, left, std::max(left, right)); }
    double y(double v) const { return std::clamp(v, top, std::max(top, bottom)); }
};

// The pixel of `p` nearest to (x, y), for Anti-alias off ("whether or not transformed areas are
// smoothed").
inline void nearest(const Picture& p, double x, double y, float out[4]) {
    const int ix = std::clamp(static_cast<int>(std::floor(x)), 0, p.width() - 1);
    const int iy = std::clamp(static_cast<int>(std::floor(y)), 0, p.height() - 1);
    std::copy(p.pixel(ix, iy), p.pixel(ix, iy) + 4, out);
}

// A distortion: each pixel of the result takes the source at map(x, y) (pixel centres), held inside
// the Border's box, smoothed or not. `map` returns false where nothing shows (transparent).
template <typename Map>
inline Picture warp(const Picture& layer, const SourceBox& box, bool antiAlias, Map map) {
    Picture out(layer.width(), layer.height());
    float in[4];
    for (int y = 0; y < layer.height(); ++y) {
        for (int x = 0; x < layer.width(); ++x) {
            double sx = x + 0.5;
            double sy = y + 0.5;
            if (!map(sx, sy)) continue;
            if (antiAlias) {
                sample(layer, box.x(sx), box.y(sy), in);
            } else {
                nearest(layer, box.x(sx), box.y(sy), in);
            }
            std::copy(in, in + 4, out.pixel(x, y));
        }
    }
    return out;
}

} // namespace lumenis::media::detail::illusion

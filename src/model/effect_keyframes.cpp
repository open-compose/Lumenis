#include "lumenis/model/effect_keyframes.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <functional>

// Keyframes on effect parameters (effect_keyframes.h). Keyframes are evaluated on the effect's
// timeline as fractions (0 to 1); Fixed keyframes, kept in frames, are turned into fractions of the
// effect's length first.
namespace lumenis::model {

namespace {

QString interpolationName(Interpolation i) {
    switch (i) {
    case Interpolation::Shelf: return QStringLiteral("shelf");
    case Interpolation::Linear: return QStringLiteral("linear");
    case Interpolation::Spline: return QStringLiteral("spline");
    case Interpolation::Bezier: return QStringLiteral("bezier");
    }
    return QStringLiteral("linear");
}

Interpolation interpolationFrom(const QString& name) {
    if (name == QLatin1String("shelf")) return Interpolation::Shelf;
    if (name == QLatin1String("spline")) return Interpolation::Spline;
    if (name == QLatin1String("bezier")) return Interpolation::Bezier;
    return Interpolation::Linear;
}

double lastFrame(std::int64_t length) {
    return static_cast<double>(std::max<std::int64_t>(length - 1, 1));
}

// A keyframe's place as a fraction of the effect.
double fractionOf(const ParameterKeyframes& p, const EffectKeyframe& k, std::int64_t length) {
    return p.fixed ? k.at / lastFrame(length) : k.at;
}

struct Point {
    double t = 0.0;
    double v = 0.0;
    const EffectKeyframe* key = nullptr;
};

// The Spline's slope at point i: the chord between its neighbours, flat at the first and last
// keyframes so that two keyframes ease in and out (p.121).
double splineSlope(const QList<Point>& p, qsizetype i) {
    if (i <= 0 || i >= p.size() - 1) return 0.0;
    const double dt = p.at(i + 1).t - p.at(i - 1).t;
    return dt > 0 ? (p.at(i + 1).v - p.at(i - 1).v) / dt : 0.0;
}

// One coordinate of a cubic Bézier at s.
double cubic(double a, double b, double c, double d, double s) {
    const double r = 1.0 - s;
    return r * r * r * a + 3 * r * r * s * b + 3 * r * s * s * c + s * s * s * d;
}

double evaluate(const ParameterKeyframes& p, const QList<Point>& pts, double t) {
    const Point& first = pts.first();
    const Point& last = pts.last();
    auto slopeOf = [](const Point& a, const Point& b) { return b.t > a.t ? (b.v - a.v) / (b.t - a.t) : 0.0; };
    if (t <= first.t || pts.size() == 1) {
        // Before the first keyframe: Hold keeps its value; Extrapolate carries the first change on.
        if (!p.extrapolate || pts.size() == 1 || p.interpolation == Interpolation::Shelf) return first.v;
        return first.v + (t - first.t) * slopeOf(first, pts.at(1));
    }
    if (t >= last.t) {
        if (!p.extrapolate || p.interpolation == Interpolation::Shelf) return last.v;
        return last.v + (t - last.t) * slopeOf(pts.at(pts.size() - 2), last);
    }
    qsizetype i = 0;
    while (i + 1 < pts.size() && pts.at(i + 1).t <= t) ++i;
    const Point& a = pts.at(i);
    const Point& b = pts.at(i + 1);
    const double dt = b.t - a.t;
    if (dt <= 0) return b.v;
    const double u = (t - a.t) / dt;
    switch (p.interpolation) {
    case Interpolation::Shelf: return a.v; // "does not change between keyframes and then changes abruptly"
    case Interpolation::Linear: return a.v + (b.v - a.v) * u;
    case Interpolation::Spline: {
        // Cubic Hermite through the keyframes.
        const double ma = splineSlope(pts, i) * dt;
        const double mb = splineSlope(pts, i + 1) * dt;
        const double u2 = u * u;
        const double u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * a.v + (u3 - 2 * u2 + u) * ma + (-2 * u3 + 3 * u2) * b.v + (u3 - u2) * mb;
    }
    case Interpolation::Bezier: {
        // The handles, or the Spline's tangents a third of the way when they are not set.
        double ot = a.key->outTime, ov = a.key->outValue, it = b.key->inTime, iv = b.key->inValue;
        if (ot == 0.0 && ov == 0.0) {
            ot = dt / 3.0;
            ov = splineSlope(pts, i) * dt / 3.0;
        }
        if (it == 0.0 && iv == 0.0) {
            it = -dt / 3.0;
            iv = -splineSlope(pts, i + 1) * dt / 3.0;
        }
        const double x1 = std::clamp(a.t + ot, a.t, b.t);
        const double x2 = std::clamp(b.t + it, a.t, b.t);
        // The curve's s for time t: its time is monotonic with the handles kept inside the span.
        double lo = 0.0, hi = 1.0;
        for (int n = 0; n < 40; ++n) {
            const double mid = (lo + hi) / 2;
            (cubic(a.t, x1, x2, b.t, mid) < t ? lo : hi) = mid;
        }
        return cubic(a.v, a.v + ov, b.v + iv, b.v, (lo + hi) / 2);
    }
    }
    return a.v;
}

QList<Point> pointsOf(const ParameterKeyframes& p, std::int64_t length, const std::function<std::optional<double>(const QJsonValue&)>& number) {
    QList<Point> out;
    for (const EffectKeyframe& k : p.keys) {
        const auto v = number(k.value);
        if (!v) return {};
        out.append({fractionOf(p, k, length), *v, &k});
    }
    std::stable_sort(out.begin(), out.end(), [](const Point& x, const Point& y) { return x.t < y.t; });
    return out;
}

} // namespace

// ---------------------------------------------------------------- JSON

QJsonObject EffectKeyframe::toJson() const {
    QJsonObject o{{QStringLiteral("at"), at}, {QStringLiteral("value"), value}};
    if (inTime != 0.0 || inValue != 0.0) o.insert(QStringLiteral("in"), QJsonArray{inTime, inValue});
    if (outTime != 0.0 || outValue != 0.0) o.insert(QStringLiteral("out"), QJsonArray{outTime, outValue});
    return o;
}

EffectKeyframe EffectKeyframe::fromJson(const QJsonObject& o) {
    EffectKeyframe k;
    k.at = o.value(QStringLiteral("at")).toDouble();
    k.value = o.value(QStringLiteral("value"));
    const QJsonArray in = o.value(QStringLiteral("in")).toArray();
    const QJsonArray out = o.value(QStringLiteral("out")).toArray();
    k.inTime = in.at(0).toDouble();
    k.inValue = in.at(1).toDouble();
    k.outTime = out.at(0).toDouble();
    k.outValue = out.at(1).toDouble();
    return k;
}

QJsonObject ParameterKeyframes::toJson() const {
    QJsonArray list;
    for (const EffectKeyframe& k : keys) list.append(k.toJson());
    QJsonObject o{{QStringLiteral("keys"), list}};
    if (interpolation != Interpolation::Linear) o.insert(QStringLiteral("interpolation"), interpolationName(interpolation));
    if (fixed) o.insert(QStringLiteral("fixed"), true);
    if (extrapolate) o.insert(QStringLiteral("extrapolate"), true);
    return o;
}

ParameterKeyframes ParameterKeyframes::fromJson(const QJsonObject& o) {
    ParameterKeyframes p;
    for (const QJsonValue& v : o.value(QStringLiteral("keys")).toArray()) p.keys.append(EffectKeyframe::fromJson(v.toObject()));
    std::stable_sort(p.keys.begin(), p.keys.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) { return a.at < b.at; });
    p.interpolation = interpolationFrom(o.value(QStringLiteral("interpolation")).toString());
    p.fixed = o.value(QStringLiteral("fixed")).toBool();
    p.extrapolate = o.value(QStringLiteral("extrapolate")).toBool();
    return p;
}

// ---------------------------------------------------------------- access

QString parameterKey(const QString& category, const QString& name) {
    return category + QLatin1Char('/') + name;
}

ParameterKeyframes keyframesOf(const Effect& effect, const QString& category, const QString& name) {
    return ParameterKeyframes::fromJson(
        effect.parameters.value(QLatin1String(kKeyframesKey)).toObject().value(parameterKey(category, name)).toObject());
}

void setKeyframes(Effect& effect, const QString& category, const QString& name, const ParameterKeyframes& keys) {
    QJsonObject all = effect.parameters.value(QLatin1String(kKeyframesKey)).toObject();
    if (keys.keys.isEmpty()) {
        all.remove(parameterKey(category, name));
    } else {
        all.insert(parameterKey(category, name), keys.toJson());
    }
    if (all.isEmpty()) {
        effect.parameters.remove(QLatin1String(kKeyframesKey));
    } else {
        effect.parameters.insert(QLatin1String(kKeyframesKey), all);
    }
}

QStringList keyframedParameters(const Effect& effect) {
    return effect.parameters.value(QLatin1String(kKeyframesKey)).toObject().keys();
}

std::optional<double> numberAt(const Effect& effect, const QString& category, const QString& name, double t, std::int64_t length) {
    const ParameterKeyframes p = keyframesOf(effect, category, name);
    auto number = [](const QJsonValue& v) -> std::optional<double> {
        if (v.isDouble()) return v.toDouble();
        return std::nullopt;
    };
    if (const QList<Point> pts = pointsOf(p, length, number); !pts.isEmpty()) return evaluate(p, pts, t);
    return number(effect.parameters.value(category).toObject().value(name));
}

// "#rrggbb" to its channels.
static std::optional<Rgb> rgbOf(const QString& text) {
    if (text.size() != 7 || !text.startsWith(QLatin1Char('#'))) return std::nullopt;
    bool ok = false;
    const uint v = text.mid(1).toUInt(&ok, 16);
    if (!ok) return std::nullopt;
    return Rgb{((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0};
}

std::optional<Rgb> colorAt(const Effect& effect, const QString& category, const QString& name, double t, std::int64_t length) {
    const ParameterKeyframes p = keyframesOf(effect, category, name);
    if (!p.keys.isEmpty()) {
        // Each channel on its own curve.
        double rgb[3] = {0, 0, 0};
        for (int c = 0; c < 3; ++c) {
            auto channel = [c](const QJsonValue& v) -> std::optional<double> {
                const auto col = rgbOf(v.toString());
                if (!col) return std::nullopt;
                return c == 0 ? col->red : c == 1 ? col->green : col->blue;
            };
            const QList<Point> pts = pointsOf(p, length, channel);
            if (pts.isEmpty()) return std::nullopt;
            rgb[c] = std::clamp(evaluate(p, pts, t), 0.0, 1.0);
        }
        return Rgb{rgb[0], rgb[1], rgb[2]};
    }
    return rgbOf(effect.parameters.value(category).toObject().value(name).toString());
}

// ---------------------------------------------------------------- changes

void setKeyframe(Effect& effect, const QString& category, const QString& name, double t, std::int64_t length, const QJsonValue& value) {
    ParameterKeyframes p = keyframesOf(effect, category, name);
    const double at = p.fixed ? t * lastFrame(length) : t;
    const double halfFrame = (p.fixed ? 0.5 : 0.5 / lastFrame(length));
    for (EffectKeyframe& k : p.keys) {
        if (std::abs(k.at - at) <= halfFrame) {
            k.value = value;
            setKeyframes(effect, category, name, p);
            return;
        }
    }
    EffectKeyframe k;
    k.at = at;
    k.value = value;
    p.keys.append(k);
    std::stable_sort(p.keys.begin(), p.keys.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) { return a.at < b.at; });
    setKeyframes(effect, category, name, p);
}

bool removeKeyframe(Effect& effect, const QString& category, const QString& name, double t, std::int64_t length) {
    ParameterKeyframes p = keyframesOf(effect, category, name);
    const double at = p.fixed ? t * lastFrame(length) : t;
    const double halfFrame = (p.fixed ? 0.5 : 0.5 / lastFrame(length));
    const qsizetype before = p.keys.size();
    p.keys.removeIf([&](const EffectKeyframe& k) { return std::abs(k.at - at) <= halfFrame; });
    if (p.keys.size() == before) return false;
    setKeyframes(effect, category, name, p);
    return true;
}

void reverseKeyframes(Effect& effect, std::int64_t length, const QStringList& parameters, bool transition) {
    const QStringList which = parameters.isEmpty() ? keyframedParameters(effect) : parameters;
    for (const QString& key : which) {
        const qsizetype slash = key.indexOf(QLatin1Char('/'));
        if (slash < 0) continue;
        const QString category = key.left(slash);
        const QString name = key.mid(slash + 1);
        ParameterKeyframes p = keyframesOf(effect, category, name);
        for (EffectKeyframe& k : p.keys) {
            k.at = p.fixed ? lastFrame(length) - k.at : 1.0 - k.at;
            // Mirrored in time, a keyframe's way in becomes its way out.
            const double inT = k.inTime, inV = k.inValue;
            k.inTime = -k.outTime;
            k.inValue = k.outValue;
            k.outTime = -inT;
            k.outValue = inV;
        }
        std::stable_sort(p.keys.begin(), p.keys.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) { return a.at < b.at; });
        setKeyframes(effect, category, name, p);
    }
    // "If the effect is a transition effect that contains a Swap Sources button, Media Composer
    // automatically enables the Swap Sources button when you reverse the keyframes on the entire effect"
    // (p.124).
    if (transition && parameters.isEmpty()) {
        QJsonObject fg = effect.parameters.value(QStringLiteral("Foreground")).toObject();
        fg.insert(QStringLiteral("Swap Sources"), !fg.value(QStringLiteral("Swap Sources")).toBool());
        effect.parameters.insert(QStringLiteral("Foreground"), fg);
    }
}

void fadeEffect(Effect& effect, std::int64_t length, std::int64_t up, std::int64_t down) {
    const double last = lastFrame(length);
    up = std::clamp<std::int64_t>(up, 0, length);
    down = std::clamp<std::int64_t>(down, 0, length - up);
    ParameterKeyframes p = keyframesOf(effect, QStringLiteral("Foreground"), QStringLiteral("Level"));
    p.keys.clear();
    p.fixed = false;
    auto key = [&](double frame, double level) {
        EffectKeyframe k;
        k.at = std::clamp(frame / last, 0.0, 1.0);
        k.value = level;
        p.keys.append(k);
    };
    if (up > 0) {
        key(0, 0);
        key(static_cast<double>(up), 100);
    }
    if (down > 0) {
        key(last - static_cast<double>(down), 100);
        key(last, 0);
    }
    setKeyframes(effect, QStringLiteral("Foreground"), QStringLiteral("Level"), p);
}

} // namespace lumenis::model

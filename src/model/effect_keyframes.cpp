#include "lumenis/model/effect_keyframes.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

// Keyframes on effect parameters (effect_keyframes.h). Keyframes are evaluated on the effect's
// time (EffectSpan); Fixed keyframes, kept in frames, are turned into times first.
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

void sortKeys(QList<EffectKeyframe>& keys) {
    std::stable_sort(keys.begin(), keys.end(), [](const EffectKeyframe& a, const EffectKeyframe& b) { return a.at < b.at; });
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

// The value at time t. Handle times are in the units of `at`: `handleScale` turns them into time (the
// time of one frame for Fixed keyframes, 1 for Elastic ones).
double evaluate(const ParameterKeyframes& p, const QList<Point>& pts, double t, double handleScale) {
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
        double ot = a.key->outTime * handleScale, ov = a.key->outValue, it = b.key->inTime * handleScale, iv = b.key->inValue;
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

// Time per unit of a handle's time: a frame's for Fixed keyframes (kept in frames), else 1.
double handleScaleOf(const ParameterKeyframes& p, const EffectSpan& span) {
    return p.fixed ? span.timeOf(1.0) - span.timeOf(0.0) : 1.0;
}

QList<Point> pointsOf(const ParameterKeyframes& p, const EffectSpan& span, const std::function<std::optional<double>(const QJsonValue&)>& number) {
    QList<Point> out;
    for (const EffectKeyframe& k : p.keys) {
        const auto v = number(k.value);
        if (!v) return {};
        out.append({keyframeTime(p, k, span), *v, &k});
    }
    std::stable_sort(out.begin(), out.end(), [](const Point& x, const Point& y) { return x.t < y.t; });
    return out;
}

} // namespace

// ---------------------------------------------------------------- time

double EffectSpan::timeOf(double frame) const {
    if (transition) return (frame + 1.0) / static_cast<double>(std::max<std::int64_t>(length, 0) + 1);
    return frame / static_cast<double>(std::max<std::int64_t>(length - 1, 1));
}

double EffectSpan::frameOf(double t) const {
    if (transition) return t * static_cast<double>(std::max<std::int64_t>(length, 0) + 1) - 1.0;
    return t * static_cast<double>(std::max<std::int64_t>(length - 1, 1));
}

double keyframeTime(const ParameterKeyframes& keys, const EffectKeyframe& key, const EffectSpan& span) {
    return keys.fixed ? span.timeOf(key.at) : key.at;
}

double storedPosition(const ParameterKeyframes& keys, double t, const EffectSpan& span) {
    return keys.fixed ? span.frameOf(t) : t;
}

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
    sortKeys(p.keys);
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

std::optional<double> numberAt(const Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span) {
    const ParameterKeyframes p = keyframesOf(effect, category, name);
    auto number = [](const QJsonValue& v) -> std::optional<double> {
        if (v.isDouble()) return v.toDouble();
        return std::nullopt;
    };
    if (const QList<Point> pts = pointsOf(p, span, number); !pts.isEmpty()) return evaluate(p, pts, t, handleScaleOf(p, span));
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

std::optional<Rgb> colorAt(const Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span) {
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
            const QList<Point> pts = pointsOf(p, span, channel);
            if (pts.isEmpty()) return std::nullopt;
            rgb[c] = std::clamp(evaluate(p, pts, t, handleScaleOf(p, span)), 0.0, 1.0);
        }
        return Rgb{rgb[0], rgb[1], rgb[2]};
    }
    return rgbOf(effect.parameters.value(category).toObject().value(name).toString());
}

// ---------------------------------------------------------------- changes

int keyframeAt(const ParameterKeyframes& keys, double t, const EffectSpan& span) {
    for (int i = 0; i < keys.keys.size(); ++i) {
        if (std::abs(keyframeTime(keys, keys.keys.at(i), span) - t) <= span.halfFrame()) return i;
    }
    return -1;
}

void setKeyframe(Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span, const QJsonValue& value) {
    ParameterKeyframes p = keyframesOf(effect, category, name);
    if (const int i = keyframeAt(p, t, span); i >= 0) {
        p.keys[i].value = value;
    } else {
        EffectKeyframe k;
        k.at = storedPosition(p, t, span);
        k.value = value;
        p.keys.append(k);
        sortKeys(p.keys);
    }
    setKeyframes(effect, category, name, p);
}

bool removeKeyframe(Effect& effect, const QString& category, const QString& name, double t, const EffectSpan& span) {
    ParameterKeyframes p = keyframesOf(effect, category, name);
    const qsizetype before = p.keys.size();
    p.keys.removeIf([&](const EffectKeyframe& k) { return std::abs(keyframeTime(p, k, span) - t) <= span.halfFrame(); });
    if (p.keys.size() == before) return false;
    setKeyframes(effect, category, name, p);
    return true;
}

std::pair<BezierHandle, BezierHandle> effectiveHandles(const ParameterKeyframes& keys, int index, const EffectSpan& span) {
    if (index < 0 || index >= keys.keys.size()) return {};
    QList<Point> pts;
    for (const EffectKeyframe& k : keys.keys) pts.append({keyframeTime(keys, k, span), k.value.toDouble(), &k});
    const double scale = handleScaleOf(keys, span);
    const EffectKeyframe& k = keys.keys.at(index);
    const Point& here = pts.at(index);
    // As evaluate() takes them: the handles set, or the Spline's tangent a third of the way to the
    // neighbour on that side.
    BezierHandle in{k.inTime * scale, k.inValue};
    BezierHandle out{k.outTime * scale, k.outValue};
    const double slope = splineSlope(pts, index);
    if (k.inTime == 0.0 && k.inValue == 0.0 && index > 0) {
        const double dt = here.t - pts.at(index - 1).t;
        in = {-dt / 3.0, -slope * dt / 3.0};
    }
    if (k.outTime == 0.0 && k.outValue == 0.0 && index + 1 < pts.size()) {
        const double dt = pts.at(index + 1).t - here.t;
        out = {dt / 3.0, slope * dt / 3.0};
    }
    return {in, out};
}

void setHandles(ParameterKeyframes& keys, int index, const BezierHandle& in, const BezierHandle& out, const EffectSpan& span) {
    if (index < 0 || index >= keys.keys.size()) return;
    const double scale = handleScaleOf(keys, span);
    EffectKeyframe& k = keys.keys[index];
    // The way in comes from before the keyframe and the way out goes after it.
    k.inTime = std::min(in.time, 0.0) / scale;
    k.inValue = in.value;
    k.outTime = std::max(out.time, 0.0) / scale;
    k.outValue = out.value;
}

void setFixed(ParameterKeyframes& keys, bool fixed, const EffectSpan& span) {
    if (keys.fixed == fixed) return;
    for (EffectKeyframe& k : keys.keys) {
        const double t = keyframeTime(keys, k, span);
        // Handles are in the units of `at`, so they convert with it (linearly, from 0).
        const double scale = fixed ? span.frameOf(1.0) - span.frameOf(0.0) : span.timeOf(1.0) - span.timeOf(0.0);
        k.inTime *= scale;
        k.outTime *= scale;
        k.at = fixed ? span.frameOf(t) : t;
    }
    keys.fixed = fixed;
}

bool moveKeyframe(ParameterKeyframes& keys, int index, std::optional<double> t, std::optional<double> value, const EffectSpan& span) {
    if (index < 0 || index >= keys.keys.size()) return false;
    EffectKeyframe& k = keys.keys[index];
    const EffectKeyframe before = k;
    if (t) {
        // A frame short of each neighbour (p.114).
        const double frame = 2.0 * span.halfFrame();
        double lo = -std::numeric_limits<double>::infinity();
        double hi = std::numeric_limits<double>::infinity();
        if (index > 0) lo = keyframeTime(keys, keys.keys.at(index - 1), span) + frame;
        if (index + 1 < keys.keys.size()) hi = keyframeTime(keys, keys.keys.at(index + 1), span) - frame;
        if (lo <= hi) k.at = storedPosition(keys, std::clamp(*t, lo, hi), span);
    }
    if (value) k.value = *value;
    return !(k == before);
}

bool alignKeyframes(ParameterKeyframes& keys, const QList<int>& selected, int reference, double t, const EffectSpan& span, bool slip) {
    if (reference < 0 || reference >= keys.keys.size()) return false;
    double delta = t - keyframeTime(keys, keys.keys.at(reference), span);
    QList<bool> moving(keys.keys.size(), slip);
    if (!slip) {
        for (int i : selected) {
            if (i >= 0 && i < moving.size()) moving[i] = true;
        }
        moving[reference] = true;
        // As a group, stopping a frame short of a keyframe that stays (p.115, ours: the group keeps
        // its spacing).
        const double frame = 2.0 * span.halfFrame();
        for (int i = 0; i < keys.keys.size(); ++i) {
            if (!moving.at(i)) continue;
            const double at = keyframeTime(keys, keys.keys.at(i), span);
            for (int j = i - 1; j >= 0; --j) {
                if (moving.at(j)) continue;
                delta = std::max(delta, keyframeTime(keys, keys.keys.at(j), span) + frame - at);
                break;
            }
            for (int j = i + 1; j < keys.keys.size(); ++j) {
                if (moving.at(j)) continue;
                delta = std::min(delta, keyframeTime(keys, keys.keys.at(j), span) - frame - at);
                break;
            }
        }
    }
    if (std::abs(delta) < 1e-12) return false;
    for (int i = 0; i < keys.keys.size(); ++i) {
        if (!moving.at(i)) continue;
        EffectKeyframe& k = keys.keys[i];
        k.at = storedPosition(keys, keyframeTime(keys, k, span) + delta, span);
    }
    sortKeys(keys.keys);
    return true;
}

int removeRedundantKeyframes(ParameterKeyframes& keys) {
    if (keys.keys.size() < 3) return 0;
    // Numbers, or a colour's three channels.
    auto values = [](const QJsonValue& v) -> QList<double> {
        if (v.isDouble()) return {v.toDouble()};
        const QString text = v.toString();
        bool ok = false;
        const uint c = text.size() == 7 && text.startsWith(QLatin1Char('#')) ? text.mid(1).toUInt(&ok, 16) : 0;
        if (!ok) return {};
        return {static_cast<double>((c >> 16) & 0xff), static_cast<double>((c >> 8) & 0xff), static_cast<double>(c & 0xff)};
    };
    auto redundant = [&](const EffectKeyframe& a, const EffectKeyframe& k, const EffectKeyframe& b) {
        const QList<double> va = values(a.value), vk = values(k.value), vb = values(b.value);
        if (va.isEmpty() || va.size() != vk.size() || vk.size() != vb.size()) return false;
        for (int c = 0; c < vk.size(); ++c) {
            const double tol = 1e-9 * std::max({1.0, std::abs(va.at(c)), std::abs(vb.at(c))});
            switch (keys.interpolation) {
            case Interpolation::Linear: {
                const double span = b.at - a.at;
                const double expected = span > 0 ? va.at(c) + (vb.at(c) - va.at(c)) * (k.at - a.at) / span : va.at(c);
                if (std::abs(vk.at(c) - expected) > tol) return false;
                break;
            }
            case Interpolation::Shelf:
                if (std::abs(vk.at(c) - va.at(c)) > tol) return false;
                break;
            case Interpolation::Spline:
            case Interpolation::Bezier:
                // Conservative (p.107): only a flat run, and only without handles.
                if (std::abs(vk.at(c) - va.at(c)) > tol || std::abs(vk.at(c) - vb.at(c)) > tol) return false;
                if (k.inTime != 0.0 || k.inValue != 0.0 || k.outTime != 0.0 || k.outValue != 0.0) return false;
                break;
            }
        }
        return true;
    };
    QList<EffectKeyframe> kept{keys.keys.first()};
    for (int i = 1; i + 1 < keys.keys.size(); ++i) {
        if (!redundant(kept.last(), keys.keys.at(i), keys.keys.at(i + 1))) kept << keys.keys.at(i);
    }
    kept << keys.keys.last();
    const int removed = static_cast<int>(keys.keys.size() - kept.size());
    keys.keys = kept;
    return removed;
}

void shiftFixedKeyframes(Effect& effect, std::int64_t frames) {
    if (frames == 0) return;
    const QJsonObject all = effect.parameters.value(QLatin1String(kKeyframesKey)).toObject();
    QJsonObject out = all;
    for (auto it = all.begin(); it != all.end(); ++it) {
        ParameterKeyframes p = ParameterKeyframes::fromJson(it.value().toObject());
        if (!p.fixed) continue;
        for (EffectKeyframe& k : p.keys) k.at += static_cast<double>(frames);
        out.insert(it.key(), p.toJson());
    }
    if (!all.isEmpty()) effect.parameters.insert(QLatin1String(kKeyframesKey), out);
}

void reverseKeyframes(Effect& effect, const EffectSpan& span, const QStringList& parameters) {
    const QStringList which = parameters.isEmpty() ? keyframedParameters(effect) : parameters;
    for (const QString& key : which) {
        const qsizetype slash = key.indexOf(QLatin1Char('/'));
        if (slash < 0) continue;
        const QString category = key.left(slash);
        const QString name = key.mid(slash + 1);
        ParameterKeyframes p = keyframesOf(effect, category, name);
        for (EffectKeyframe& k : p.keys) {
            k.at = storedPosition(p, 1.0 - keyframeTime(p, k, span), span);
            // Mirrored in time, a keyframe's way in becomes its way out.
            const double inT = k.inTime, inV = k.inValue;
            k.inTime = -k.outTime;
            k.inValue = k.outValue;
            k.outTime = -inT;
            k.outValue = inV;
        }
        sortKeys(p.keys);
        setKeyframes(effect, category, name, p);
    }
    // "If the effect is a transition effect that contains a Swap Sources button, Media Composer
    // automatically enables the Swap Sources button when you reverse the keyframes on the entire effect"
    // (p.124).
    if (span.transition && parameters.isEmpty()) {
        QJsonObject fg = effect.parameters.value(QStringLiteral("Foreground")).toObject();
        fg.insert(QStringLiteral("Swap Sources"), !fg.value(QStringLiteral("Swap Sources")).toBool());
        effect.parameters.insert(QStringLiteral("Foreground"), fg);
    }
}

void fadeEffect(Effect& effect, std::int64_t length, std::int64_t up, std::int64_t down) {
    const EffectSpan span(length);
    const double last = static_cast<double>(std::max<std::int64_t>(length - 1, 0));
    up = std::clamp<std::int64_t>(up, 0, length);
    down = std::clamp<std::int64_t>(down, 0, length - up);
    ParameterKeyframes p = keyframesOf(effect, QStringLiteral("Foreground"), QStringLiteral("Level"));
    p.keys.clear();
    p.fixed = false;
    auto key = [&](double frame, double level) {
        EffectKeyframe k;
        k.at = std::clamp(span.timeOf(frame), 0.0, 1.0);
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

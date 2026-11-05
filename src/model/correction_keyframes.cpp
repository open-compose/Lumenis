// Keyframing colour corrections (FX Guide p.683-686): which controls take keyframes, their values at a time and
// adjustments that add keyframes. See lumenis/model/correction_keyframes.h.

#include "lumenis/model/correction_keyframes.h"


#include <algorithm>
#include <cmath>

namespace lumenis::model {

namespace {

// The category and name an Effect keeps a control's keyframes under: the path up to its last part, and that part.
std::pair<QString, QString> split(const QString& path) {
    const qsizetype at = path.lastIndexOf(QLatin1Char('/'));
    return at < 0 ? std::pair{QString(), path} : std::pair{path.left(at), path.mid(at + 1)};
}

// An effect holding only the control's keyframes, for the effect keyframe functions.
Effect holder(const QString& path, const ParameterKeyframes& keys) {
    Effect e;
    const auto [category, name] = split(path);
    setKeyframes(e, category, name, keys);
    return e;
}

} // namespace

CorrectionKeyframing correctionKeyframing(const QString& path) {
    const QString p = canonicalControlPath(path);
    if (p.startsWith(QLatin1String("hsl/controls/"))) return CorrectionKeyframing::Graph;
    if (p.startsWith(QLatin1String("hsl/hueOffsets/")))
        return p.endsWith(QLatin1String("/hue")) || p.endsWith(QLatin1String("/amount")) ? CorrectionKeyframing::Graph : CorrectionKeyframing::None;
    if (p.startsWith(QLatin1String("secondary/vectors/"))) return CorrectionKeyframing::Graph;
    if (p.startsWith(QLatin1String("curves/")) || p.startsWith(QLatin1String("levels/"))) return CorrectionKeyframing::Track;
    return CorrectionKeyframing::None;
}

ParameterKeyframes correctionKeyframesOf(const ColorCorrection& c, const QString& path) {
    const QJsonValue v = c.keyframes.value(canonicalControlPath(path));
    return v.isObject() ? ParameterKeyframes::fromJson(v.toObject()) : ParameterKeyframes{};
}

void setCorrectionKeyframes(ColorCorrection& c, const QString& path, const ParameterKeyframes& keys) {
    const QString p = canonicalControlPath(path);
    if (keys.keys.isEmpty()) {
        c.keyframes.remove(p);
    } else {
        c.keyframes.insert(p, keys.toJson());
    }
}

QStringList keyframedControls(const ColorCorrection& c) {
    return c.keyframes.keys();
}

void setCorrectionKeyframe(ColorCorrection& c, const QString& path, double t, const EffectSpan& span, const QJsonValue& value) {
    const QString p = canonicalControlPath(path);
    const CorrectionKeyframing kind = correctionKeyframing(p);
    if (kind == CorrectionKeyframing::None) {
        c.set(p, value);
        return;
    }
    // The value as the control keeps it (clamped to its range), and its Enable buttons on, as setting it does.
    ColorCorrection one;
    one.set(p, value);
    const QJsonValue kept = one.values.value(p);
    for (const QString& d : QStringList(c.disabled)) {
        if (p == d || p.startsWith(d + QLatin1Char('/'))) c.disabled.removeAll(d);
    }
    Effect e = holder(p, correctionKeyframesOf(c, p));
    const auto [category, name] = split(p);
    setKeyframe(e, category, name, t, span, kept);
    ParameterKeyframes keys = keyframesOf(e, category, name);
    // Curves and Levels points do not blend (ours): their keyframes hold.
    if (kind == CorrectionKeyframing::Track) keys.interpolation = Interpolation::Shelf;
    setCorrectionKeyframes(c, p, keys);
    c.values.insert(p, kept);
}

bool removeCorrectionKeyframe(ColorCorrection& c, const QString& path, double t, const EffectSpan& span) {
    const QString p = canonicalControlPath(path);
    ParameterKeyframes keys = correctionKeyframesOf(c, p);
    const int i = keyframeAt(keys, t, span);
    if (i < 0) return false;
    const QJsonValue last = keys.keys.at(i).value;
    keys.keys.removeAt(i);
    setCorrectionKeyframes(c, p, keys);
    if (keys.keys.isEmpty()) c.values.insert(p, last);
    return true;
}

ColorCorrection correctionAt(const ColorCorrection& c, double t, const EffectSpan& span) {
    ColorCorrection out = c;
    out.keyframes = {};
    for (auto it = c.keyframes.constBegin(); it != c.keyframes.constEnd(); ++it) {
        const ParameterKeyframes keys = ParameterKeyframes::fromJson(it.value().toObject());
        if (keys.keys.isEmpty()) continue;
        const bool numbers = std::all_of(keys.keys.cbegin(), keys.keys.cend(), [](const EffectKeyframe& k) { return k.value.isDouble(); });
        if (numbers && keys.interpolation != Interpolation::Shelf) {
            const auto [category, name] = split(it.key());
            if (const auto v = numberAt(holder(it.key(), keys), category, name, t, span)) out.values.insert(it.key(), *v);
            continue;
        }
        // Held: the keyframe at or before the time (half a frame early counts), else the first.
        QJsonValue v = keys.keys.first().value;
        for (const EffectKeyframe& k : keys.keys) {
            if (keyframeTime(keys, k, span) <= t + span.halfFrame()) v = k.value;
        }
        out.values.insert(it.key(), v);
    }
    return out;
}

ColorCorrection adjustedAt(const ColorCorrection& stored, const ColorCorrection& edited, double t, const EffectSpan& span) {
    const ColorCorrection now = correctionAt(stored, t, span);
    ColorCorrection out = stored;
    out.disabled = edited.disabled;
    QStringList keys = now.values.keys();
    for (const QString& k : edited.values.keys()) {
        if (!keys.contains(k)) keys << k;
    }
    for (const QString& k : keys) {
        const bool has = edited.values.contains(k);
        if (has == now.values.contains(k) && (!has || edited.values.value(k) == now.values.value(k))) continue;
        if (stored.keyframes.contains(k)) {
            if (!has) {
                out.keyframes.remove(k);
                out.values.remove(k);
            } else {
                const QStringList disabled = out.disabled;
                setCorrectionKeyframe(out, k, t, span, edited.values.value(k));
                out.disabled = disabled; // the tool's buttons, as edited
            }
        } else if (has) {
            out.values.insert(k, edited.values.value(k));
        } else {
            out.values.remove(k);
        }
    }
    return out;
}

QList<double> correctionKeyframeFrames(const ColorCorrection& c, const EffectSpan& span) {
    QList<double> out;
    for (auto it = c.keyframes.constBegin(); it != c.keyframes.constEnd(); ++it) {
        const ParameterKeyframes keys = ParameterKeyframes::fromJson(it.value().toObject());
        for (const EffectKeyframe& k : keys.keys) {
            const double f = std::round(span.frameOf(keyframeTime(keys, k, span)) * 1000.0) / 1000.0;
            if (!out.contains(f)) out << f;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace lumenis::model

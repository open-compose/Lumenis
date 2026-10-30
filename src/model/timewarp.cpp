#include "lumenis/model/timewarp.h"

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/motion_effect.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

// The Timewarp effect's model (timewarp.h).
namespace lumenis::model {

QString timewarpTypeName(TimewarpType t) {
    switch (t) {
    case TimewarpType::DuplicatedField: return QStringLiteral("duplicated");
    case TimewarpType::BothFields: return QStringLiteral("both");
    case TimewarpType::InterpolatedField: return QStringLiteral("interpolated");
    case TimewarpType::VtrStyle: return QStringLiteral("vtr");
    case TimewarpType::BlendedInterpolated: return QStringLiteral("blendedInterpolated");
    case TimewarpType::BlendedVtr: return QStringLiteral("blendedVtr");
    case TimewarpType::FluidMotionDraft: return QStringLiteral("fluidMotionDraft");
    case TimewarpType::FluidMotion: return QStringLiteral("fluidMotion");
    }
    return QStringLiteral("duplicated");
}

TimewarpType timewarpTypeFromName(const QString& name) {
    // A plain array rather than a braced list in the loop, which MSVC has mishandled.
    static const TimewarpType all[] = {TimewarpType::BothFields, TimewarpType::InterpolatedField, TimewarpType::VtrStyle,
                                       TimewarpType::BlendedInterpolated, TimewarpType::BlendedVtr, TimewarpType::FluidMotionDraft,
                                       TimewarpType::FluidMotion};
    for (const TimewarpType t : all) {
        if (timewarpTypeName(t) == name) return t;
    }
    return TimewarpType::DuplicatedField;
}

bool isTimewarp(const Effect& effect) {
    return effect.category == QLatin1String(kTimewarpCategory);
}

Timewarp timewarpOf(const Effect& effect) {
    Timewarp t;
    const QJsonObject o = effect.parameters.value(QLatin1String(kTimewarpCategory)).toObject();
    t.positionGraph = o.value(QStringLiteral("Graph")).toString() == QLatin1String("position");
    t.anchor = o.value(QStringLiteral("Anchor")).toDouble(0.0);
    t.anchorAt = std::clamp(o.value(QStringLiteral("Anchor At")).toDouble(0.0), 0.0, 1.0);
    t.strobe = std::max(1, o.value(QStringLiteral("Strobe")).toInt(1));
    t.type = timewarpTypeFromName(o.value(QStringLiteral("Type")).toString());
    return t;
}

void setTimewarp(Effect& effect, const Timewarp& t) {
    effect.parameters.insert(QLatin1String(kTimewarpCategory),
                             QJsonObject{{QStringLiteral("Graph"), t.positionGraph ? QStringLiteral("position") : QStringLiteral("speed")},
                                         {QStringLiteral("Anchor"), t.anchor},
                                         {QStringLiteral("Anchor At"), t.anchorAt},
                                         {QStringLiteral("Strobe"), t.strobe},
                                         {QStringLiteral("Type"), timewarpTypeName(t.type)}});
}

namespace {

// Keyframes for one of the two graphs, at effect times, with an interpolation.
void setGraph(Effect& effect, bool position, const QList<std::pair<double, double>>& points, std::int64_t length,
              Interpolation interpolation) {
    const QString category = QLatin1String(position ? kPositionCategory : kSpeedCategory);
    const QString name = QLatin1String(position ? kPositionParameter : kSpeedParameter);
    const EffectSpan span(length);
    for (const auto& [t, value] : points) setKeyframe(effect, category, name, t, span, value);
    ParameterKeyframes keys = keyframesOf(effect, category, name);
    keys.interpolation = interpolation;
    setKeyframes(effect, category, name, keys);
}

} // namespace

Effect timewarpForSegment(const Effect& effect, std::int64_t length) {
    if (!isTimewarp(effect)) return effect;
    Effect out = effect;
    // A template already set up (dragged from a bin, applied again) keeps its graphs.
    if (out.parameters.contains(QLatin1String(kTimewarpCategory))) return out;
    Timewarp t;
    const double last = static_cast<double>(std::max<std::int64_t>(length - 1, 0));
    const QString& name = effect.name;
    if (name == QLatin1String("0% To 100%")) {
        setGraph(out, false, {{0.0, 0.0}, {1.0, 100.0}}, length, Interpolation::Linear);
    } else if (name.compare(QLatin1String("100% to 0%"), Qt::CaseInsensitive) == 0) {
        setGraph(out, false, {{0.0, 100.0}, {1.0, 0.0}}, length, Interpolation::Linear);
        // "The last frame of the clip is the last frame of the motion effect" (p.203).
        t.anchor = last;
        t.anchorAt = 1.0;
    } else if (name == QLatin1String("Reverse Motion")) {
        t.positionGraph = true;
        setGraph(out, true, {{0.0, last}, {1.0, 0.0}}, length, Interpolation::Linear);
    } else if (name == QLatin1String("Speed Boost")) {
        setGraph(out, false, {{0.0, 50.0}, {0.5, 200.0}, {1.0, 50.0}}, length, Interpolation::Spline);
    } else if (name == QLatin1String("Speed Bump")) {
        setGraph(out, false, {{0.0, 100.0}, {0.5, 50.0}, {1.0, 100.0}}, length, Interpolation::Spline);
    } else if (name == QLatin1String("Trim to Fill")) {
        t.positionGraph = true;
        setGraph(out, true, {{0.0, 0.0}, {1.0, last}}, length, Interpolation::Linear);
    } else if (name.startsWith(QLatin1String("FluidFilm"))) {
        // "Rendering option: FluidMotion" (p.204).
        t.type = TimewarpType::FluidMotion;
    }
    setTimewarp(out, t);
    return out;
}

double timewarpPosition(const Effect& effect, std::int64_t offset, std::int64_t length) {
    const Timewarp t = timewarpOf(effect);
    const EffectSpan span(std::max<std::int64_t>(length, 1));
    // Strobe: "a rate of 5 causes every fifth frame to be held for five frames before updating" (p.194).
    const std::int64_t o = t.strobe > 1 ? offset - ((offset % t.strobe) + t.strobe) % t.strobe : offset;
    if (t.positionGraph) {
        const auto v = numberAt(effect, QLatin1String(kPositionCategory), QLatin1String(kPositionParameter), span.timeOf(static_cast<double>(o)), span);
        return v ? *v : static_cast<double>(o);
    }
    // The Speed graph: the speeds of the frames between the anchor and this one, added up.
    const QString category = QLatin1String(kSpeedCategory);
    const QString name = QLatin1String(kSpeedParameter);
    const std::int64_t anchorFrame = std::llround(span.frameOf(t.anchorAt));
    const bool keyed = !keyframesOf(effect, category, name).keys.isEmpty();
    auto speed = [&](std::int64_t frame) {
        const auto v = numberAt(effect, category, name, span.timeOf(static_cast<double>(frame)), span);
        return (v ? *v : 100.0) / 100.0;
    };
    if (!keyed) return t.anchor + speed(0) * static_cast<double>(o - anchorFrame);
    double position = t.anchor;
    if (o >= anchorFrame) {
        for (std::int64_t f = anchorFrame; f < o; ++f) position += speed(f);
    } else {
        for (std::int64_t f = o; f < anchorFrame; ++f) position -= speed(f);
    }
    return position;
}

bool blendsFrames(const Effect& effect) {
    if (!isTimewarp(effect)) return false;
    switch (timewarpOf(effect).type) {
    case TimewarpType::BlendedInterpolated:
    case TimewarpType::BlendedVtr:
    case TimewarpType::FluidMotionDraft:
    case TimewarpType::FluidMotion: return true;
    default: return false;
    }
}

Effect promotedTimewarp(double speed) {
    Effect e{QLatin1String(kTimewarpCategory), QStringLiteral("Timewarp"), {}};
    e.parameters.insert(QLatin1String(kSpeedCategory), QJsonObject{{QLatin1String(kSpeedParameter), speed * 100.0}});
    setTimewarp(e, Timewarp{});
    return e;
}

bool promoteToTimewarp(Segment& segment, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve) {
    if (segment.effect) {
        const auto speed = motionSpeed(*segment.effect);
        if (!speed) return false;
        segment.effect = promotedTimewarp(*speed);
        return true;
    }
    if (segment.isFiller() || !resolve) return false;
    const auto clip = resolve(segment.binId, segment.clipId);
    const auto motion = clip ? motionEffectOf(*clip) : std::nullopt;
    if (!motion) return false;
    // The source clip, from the source frame the segment's first frame showed, at the clip's speed.
    segment.binId = motion->sourceBin;
    segment.clipId = motion->sourceClip;
    segment.sourceIn = motion->sourceFrame(segment.sourceIn);
    segment.sourceMob = MobId();
    segment.effect = promotedTimewarp(motion->speed);
    return true;
}

} // namespace lumenis::model

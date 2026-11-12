// Effect templates in bins (FX Guide p.52-55; docs/spec/effect-templates.md): what a template keeps, the bin
// item that holds it, and applying it to segments, transitions and one parameter group.
#include "lumenis/model/effect_template.h"

#include "lumenis/model/shown_names.h"

#include "lumenis/model/effect_keyframes.h"

#include <QJsonArray>

#include <algorithm>

namespace lumenis::model {

namespace {

// The composition key of a video effect template (Audio EQ templates use "eq", AudioSuite ones "audioSuite").
constexpr char kTemplateKey[] = "videoEffect";

const std::pair<TransitionAlignment, const char*> kAlignments[] = {
    {TransitionAlignment::EndingAtCut, "ending"},
    {TransitionAlignment::CenteredOnCut, "centered"},
    {TransitionAlignment::StartingAtCut, "starting"},
    {TransitionAlignment::CustomStart, "custom"},
};

QString alignmentName(TransitionAlignment a) {
    for (const auto& [value, name] : kAlignments) {
        if (value == a) return QLatin1String(name);
    }
    return QStringLiteral("centered");
}

TransitionAlignment alignmentFrom(const QString& name) {
    for (const auto& [value, text] : kAlignments) {
        if (name == QLatin1String(text)) return value;
    }
    return TransitionAlignment::CenteredOnCut;
}

// A segment kept as a one-track sequence's JSON (the sequence format already reads and writes every
// member of a segment).
QJsonObject segmentJson(const Segment& segment) {
    Sequence holder;
    holder.ensureTrack(QStringLiteral("V1"));
    holder.overwrite(QStringLiteral("V1"), 0, {segment});
    return holder.toJson();
}

std::optional<Segment> segmentFrom(const QJsonObject& o) {
    const Sequence holder = Sequence::fromJson(o);
    const Track* v1 = holder.track(QStringLiteral("V1"));
    if (!v1) return std::nullopt;
    for (const Segment& s : v1->segments) {
        if (!s.isFiller() || s.effect) return s;
    }
    return std::nullopt;
}

} // namespace

QJsonObject EffectTemplate::toJson() const {
    QJsonObject o{{QStringLiteral("effect"), effect.toJson()}};
    if (transition) {
        o.insert(QStringLiteral("transition"), true);
        o.insert(QStringLiteral("duration"), static_cast<qint64>(duration));
        o.insert(QStringLiteral("alignment"), alignmentName(alignment));
        if (alignment == TransitionAlignment::CustomStart) o.insert(QStringLiteral("start"), static_cast<qint64>(start));
    }
    if (length > 0) o.insert(QStringLiteral("length"), static_cast<qint64>(length));
    if (editRate.num() > 0) o.insert(QStringLiteral("editRate"), QJsonArray{static_cast<qint64>(editRate.num()), static_cast<qint64>(editRate.den())});
    if (!source.isEmpty()) o.insert(QStringLiteral("source"), source);
    return o;
}

std::optional<EffectTemplate> EffectTemplate::fromJson(const QJsonObject& o) {
    if (!o.value(QStringLiteral("effect")).isObject()) return std::nullopt;
    EffectTemplate t;
    t.effect = Effect::fromJson(o.value(QStringLiteral("effect")).toObject());
    if (t.effect.name.isEmpty()) return std::nullopt;
    t.transition = o.value(QStringLiteral("transition")).toBool();
    t.duration = std::max<qint64>(o.value(QStringLiteral("duration")).toInteger(), 0);
    t.alignment = alignmentFrom(o.value(QStringLiteral("alignment")).toString());
    t.start = o.value(QStringLiteral("start")).toInteger();
    t.length = std::max<qint64>(o.value(QStringLiteral("length")).toInteger(), 0);
    const QJsonArray rate = o.value(QStringLiteral("editRate")).toArray();
    if (rate.size() == 2 && rate.at(0).toInteger() > 0 && rate.at(1).toInteger() > 0) {
        t.editRate = Rational(rate.at(0).toInteger(), rate.at(1).toInteger());
    }
    t.source = o.value(QStringLiteral("source")).toObject();
    return t;
}

std::optional<EffectTemplate> segmentTemplate(const Segment& segment, const Rational& editRate, bool withSource) {
    if (!segment.effect) return std::nullopt;
    EffectTemplate t;
    t.effect = *segment.effect;
    t.length = segment.length;
    t.editRate = editRate;
    if (withSource) {
        // The segment as it lies, without what belongs to its place in the sequence: the transition at its
        // head, the renders, an Add Edit.
        Segment kept = segment;
        kept.transition.reset();
        kept.transitionRender.reset();
        kept.render.reset();
        kept.addEdit = false;
        t.source = segmentJson(kept);
    }
    return t;
}

EffectTemplate transitionTemplate(const Transition& transition, const Rational& editRate) {
    EffectTemplate t;
    t.effect = transition.effect;
    t.transition = true;
    t.duration = transition.duration;
    t.alignment = transition.alignment;
    t.start = transition.start;
    t.length = transition.duration;
    t.editRate = editRate;
    return t;
}

std::optional<Segment> templateSource(const EffectTemplate& t) {
    if (!t.hasSource()) return std::nullopt;
    auto s = segmentFrom(t.source);
    if (s) s->effect = t.effect;
    return s;
}

BinItem effectTemplateItem(const EffectTemplate& t, const QString& name, const QDateTime& when) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::Effect;
    item.name = name.isEmpty() ? effectTemplateName(t) : name;
    item.created = when;
    item.modified = when;
    if (t.hasSource()) {
        // A template with its source media lasts as long as the segment did.
        item.durationFrames = t.length;
    }
    item.composition.insert(QLatin1String(kTemplateKey), t.toJson());
    return item;
}

std::optional<EffectTemplate> effectTemplateOf(const BinItem& item) {
    if (item.kind != BinItemKind::Effect || !item.composition.value(QLatin1String(kTemplateKey)).isObject()) return std::nullopt;
    return EffectTemplate::fromJson(item.composition.value(QLatin1String(kTemplateKey)).toObject());
}

QString effectTemplateName(const EffectTemplate& t) {
    return shownName(t.effect.name);
}

Effect adaptedEffect(const EffectTemplate& t, const Rational& editRate) {
    Effect e = t.effect;
    if (t.editRate.num() <= 0 || editRate.num() <= 0 || t.editRate == editRate) return e;
    const double scale = editRate.toDouble() / t.editRate.toDouble();
    for (const QString& key : keyframedParameters(e)) {
        const qsizetype slash = key.indexOf(QLatin1Char('/'));
        if (slash < 0) continue;
        const QString category = key.left(slash);
        const QString name = key.mid(slash + 1);
        ParameterKeyframes keys = keyframesOf(e, category, name);
        if (!keys.fixed) continue;
        // Fixed keyframes count in frames from the effect's start (p.124): the same time at the new rate.
        for (EffectKeyframe& k : keys.keys) {
            k.at *= scale;
            k.inTime *= scale;
            k.outTime *= scale;
        }
        setKeyframes(e, category, name, keys);
    }
    return e;
}

bool applyTemplate(Sequence& sequence, const QString& track, std::int64_t frame, const EffectTemplate& t,
                   const Rational& editRate, bool nest, bool sizeToFitRoom, const SourceLength& sourceLength) {
    Track* tr = sequence.track(track);
    if (!tr || tr->kind != TrackKind::Video || tr->locked) return false;
    const Effect effect = adaptedEffect(t, editRate);
    if (const auto source = templateSource(t)) {
        // Edited in over the track from the frame, effect and all.
        Segment s = *source;
        s.effect = effect;
        if (s.length <= 0) return false;
        sequence.overwrite(track, std::max<std::int64_t>(frame, 0), {s});
        return true;
    }
    if (t.transition) {
        Transition transition{effect, std::max<std::int64_t>(t.duration, 1), t.alignment, t.start};
        const auto room = transitionRoom(sequence, track, frame, sourceLength);
        if (!room) return false;
        if (!transitionFits(transition, *room)) {
            if (!sizeToFitRoom) return false;
            transition = sizeToFit(transition, *room);
        }
        return applyTransition(sequence, track, frame, transition, sourceLength);
    }
    return applySegmentEffect(sequence, track, frame, effect, nest);
}

bool applyTemplateGroup(Effect& target, const EffectTemplate& t, const QString& category, const Rational& editRate) {
    const Effect from = adaptedEffect(t, editRate);
    Effect changed = target;
    // The group's values: the template's, or none (its defaults).
    if (from.parameters.contains(category)) {
        changed.parameters.insert(category, from.parameters.value(category));
    } else {
        changed.parameters.remove(category);
    }
    // And its keyframes.
    const QString prefix = category + QLatin1Char('/');
    for (const QString& key : keyframedParameters(changed)) {
        if (key.startsWith(prefix)) setKeyframes(changed, category, key.mid(prefix.size()), {});
    }
    for (const QString& key : keyframedParameters(from)) {
        if (key.startsWith(prefix)) {
            const QString name = key.mid(prefix.size());
            setKeyframes(changed, category, name, keyframesOf(from, category, name));
        }
    }
    if (changed == target) return false;
    target = changed;
    return true;
}

QString promotedTemplateName(const QString& name, const QStringList& taken) {
    for (int n = 1;; ++n) {
        const QString candidate = QStringLiteral("%1.Promoted.%2").arg(name).arg(n, 2, 10, QLatin1Char('0'));
        if (!taken.contains(candidate)) return candidate;
    }
}

} // namespace lumenis::model

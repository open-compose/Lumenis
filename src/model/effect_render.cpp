#include "lumenis/model/effect_render.h"

#include "lumenis/model/timewarp.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

// Which effects can be rendered, render keys and Clear Renders (docs/spec/rendering.md). See
// effect_render.h.
namespace lumenis::model {

namespace {

// The first frame of each segment of a track, and the track's end.
QList<std::int64_t> starts(const Track& t) {
    QList<std::int64_t> s;
    std::int64_t at = 0;
    for (const Segment& seg : t.segments) {
        s << at;
        at += seg.length;
    }
    s << at;
    return s;
}

// The transition at the start of segment `i` (starting at `at`) as a target.
RenderTarget transitionTarget(const Track& t, int i, std::int64_t at) {
    const Transition& tr = *t.segments.at(i).transition;
    return {t.name(), i, true, at - tr.before(), tr.duration};
}

const Segment* segmentOf(const Sequence& sequence, const RenderTarget& target) {
    const Track* t = sequence.track(target.track);
    if (!t || t->kind != TrackKind::Video || target.segment < 0 || target.segment >= t->segments.size()) return nullptr;
    return &t->segments.at(target.segment);
}

// A segment as the key sees it: where it starts relative to `origin`, and what it shows.
QJsonObject piece(const Segment& s, std::int64_t at, std::int64_t origin) {
    QJsonObject o{{QStringLiteral("at"), static_cast<qint64>(at - origin)}, {QStringLiteral("length"), static_cast<qint64>(s.length)}};
    if (!s.isFiller()) {
        o.insert(QStringLiteral("clip"), s.clipId);
        o.insert(QStringLiteral("bin"), s.binId);
        o.insert(QStringLiteral("track"), s.sourceTrack);
        o.insert(QStringLiteral("in"), static_cast<qint64>(s.sourceIn));
    }
    if (s.effect) o.insert(QStringLiteral("effect"), s.effect->toJson());
    // So is a Color Correction mode correction, applied before the effect (FX Guide p.597).
    if (s.hasCorrection()) o.insert(QStringLiteral("correction"), segmentCorrection(s).toJson());
    // A nest is the effect's picture (6.6a): an edit inside it puts the render out of date.
    if (s.hasNest()) {
        o.insert(QStringLiteral("nest"), s.nest);
        o.insert(QStringLiteral("nestIn"), static_cast<qint64>(s.nestIn));
    }
    if (s.transition) {
        o.insert(QStringLiteral("transition"), QJsonObject{{QStringLiteral("effect"), s.transition->effect.toJson()},
                                                           {QStringLiteral("before"), static_cast<qint64>(s.transition->before())},
                                                           {QStringLiteral("duration"), static_cast<qint64>(s.transition->duration)}});
    }
    return o;
}

} // namespace

QList<RenderTarget> renderTargets(const Sequence& sequence, const QStringList& tracks, std::int64_t from, std::int64_t to) {
    QList<RenderTarget> out;
    QList<const Track*> chosen;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Video && tracks.contains(t.name())) chosen << &t;
    }
    std::sort(chosen.begin(), chosen.end(), [](const Track* a, const Track* b) { return a->number < b->number; });
    for (const Track* t : chosen) {
        const QList<std::int64_t> at = starts(*t);
        for (int i = 0; i < t->segments.size(); ++i) {
            const Segment& s = t->segments.at(i);
            if (s.transition && s.transition->duration > 0) {
                const RenderTarget r = transitionTarget(*t, i, at.at(i));
                if (r.start < to && r.end() > from) out << r;
            }
            if (s.effect) {
                const RenderTarget r{t->name(), i, false, at.at(i), s.length};
                if (r.start < to && r.end() > from) out << r;
            }
        }
    }
    return out;
}

std::optional<RenderTarget> renderTargetAt(const Sequence& sequence, const QString& track, std::int64_t frame) {
    const Track* t = sequence.track(track);
    if (!t || t->kind != TrackKind::Video) return std::nullopt;
    std::int64_t at = 0;
    const int i = t->segmentAt(frame, &at);
    if (i < 0) return std::nullopt;
    // The next segment's transition reaching back into this one, then this one's own reaching in.
    if (i + 1 < t->segments.size() && t->segments.at(i + 1).transition) {
        const RenderTarget r = transitionTarget(*t, i + 1, at + t->segments.at(i).length);
        if (r.length > 0 && frame >= r.start && frame < r.end()) return r;
    }
    if (t->segments.at(i).transition) {
        const RenderTarget r = transitionTarget(*t, i, at);
        if (r.length > 0 && frame >= r.start && frame < r.end()) return r;
    }
    if (t->segments.at(i).effect) return RenderTarget{t->name(), i, false, at, t->segments.at(i).length};
    return std::nullopt;
}

const Effect* effectOf(const Sequence& sequence, const RenderTarget& target) {
    const Segment* s = segmentOf(sequence, target);
    if (!s) return nullptr;
    if (target.transition) return s->transition ? &s->transition->effect : nullptr;
    return s->effect ? &*s->effect : nullptr;
}

std::optional<EffectRender> renderOf(const Sequence& sequence, const RenderTarget& target) {
    const Segment* s = segmentOf(sequence, target);
    if (!s) return std::nullopt;
    if (target.transition) return s->transition ? s->transitionRender : std::nullopt;
    return s->effect ? s->render : std::nullopt;
}

QString renderKey(const Sequence& sequence, const RenderTarget& target) {
    const Effect* effect = effectOf(sequence, target);
    const auto ref = parseTrackName(target.track);
    if (!effect || !ref) return {};
    QJsonObject key{{QStringLiteral("effect"), effect->toJson()},
                    {QStringLiteral("transition"), target.transition},
                    {QStringLiteral("length"), static_cast<qint64>(target.length)}};
    QJsonObject tracks;
    for (const Track& t : sequence.tracks()) {
        if (t.kind != TrackKind::Video || t.number > ref->number) continue;
        QJsonArray pieces;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            // Segments over the effect's frames, and the one starting just after them (its transition
            // can reach back in).
            if (at <= target.end() && at + s.length > target.start) pieces.append(piece(s, at, target.start));
            at += s.length;
        }
        tracks.insert(t.name(), pieces);
    }
    key.insert(QStringLiteral("tracks"), tracks);
    const QByteArray json = QJsonDocument(key).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha1).toHex());
}

std::int64_t renderedFrames(const EffectRender& render) {
    std::int64_t frames = 0;
    for (const QJsonObject& c : render.clips) {
        if (const auto item = binItemFromJson(c)) frames += std::max<std::int64_t>(item->durationFrames, 0);
    }
    return frames;
}

std::optional<std::pair<QJsonObject, std::int64_t>> renderedClipAt(const EffectRender& render, std::int64_t frame) {
    std::int64_t at = 0;
    for (const QJsonObject& c : render.clips) {
        const auto item = binItemFromJson(c);
        if (!item) continue;
        if (frame >= at && frame < at + item->durationFrames) return std::make_pair(c, frame - at);
        at += item->durationFrames;
    }
    return std::nullopt;
}

namespace {

// The target's render when it is in date.
std::optional<EffectRender> inDate(const Sequence& sequence, const RenderTarget& target) {
    auto r = renderOf(sequence, target);
    if (!r || r->key.isEmpty() || r->key != renderKey(sequence, target)) return std::nullopt;
    return r;
}

} // namespace

bool isRendered(const Sequence& sequence, const RenderTarget& target) {
    const auto r = inDate(sequence, target);
    return r && renderedFrames(*r) >= target.length;
}

bool isPartlyRendered(const Sequence& sequence, const RenderTarget& target) {
    const auto r = inDate(sequence, target);
    if (!r) return false;
    const std::int64_t frames = renderedFrames(*r);
    return frames > 0 && frames < target.length;
}

bool isRenderedAt(const Sequence& sequence, const RenderTarget& target, std::int64_t frame) {
    const auto r = inDate(sequence, target);
    return r && frame >= 0 && frame < renderedFrames(*r);
}

bool setRender(Sequence& sequence, const RenderTarget& target, const std::optional<EffectRender>& render) {
    Track* t = sequence.track(target.track);
    if (!t || t->kind != TrackKind::Video || target.segment < 0 || target.segment >= t->segments.size()) return false;
    Segment& s = t->segments[target.segment];
    if (target.transition) {
        if (!s.transition) return false;
        s.transitionRender = render;
        return true;
    }
    if (!s.effect) return false;
    s.render = render;
    return true;
}

QList<RenderTarget> expertRender(const Sequence& sequence, const QStringList& selected, std::int64_t from, std::int64_t to,
                                 const ExpertRender& options) {
    // The video tracks taken into account: those at or below the topmost selected one, top first.
    int top = 0;
    for (const QString& name : selected) {
        const auto ref = parseTrackName(name);
        const Track* t = sequence.track(name);
        if (ref && t && t->kind == TrackKind::Video) top = std::max(top, ref->number);
    }
    QList<const Track*> tracks;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Video && t.number <= top) tracks << &t;
    }
    std::sort(tracks.begin(), tracks.end(), [](const Track* a, const Track* b) { return a->number > b->number; });
    QStringList names;
    for (const Track* t : tracks) names << t->name();
    // Where the effects there begin and end: between two such points the same effects show.
    const QList<RenderTarget> all = renderTargets(sequence, names, from, to);
    QList<std::int64_t> cuts{from, to};
    for (const RenderTarget& t : all) {
        if (t.start > from && t.start < to) cuts << t.start;
        if (t.end() > from && t.end() < to) cuts << t.end();
    }
    for (const auto& [a, b] : options.recommended) {
        if (a > from && a < to) cuts << a;
        if (b > from && b < to) cuts << b;
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    QList<RenderTarget> out;
    for (qsizetype i = 0; i + 1 < cuts.size(); ++i) {
        const std::int64_t a = cuts.at(i);
        const std::int64_t b = cuts.at(i + 1);
        if (options.mode == ExpertMode::RecommendedRanges) {
            bool inside = false;
            for (const auto& [ra, rb] : options.recommended) inside = inside || (a < rb && b > ra);
            if (!inside) continue;
        }
        // The unrendered effects showing over [a, b), top track first.
        QList<RenderTarget> here;
        bool slow = false;
        for (const Track* t : tracks) {
            const auto target = renderTargetAt(sequence, t->name(), a);
            if (!target || isRendered(sequence, *target)) continue;
            here << *target;
            const Effect* e = effectOf(sequence, *target);
            if (e && options.realTime && !options.realTime(*e)) slow = true;
        }
        if (here.isEmpty()) continue;
        if (options.mode != ExpertMode::DigitalCut && !slow && here.size() <= options.realTimeLimit) continue;
        // The topmost selected track's effect takes in everything below it.
        for (const RenderTarget& t : here) {
            if (!selected.contains(t.track)) continue;
            if (!out.contains(t)) out << t;
            break;
        }
    }
    std::sort(out.begin(), out.end(), [](const RenderTarget& x, const RenderTarget& y) {
        const auto rx = parseTrackName(x.track);
        const auto ry = parseTrackName(y.track);
        const int nx = rx ? rx->number : 0;
        const int ny = ry ? ry->number : 0;
        return nx != ny ? nx < ny : x.start < y.start;
    });
    return out;
}

int clearRenders(Sequence& sequence, const QList<RenderTarget>& targets, const ClearRenders& options) {
    int cleared = 0;
    for (const RenderTarget& target : targets) {
        const Effect* effect = effectOf(sequence, target);
        if (!effect || !renderOf(sequence, target)) continue;
        // Motion effects: a segment's Motion Effect and Timewarps (FX Guide p.165).
        if (options.skipMotionEffects && (motionSpeed(*effect) || isTimewarp(*effect))) continue;
        if (options.skipUnknownEffects && options.known && !options.known(*effect)) continue;
        if (setRender(sequence, target, std::nullopt)) ++cleared;
    }
    return cleared;
}

} // namespace lumenis::model

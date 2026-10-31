#include "lumenis/model/nest.h"

#include "lumenis/model/timewarp.h"

#include <algorithm>
#include <optional>

namespace lumenis::model {

Sequence nestOf(const Segment& segment) {
    if (!segment.hasNest()) {
        Sequence empty;
        return empty;
    }
    return Sequence::fromJson(segment.nest);
}

void setNest(Segment& segment, const Sequence& nest) {
    segment.nest = nest.toJson();
}

namespace {

// The segment's picture alone, as its nest's V1 holds it: the clip without the effect, transition or render.
Segment innerOf(const Segment& segment) {
    Segment inner = segment;
    inner.transition.reset();
    inner.transitionRender.reset();
    inner.addEdit = false;
    return inner;
}

// A nest of one video track holding `material`, the rest of the default tracks empty.
Sequence oneTrackNest(const Segment& material) {
    Sequence nest;
    nest.ensureTrack(QStringLiteral("V1"));
    nest.overwrite(QStringLiteral("V1"), 0, {material});
    return nest;
}

} // namespace

bool ensureNest(Segment& segment) {
    if (segment.hasNest()) return true;
    if (!segment.effect) return false;
    Segment inner = innerOf(segment);
    inner.effect.reset();
    inner.render.reset();
    inner.nest = {};
    // Filler (a Submaster's) nests filler: an empty track.
    if (inner.isFiller()) inner = Segment::filler(segment.length);
    setNest(segment, oneTrackNest(inner));
    segment.nestIn = 0;
    return true;
}

void nestEffect(Segment& segment, const Effect& outer) {
    if (!segment.effect) {
        segment.effect = outer;
        return;
    }
    // The segment as it is, effect, render and nest and all, goes inside.
    Segment inner = innerOf(segment);
    if (segment.hasNest()) {
        // Its nest frames go with it: the inner segment shows the nest from where this one did.
        inner.nestIn = segment.nestIn;
    }
    if (inner.isFiller() && !inner.effect && !inner.hasNest()) inner = Segment::filler(segment.length);
    setNest(segment, oneTrackNest(inner));
    segment.nestIn = 0;
    segment.effect = outer;
    segment.render.reset();
}

bool isSubmaster(const Effect& effect) {
    return effect.category == QLatin1String(kSubmasterCategory) && effect.name == QLatin1String(kSubmasterName);
}

bool collapseTracks(Sequence& sequence, const QStringList& tracks, std::int64_t start, std::int64_t end, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (end <= start) return fail(QStringLiteral("Mark an IN point and an OUT point around the area to collapse."));
    // The video tracks, lowest first; "The tracks must be adjacent" (p.226).
    QList<int> numbers;
    for (const QString& name : tracks) {
        const auto ref = parseTrackName(name);
        if (!ref || ref->kind != TrackKind::Video || !sequence.track(name)) continue;
        numbers << ref->number;
    }
    std::sort(numbers.begin(), numbers.end());
    numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
    if (numbers.size() < 1) return fail(QStringLiteral("Select the video tracks to collapse."));
    for (int k = 1; k < numbers.size(); ++k) {
        if (numbers.at(k) != numbers.at(k - 1) + 1) return fail(QStringLiteral("The tracks to collapse must be adjacent."));
    }
    for (const int n : numbers) {
        if (sequence.track(trackName(TrackKind::Video, n))->locked) return fail(QStringLiteral("A track to collapse is locked."));
    }
    const std::int64_t length = end - start;
    Sequence nest;
    for (int k = 0; k < numbers.size(); ++k) {
        const QString to = trackName(TrackKind::Video, k + 1);
        nest.ensureTrack(to);
        nest.overwrite(to, 0, sequence.material(trackName(TrackKind::Video, numbers.at(k)), start, length));
    }
    Segment submaster = Segment::filler(length);
    submaster.effect = Effect{QLatin1String(kSubmasterCategory), QLatin1String(kSubmasterName), {}};
    setNest(submaster, nest);
    for (int k = 0; k < numbers.size(); ++k) {
        const QString name = trackName(TrackKind::Video, numbers.at(k));
        sequence.overwrite(name, start, {k == 0 ? submaster : Segment::filler(length)});
    }
    return true;
}

namespace {

// The nest's single video track holding material (V1), or null when it has none or several: only then does
// the list of nested effects go on into it.
const Track* singleVideoTrack(const Sequence& nest) {
    const Track* found = nullptr;
    for (const Track& t : nest.tracks()) {
        if (t.kind != TrackKind::Video) continue;
        const bool empty = std::all_of(t.segments.begin(), t.segments.end(),
                                       [](const Segment& s) { return s.isFiller() && !s.effect; });
        if (empty) continue;
        if (found) return nullptr;
        found = &t;
    }
    return found && found->name() == QLatin1String("V1") ? found : nullptr;
}

// The segment one level in from `segment` at `offset`: its index on the nest's V1 and the offset into it.
struct Inner {
    Sequence nest;
    int index = -1;
    std::int64_t start = 0; // in the nest's frames
    std::int64_t offset = 0;
};
std::optional<Inner> innerAt(const Segment& segment, std::int64_t offset) {
    if (!segment.hasNest()) return std::nullopt;
    Inner in;
    in.nest = nestOf(segment);
    const Track* v1 = singleVideoTrack(in.nest);
    if (!v1) return std::nullopt;
    const std::int64_t frame = segment.nestIn + std::clamp<std::int64_t>(offset, 0, std::max<std::int64_t>(segment.length - 1, 0));
    in.index = v1->segmentAt(frame, &in.start);
    if (in.index < 0 || !v1->segments.at(in.index).effect) return std::nullopt;
    in.offset = frame - in.start;
    return in;
}

} // namespace

QList<NestedEffect> nestedEffects(const Segment& segment, std::int64_t offset) {
    QList<NestedEffect> out;
    if (!segment.effect) return out;
    out << NestedEffect{*segment.effect, 0, segment.length};
    Segment at = segment;
    std::int64_t origin = 0; // where `at` starts, in the outer segment's frames
    std::int64_t off = offset;
    // Nests can hold nests without limit (p.219); a depth guard keeps a malformed bin from looping.
    for (int depth = 0; depth < 256; ++depth) {
        const auto in = innerAt(at, off);
        if (!in) break;
        const Segment inner = in->nest.track(QStringLiteral("V1"))->segments.at(in->index);
        origin += in->start - at.nestIn;
        out << NestedEffect{*inner.effect, origin, inner.length};
        at = inner;
        off = in->offset;
    }
    return out;
}

bool setNestedEffect(Segment& segment, std::int64_t offset, int level, const Effect& effect) {
    if (level < 0 || !segment.effect) return false;
    if (level == 0) {
        segment.effect = effect;
        segment.render.reset();
        return true;
    }
    auto in = innerAt(segment, offset);
    if (!in) return false;
    Track* v1 = in->nest.track(QStringLiteral("V1"));
    if (!setNestedEffect(v1->segments[in->index], in->offset, level - 1, effect)) return false;
    setNest(segment, in->nest);
    segment.render.reset();
    return true;
}

bool moveNestedEffect(Segment& segment, std::int64_t offset, int from, int to, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return false;
    };
    const QList<NestedEffect> chain = nestedEffects(segment, offset);
    const auto n = static_cast<int>(chain.size());
    if (from < 0 || to < 0 || from >= n || to >= n) return fail(QStringLiteral("There is no such nested effect."));
    if (from == to) return true;
    QList<Effect> order;
    for (const NestedEffect& e : chain) order << e.effect;
    order.move(from, to);
    // "it must be the innermost effect" (p.177).
    for (int k = 0; k + 1 < n; ++k) {
        if (isTimewarp(order.at(k))) return fail(QStringLiteral("A Timewarp effect must be the innermost effect."));
    }
    // The innermost level's nest of several tracks gives its effect the inputs it needs; another effect there
    // would not have them, nor would it elsewhere.
    {
        // Walk to the innermost segment along the same frames as nestedEffects() did.
        Segment at = segment;
        std::int64_t off = offset;
        for (int k = 1; k < n; ++k) {
            const auto in = innerAt(at, off);
            if (!in) break;
            at = in->nest.track(QStringLiteral("V1"))->segments.at(in->index);
            off = in->offset;
        }
        if (at.hasNest() && !singleVideoTrack(nestOf(at)) && (from == n - 1 || to == n - 1)) {
            return fail(QStringLiteral("The effect would not have enough video inputs."));
        }
    }
    Segment changed = segment;
    for (int k = 0; k < n; ++k) {
        if (!setNestedEffect(changed, offset, k, order.at(k))) return fail(QStringLiteral("The nested effects could not be reordered."));
    }
    segment = changed;
    return true;
}

QString nestLevelLabel(int level) {
    return level > 0 ? QStringLiteral("N%1").arg(level) : QString();
}

} // namespace lumenis::model

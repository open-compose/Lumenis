#include "lumenis/model/nest.h"

#include <algorithm>

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

QString nestLevelLabel(int level) {
    return level > 0 ? QStringLiteral("N%1").arg(level) : QString();
}

} // namespace lumenis::model

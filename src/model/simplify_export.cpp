// The Simplified AAF's spans and audio tracks (lumenis/model/simplify_export.h).
#include "lumenis/model/simplify_export.h"

#include <algorithm>
#include <set>

namespace lumenis::model {

namespace {

// The video tracks from the top (the highest number) down.
QList<const Track*> videoTracksTopDown(const Sequence& sequence) {
    QList<const Track*> out;
    for (const Track& t : sequence.tracks()) {
        if (t.kind == TrackKind::Video) out << &t;
    }
    std::sort(out.begin(), out.end(), [](const Track* a, const Track* b) {
        const auto ra = parseTrackName(a->name());
        const auto rb = parseTrackName(b->name());
        return (ra ? ra->number : 0) > (rb ? rb->number : 0);
    });
    return out;
}

} // namespace

QList<SimplifiedSpan> simplifiedVideo(const Sequence& sequence) {
    const QList<const Track*> tracks = videoTracksTopDown(sequence);
    const std::int64_t duration = sequence.duration();
    // The places where what shows can change: every cut, and the ends of every transition.
    std::set<std::int64_t> cuts{0, duration};
    struct Region {
        std::int64_t from, to;
    };
    QList<Region> transitions;
    for (const Track* t : tracks) {
        std::int64_t at = 0;
        for (const Segment& s : t->segments) {
            cuts.insert(at);
            if (s.transition && s.transition->duration > 0) {
                const Region r{at - s.transition->before(), at + s.transition->after()};
                transitions << r;
                cuts.insert(std::clamp<std::int64_t>(r.from, 0, duration));
                cuts.insert(std::clamp<std::int64_t>(r.to, 0, duration));
            }
            at += s.length;
            cuts.insert(std::min(at, duration));
        }
    }
    QList<SimplifiedSpan> out;
    auto push = [&](SimplifiedSpan span) {
        if (span.end <= span.start) return;
        if (!out.isEmpty()) {
            SimplifiedSpan& last = out.last();
            const bool sameClip = span.kind == SimplifiedSpan::Kind::Clip && last.kind == SimplifiedSpan::Kind::Clip &&
                                  last.segment.clipId == span.segment.clipId && last.segment.sourceTrack == span.segment.sourceTrack &&
                                  last.segment.sourceIn + last.segment.length == span.segment.sourceIn;
            if ((span.kind != SimplifiedSpan::Kind::Clip && span.kind == last.kind) || sameClip) {
                last.end = span.end;
                if (sameClip) last.segment.length += span.segment.length;
                return;
            }
        }
        out << span;
    };
    for (auto it = cuts.begin(); it != cuts.end(); ++it) {
        const auto next = std::next(it);
        if (next == cuts.end()) break;
        const std::int64_t a = *it, b = *next;
        if (a >= duration) break;
        SimplifiedSpan span;
        span.start = a;
        span.end = b;
        const bool inTransition =
            std::any_of(transitions.begin(), transitions.end(), [&](const Region& r) { return r.from < b && a < r.to; });
        const Segment* top = nullptr;
        std::int64_t topStart = 0;
        for (const Track* t : tracks) {
            std::int64_t start = 0;
            const int i = t->segmentAt(a, &start);
            if (i < 0) continue;
            const Segment& s = t->segments.at(i);
            if (s.isFiller() && !s.effect) continue;
            top = &s;
            topStart = start;
            break;
        }
        if (!top) {
            span.kind = inTransition ? SimplifiedSpan::Kind::Mixdown : SimplifiedSpan::Kind::Filler;
        } else if (top->effect || inTransition || !top->nest.isEmpty()) {
            span.kind = SimplifiedSpan::Kind::Mixdown;
        } else {
            span.kind = SimplifiedSpan::Kind::Clip;
            span.segment = *top;
            span.segment.sourceIn = top->sourceIn + (a - topStart);
            span.segment.length = b - a;
            span.segment.transition.reset();
            span.segment.transitionRender.reset();
        }
        push(span);
    }
    return out;
}

bool audioTrackHasEffects(const Track& track) {
    if (track.kind != TrackKind::Audio) return false;
    if (std::any_of(track.inserts.begin(), track.inserts.end(), [](const auto& i) { return i.has_value(); })) return true;
    return std::any_of(track.segments.begin(), track.segments.end(), [](const Segment& s) {
        return !s.isFiller() && (s.gain != 0.0 || !s.volumeKeys.isEmpty() || s.eq || s.audioSuite);
    });
}

} // namespace lumenis::model

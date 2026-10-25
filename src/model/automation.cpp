#include "lumenis/model/automation.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>

// Volume and pan automation (automation.h). Every operation walks the audio tracks' segments with
// their record start; a keyframe's record frame is start + (key frame - source IN).
namespace lumenis::model {

namespace {

// Keyframes this close (in frames) are the same keyframe.
constexpr double kSame = 1e-6;

double clampValue(AutomationParam param, double value) {
    return param == AutomationParam::Volume ? clampGain(value) : std::clamp(value, -double(kPanMax), double(kPanMax));
}

bool wanted(const Track& t, const QStringList& tracks) {
    return t.kind == TrackKind::Audio && (tracks.isEmpty() || tracks.contains(t.name()));
}

// Calls `f` with each material segment of the wanted audio tracks and its record start.
void eachSegment(Sequence& sequence, const QStringList& tracks, bool skipLocked,
                 const std::function<void(Track&, Segment&, std::int64_t)>& f) {
    for (const QString& name : sequence.trackNames()) {
        Track* t = sequence.track(name);
        if (!t || !wanted(*t, tracks) || (skipLocked && t->locked)) continue;
        std::int64_t at = 0;
        for (Segment& s : t->segments) {
            const std::int64_t start = at;
            at += s.length;
            if (!s.isFiller()) f(*t, s, start);
        }
    }
}

void eachSegment(const Sequence& sequence, const QStringList& tracks,
                 const std::function<void(const Track&, const Segment&, std::int64_t)>& f) {
    for (const Track& t : sequence.tracks()) {
        if (!wanted(t, tracks)) continue;
        std::int64_t at = 0;
        for (const Segment& s : t.segments) {
            const std::int64_t start = at;
            at += s.length;
            if (!s.isFiller()) f(t, s, start);
        }
    }
}

double recordFrame(const Segment& s, std::int64_t start, double sourceFrame) {
    return static_cast<double>(start) + (sourceFrame - static_cast<double>(s.sourceIn));
}

double sourceFrame(const Segment& s, std::int64_t start, double record) {
    return static_cast<double>(s.sourceIn) + (record - static_cast<double>(start));
}

// The segment's material, in its source frames: [first, end).
bool visible(const Segment& s, double frame) {
    return frame >= static_cast<double>(s.sourceIn) - kSame &&
           frame < static_cast<double>(s.sourceIn + s.length) - kSame;
}

// Sorts by frame and keeps one keyframe a position: the later one in the list wins.
void tidy(QList<AudioKey>& keys) {
    std::stable_sort(keys.begin(), keys.end(), [](const AudioKey& a, const AudioKey& b) { return a.frame < b.frame; });
    QList<AudioKey> out;
    for (const AudioKey& k : keys) {
        if (!out.isEmpty() && std::abs(out.last().frame - k.frame) < kSame) {
            out.last() = k;
        } else {
            out << k;
        }
    }
    keys = out;
}

// Inserts or replaces the keyframe at `frame`.
void put(QList<AudioKey>& keys, AudioKey key) {
    for (AudioKey& k : keys) {
        if (std::abs(k.frame - key.frame) < kSame) {
            k.value = key.value;
            return;
        }
    }
    keys << key;
    tidy(keys);
}

// The index of the keyframe at `frame`, or -1.
int indexAt(const QList<AudioKey>& keys, double frame) {
    for (int i = 0; i < keys.size(); ++i) {
        if (std::abs(keys.at(i).frame - frame) < kSame) return i;
    }
    return -1;
}

// The track and keyframe positions the refs name, by track.
QHash<QString, QList<double>> byTrack(const QList<KeyRef>& keys) {
    QHash<QString, QList<double>> out;
    for (const KeyRef& k : keys) out[k.track] << k.frame;
    return out;
}

bool named(const QList<double>& frames, double record) {
    return std::any_of(frames.begin(), frames.end(), [record](double f) { return std::abs(f - record) < kSame; });
}

// The segment's material overlaps record frames [from, to).
bool overlaps(const Segment& s, std::int64_t start, double from, double to) {
    return static_cast<double>(start + s.length) > from && static_cast<double>(start) < to;
}

} // namespace

const QList<AudioKey>& keysOf(const Segment& segment, AutomationParam param) {
    return param == AutomationParam::Volume ? segment.volumeKeys : segment.panKeys;
}

QList<AudioKey>& keysOf(Segment& segment, AutomationParam param) {
    return param == AutomationParam::Volume ? segment.volumeKeys : segment.panKeys;
}

std::optional<double> automationAt(const QList<AudioKey>& keys, double frame) {
    if (keys.isEmpty()) return std::nullopt;
    if (frame <= keys.first().frame) return keys.first().value;
    if (frame >= keys.last().frame) return keys.last().value;
    // The keyframes either side of `frame`.
    const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
                                       [](double f, const AudioKey& k) { return f < k.frame; });
    const AudioKey& b = *next;
    const AudioKey& a = *(next - 1);
    if (b.frame - a.frame < kSame) return b.value;
    if (std::abs(frame - a.frame) < kSame) return a.value;
    // -∞ ramps from the floor (kRampFloorDb); kGainOff marks only volume keyframes.
    const double va = a.value <= kGainOff ? kRampFloorDb : a.value;
    const double vb = b.value <= kGainOff ? kRampFloorDb : b.value;
    return va + (vb - va) * (frame - a.frame) / (b.frame - a.frame);
}

int sidePan(int trackNumber, bool alternatingPan) {
    if (!alternatingPan) return 0;
    return trackNumber % 2 == 1 ? -kPanMax : kPanMax;
}

double volumeAt(const Segment& segment, double frame) {
    const double automation = automationAt(segment.volumeKeys, frame).value_or(0.0);
    // A keyframe at -∞ silences only on its own frame; ramps to it pass kRampFloorDb.
    if (segment.gain <= kGainOff) return kGainOff;
    const int i = indexAt(segment.volumeKeys, frame);
    if (i >= 0 && segment.volumeKeys.at(i).value <= kGainOff) return kGainOff;
    if (!segment.volumeKeys.isEmpty() && automation <= kGainOff) return kGainOff;
    return std::min(segment.gain + automation, kGainMax);
}

double panAt(const Segment& segment, double frame, int side) {
    if (const auto pan = automationAt(segment.panKeys, frame)) return *pan;
    return segment.pan != 0 ? segment.pan : side;
}

double automationValue(const Segment& segment, AutomationParam param, double frame, int side) {
    if (param == AutomationParam::Pan) return panAt(segment, frame, side);
    const int i = indexAt(segment.volumeKeys, frame);
    if (i >= 0) return segment.volumeKeys.at(i).value;
    return automationAt(segment.volumeKeys, frame).value_or(0.0);
}

QList<KeyRef> keyframesIn(const Sequence& sequence, const QStringList& tracks, double from, double to,
                          AutomationParam param, bool hidden) {
    QList<KeyRef> out;
    eachSegment(sequence, tracks, [&](const Track& t, const Segment& s, std::int64_t start) {
        for (const AudioKey& k : keysOf(s, param)) {
            if (!hidden && !visible(s, k.frame)) continue;
            const double record = recordFrame(s, start, k.frame);
            if (record >= from - kSame && record < to - kSame) out << KeyRef{t.name(), record};
        }
    });
    return out;
}

int addKeyframes(Sequence& sequence, const QStringList& tracks, double frame, AutomationParam param,
                 bool alternatingPan) {
    int added = 0;
    eachSegment(sequence, tracks, true, [&](Track& t, Segment& s, std::int64_t start) {
        if (frame < static_cast<double>(start) - kSame || frame >= static_cast<double>(start + s.length) - kSame) return;
        const double at = sourceFrame(s, start, frame);
        QList<AudioKey>& keys = keysOf(s, param);
        if (indexAt(keys, at) >= 0) return;
        put(keys, AudioKey{at, clampValue(param, automationValue(s, param, at, sidePan(t.number, alternatingPan)))});
        ++added;
    });
    return added;
}

int setKeyframes(Sequence& sequence, const QList<KeyRef>& keys, AutomationParam param, double value) {
    const auto wantedKeys = byTrack(keys);
    int found = 0;
    eachSegment(sequence, wantedKeys.keys(), true, [&](Track& t, Segment& s, std::int64_t start) {
        const QList<double>& frames = wantedKeys.value(t.name());
        for (AudioKey& k : keysOf(s, param)) {
            if (!named(frames, recordFrame(s, start, k.frame))) continue;
            k.value = clampValue(param, value);
            ++found;
        }
    });
    return found;
}

int moveKeyframes(Sequence& sequence, const QList<KeyRef>& keys, AutomationParam param, double frames, double by) {
    const auto wantedKeys = byTrack(keys);
    int found = 0;
    eachSegment(sequence, wantedKeys.keys(), true, [&](Track& t, Segment& s, std::int64_t start) {
        const QList<double>& names = wantedKeys.value(t.name());
        QList<AudioKey>& list = keysOf(s, param);
        QList<AudioKey> kept;
        QList<AudioKey> moved;
        const double first = static_cast<double>(s.sourceIn);
        const double last = static_cast<double>(s.sourceIn + s.length) - 1e-3;
        for (const AudioKey& k : list) {
            if (!named(names, recordFrame(s, start, k.frame))) {
                kept << k;
                continue;
            }
            AudioKey m = k;
            // Only keyframes on the material move in time; it stays on it.
            if (frames != 0 && visible(s, k.frame)) m.frame = std::clamp(k.frame + frames, first, last);
            m.value = clampValue(param, (k.value <= kGainOff && param == AutomationParam::Volume && by > 0)
                                            ? kRampFloorDb + by
                                            : k.value + by);
            moved << m;
            ++found;
        }
        // A moved keyframe takes the place of one it lands on.
        list = kept + moved;
        tidy(list);
    });
    return found;
}

int deleteKeyframes(Sequence& sequence, const QList<KeyRef>& keys, AutomationParam param) {
    const auto wantedKeys = byTrack(keys);
    int found = 0;
    eachSegment(sequence, wantedKeys.keys(), true, [&](Track& t, Segment& s, std::int64_t start) {
        const QList<double>& names = wantedKeys.value(t.name());
        QList<AudioKey>& list = keysOf(s, param);
        const auto before = list.size();
        list.removeIf([&](const AudioKey& k) { return named(names, recordFrame(s, start, k.frame)); });
        found += static_cast<int>(before - list.size());
    });
    return found;
}

int removeAutomation(Sequence& sequence, const QStringList& tracks, double from, double to, AutomationParam param) {
    int removed = 0;
    eachSegment(sequence, tracks, true, [&](Track&, Segment& s, std::int64_t start) {
        if (!overlaps(s, start, from, to)) return;
        QList<AudioKey>& list = keysOf(s, param);
        const auto before = list.size();
        list.removeIf([&](const AudioKey& k) {
            const double r = recordFrame(s, start, k.frame);
            return visible(s, k.frame) && r >= from - kSame && r < to - kSame;
        });
        removed += static_cast<int>(before - list.size());
    });
    return removed;
}

int adjustAutomation(Sequence& sequence, const QStringList& tracks, double from, double to, AutomationParam param,
                     double by, bool alternatingPan) {
    int changed = 0;
    eachSegment(sequence, tracks, true, [&](Track& t, Segment& s, std::int64_t start) {
        if (!overlaps(s, start, from, to)) return;
        QList<AudioKey>& list = keysOf(s, param);
        if (list.isEmpty()) {
            const double at = static_cast<double>(s.sourceIn);
            list << AudioKey{at, clampValue(param, automationValue(s, param, at, sidePan(t.number, alternatingPan)) + by)};
            ++changed;
            return;
        }
        for (AudioKey& k : list) {
            const double r = recordFrame(s, start, k.frame);
            if (!visible(s, k.frame) || r < from - kSame || r >= to - kSame) continue;
            k.value = clampValue(param, (k.value <= kGainOff && param == AutomationParam::Volume) ? k.value : k.value + by);
            ++changed;
        }
    });
    return changed;
}

int filterAutomation(Sequence& sequence, const QStringList& tracks, double from, double to, AutomationParam param,
                     bool peaksOnly) {
    int removed = 0;
    eachSegment(sequence, tracks, true, [&](Track&, Segment& s, std::int64_t start) {
        if (!overlaps(s, start, from, to)) return;
        QList<AudioKey>& list = keysOf(s, param);
        // The keyframes in the range, by index; the first and last of them stay.
        QList<int> in;
        for (int i = 0; i < list.size(); ++i) {
            const double r = recordFrame(s, start, list.at(i).frame);
            if (visible(s, list.at(i).frame) && r >= from - kSame && r < to - kSame) in << i;
        }
        if (in.size() < 3) return;
        auto peak = [&list](int i) {
            const double v = list.at(i).value;
            const double a = list.at(i - 1).value;
            const double b = list.at(i + 1).value;
            return (v > a && v > b) || (v < a && v < b);
        };
        QSet<int> drop;
        const int interior = static_cast<int>(in.size()) - 2;
        if (peaksOnly) {
            for (int j = 1; j + 1 < in.size(); ++j) {
                if (!peak(in.at(j))) drop.insert(in.at(j));
            }
        } else {
            // Points on a straight line between their neighbours first.
            for (int j = 1; j + 1 < in.size(); ++j) {
                const int i = in.at(j);
                const AudioKey& a = list.at(i - 1);
                const AudioKey& b = list.at(i + 1);
                if (b.frame - a.frame < kSame) continue;
                const double line = a.value + (b.value - a.value) * (list.at(i).frame - a.frame) / (b.frame - a.frame);
                if (std::abs(line - list.at(i).value) < 0.05) drop.insert(i);
            }
            // Then every other one that is not a peak, until about half are gone.
            const int target = (interior + 1) / 2;
            bool skip = false;
            for (int j = 1; j + 1 < in.size() && static_cast<int>(drop.size()) < target; ++j) {
                const int i = in.at(j);
                if (drop.contains(i) || peak(i)) continue;
                if (!skip) drop.insert(i);
                skip = !skip;
            }
        }
        QList<AudioKey> out;
        for (int i = 0; i < list.size(); ++i) {
            if (!drop.contains(i)) out << list.at(i);
        }
        removed += static_cast<int>(list.size() - out.size());
        list = out;
    });
    return removed;
}

int removeHiddenKeyframes(Sequence& sequence, const QStringList& tracks, double from, double to,
                          AutomationParam param) {
    int removed = 0;
    eachSegment(sequence, tracks, true, [&](Track&, Segment& s, std::int64_t start) {
        if (!overlaps(s, start, from, to)) return;
        QList<AudioKey>& list = keysOf(s, param);
        const double first = static_cast<double>(s.sourceIn);
        const double last = static_cast<double>(s.sourceIn + s.length - 1);
        const bool left = std::any_of(list.begin(), list.end(), [first](const AudioKey& k) { return k.frame < first - kSame; });
        const bool right = std::any_of(list.begin(), list.end(), [last](const AudioKey& k) { return k.frame > last + kSame; });
        if (!left && !right) return;
        // The values at the edges of the material, before the hidden keyframes go.
        const std::optional<double> atFirst = automationAt(list, first);
        const std::optional<double> atLast = automationAt(list, last);
        const auto before = list.size();
        list.removeIf([&](const AudioKey& k) { return !visible(s, k.frame); });
        removed += static_cast<int>(before - list.size());
        if (left && atFirst && indexAt(list, first) < 0) put(list, AudioKey{first, *atFirst});
        if (right && atLast && indexAt(list, last) < 0) put(list, AudioKey{last, *atLast});
    });
    return removed;
}

bool KeyClipboard::isEmpty() const {
    return std::all_of(tracks.begin(), tracks.end(), [](const Track& t) { return t.volume.isEmpty() && t.pan.isEmpty(); });
}

KeyClipboard copyKeyframes(const Sequence& sequence, const QList<KeyRef>& volume, const QList<KeyRef>& pan) {
    QStringList order;
    double origin = 0;
    bool any = false;
    for (const QList<KeyRef>* list : {&volume, &pan}) {
        for (const KeyRef& k : *list) {
            if (!order.contains(k.track)) order << k.track;
            origin = any ? std::min(origin, k.frame) : k.frame;
            any = true;
        }
    }
    KeyClipboard out;
    for (const QString& track : order) {
        KeyClipboard::Track t;
        for (const auto& [list, param] : {std::pair{&volume, AutomationParam::Volume}, std::pair{&pan, AutomationParam::Pan}}) {
            QList<double> frames;
            for (const KeyRef& k : *list) {
                if (k.track == track) frames << k.frame;
            }
            QList<AudioKey>& into = param == AutomationParam::Volume ? t.volume : t.pan;
            eachSegment(sequence, {track}, [&](const Track&, const Segment& s, std::int64_t start) {
                for (const AudioKey& k : keysOf(s, param)) {
                    const double r = recordFrame(s, start, k.frame);
                    if (named(frames, r)) into << AudioKey{r - origin, k.value};
                }
            });
            tidy(into);
        }
        out.tracks << t;
    }
    return out;
}

KeyClipboard copyKeyframes(const Sequence& sequence, const QStringList& tracks, double from, double to) {
    KeyClipboard out;
    for (const QString& track : tracks) {
        KeyClipboard::Track t;
        for (AutomationParam param : {AutomationParam::Volume, AutomationParam::Pan}) {
            QList<AudioKey>& into = param == AutomationParam::Volume ? t.volume : t.pan;
            eachSegment(sequence, {track}, [&](const Track&, const Segment& s, std::int64_t start) {
                for (const AudioKey& k : keysOf(s, param)) {
                    const double r = recordFrame(s, start, k.frame);
                    if (visible(s, k.frame) && r >= from - kSame && r < to - kSame) into << AudioKey{r - from, k.value};
                }
            });
            tidy(into);
        }
        out.tracks << t;
    }
    return out;
}

int pasteKeyframes(Sequence& sequence, const QStringList& tracks, double at, const KeyClipboard& clipboard, bool volume,
                   bool pan) {
    int pasted = 0;
    for (int i = 0; i < clipboard.tracks.size() && i < tracks.size(); ++i) {
        const KeyClipboard::Track& from = clipboard.tracks.at(i);
        eachSegment(sequence, {tracks.at(i)}, true, [&](Track&, Segment& s, std::int64_t start) {
            for (const auto& [use, keys, param] :
                 {std::tuple{volume, &from.volume, AutomationParam::Volume}, std::tuple{pan, &from.pan, AutomationParam::Pan}}) {
                if (!use) continue;
                for (const AudioKey& k : *keys) {
                    const double record = at + k.frame;
                    if (record < static_cast<double>(start) - kSame ||
                        record >= static_cast<double>(start + s.length) - kSame) {
                        continue;
                    }
                    put(keysOf(s, param), AudioKey{sourceFrame(s, start, record), clampValue(param, k.value)});
                    ++pasted;
                }
            }
        });
    }
    return pasted;
}

} // namespace lumenis::model

// Clip gain and pan (BinItem::audioGain and audioPan) and Auto VO (clip_audio.h).
#include "lumenis/model/clip_audio.h"

#include <algorithm>

namespace lumenis::model {

bool takesClipAudio(const BinItem& item) {
    return item.kind == BinItemKind::MasterClip || item.kind == BinItemKind::Subclip || item.kind == BinItemKind::AudioClip;
}

bool applyClipGain(BinItem& item, double db) {
    if (!takesClipAudio(item)) return false;
    item.audioGain = clampGain(std::clamp(db, kClipGainMin, kClipGainMax));
    return true;
}

bool centerClipPan(BinItem& item) {
    if (!takesClipAudio(item)) return false;
    item.audioPan = 0;
    return true;
}

double clipGain(const BinItem& item) {
    return takesClipAudio(item) ? clampGain(item.audioGain) : 0.0;
}

std::optional<int> clipPan(const BinItem& item) {
    if (!takesClipAudio(item) || !item.audioPan) return std::nullopt;
    return std::clamp(*item.audioPan, -kPanMax, kPanMax);
}

// The track's material segments, as [start, end) ranges with touching ones joined.
std::optional<Sequence> autoVo(const Sequence& sequence, const QString& track, bool keep) {
    const Track* t = sequence.track(track);
    if (!t || t->kind != TrackKind::Audio) return std::nullopt;
    QList<std::pair<std::int64_t, std::int64_t>> ranges;
    std::int64_t at = 0;
    for (const Segment& s : t->segments) {
        const std::int64_t start = at;
        at += s.length;
        if (s.isFiller()) continue;
        if (!ranges.isEmpty() && ranges.last().second == start) {
            ranges.last().second = at;
        } else {
            ranges.append({start, at});
        }
    }
    std::int64_t end = 0;
    for (const Track& each : sequence.tracks()) end = std::max(end, each.length());
    QList<std::pair<std::int64_t, std::int64_t>> cut = ranges;
    if (keep) {
        cut.clear();
        std::int64_t from = 0;
        for (const auto& [a, b] : ranges) {
            if (a > from) cut.append({from, a});
            from = b;
        }
        if (from < end) cut.append({from, end});
    }
    Sequence out = sequence;
    // From the end backward, so the earlier ranges keep their places.
    for (auto it = cut.crbegin(); it != cut.crend(); ++it) {
        for (const Track& each : sequence.tracks()) {
            const Track* o = out.track(each.name());
            const std::int64_t length = o ? o->length() : 0;
            if (it->first < length) out.extract(each.name(), it->first, std::min(it->second, length) - it->first);
        }
    }
    return out;
}

QString autoVoName(const QString& name) {
    return name + QStringLiteral(".vo");
}

} // namespace lumenis::model

#include "lumenis/model/color_framing.h"

// Color framing's phase checks (color_framing.h; Editing Guide p.643, p.1159).
namespace lumenis::model {

int colorFrameLength(const QString& setting) {
    // "4 field: For NTSC video. 8 field: For PAL video" (p.1159): four fields are two frames, eight are four.
    if (setting == QLatin1String("4field")) return 2;
    if (setting == QLatin1String("8field")) return 4;
    return 0;
}

bool inColorFramePhase(std::int64_t sourceTimecode, std::int64_t recordTimecode, int length) {
    if (length <= 0) return true;
    const std::int64_t d = (sourceTimecode - recordTimecode) % length;
    return d == 0;
}

QList<std::int64_t> colorFrameBreaks(const Sequence& sequence, const QString& track, std::int64_t sequenceStart,
                                     const std::function<std::optional<std::int64_t>(const Segment&)>& sourceTimecode,
                                     int length) {
    QList<std::int64_t> out;
    const Track* t = sequence.track(track);
    if (length <= 0 || !t) return out;
    std::int64_t at = 0;
    for (const Segment& s : t->segments) {
        if (!s.isFiller()) {
            if (const auto tc = sourceTimecode(s); tc && !inColorFramePhase(*tc, sequenceStart + at, length)) out << at;
        }
        at += s.length;
    }
    return out;
}

} // namespace lumenis::model

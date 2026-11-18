#pragma once

#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>

// Color framing (Editing Guide p.643, the Composer setting's Edit tab p.1159; ROADMAP 11.6d): for an online edit from
// 1-inch reel-to-reel tapes, an edit has to keep the colour-sync signal's phase, which repeats every four fields (two
// frames) in NTSC and every eight fields (four frames) in PAL. Ours: a frame's phase is its timecode in frames, counted
// in colour frames, so a source frame is in phase where it lands when its timecode and the record timecode are a whole
// number of colour frames apart. The Composer draws what these say (Composer::ColorFramingDisplay).
namespace lumenis::model {

// The Color Framing setting's value ("4field", "8field"; anything else is off) as the colour frame's length in frames:
// 2, 4, or 0 when off.
int colorFrameLength(const QString& setting);

// Whether a source frame of timecode `sourceTimecode` (in frames) laid at the record frame of timecode `recordTimecode`
// keeps the phase. Always true when `length` is 0 (off).
bool inColorFramePhase(std::int64_t sourceTimecode, std::int64_t recordTimecode, int length);

// The record frames, from the sequence's first, where a segment of `track` starts out of phase ("an edit cuts between
// the four fields", p.643): its first frame's source timecode (`sourceTimecode`, nothing for filler and clips it cannot
// find) against the record timecode there, the sequence starting at `sequenceStart`. Empty when `length` is 0.
QList<std::int64_t> colorFrameBreaks(const Sequence& sequence, const QString& track, std::int64_t sequenceStart,
                                     const std::function<std::optional<std::int64_t>(const Segment&)>& sourceTimecode,
                                     int length);

} // namespace lumenis::model

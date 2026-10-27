#pragma once

#include "lumenis/model/sequence.h"

#include <optional>

// Audio commands on bin items (Editing Guide p.216, p.713-714, p.753-754;
// docs/spec/clip-audio.md): a master clip's own gain and pan, which play with
// the clip and carry into sequences when it is edited (p.711), and Auto VO,
// which makes a new sequence from an old one by the segments of one audio
// track. MainWindow runs them on the bin's selection.
namespace lumenis::model {

// Whether an item can take a clip gain or pan: a master clip (with or without picture) or subclip.
bool takesClipAudio(const BinItem& item);
// Clip > Audio > Apply Gain (p.216): "If there was a gain previously associated with the clip, the
// new gain value overrides it." From -96 to 12 dB (the dialog's range), clamped. False for an item
// that cannot take it.
bool applyClipGain(BinItem& item, double db);
// Clip > Audio > Center Pan (p.714): the clip's channels pan to the centre, a pan of its own.
bool centerClipPan(BinItem& item);
// The clip's gain (0 dB without one) and its pan, if it has one of its own (Center Pan).
double clipGain(const BinItem& item);
std::optional<int> clipPan(const BinItem& item);
inline constexpr double kClipGainMin = -96.0;
inline constexpr double kClipGainMax = 12.0;

// Clip > Auto VO (p.753-754): a copy of `sequence` with the time of the material segments of audio
// track `track` extracted from every track (`keep` false: "Extract segments"), or with all other
// time extracted (`keep` true: "Keep segments"). Nothing when the track is not an audio track.
std::optional<Sequence> autoVo(const Sequence& sequence, const QString& track, bool keep);
// The new sequence's name: the old one's with ".vo" (p.754).
QString autoVoName(const QString& name);

} // namespace lumenis::model

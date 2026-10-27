#pragma once

#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

// Surround panning (Editing Guide p.704-710; docs/spec/surround.md): the pans of mono and stereo
// segments in 5.1 and 7.1 sequences (model::SurroundPan in sequence.h), as the Audio Mixer's Pan grid
// and Advanced Panner set them, and LFE-only tracks. What a pan sends to each speaker is
// media::surroundGains (lumenis/media/surround.h).
namespace lumenis::model {

QJsonObject surroundPanToJson(const SurroundPan& pan);
// Missing members keep their defaults; values are clamped to their ranges.
SurroundPan surroundPanFromJson(const QJsonObject& o);

// The pans a segment on a track of `format` plays with: its own, else the default. The default puts
// each channel at the front: a mono segment where its stereo pan is (`stereoPan`: the segment's pan,
// or its track's side when it has none), a stereo segment's channels left and right. Nothing for
// multichannel (5.1, 7.1) tracks, whose channels have their own speakers.
QList<SurroundPan> surroundPansOf(const Segment& segment, AudioFormat format, int stereoPan);

// The Advanced Panner's texts: a left/right position "L50", "MID", "R50"; the F/R position "F100",
// "MID", "R100" (picture on p.705).
QString surroundXText(int x);
QString surroundYText(int y);

// Stereo Link and Stereo Mirror (p.705): the other channel's pan when one channel of a stereo track
// moves from `before` to `after`. Linked moves it the same way; mirrored the opposite way left to right
// and the same way front to back. The percentages follow.
enum class StereoLink { Unlinked, Linked, Mirror };
SurroundPan linkedPan(const SurroundPan& other, const SurroundPan& before, const SurroundPan& after, StereoLink link);

// Clip Volume and Pan mode for surround pans (p.702, p.706): sets channel `channel`'s pan of the whole
// segments of a mono or stereo audio track that overlap [start, start + length); with `other`, also
// the other channel's of a stereo track. Filler and locked tracks are left alone. Returns how many
// segments were set.
int setSurroundPan(Sequence& sequence, const QString& track, std::int64_t start, std::int64_t length, int channel,
                   const SurroundPan& pan, const std::optional<SurroundPan>& other = std::nullopt);

// "This track is LFE only" (p.710): for mono audio tracks only. False for other tracks.
bool setLfeOnly(Sequence& sequence, const QString& track, bool on);

} // namespace lumenis::model

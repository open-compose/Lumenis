#pragma once

#include "lumenis/model/sequence.h"

#include <QJsonObject>
#include <QString>

#include <optional>

// AudioSuite plug-in effects on segments (Editing Guide p.796-800; docs/spec/audiosuite.md): when a
// segment's effect is rendered, setting and removing it, and its form in sequences. The processes
// themselves are media::audioSuitePlugins() (src/media/audio_suite.cpp); rendering writes their
// sound as managed media (media::renderAudioSuite); playback and Audio Mixdown play the rendered
// sound in place of the source (media::audioPieces); the tool is src/ui/audio_suite.cpp.
namespace lumenis::model {

// What a segment's AudioSuite render was made from: its source (clip, track, first frame and
// length), its clip gain (changing it unrenders the effect, p.700; volume automation does not), the
// process and its parameter values. Empty for a segment without an effect.
QString audioSuiteKey(const Segment& segment);
// Whether the segment's effect has been rendered for the segment as it is now.
bool isRendered(const Segment& segment);
// Whether the segment has an effect that is not rendered (or whose render is out of date).
bool needsRender(const Segment& segment);

// The AudioSuite effect on audio track `track`'s segments that overlap record frames [from, to):
// `effect` (its render forgotten), or without one the effect removed. A segment that already has this
// effect with these values keeps its render. Filler and a locked track are left alone. Returns how
// many segments changed.
int setAudioSuite(Sequence& sequence, const QString& track, std::int64_t from, std::int64_t to,
                  const std::optional<AudioSuiteEffect>& effect);

// The effect as kept in sequences: {effect, name, params, rendered, renderedStream, renderedChannel,
// renderedKey}; render fields only when rendered.
QJsonObject audioSuiteToJson(const AudioSuiteEffect& effect);
AudioSuiteEffect audioSuiteFromJson(const QJsonObject& o);

} // namespace lumenis::model

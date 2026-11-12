#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
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
// effect with these values keeps its render. Filler, a locked track and multichannel tracks (later)
// are left alone. Returns how many segments changed.
int setAudioSuite(Sequence& sequence, const QString& track, std::int64_t from, std::int64_t to,
                  const std::optional<AudioSuiteEffect>& effect);

// An AudioSuite effect template (p.804): the effect and its values, and for one made by creating a
// new master clip, the master clip it was applied to (its bin and id), the clip's tracks used
// (0-based audio tracks), its marks then (clip frames, inclusive) and the handle length in seconds.
// "This effect template contains a reference to the original master clip" (p.804); a template saved
// from the Timeline (the effect icon dragged to a bin, p.798) has no source.
struct AudioSuiteTemplate {
    AudioSuiteEffect effect;
    QString sourceBin;
    QString sourceClip;
    QList<int> tracks;
    std::optional<std::int64_t> in;
    std::optional<std::int64_t> out;
    double handleSeconds = 0;
    bool hasSource() const { return !sourceClip.isEmpty(); }
    bool operator==(const AudioSuiteTemplate&) const = default;
};
// The template as JSON, as bin items keep it (and as it is dragged from the AudioSuite tool to a
// bin); the effect's render is left out. Nothing for JSON without an effect.
QJsonObject audioSuiteTemplateToJson(const AudioSuiteTemplate& t);
std::optional<AudioSuiteTemplate> audioSuiteTemplateFromJson(const QJsonObject& o);
// The template as a bin item: an Effect named `name`, holding the template in
// BinItem::composition["audioSuite"] (render fields left out).
BinItem audioSuiteTemplateItem(const AudioSuiteTemplate& t, const QString& name, const QDateTime& when);
// The template a bin item holds; nothing for any other item.
std::optional<AudioSuiteTemplate> audioSuiteTemplateOf(const BinItem& item);
// The names Media Composer gives what it makes from a master clip (p.803-804): the new clip
// "<clip>_<effect>" (Windows) or "<clip>.<effect>" (macOS), and the template "<clip> - Audio Processing
// Effect: <effect>" (Windows; the guide's "<clip> - AudioSuite Plug-In Effect: <effect>" with our name for
// the feature, docs/spec/trademarks.md) or "<clip>.<effect>.effect" (macOS). Linux takes the Windows form
// (ours). `mac` picks the macOS form.
QString audioSuiteClipName(const QString& clip, const QString& effect, bool mac);
QString audioSuiteTemplateName(const QString& clip, const QString& effect, bool mac);

// The effect as kept in sequences: {effect, name, params, rendered, renderedStream, renderedChannel,
// renderedKey}; render fields only when rendered.
QJsonObject audioSuiteToJson(const AudioSuiteEffect& effect);
AudioSuiteEffect audioSuiteFromJson(const QJsonObject& o);

} // namespace lumenis::model

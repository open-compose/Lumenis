#pragma once

#include "lumenis/model/sequence.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <vector>

// Surround mixing (Editing Guide p.685-689, p.691-692; docs/spec/surround.md): the speakers a mix is
// made for and how it reaches the speakers there are.
//
// Playback mixes every sequence onto a bus of eight speakers, the 7.1 set (AudioMix in
// lumenis/media/playback.h). The sequence's mix format (model::Sequence::mixFormat, the Sequence
// Mix Format button, p.686) says which of them it has: a stereo sequence only L and R, a 5.1 one
// also C, Lfe and the two surrounds. What is on the others is folded into those as the guide's
// table on p.687-688 says. The Monitor Mix Format (the Mix Mode Selection button, p.689, p.691-692)
// says which speakers are listening and in which order the output's channels go; again the mix is
// folded into them. outputMatrix() is both folds, one gain for each speaker of the bus into each
// output channel.
namespace lumenis::media {

// The bus's speakers. A 5.1 mix's left and right surrounds (Ls, Rs) are Lsr and Rsr: "C, L, R, Lsr,
// Rsr, and LFE channels panned to appropriate speakers" when 7.1 plays on 5.1 speakers (p.688).
enum class Speaker { L, R, C, Lfe, Lss, Rss, Lsr, Rsr };
inline constexpr int kSpeakers = 8;
// Gains from each speaker of the bus, by Speaker, into one output channel.
using SpeakerGains = std::array<float, kSpeakers>;

// "L", "R", "C", "Lfe", "Lss", "Rss", "Lsr", "Rsr".
QString speakerName(Speaker speaker);
// The speakers of a multichannel track's channels, in their order (Pro Tools: L C R Ls Rs Lfe and
// L C R Lss Rss Lsr Rsr Lfe, lumenis/model/multichannel.h): [L] for mono (ours), [L, R] for stereo.
QList<Speaker> trackSpeakers(model::AudioFormat format);

// The Mix Mode Selection button's mixes (p.691-692): the speakers listening, in output order.
// Direct Out, which needs the Audio Project setting's output assignments, is not one yet.
enum class MonitorMix {
    Stereo,     // L R
    Mono,       // one speaker
    LrcLfe,     // 4-channel LRCLfe
    Lrcs,       // 4-channel LRCS: L R C and one rear speaker
    Quad,       // 4-channel Quadraphonic: L R Ls Rs
    ProTools51, // L C R Ls Rs Lfe
    Smpte51,    // L R C Lfe Ls Rs
    ProTools71, // L C R Lss Rss Lsr Rsr Lfe
    Ext71,      // L R C Lfe Lsr Rsr Lss Rss
    SmpteDs71,  // L R C Lfe Lss Rss Lsr Rsr
};
// Every mix, in the menu's order.
QList<MonitorMix> monitorMixes();
// The Audio Project setting's value: "stereo", "mono", "lrclfe", "lrcs", "quad", "5.1-protools",
// "5.1-smpte", "7.1-protools", "7.1-ext", "7.1-smpte-ds".
QString monitorMixId(MonitorMix mix);
std::optional<MonitorMix> monitorMixFromId(const QString& id);
// The menu's text: "5.1 Pro Tools: L C R Ls Rs Lfe" (p.689), "4-channel LRCLfe" (p.691).
QString monitorMixName(MonitorMix mix);
// The button's short text: "Stereo", "5.1 PT", ...
QString monitorMixShortName(MonitorMix mix);
// The names of the output's channels, in order ("L", "C", "R", "Ls", "Rs", "Lfe"); "M" for mono and
// "S" for LRCS's rear speaker.
QStringList monitorChannelNames(MonitorMix mix);
int monitorChannels(MonitorMix mix);

// The gains from the bus into each output channel of `monitor`, for a sequence mixed in
// `sequenceFormat` (Stereo, 5.1 or 7.1; Mono is taken as Stereo):
// - the sequence's fold (ours, from p.687-688): a stereo sequence folds C and Lfe into the middle
//   (3 dB down on each side) and every surround into its side 3 dB down; a 5.1 sequence folds Lss and
//   Rss into the middle of the L and Lsr and the R and Rsr speaker pairs;
// - then the monitor's (p.687-688, p.691-692): Stereo folds a surround mix as a stereo sequence does;
//   Mono has every channel in the middle (each 3 dB down, ours); 5.1 speakers hear a 7.1 mix with Lss
//   and Rss in the middle of their speaker pairs; 7.1 speakers leave Lss and Rss silent for a 5.1
//   mix; LRCLfe folds the surrounds into L and R 3 dB down; LRCS folds Lfe into L and R 3 dB down,
//   the rear surrounds into S (3 dB down, ours) and the side ones between S and their front speaker;
//   Quad puts C, and Lss and Rss, in the middle of their speaker pairs and Lfe into L and R 3 dB down.
std::vector<SpeakerGains> outputMatrix(model::AudioFormat sequenceFormat, MonitorMix monitor);
// What a surround pan (model::SurroundPan, p.704-710) sends to each speaker of the bus in a sequence
// mixed in `sequenceFormat` (5.1 or 7.1; Stereo gives L and R as an equal-power pan of the cursor's
// position). Ours, as the guide gives no pan law:
// - the cursor's row is between the front speakers and the rear surrounds (7.1: the front, the side
//   and the rear surrounds), crossfaded by the F/R position with equal power;
// - in the front row the front position pans across L, C and R; Center percentage blends that with a
//   phantom centre (L and R only); the rear row pans the rear position between the rear surrounds, and
//   7.1's side row the cursor's position between the side surrounds; Side percentage moves the side
//   row's share to the front and rear rows;
// - so moving along the trajectory with the Position controls is heard on the front and rear pairs
//   only (p.707);
// - LFE adds its percentage of the signal to the LFE channel.
// Without LFE the gains have unit power.
SpeakerGains surroundGains(const model::SurroundPan& pan, model::AudioFormat sequenceFormat);

// The gains into the channels of a track of `format` (Stereo, 5.1 or 7.1, in the track's channel
// order, trackSpeakers()): what an Audio Mixdown to that format writes (p.732).
std::vector<SpeakerGains> trackMatrix(model::AudioFormat sequenceFormat, model::AudioFormat format);

} // namespace lumenis::media

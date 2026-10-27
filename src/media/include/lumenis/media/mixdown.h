#pragma once

#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

// Audio Mixdown (Editing Guide p.731-732; ROADMAP Phase 5; docs/spec/audio-mixdown.md): the
// selected audio tracks of a sequence, between its marks, mixed into new sound that becomes a master
// clip of managed media. The mix is the one playback hears (audioPieces(), AudioMix): each segment's
// clip gain and pan, then the Master fader when it is included.
namespace lumenis::media {

// The guide's Mono, Dual Mono, Stereo, 5.1 and 7.1 (p.732). Multichannel tracks mix in through their
// channels as playback places them; the stereo and surround mixes are the sequence's mix folded into
// a track of that format (trackMatrix(), lumenis/media/surround.h; docs/spec/surround.md).
enum class MixdownFormat {
    Mono,       // one track: every source track summed at its clip gain, without its pan
    DualMono,   // the stereo mix as two mono tracks, the left panned left and the right panned right
    Stereo,     // the stereo mix as one stereo track
    Surround51, // the mix as one 5.1 track, its channels in Pro Tools order (L C R Ls Rs Lfe)
    Surround71, // the mix as one 7.1 track (L C R Lss Rss Lsr Rsr Lfe)
};
// Output channels a format makes: 1, 2, 2, 6 and 8.
int mixdownChannels(MixdownFormat format);
// The format of the track a mixdown goes on: Mono for Mono and Dual Mono (two mono tracks), else
// Stereo, 5.1 or 7.1.
model::AudioFormat mixdownTrackFormat(MixdownFormat format);

struct MixdownRequest {
    model::BinItem sequence;
    QStringList tracks;           // the audio tracks mixed (selected in the Track Selector)
    std::int64_t start = 0;       // sequence frames [start, end)
    std::int64_t end = 0;
    MixdownFormat format = MixdownFormat::DualMono;
    double masterGainDb = 0.0;    // applied when includeMaster (Include Master Fader, p.732)
    bool includeMaster = false;
    bool alternatingPan = true;   // Audio settings, Default Pan for mono tracks (p.1147)
    ClipLookup clips;
};

// Renders `count` samples of each output track from sample `at` of the range (48 kHz) into
// out[track] (resized to count).
class MixdownRenderer {
public:
    explicit MixdownRenderer(const MixdownRequest& request);
    int channels() const { return channels_; }
    // The range's length in output samples.
    std::int64_t samples() const { return samples_; }
    void render(std::int64_t at, int count, std::vector<std::vector<float>>& out);

private:
    int channels_;
    std::int64_t offset_; // the range's first output sample
    std::int64_t samples_;
    AudioMix mix_;
};

// The mixdown as a new audio clip ("<sequence> Mixdown", ours) of managed media on the creation's
// audio drive, starting at the range's timecode in the sequence. A stereo or surround mixdown's clip
// has its channels as one track of that format (its Track Formats, lumenis/model/multichannel.h).
std::optional<model::BinItem> mixdownClip(const MixdownRequest& request, const QString& name, const QString& startTimecode,
                                          const MediaCreation& creation, QString* error = nullptr);

// Timeline > Mixdown > Audio > Multiple Mixes, the Multi-Mix Tool (p.732-734;
// docs/spec/multi-mix.md): several mixdowns of one sequence at once, each an output with its own
// inputs, format and Master Fader choice.
struct MixOutput {
    QString name;        // Edit Mix's name; empty: "<sequence> Mix <n>" (ours)
    MixdownFormat format = MixdownFormat::Stereo;
    bool masterFader = false; // the button above the output: the Master Fader's effects and gain
    QStringList inputs;  // the audio tracks mixed into it, "A1" ...
    bool operator==(const MixOutput&) const = default;
};
struct MultiMix {
    QList<MixOutput> outputs;
    bool useMarks = true;
    bool newSequence = true;
    QString sequenceName; // empty: "<sequence>.MultiMix" (ours)
    bool excludeVideoAndData = false;
    bool operator==(const MultiMix&) const = default;

    QJsonObject toJson() const;
    // Unknown or missing values take their defaults.
    static MultiMix fromJson(const QJsonObject& o);
};
// "You can create up to 24 output channels for the mix"; "The Dual Mono option counts as two tracks
// against the limit" (p.733-734): an output takes one track, Dual Mono two.
inline constexpr int kMultiMixTracks = 24;
int multiMixTracks(const MultiMix& mix);
// The output's first track number (1-based) in the tracks of the mix.
int multiMixTrackNumber(const MultiMix& mix, int output);
// What stops the mix, or empty: no outputs, an output without inputs, more than 24 tracks, or Dual
// Mono on an even-numbered track ("Only odd-numbered tracks can be set to Dual Mono", p.734).
QString multiMixProblem(const MultiMix& mix);
// "Mono", "Stereo", "Dual Mono", "5.1", "7.1"; and back (nothing for other text).
QString mixdownFormatName(MixdownFormat format);
std::optional<MixdownFormat> mixdownFormatFromName(const QString& name);

} // namespace lumenis::media

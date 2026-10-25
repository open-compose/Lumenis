#pragma once

#include "lumenis/media/managed.h"
#include "lumenis/media/playback.h"
#include "lumenis/model/bin.h"

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

// The guide's Mono, Dual Mono, Stereo, 5.1 and 7.1 (p.732). Cutthulhu makes the first two: its
// sequences hold one clip channel per track, so a stereo or surround track of several channels
// waits for multichannel tracks.
enum class MixdownFormat {
    Mono,     // one track: every source track summed at its clip gain, without its pan
    DualMono, // the stereo mix as two mono tracks, the left panned left and the right panned right
};
// Output tracks a format makes.
int mixdownChannels(MixdownFormat format);

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
    MixdownFormat format_;
    int channels_;
    std::int64_t offset_; // the range's first output sample
    std::int64_t samples_;
    AudioMix mix_;
};

// The mixdown as a new audio clip ("<sequence> Mixdown", ours) of managed media on the creation's
// audio drive, starting at the range's timecode in the sequence.
std::optional<model::BinItem> mixdownClip(const MixdownRequest& request, const QString& name, const QString& startTimecode,
                                          const MediaCreation& creation, QString* error = nullptr);

} // namespace lumenis::media

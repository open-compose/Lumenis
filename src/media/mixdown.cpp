#include "lumenis/media/mixdown.h"

#include "lumenis/model/multichannel.h"
#include "lumenis/model/sequence.h"

#include <QObject>

#include <algorithm>
#include <cmath>

// Audio Mixdown (mixdown.h). The pieces are those playback mixes; a mono mixdown gives each piece
// its own gain on one channel, the others the playback mix through the matrix of their format.
namespace lumenis::media {

namespace {

std::vector<AudioPiece> piecesFor(const MixdownRequest& r) {
    const QStringList tracks = r.tracks;
    std::vector<AudioPiece> pieces =
        audioPieces(r.sequence, r.clips, [&tracks](const QString& t) { return tracks.contains(t); }, r.alternatingPan);
    const float master = !r.includeMaster ? 1.0f
                         : r.masterGainDb <= model::kGainOff
                             ? 0.0f
                             : static_cast<float>(std::pow(10.0, r.masterGainDb / 20.0));
    for (AudioPiece& p : pieces) {
        if (r.format == MixdownFormat::Mono) {
            // All on the left, which the mono output takes alone.
            p.left = p.gain;
            p.right = 0.0f;
            p.monoGain = true;
            p.speaker = -1;
        }
        p.left *= master;
        p.right *= master;
        p.scale = master;
    }
    return pieces;
}

// The Audio Track Effects of the tracks mixed down: they are heard in the mix, so they go into it.
TrackInserts insertsFor(const MixdownRequest& r) {
    TrackInserts all = trackInserts(r.sequence);
    for (auto it = all.begin(); it != all.end();) {
        // A multichannel track's channels are "A1", "A1#2" ... (channelTrackKey()).
        it = r.tracks.contains(it->first.section(QLatin1Char('#'), 0, 0)) ? std::next(it) : all.erase(it);
    }
    return all;
}

} // namespace

int mixdownChannels(MixdownFormat format) {
    switch (format) {
    case MixdownFormat::Mono: return 1;
    case MixdownFormat::DualMono:
    case MixdownFormat::Stereo: return 2;
    case MixdownFormat::Surround51: return 6;
    case MixdownFormat::Surround71: return 8;
    }
    return 2;
}

model::AudioFormat mixdownTrackFormat(MixdownFormat format) {
    switch (format) {
    case MixdownFormat::Mono:
    case MixdownFormat::DualMono: return model::AudioFormat::Mono;
    case MixdownFormat::Stereo: return model::AudioFormat::Stereo;
    case MixdownFormat::Surround51: return model::AudioFormat::Surround51;
    case MixdownFormat::Surround71: return model::AudioFormat::Surround71;
    }
    return model::AudioFormat::Mono;
}

namespace {

// The output's gains from the bus: L alone for Mono (each piece's gain is there), else the track
// format's fold of the sequence's mix.
std::vector<SpeakerGains> matrixFor(const MixdownRequest& r) {
    if (r.format == MixdownFormat::Mono) {
        SpeakerGains left{};
        left[static_cast<std::size_t>(Speaker::L)] = 1.0f;
        return {left};
    }
    const model::AudioFormat target =
        r.format == MixdownFormat::DualMono ? model::AudioFormat::Stereo : mixdownTrackFormat(r.format);
    return trackMatrix(model::Sequence::fromItem(r.sequence).mixFormat(), target);
}

} // namespace

MixdownRenderer::MixdownRenderer(const MixdownRequest& request)
    : channels_(mixdownChannels(request.format)),
      offset_(framesToSamples(request.start, request.sequence.editRate)),
      samples_(std::max<std::int64_t>(framesToSamples(request.end, request.sequence.editRate) - offset_, 0)),
      mix_(piecesFor(request), insertsFor(request)) {
    mix_.setOutput(matrixFor(request));
}

void MixdownRenderer::render(std::int64_t at, int count, std::vector<std::vector<float>>& out) {
    const auto chs = static_cast<std::size_t>(channels_);
    std::vector<float> mixed(chs * static_cast<std::size_t>(std::max(count, 0)));
    if (count > 0) mix_.render(offset_ + at, count, mixed.data());
    out.resize(chs);
    for (std::size_t c = 0; c < chs; ++c) {
        std::vector<float>& o = out[c];
        o.resize(static_cast<std::size_t>(std::max(count, 0)));
        for (int i = 0; i < count; ++i) o[static_cast<std::size_t>(i)] = mixed[chs * static_cast<std::size_t>(i) + c];
    }
}

std::optional<model::BinItem> mixdownClip(const MixdownRequest& request, const QString& name, const QString& startTimecode,
                                          const MediaCreation& creation, QString* error) {
    if (request.end <= request.start) {
        if (error) *error = QObject::tr("There is nothing to mix down between the marks.");
        return std::nullopt;
    }
    if (request.tracks.isEmpty()) {
        if (error) *error = QObject::tr("Select the audio tracks to mix down.");
        return std::nullopt;
    }
    MixdownRenderer renderer(request);
    auto clip = renderedAudioClip(
        name, request.sequence.editRate, request.end - request.start, renderer.channels(),
        [&renderer](std::int64_t at, int count, std::vector<std::vector<float>>& out) { renderer.render(at, count, out); },
        startTimecode, creation, error);
    // A stereo or surround mixdown is one multichannel track (p.732).
    const model::AudioFormat format = mixdownTrackFormat(request.format);
    if (clip && format != model::AudioFormat::Mono && !model::setTrackFormats(*clip, {model::ChannelGroup{format, 1}}, error)) {
        return std::nullopt;
    }
    return clip;
}

} // namespace lumenis::media

#include "lumenis/media/mixdown.h"

#include "lumenis/model/sequence.h"

#include <QObject>

#include <algorithm>
#include <cmath>

// Audio Mixdown (mixdown.h). The pieces are those playback mixes; a mono mixdown gives each piece
// its own gain on one channel, a dual mono one the playback mix's two sides.
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
            // All on the left, which render() takes.
            p.left = p.gain;
            p.right = 0.0f;
            p.monoGain = true;
        }
        p.left *= master;
        p.right *= master;
        p.scale = master;
    }
    return pieces;
}

} // namespace

int mixdownChannels(MixdownFormat format) {
    return format == MixdownFormat::Mono ? 1 : 2;
}

MixdownRenderer::MixdownRenderer(const MixdownRequest& request)
    : channels_(mixdownChannels(request.format)),
      offset_(framesToSamples(request.start, request.sequence.editRate)),
      samples_(std::max<std::int64_t>(framesToSamples(request.end, request.sequence.editRate) - offset_, 0)),
      mix_(piecesFor(request)) {}

void MixdownRenderer::render(std::int64_t at, int count, std::vector<std::vector<float>>& out) {
    std::vector<float> stereo(2 * static_cast<std::size_t>(std::max(count, 0)));
    if (count > 0) mix_.render(offset_ + at, count, stereo.data());
    out.resize(static_cast<std::size_t>(channels_));
    for (int c = 0; c < channels_; ++c) {
        std::vector<float>& o = out[static_cast<std::size_t>(c)];
        o.resize(static_cast<std::size_t>(std::max(count, 0)));
        for (int i = 0; i < count; ++i) o[static_cast<std::size_t>(i)] = stereo[2 * static_cast<std::size_t>(i) + static_cast<std::size_t>(c)];
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
    return renderedAudioClip(
        name, request.sequence.editRate, request.end - request.start, renderer.channels(),
        [&renderer](std::int64_t at, int count, std::vector<std::vector<float>>& out) { renderer.render(at, count, out); },
        startTimecode, creation, error);
}

} // namespace lumenis::media

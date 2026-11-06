// Exporting a sequence's picture and sound as a movie file (Phase 7.1a; docs/spec/movie-export.md): File > Output >
// Export to File with the Export setting's Export As MOV, MP4 or QuickTime Movie (Editing Guide p.879-881, p.893-899,
// p.1179-1180). The caller (MainWindow::exportToFile) composes the frames (media::composeImage) and mixes the sound
// (MixdownRenderer); this file fits each frame to the output raster, resamples the sound and writes both through
// MediaWriter (FFmpeg).
#pragma once

#include "lumenis/core/rational.h"

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <vector>

namespace lumenis::media {

enum class MovieContainer { Mov, Mp4 };

// Source Scaling (p.895): how a frame is fitted to an output raster of another shape.
enum class MovieScaling {
    Stretch,        // "source gets stretched to output geometry"
    Letterbox,      // "Pillarbox/letterbox - source is scaled uniformly to match either the width or the height"
    CenterCrop,     // "Center crop - source is scaled uniformly to match the width of the output raster" (ours: to
                    // fill it, cropping what is outside)
    CenterKeepSize, // "Center keep size - source's size in pixels is mapped 1:1 to output geometry"
};

// One entry of the Export setting's Compression menu (p.895, "Select the Codec Family, Compression, and Color
// Depth"): its family, its FFmpeg encoder and profile, its colour depth and the containers it goes in (ours).
struct MovieCompression {
    QString id;          // the setting's value, "dnxhr_hq"
    QString family;      // "Avid DNxHR", "Apple ProRes", "H.264", "HEVC", "AV1", "Motion JPEG", "PNG", "Uncompressed"
    QString label;       // "Avid DNxHR HQ"
    QString encoder;     // FFmpeg's encoder name, "dnxhd"
    QString profile;     // its "profile" option, "dnxhr_hq"
    QString pixelFormat; // FFmpeg's pixel format, "yuv422p"
    int depth = 8;       // the Color Depth, bits
    bool mov = true, mp4 = false;
    bool bitRate = false; // takes the Bit Rate option
};

// Every compression we offer, in the menu's order.
const QList<MovieCompression>& movieCompressions();
const MovieCompression* findMovieCompression(const QString& id);
// Whether this build can write it: FFmpeg has the encoder, and it is not a codec awaiting legal review in a build
// without them (codecAllowed, docs/EXTENSIONS.md).
bool movieCompressionAvailable(const MovieCompression& compression);
// Whether `compression` goes in `container`.
bool movieCompressionFits(const MovieCompression& compression, MovieContainer container);

struct MovieExportOptions {
    MovieContainer container = MovieContainer::Mov;
    bool video = true; // Video and Audio, Video Only, Audio Only (p.880)
    bool audio = true;
    QString compression = QStringLiteral("dnxhr_hq");
    int bitRateMbps = 0;      // 0: the encoder's own choice
    QSize size;               // Image Size; empty: the source raster
    MovieScaling scaling = MovieScaling::Letterbox;
    bool fullRange = false;   // Color Levels: RGB (full range) or 601/709 (legal range) (p.881)
    int audioChannels = 2;    // Audio Format: Mono 1, Stereo 2, 5.1 6, 7.1 8 (p.1180)
    bool aac = false;         // AAC rather than PCM (p.897); an MP4 is always AAC, or Opus without AAC (ours)
    int sampleRate = 48000;   // 48 kHz or 44.1 kHz ("does not support sample rates higher than 48kHz", p.879)
    int bitDepth = 24;        // PCM's, 16 or 24
    QString startTimecode;    // embedded in the movie (p.881)
};

// What to export: `frames` frames at `frameRate`, each `picture(f)` (any size; fitted to the output raster), and
// the sound at 48 kHz in options' audioChannels channels, `sound(at, count, out)` filling out[channel] with `count`
// samples from sample `at`.
struct MovieSource {
    std::int64_t frames = 0;
    Rational frameRate{25, 1};
    QSize raster;
    std::function<QImage(std::int64_t frame)> picture;
    std::function<void(std::int64_t at, int count, std::vector<std::vector<float>>& out)> sound;
};

// `picture` drawn into an `out` raster by `scaling`, on black.
QImage fitMoviePicture(const QImage& picture, QSize out, MovieScaling scaling);

// Writes the movie. `progress` gets 0 to 1 and returns false to cancel. On failure or cancel the file is removed
// and `error` says why.
bool exportMovie(const QString& path, const MovieSource& source, const MovieExportOptions& options,
                 const std::function<bool(double)>& progress = {}, QString* error = nullptr);

} // namespace lumenis::media

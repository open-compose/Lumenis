// Exporting a sequence's picture and sound as a movie file (Phase 7.1a; docs/spec/movie-export.md): File > Output >
// Export to File with the Export setting's Export As MOV, MP4 or QuickTime Movie (Editing Guide p.879-881, p.893-899,
// p.1179-1180). The caller (MainWindow::exportToFile) composes the frames (media::composeImage) and mixes the sound
// (MixdownRenderer); this file fits each frame to the output raster, resamples the sound and writes both through
// MediaWriter (FFmpeg). 7.1b adds the Graphic, DPX and OpenEXR stills and image sequences (p.890, p.900,
// p.1185-1187) and the Audio export's WAVE and AIFF-C files (p.1185).
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

// ---------------------------------------------------------------- stills and sound files (7.1b)

// The Graphic export's formats (p.1186-1187) and the DPX and OpenEXR exports (p.890, p.900).
enum class StillFormat { Png, Jpeg, Tiff, Bmp, Pict, Dpx, OpenExr };

struct StillOptions {
    StillFormat format = StillFormat::Png;
    QSize size;               // Width x Height; empty: the picture's
    MovieScaling scaling = MovieScaling::Stretch; // Size to Fit (Stretch) or Crop/Pad (CenterKeepSize), p.1186
    bool legalRange = false;  // Color Levels "Keep as Legal Range": RGB squeezed into 16-235
    int depth = 8;            // PNG and TIFF 8 or 16; DPX 8, 10 or 16; OpenEXR 16 (half) or 32 (float)
    int quality = 90;         // JPEG's Quality, 1-100
};

// The file name extension of a format: "png", "jpg", "tif", "bmp", "pct", "dpx", "exr".
QString stillSuffix(StillFormat format);
// RGB from 0-255 into the legal 16-235.
QImage legalRangePicture(const QImage& picture);
// Writes one still, fitted to the options' size. False with the reason in `error`; nothing is left on failure.
bool writeStill(const QString& path, const QImage& picture, const StillOptions& options, QString* error = nullptr);
// The file of image `number` in a sequence named after `path`: "<folder>/<name>.<number, 7 digits>.<suffix>" (ours).
QString numberedStillPath(const QString& path, std::int64_t number);

// Export Settings: Audio (p.1185): WAVE or AIFF-C, mono or stereo, a sample rate and a bit depth.
struct AudioFileOptions {
    bool aiff = false;
    int channels = 2;       // the source's sound comes in this many channels
    int sampleRate = 48000; // 32, 44.1, 48, 88.2 or 96 kHz; the source's 48 kHz is resampled
    int bitDepth = 24;      // 16 or 24
};

// Writes `source`'s sound (its picture is not used) as one audio file. Same contract as exportMovie.
bool exportAudioFile(const QString& path, const MovieSource& source, const AudioFileOptions& options,
                     const std::function<bool(double)>& progress = {}, QString* error = nullptr);

} // namespace lumenis::media

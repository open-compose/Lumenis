#pragma once

#include "lumenis/core/rational.h"

#include <QImage>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace lumenis::ext {
class ProviderRegistry;
}

// Reading and writing media files through FFmpeg (ROADMAP Phase 2). Nothing
// outside this library sees FFmpeg's own types.
namespace lumenis::media {

// The FFmpeg libraries in use, e.g. "libavcodec 60.31.102, libavformat ...".
QString ffmpegVersion();

// Codecs that wait for counsel's review (docs/EXTENSIONS.md, marked with a
// dagger). With LUMENIS_PATENT_CODECS off they are probed but not decoded
// or encoded.
bool isPatentCodec(const QString& codec);
bool patentCodecsAllowed();
bool codecAllowed(const QString& codec);

enum class StreamKind { Video, Audio, Data, Other };

// One stream of a media file.
struct StreamInfo {
    int index = 0;
    StreamKind kind = StreamKind::Other;
    QString codec;        // FFmpeg's name: "dnxhd", "prores", "pcm_s24le", ...
    QString codecLongName;
    QString profile;      // e.g. "DNXHR HQ", "422 HQ"
    // Video.
    int width = 0;
    int height = 0;
    Rational frameRate;   // edit rate
    Rational aspect{1, 1}; // display aspect ratio, e.g. 16:9
    QString pixelFormat;  // "yuv422p10le", ...
    bool interlaced = false;
    // Audio.
    int sampleRate = 0;
    int channels = 0;
    QString sampleFormat; // "s16", "s32", "fltp", ...
    int bitsPerSample = 0;
    // Length in frames (video) or samples (audio); 0 if unknown.
    std::int64_t length = 0;
    bool decodable = false; // a decoder exists and the codec is allowed
};

// What a file holds (the Source Browser's columns, Editing Guide ch. 11).
struct MediaInfo {
    QString path;
    QString format;        // "mov,mp4,m4a,3gp,3g2,mj2", "mxf", ...
    QString formatLongName;
    double durationSeconds = 0;
    QString startTimecode; // "01:00:00:00" when the file records one
    QString reelName;      // tape or reel name metadata, if any
    // The file's own metadata ("material_package_name", "comment_...").
    QHash<QString, QString> metadata;
    std::vector<StreamInfo> streams;

    const StreamInfo* firstVideo() const;
    std::vector<const StreamInfo*> audio() const;
    QJsonObject toJson() const;
};

// A numbered run of still images (DPX, OpenEXR, TIFF, PNG, ...) read as one
// clip (Editing Guide p.325-328). Wherever a file path is taken, the
// sequence's spec() can be given instead.
struct ImageSequence {
    QString pattern;         // "/media/Kermit_%04d.dpx": FFmpeg's image2 pattern
    std::int64_t first = 0;  // the first file's number
    std::int64_t count = 0;  // files in the run
    Rational rate{24, 1};    // stills have none: the project's

    QString fileAt(std::int64_t index) const; // index from 0
    // "Kermit", from "Kermit_0001.dpx".
    QString name() const;
    // "seq:<pattern>?first=1&count=40&rate=24/1".
    QString spec() const;
    static std::optional<ImageSequence> fromSpec(const QString& spec);
};
bool isSequenceSpec(const QString& path);
// Whether a file name is a numbered still that can be part of a sequence:
// its stem ends in digits and its extension is an image format.
bool isNumberedStill(const QString& path);

std::optional<MediaInfo> probe(const QString& path, QString* error = nullptr);

// Reads the frames of one video stream by frame number, from 0. Reading the
// next frame continues decoding; other frames seek to the keyframe before
// them and decode forward, so every frame is exact.
class VideoReader {
public:
    VideoReader();
    ~VideoReader();
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    // The first video stream when `stream` is -1.
    bool open(const QString& path, int stream = -1, QString* error = nullptr);
    bool isOpen() const;
    const StreamInfo& info() const;
    std::int64_t frameCount() const;

    // The frame as RGBA (8 bits per channel), scaled to `size` if given,
    // keeping it otherwise. Empty if the frame is past the end.
    QImage frame(std::int64_t number, QSize size = {});
    // The MD5 of the frame as decoded, before any conversion, as FFmpeg's
    // framemd5 gives it (the decode corpus, ROADMAP Phase 2 gate 1).
    QString frameMd5(std::int64_t number);
    // The frame as Y'CbCr 4:4:4 16-bit planes at `size` (or its own), for
    // comparing pictures.
    bool planes(std::int64_t number, QSize size, std::vector<std::uint16_t>* y, std::vector<std::uint16_t>* cb,
                std::vector<std::uint16_t>* cr);

private:
    friend class MediaWriter;
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Reads one audio stream as 32-bit float samples, interleaved by channel.
class AudioReader {
public:
    AudioReader();
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    bool open(const QString& path, int stream = -1, QString* error = nullptr);
    bool isOpen() const;
    const StreamInfo& info() const;
    int channels() const;
    int sampleRate() const;
    // `count` samples per channel from sample `start`; fewer at the end.
    std::vector<float> samples(std::int64_t start, int count);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Writes a media file: one video stream and/or one audio stream. Used to
// make media (Phase 2.4: transcode, consolidate) and test files.
struct WriterSettings {
    QString format;      // FFmpeg muxer: "mov", "mxf", "mxf_opatom", "wav", "matroska"
    // Video; empty codec for none.
    QString videoCodec;  // "dnxhd", "prores_ks", "mpeg2video", "ffv1", ...
    QString videoProfile; // e.g. "dnxhr_hq"
    int width = 0;
    int height = 0;
    Rational frameRate{25, 1};
    QString pixelFormat; // empty: the codec's first
    std::int64_t bitRate = 0;
    // Audio; empty codec for none.
    QString audioCodec;  // "pcm_s16le", "pcm_s24le", ...
    int sampleRate = 48000;
    int channels = 2;
    QString startTimecode; // written as metadata when the format takes it
    QString reelName;      // likewise
    // The file's metadata, e.g. MXF's "material_package_name" and
    // "comment_<name>" user comments.
    QList<QPair<QString, QString>> metadata;
    // The muxer's own options, e.g. mxf_opatom's "mxf_audio_edit_rate".
    QList<QPair<QString, QString>> formatOptions;
};

class MediaWriter {
public:
    MediaWriter();
    ~MediaWriter();
    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;

    bool open(const QString& path, const WriterSettings& settings, QString* error = nullptr);
    // The next video frame (any QImage format; converted and scaled).
    bool writeVideo(const QImage& frame);
    // The next video frame from a reader's frame, straight from its decoded
    // picture at full depth (transcoding), scaled into `target` (the whole
    // raster if empty) on black. False if the frame cannot be read.
    bool writeVideoFrom(VideoReader& reader, std::int64_t frame, QRect target = {});
    // Interleaved float samples, `count` per channel.
    bool writeAudio(const float* samples, int count);
    // Flushes the encoders and writes the trailer.
    bool finish(QString* error = nullptr);
    QString lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// How alike two pictures are, plane by plane in Y'CbCr: the PSNR in dB of
// luma and each chroma plane, 16-bit samples (the transcode round trips,
// ROADMAP Phase 2 gate 5). Pictures of other sizes or RGB are converted
// first, as Rec. 709.
struct Psnr {
    double y = 0;
    double cb = 0;
    double cr = 0;
    double worst() const;
};
std::optional<Psnr> comparePictures(VideoReader& a, std::int64_t frameA, VideoReader& b, std::int64_t frameB);

// Converts interleaved float audio from one sample rate to another
// (FFmpeg's swresample).
class Resampler {
public:
    Resampler(int channels, int fromRate, int toRate);
    ~Resampler();
    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    bool isValid() const;
    // Samples in (`count` per channel), what is ready out.
    std::vector<float> push(const float* samples, int count);
    // What is still held, at the end.
    std::vector<float> flush();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Copies frames [first, first + count) of a file's video stream into a new
// file of `format` without decoding them; for codecs whose every frame is a
// keyframe (DNx, ProRes). `settings` gives the format, metadata and options;
// its codec fields are ignored.
bool copyVideoFrames(const QString& from, int stream, std::int64_t first, std::int64_t count, const QString& to,
                     const WriterSettings& settings, QString* error = nullptr);

// Registers FFmpeg's codec provider ("org.ffmpeg.libavcodec") with the
// application's provider registry (docs/EXTENSIONS.md).
void registerProviders(ext::ProviderRegistry& registry);

} // namespace lumenis::media

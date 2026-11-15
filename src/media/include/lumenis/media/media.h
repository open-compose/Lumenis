#pragma once

#include "lumenis/core/mob_id.h"
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
// Whether FFmpeg's codec name (any case) is one of those.
bool isPatentCodec(const QString& codec);
// Whether this build was made with LUMENIS_PATENT_CODECS on.
bool patentCodecsAllowed();
// Whether a codec may be decoded or encoded in this build.
bool codecAllowed(const QString& codec);
// Whether FFmpeg has the encoder `name` ("prores_videotoolbox", "prores_ks") and this build may use it
// (codecAllowed; an operating system's encoder, such as VideoToolbox's, is licensed with the system).
bool encoderAvailable(const QString& name);

// What a stream carries, as FFmpeg's media type says.
enum class StreamKind { Video, Audio, Data, Other };

// One stream of a media file.
struct StreamInfo {
    // The stream's index in the file, as VideoReader::open() and AudioReader::open() take it.
    int index = 0;
    StreamKind kind = StreamKind::Other;
    QString codec;        // FFmpeg's name: "dnxhd", "prores", "pcm_s24le", ...
    // FFmpeg's long name for the codec.
    QString codecLongName;
    QString profile;      // e.g. "DNXHR HQ", "422 HQ"
    // Video.
    int width = 0;
    int height = 0;
    Rational frameRate;   // edit rate
    Rational aspect{1, 1}; // display aspect ratio, e.g. 16:9
    QString pixelFormat;  // "yuv422p10le", ...
    // FFmpeg's name for the transfer characteristic ("bt709", "linear", "iec61966-2-1" ...); empty when unspecified.
    QString colorTransfer;
    bool interlaced = false;
    // Interlaced: whether the first field in time is on the even lines (FFmpeg's field order tt or tb);
    // true when the media does not say.
    bool topFieldFirst = true;
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
    // The path or ImageSequence spec probed.
    QString path;
    QString format;        // "mov,mp4,m4a,3gp,3g2,mj2", "mxf", ...
    // FFmpeg's description of the format.
    QString formatLongName;
    // The file's duration as the demuxer gives it (an image sequence's from its count and rate).
    double durationSeconds = 0;
    QString startTimecode; // "01:00:00:00" when the file records one
    QString reelName;      // tape or reel name metadata, if any
    // The file's own metadata ("material_package_name", "comment_...").
    QHash<QString, QString> metadata;
    std::vector<StreamInfo> streams;

    // The first video stream, null if there is none.
    const StreamInfo* firstVideo() const;
    // The audio streams, in file order.
    std::vector<const StreamInfo*> audio() const;
    // What `--probe` prints (app/main.cpp).
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
    // Parses spec(); nothing if `spec` is not one or names no files.
    static std::optional<ImageSequence> fromSpec(const QString& spec);
};
// Whether a path is an ImageSequence spec ("seq:...").
bool isSequenceSpec(const QString& path);
// Whether a file name is a numbered still that can be part of a sequence:
// its stem ends in digits and its extension is an image format.
bool isNumberedStill(const QString& path);

// Reads a file's (or an ImageSequence spec's) format, metadata and streams without decoding. Nothing
// if FFmpeg cannot open it, with its reason in *error. Lengths of formats without an index are
// counted by reading the file through.
std::optional<MediaInfo> probe(const QString& path, QString* error = nullptr);

// Reads the frames of one video stream by frame number, from 0. Reading the
// next frame continues decoding; other frames seek to the keyframe before
// them and decode forward, so every frame is exact.
// Not thread-safe: one thread at a time (FrameCache gives each open file its own lock).
// Whether an FFmpeg pixel format ("rgba", "yuva420p", "ya8" ...) carries an alpha channel (9.2b: graphics and movies
// imported as Matte Key clips, Editing Guide p.1196). False for an unknown name.
bool pixelFormatHasAlpha(const QString& pixelFormat);

class VideoReader {
public:
    VideoReader();
    ~VideoReader();
    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;

    // The first video stream when `stream` is -1.
    bool open(const QString& path, int stream = -1, QString* error = nullptr);
    // Whether open() succeeded; info() is only valid then.
    bool isOpen() const;
    const StreamInfo& info() const;
    // Frames in the stream (an image sequence's run); 0 when not open or unknown.
    std::int64_t frameCount() const;

    // The frame as RGBA (8 bits per channel), scaled to `size` if given,
    // keeping it otherwise. Empty if the frame is past the end.
    QImage frame(std::int64_t number, QSize size = {});
    // The CEA-608 field 1 byte pair the frame carries in its A/53 closed caption side data (MPEG-2's and H.264's
    // caption user data, as FFmpeg's decoders give it; Extract DNxHD Data, 11.5a), with its parity; empty when the
    // frame carries none, or past the end.
    QByteArray captionPair(std::int64_t number);
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
// Samples are numbered from the stream's start at the file's own rate (no resampling). Not
// thread-safe; AudioMix keeps one reader per channel it plays.
class AudioReader {
public:
    AudioReader();
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    // The first audio stream when `stream` is -1. False if the stream cannot be decoded, with the reason
    // in *error.
    bool open(const QString& path, int stream = -1, QString* error = nullptr);
    bool isOpen() const;
    const StreamInfo& info() const;
    // The stream's channel count and sample rate; 0 when not open.
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
    // Full range (0-255) rather than legal range Y'CbCr (Color Levels, Editing Guide p.881; movie export, 7.1a).
    bool fullRange = false;
    // The transfer characteristic the stream declares, by FFmpeg's name ("linear" ...); empty: Rec. 709 or 601 as the
    // size says.
    QString colorTransfer;
    // The video encoder's options, its own (OpenEXR's "format": "half", "float") or FFmpeg's for every encoder ("g",
    // "maxrate", "bufsize"); and a fixed quality, FFmpeg's
    // qscale (1 best to 31), 0 for none (JPEG stills' Quality, 7.1b).
    QList<QPair<QString, QString>> videoOptions;
    int quality = 0;
    // Interlaced, upper field first: encoded with interlaced DCT and motion estimation where the encoder has them
    // (MPEG-2: XDCAM in interlaced projects), and the stream says so.
    bool interlaced = false;
    // With `interlaced`, lower field first instead (NTSC's 486 lines, Editing Guide p.1121).
    bool bottomFieldFirst = false;
    // The width of a pixel over its height (the stream's sample aspect ratio): 1 for square pixels, DVCPRO HD's 3:2
    // for 1280 x 1080 (ROADMAP 11.1c2).
    double pixelAspect = 1.0;
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
    // mxf_opatom: the MobIDs of the material package (the master clip: the
    // same in each of its track files) and of the file source package (this
    // file). Null: FFmpeg's own, which differ in every file.
    MobId materialPackage;
    MobId filePackage;
    // copyVideoFrames(): the input's frame rate when it has none of its own, a raw stream (AAF's embedded DNx frames
    // read out, aaf_essence.h); unset: the input's.
    std::optional<Rational> sourceRate;
};

// Not thread-safe. Errors are reported by returning false, with the reason in lastError() (and, for
// open() and finish(), in *error).
class MediaWriter {
public:
    MediaWriter();
    ~MediaWriter();
    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;

    // Creates the file and writes its header. The muxer's metadata keys are FFmpeg's.
    bool open(const QString& path, const WriterSettings& settings, QString* error = nullptr);
    // The next video frame (any QImage format; converted and scaled).
    bool writeVideo(const QImage& frame);
    // The next video frame from a reader's frame, straight from its decoded
    // picture at full depth (transcoding), scaled into `target` (the whole
    // raster if empty) on black. False if the frame cannot be read.
    bool writeVideoFrom(VideoReader& reader, std::int64_t frame, QRect target = {});
    // A CEA-608 field 1 byte pair (with parity) for the next video frame, sent as its A/53 closed caption side data:
    // MPEG-2's and H.264's encoders carry it as caption user data (tests, and media with captions). Empty: none.
    void setNextCaption(const QByteArray& pair);
    // Interleaved float samples, `count` per channel.
    bool writeAudio(const float* samples, int count);
    // Flushes the encoders and writes the trailer.
    bool finish(QString* error = nullptr);
    // The reason the last call failed.
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
// Compares frame `frameA` of `a` with frame `frameB` of `b` (scaled to a's raster); nothing if
// either frame cannot be read. Identical planes give 100 dB.
std::optional<Psnr> comparePictures(VideoReader& a, std::int64_t frameA, VideoReader& b, std::int64_t frameB);

// Converts interleaved float audio from one sample rate to another
// (FFmpeg's swresample).
class Resampler {
public:
    // Channels and rates must be positive; otherwise, or if swresample refuses, isValid() is false
    // and push() returns nothing.
    // `filterSize` is swresample's filter length: longer is cleaner and slower (32, its default;
    // Change Sample Rate's High, Medium and Low quality use 64, 32 and 8).
    Resampler(int channels, int fromRate, int toRate, int filterSize = 32);
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
// its codec fields are ignored but sourceRate. mxf_opatom files get the clip-wrapped label
// and the settings' materialPackage and filePackage, as MediaWriter's do.
bool copyVideoFrames(const QString& from, int stream, std::int64_t first, std::int64_t count, const QString& to,
                     const WriterSettings& settings, QString* error = nullptr);

// Registers FFmpeg's codec provider ("org.ffmpeg.libavcodec") and the CLAP host
// ("org.cleveraudio.clap-host", clap_host.h) with the application's provider
// registry (docs/EXTENSIONS.md).
void registerProviders(ext::ProviderRegistry& registry);

} // namespace lumenis::media

#include "lumenis/media/audio_cd.h"
#include "lumenis/media/media.h"

#include "lumenis/ext/codec.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/clap_host.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/media/tape_deck.h"
#include "lumenis/media/transcription.h"
#include "lumenis/media/xdcam_disk.h"

#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QStringList>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <exception>
#include <cmath>
#include <cstring>
#include <limits>

// The FFmpeg layer of the media library (ROADMAP Phase 2.1): everything here talks to libavformat,
// libavcodec, libswscale and libswresample directly, and the rest of the application sees only the
// types of media.h. It holds:
// - the codec policy (isPatentCodec, LUMENIS_PATENT_CODECS; docs/EXTENSIONS.md);
// - probe(), which fills MediaInfo for the Source Browser (src/ui/source_browser.cpp), linking
//   (link.cpp) and `--probe` (app/main.cpp);
// - ImageSequence, numbered stills read through FFmpeg's image2 demuxer;
// - VideoReader and AudioReader, frame- and sample-exact readers used by FrameCache (link.cpp), the
//   audio mix (playback.cpp), transcoding (managed.cpp) and the decode corpus (`--framemd5`);
// - MediaWriter, which encodes new media (managed.cpp) and test files, and fixOpAtom(), which
//   corrects what FFmpeg's mxf_opatom muxer writes (docs/spec/native-model.md);
// - Resampler, copyVideoFrames() (Consolidate without re-encoding) and comparePictures() (PSNR);
// - the "org.ffmpeg.libavcodec" codec provider (docs/EXTENSIONS.md, src/ext).
// The supported formats are summarised in docs/spec/media-formats.md; how FFmpeg is found for a build
// is in BUILDING.md and cmake/FindFFmpeg.cmake.

namespace lumenis::media {

namespace {

// AV_NOPTS_VALUE, without the C cast in FFmpeg's macro.
constexpr std::int64_t kNoPts = std::numeric_limits<std::int64_t>::min();

// FFmpeg's message for a negative AVERROR code.
QString errorText(int code) {
    char buf[256] = {};
    av_strerror(code, buf, sizeof buf);
    return QString::fromUtf8(buf);
}

// Sets *error when the caller asked for it (error may be null).
void setError(QString* error, const QString& text) {
    if (error) *error = text;
}

// An FFmpeg rational as ours; a zero denominator (FFmpeg's "unknown") becomes the default Rational.
Rational toRational(AVRational r) {
    if (r.den == 0) return Rational();
    return Rational(r.num, r.den);
}

// Ours as FFmpeg's; the terms are narrowed to int, which edit rates and time bases fit.
AVRational toAv(const Rational& r) {
    return AVRational{static_cast<int>(r.num()), static_cast<int>(r.den())};
}

// 1/r: turns an edit rate into the time base that counts its frames, and back.
AVRational inverse(AVRational r) {
    return AVRational{r.den, r.num};
}

// A metadata entry's value, empty if the key is not there.
QString metadata(AVDictionary* dict, const char* key) {
    const AVDictionaryEntry* e = av_dict_get(dict, key, nullptr, 0);
    return e ? QString::fromUtf8(e->value) : QString();
}

// Whether a pixel format holds RGB (packed or planar) rather than Y'CbCr.
bool isRgbFormat(AVPixelFormat format) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_RGB);
}

// The colour space a decoded frame is in: what it says, else Rec. 709 for
// HD and larger, Rec. 601 for SD.
int colourSpaceOf(const AVFrame* f) {
    if (f->colorspace == AVCOL_SPC_BT709) return SWS_CS_ITU709;
    if (f->colorspace == AVCOL_SPC_SMPTE170M || f->colorspace == AVCOL_SPC_BT470BG) return SWS_CS_SMPTE170M;
    return f->height > 576 ? SWS_CS_ITU709 : SWS_CS_SMPTE170M;
}

// Whether a frame's samples use the full range (RGB, JPEG-range Y'CbCr) rather than legal range.
bool fullRange(const AVFrame* f) {
    return isRgbFormat(static_cast<AVPixelFormat>(f->format)) || f->color_range == AVCOL_RANGE_JPEG ||
           f->format == AV_PIX_FMT_YUVJ420P || f->format == AV_PIX_FMT_YUVJ422P || f->format == AV_PIX_FMT_YUVJ444P;
}

// A planar, limited-range Y'CbCr picture as 16-bit 4:4:4 at its own size,
// done here: luma shifted up, chroma interpolated linearly (co-sited
// horizontally, centred vertically), so comparisons and DNx 444 writing
// don't depend on swscale's code for each FFmpeg version and processor.
// False for other pictures.
bool planarYuvTo444(const AVFrame* f, std::uint16_t* y, std::uint16_t* cb, std::uint16_t* cr, int stride) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(f->format));
    if (!desc || (desc->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_BE |
                                 AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM)) ||
        !(desc->flags & AV_PIX_FMT_FLAG_PLANAR) || desc->nb_components < 3 || fullRange(f)) {
        return false;
    }
    const int depth = desc->comp[0].depth;
    if (depth < 8 || depth > 16 || desc->comp[1].depth != depth || desc->comp[2].depth != depth) return false;
    for (int c = 0; c < 3; ++c) {
        if (desc->comp[c].plane != c || desc->comp[c].offset != 0 || desc->comp[c].shift != 0) return false;
    }
    const int sw = desc->log2_chroma_w;
    const int sh = desc->log2_chroma_h;
    const int cw = AV_CEIL_RSHIFT(f->width, sw);
    const int ch = AV_CEIL_RSHIFT(f->height, sh);
    const int up = 16 - depth;
    auto at = [&](int plane, int x, int row) -> std::uint32_t {
        const std::uint8_t* line = f->data[plane] + static_cast<std::ptrdiff_t>(row) * f->linesize[plane];
        if (depth == 8) return line[x];
        return reinterpret_cast<const std::uint16_t*>(line)[x];
    };
    for (int row = 0; row < f->height; ++row) {
        std::uint16_t* out[3] = {y + static_cast<std::ptrdiff_t>(row) * stride, cb + static_cast<std::ptrdiff_t>(row) * stride,
                                 cr + static_cast<std::ptrdiff_t>(row) * stride};
        for (int x = 0; x < f->width; ++x) out[0][x] = static_cast<std::uint16_t>(at(0, x, row) << up);
        // The two chroma rows around this one and the weight of the second.
        double fy = sh ? (row + 0.5) / (1 << sh) - 0.5 : row;
        fy = std::clamp(fy, 0.0, static_cast<double>(ch - 1));
        const int r0 = static_cast<int>(fy);
        const int r1 = std::min(r0 + 1, ch - 1);
        const double wy = fy - r0;
        for (int plane = 1; plane <= 2; ++plane) {
            for (int x = 0; x < f->width; ++x) {
                const double fx = std::min(static_cast<double>(x) / (1 << sw), static_cast<double>(cw - 1));
                const int c0 = static_cast<int>(fx);
                const int c1 = std::min(c0 + 1, cw - 1);
                const double wx = fx - c0;
                const double top = at(plane, c0, r0) * (1 - wx) + at(plane, c1, r0) * wx;
                const double bottom = at(plane, c0, r1) * (1 - wx) + at(plane, c1, r1) * wx;
                const double v = (top * (1 - wy) + bottom * wy) * (1 << up);
                out[plane][x] = static_cast<std::uint16_t>(std::lround(std::clamp(v, 0.0, 65535.0)));
            }
        }
    }
    return true;
}

// The edit rate of a video stream: its average rate, else its base rate.
AVRational videoRate(const AVStream* st) {
    // An average guessed from the bit rate can be absurd (raw DV: 60000
    // fps); the base rate is right then.
    const bool sane = st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0 &&
                      av_q2d(st->avg_frame_rate) <= 1000.0;
    if (!sane && st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) return st->r_frame_rate;
    if (sane) return st->avg_frame_rate;
    if (st->r_frame_rate.num > 0 && st->r_frame_rate.den > 0) return st->r_frame_rate;
    return AVRational{25, 1};
}

// The stream's first timestamp, in its time base; 0 when the file does not give one.
std::int64_t streamStart(const AVStream* st) {
    return st->start_time != kNoPts ? st->start_time : 0;
}

// Frees an input context with avformat_close_input when an Input goes out of scope.
struct InputCloser {
    void operator()(AVFormatContext* c) const {
        if (c) avformat_close_input(&c);
    }
};
using Input = std::unique_ptr<AVFormatContext, InputCloser>;

// Opens a file (or an ImageSequence spec) and reads enough of it to know its streams. Null on
// failure, with FFmpeg's message in *error.
Input openInput(const QString& path, QString* error) {
    AVFormatContext* raw = nullptr;
    QByteArray file = path.toUtf8();
    const AVInputFormat* format = nullptr;
    AVDictionary* options = nullptr;
    if (const auto seq = ImageSequence::fromSpec(path)) {
        // Image sequences through the image2 demuxer, from the run's first
        // number at the project's rate.
        file = seq->pattern.toUtf8();
        format = av_find_input_format("image2");
        av_dict_set(&options, "pattern_type", "sequence", 0);
        av_dict_set_int(&options, "start_number", seq->first, 0);
        av_dict_set(&options, "framerate",
                    QStringLiteral("%1/%2").arg(seq->rate.num()).arg(seq->rate.den()).toUtf8().constData(), 0);
    }
    // WAV files by their own demuxer first: FFmpeg 7 and 8 probe some valid WAVs (ours at 32 kHz with
    // LTC on them, CI on Windows and macOS) as another format and then fail with "End of file". A
    // .wav that is not one is probed as before.
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (!format && (suffix == QLatin1String("wav") || suffix == QLatin1String("wave") || suffix == QLatin1String("bwf"))) {
        if (const AVInputFormat* wav = av_find_input_format("wav")) {
            AVDictionary* wavOptions = nullptr;
            av_dict_copy(&wavOptions, options, 0);
            if (avformat_open_input(&raw, file.constData(), wav, &wavOptions) < 0) raw = nullptr;
            av_dict_free(&wavOptions);
        }
    }
    int rc = raw ? 0 : avformat_open_input(&raw, file.constData(), format, &options);
    av_dict_free(&options);
    if (rc < 0) {
        setError(error, errorText(rc));
        return nullptr;
    }
    Input in(raw);
    rc = avformat_find_stream_info(in.get(), nullptr);
    if (rc < 0) {
        setError(error, errorText(rc));
        return nullptr;
    }
    return in;
}

// Some formats (raw DV, elementary streams) have no index: FFmpeg guesses
// the length from the bit rate, which can be far off. Their frames are
// counted instead: a packet each.
bool lengthIsGuessed(const AVFormatContext* fmt) {
    return fmt->duration_estimation_method == AVFMT_DURATION_FROM_BITRATE;
}

// The packets of one stream, counted by reading the file to its end: the context is left at the
// end, so callers count on a context of their own or seek back.
std::int64_t countPackets(AVFormatContext* fmt, int stream) {
    AVPacket* packet = av_packet_alloc();
    std::int64_t n = 0;
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream) ++n;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    return n;
}

// A stream as StreamInfo: codec, raster, rate, aspect, sample format and length in frames (video) or
// samples (audio). The length comes from the stream's frame count, else its duration, else the
// file's; probe() and VideoReader::open() replace guessed lengths with counted ones.
StreamInfo describe(const AVFormatContext* fmt, const AVStream* st) {
    StreamInfo s;
    const AVCodecParameters* par = st->codecpar;
    s.index = st->index;
    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO: s.kind = StreamKind::Video; break;
    case AVMEDIA_TYPE_AUDIO: s.kind = StreamKind::Audio; break;
    case AVMEDIA_TYPE_DATA: s.kind = StreamKind::Data; break;
    default: s.kind = StreamKind::Other; break;
    }
    const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);
    s.codec = desc ? QString::fromUtf8(desc->name) : QString::fromUtf8(avcodec_get_name(par->codec_id));
    s.codecLongName = desc && desc->long_name ? QString::fromUtf8(desc->long_name) : s.codec;
    if (const char* profile = avcodec_profile_name(par->codec_id, par->profile)) s.profile = QString::fromUtf8(profile);
    s.bitsPerSample = par->bits_per_raw_sample > 0 ? par->bits_per_raw_sample : par->bits_per_coded_sample;
    if (s.kind == StreamKind::Video) {
        s.width = par->width;
        s.height = par->height;
        const AVRational rate = videoRate(st);
        s.frameRate = toRational(rate);
        // The display aspect ratio is the raster times the sample (pixel) aspect ratio, reduced.
        AVRational sar = par->sample_aspect_ratio.num > 0 ? par->sample_aspect_ratio : AVRational{1, 1};
        int dw = 0;
        int dh = 0;
        if (par->width > 0 && par->height > 0) {
            av_reduce(&dw, &dh, static_cast<std::int64_t>(par->width) * sar.num,
                      static_cast<std::int64_t>(par->height) * sar.den, 1024 * 1024);
            s.aspect = Rational(dw, dh);
        }
        if (const char* pf = av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format))) {
            s.pixelFormat = QString::fromUtf8(pf);
        }
        if (par->color_trc != AVCOL_TRC_UNSPECIFIED) {
            if (const char* trc = av_color_transfer_name(par->color_trc)) s.colorTransfer = QString::fromUtf8(trc);
        }
        s.interlaced = par->field_order != AV_FIELD_PROGRESSIVE && par->field_order != AV_FIELD_UNKNOWN;
        if (par->bit_rate > 0) {
            s.bitRate = par->bit_rate;
        } else if (fmt->bit_rate > 0) {
            int videos = 0;
            for (unsigned k = 0; k < fmt->nb_streams; ++k) videos += fmt->streams[k]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
            if (videos == 1) s.bitRate = fmt->bit_rate;
        }
        // The field shown first (AVFieldOrder names the coded order first: TB is "top coded first, bottom displayed
        // first", as FFmpeg reads lower-field-first MXF); unknown and progressive count as upper.
        s.topFieldFirst = par->field_order != AV_FIELD_BB && par->field_order != AV_FIELD_TB;
        if (st->nb_frames > 0) {
            s.length = st->nb_frames;
        } else if (st->duration != kNoPts && st->duration > 0) {
            s.length = av_rescale_q(st->duration, st->time_base, inverse(rate));
        } else if (fmt->duration > 0) {
            s.length = av_rescale_q(fmt->duration, AVRational{1, AV_TIME_BASE}, inverse(rate));
        }
    } else if (s.kind == StreamKind::Audio) {
        s.sampleRate = par->sample_rate;
        s.channels = par->ch_layout.nb_channels;
        if (const char* sf = av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format))) {
            s.sampleFormat = QString::fromUtf8(sf);
        }
        if (st->duration != kNoPts && st->duration > 0 && par->sample_rate > 0) {
            s.length = av_rescale_q(st->duration, st->time_base, AVRational{1, par->sample_rate});
        } else if (fmt->duration > 0 && par->sample_rate > 0) {
            s.length = av_rescale_q(fmt->duration, AVRational{1, AV_TIME_BASE}, AVRational{1, par->sample_rate});
        }
    }
    s.decodable = avcodec_find_decoder(par->codec_id) != nullptr && decodingAvailable(s.codec);
    return s;
}

// The "kind" of a stream in MediaInfo::toJson().
const char* kindName(StreamKind k) {
    switch (k) {
    case StreamKind::Video: return "video";
    case StreamKind::Audio: return "audio";
    case StreamKind::Data: return "data";
    case StreamKind::Other: return "other";
    }
    return "other";
}

// The index of the first stream of a type, -1 if there is none.
int firstStream(const AVFormatContext* fmt, AVMediaType type) {
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == type) return static_cast<int>(i);
    }
    return -1;
}

// The operating systems' decoders (the policy section below).
QList<AVHWDeviceType> osDeviceTypes();
AVPixelFormat hardwareFormat(const AVCodec* codec, AVHWDeviceType type);
const AVCodec* osAudioDecoder(const QString& codec);
AVPixelFormat hardwareOnly(AVCodecContext* ctx, const AVPixelFormat* offered);

// A frame decoded on a hardware device copied into memory in its place, so every reader goes on as with FFmpeg's
// own decoders (NV12 or P010 from VideoToolbox, D3D11VA and VA-API, which swscale takes).
bool takeFromDevice(AVFrame* frame) {
    AVFrame* copy = av_frame_alloc();
    if (!copy) return false;
    if (av_hwframe_transfer_data(copy, frame, 0) < 0 || av_frame_copy_props(copy, frame) < 0) {
        av_frame_free(&copy);
        return false;
    }
    av_frame_unref(frame);
    av_frame_move_ref(frame, copy);
    av_frame_free(&copy);
    return true;
}

// A decoder for one stream, with FFmpeg's own frame threading.
AVCodecContext* openDecoder(const AVStream* st, QString* error) {
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        setError(error, QStringLiteral("no decoder for %1").arg(QString::fromUtf8(avcodec_get_name(st->codecpar->codec_id))));
        return nullptr;
    }
    const AVCodecDescriptor* desc = avcodec_descriptor_get(st->codecpar->codec_id);
    const QString name = desc ? QString::fromUtf8(desc->name) : QString::fromUtf8(codec->name);
    AVBufferRef* device = nullptr;
    AVPixelFormat hardware = AV_PIX_FMT_NONE;
    if (!codecAllowed(name)) {
        // The release policy (docs/spec/release-codecs.md): the operating system decodes it, or nobody does.
        if (const AVCodec* os = st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ? osAudioDecoder(name) : nullptr) {
            codec = os;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            for (AVHWDeviceType type : osDeviceTypes()) {
                const AVPixelFormat format = hardwareFormat(codec, type);
                if (format == AV_PIX_FMT_NONE || av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) continue;
                hardware = format;
                break;
            }
        }
        if (codec->id == st->codecpar->codec_id && !isOsCodec(QString::fromUtf8(codec->name)) && !device) {
            setError(error, QStringLiteral("%1 is decoded by the operating system in this build, and this computer has no "
                                           "decoder for it (docs/spec/release-codecs.md)")
                                .arg(name));
            return nullptr;
        }
    }
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    if (device) {
        // Frames come back on the device and are copied to memory as they are received (takeFromDevice()).
        ctx->hw_device_ctx = device;
        ctx->opaque = reinterpret_cast<void*>(static_cast<std::intptr_t>(hardware));
        ctx->get_format = hardwareOnly;
    }
    ctx->pkt_timebase = st->time_base;
    ctx->thread_count = 0; // automatic
    const int rc = avcodec_open2(ctx, codec, nullptr);
    if (rc < 0) {
        setError(error, errorText(rc));
        avcodec_free_context(&ctx);
        return nullptr;
    }
    return ctx;
}

} // namespace

// ---------------------------------------------------------------- policy

QString ffmpegVersion() {
    auto v = [](unsigned version) {
        return QStringLiteral("%1.%2.%3").arg(version >> 16).arg((version >> 8) & 0xff).arg(version & 0xff);
    };
    return QStringLiteral("FFmpeg %1 (libavcodec %2, libavformat %3, libavutil %4)")
        .arg(QString::fromUtf8(av_version_info()), v(avcodec_version()), v(avformat_version()), v(avutil_version()));
}

bool isPatentCodec(const QString& codec) {
    // docs/EXTENSIONS.md: entries marked with a dagger. DNxHD/DNxHR are allowed in releases (the owner's decision,
    // 2026-11-15; SMPTE VC-3), so they are not here.
    static const QStringList names{
        QStringLiteral("h264"),   QStringLiteral("hevc"),     QStringLiteral("prores"),   QStringLiteral("prores_ks"),
        QStringLiteral("prores_aw"), QStringLiteral("prores_raw"), QStringLiteral("libx264"), QStringLiteral("libx265"),
        QStringLiteral("aac"),    QStringLiteral("ac3"),      QStringLiteral("eac3"),     QStringLiteral("mpeg4"),
        QStringLiteral("vc1"),
    };
    return names.contains(codec.toLower());
}

bool patentCodecsAllowed() {
#if LUMENIS_PATENT_CODECS
    return qEnvironmentVariableIntValue("LUMENIS_PATENT_CODECS_OFF") == 0;
#else
    return false;
#endif
}

bool codecAllowed(const QString& codec) {
    return patentCodecsAllowed() || !isPatentCodec(codec);
}

bool isOsCodec(const QString& implementation) {
    static const QStringList suffixes{QStringLiteral("_videotoolbox"), QStringLiteral("_at"),  QStringLiteral("_mf"),
                                      QStringLiteral("_vaapi"),        QStringLiteral("_qsv"), QStringLiteral("_nvenc"),
                                      QStringLiteral("_cuvid"),        QStringLiteral("_amf")};
    const QString name = implementation.toLower();
    return std::any_of(suffixes.begin(), suffixes.end(), [&](const QString& s) { return name.endsWith(s); });
}

namespace {

// Whether the encoder opens here with a small picture or stereo sound, tried once per encoder (defined below).
bool encoderOpens(const AVCodec* codec);

// Whether this build may use the encoder: an OS one, or FFmpeg's own for an allowed codec.
bool encoderAllowed(const AVCodec* codec) {
    if (!codec) return false;
    const QString name = QString::fromUtf8(codec->name);
    return isOsCodec(name) || (codecAllowed(name) && codecAllowed(QString::fromUtf8(avcodec_get_name(codec->id))));
}

// The hardware device types of the operating system's video decoders, in the order tried.
QList<AVHWDeviceType> osDeviceTypes() {
#if defined(Q_OS_MACOS)
    return {AV_HWDEVICE_TYPE_VIDEOTOOLBOX};
#elif defined(Q_OS_WIN)
    return {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2};
#else
    return {AV_HWDEVICE_TYPE_VAAPI};
#endif
}

// The pixel format a decoder gives on a device type through a device context; NONE when it has no such method.
AVPixelFormat hardwareFormat(const AVCodec* codec, AVHWDeviceType type) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config) return AV_PIX_FMT_NONE;
        if (config->device_type == type && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) return config->pix_fmt;
    }
}

// The OS's audio decoder for a codec FFmpeg's own may not decode (AudioToolbox's "aac_at" on macOS); null for none.
const AVCodec* osAudioDecoder(const QString& codec) {
#if defined(Q_OS_MACOS)
    return avcodec_find_decoder_by_name((codec.toLower() + QStringLiteral("_at")).toUtf8().constData());
#else
    (void)codec;
    return nullptr;
#endif
}

// Whether a device of this type can be made here (a GPU and its driver): tried once, the answer kept.
bool deviceAvailable(AVHWDeviceType type) {
    static std::mutex lock;
    static QHash<int, bool> known;
    const std::lock_guard<std::mutex> hold(lock);
    const auto it = known.constFind(static_cast<int>(type));
    if (it != known.constEnd()) return *it;
    AVBufferRef* device = nullptr;
    const bool ok = av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) >= 0;
    av_buffer_unref(&device);
    known.insert(static_cast<int>(type), ok);
    return ok;
}

// get_format for a decoder that must stay on the OS's hardware: the hardware format, or none (no fall back to
// FFmpeg's own decoding, which the policy does not allow). The wanted format is kept in the context's opaque.
AVPixelFormat hardwareOnly(AVCodecContext* ctx, const AVPixelFormat* offered) {
    const auto wanted = static_cast<AVPixelFormat>(reinterpret_cast<std::intptr_t>(ctx->opaque));
    for (const AVPixelFormat* f = offered; *f != AV_PIX_FMT_NONE; ++f) {
        if (*f == wanted) return *f;
    }
    return AV_PIX_FMT_NONE;
}

} // namespace

bool encoderAvailable(const QString& name) {
    return encoderAllowed(avcodec_find_encoder_by_name(name.toUtf8().constData()));
}

QString osEncoderFor(const QString& codec) {
#if defined(Q_OS_MACOS)
    const QString name = codec.toLower() + (codec.compare(QLatin1String("aac"), Qt::CaseInsensitive) == 0 ? QStringLiteral("_at")
                                                                                                         : QStringLiteral("_videotoolbox"));
#elif defined(Q_OS_WIN)
    const QString name = codec.toLower() + QStringLiteral("_mf");
#else
    (void)codec;
    const QString name; // VA-API's encoders take frames on the device: not used (ours)
#endif
    if (name.isEmpty()) return {};
    const AVCodec* encoder = avcodec_find_encoder_by_name(name.toUtf8().constData());
    // Compiled in is not enough: the OS must have the encoder behind it (Windows Server has no Media Foundation H.264
    // encoder, BUILDING.md "Release builds cannot export H.264 on a server").
    return encoder && encoderOpens(encoder) ? name : QString();
}

bool decodingAvailable(const QString& codec) {
    if (codecAllowed(codec)) return true;
    const AVCodecDescriptor* desc = avcodec_descriptor_get_by_name(codec.toUtf8().constData());
    const AVCodec* decoder = desc ? avcodec_find_decoder(desc->id) : nullptr;
    if (!decoder) return false;
    if (decoder->type == AVMEDIA_TYPE_AUDIO) return osAudioDecoder(codec) != nullptr;
    for (AVHWDeviceType type : osDeviceTypes()) {
        if (hardwareFormat(decoder, type) != AV_PIX_FMT_NONE && deviceAvailable(type)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- probe

const StreamInfo* MediaInfo::firstVideo() const {
    for (const StreamInfo& s : streams) {
        if (s.kind == StreamKind::Video) return &s;
    }
    return nullptr;
}

std::vector<const StreamInfo*> MediaInfo::audio() const {
    std::vector<const StreamInfo*> out;
    for (const StreamInfo& s : streams) {
        if (s.kind == StreamKind::Audio) out.push_back(&s);
    }
    return out;
}

// MediaInfo as JSON, as `--probe` prints it (app/main.cpp).
QJsonObject MediaInfo::toJson() const {
    QJsonArray list;
    for (const StreamInfo& s : streams) {
        QJsonObject o{{QStringLiteral("index"), s.index},
                      {QStringLiteral("kind"), QString::fromLatin1(kindName(s.kind))},
                      {QStringLiteral("codec"), s.codec},
                      {QStringLiteral("decodable"), s.decodable}};
        if (!s.profile.isEmpty()) o.insert(QStringLiteral("profile"), s.profile);
        if (s.kind == StreamKind::Video) {
            o.insert(QStringLiteral("width"), s.width);
            o.insert(QStringLiteral("height"), s.height);
            o.insert(QStringLiteral("rate"), QStringLiteral("%1/%2").arg(s.frameRate.num()).arg(s.frameRate.den()));
            o.insert(QStringLiteral("aspect"), QStringLiteral("%1:%2").arg(s.aspect.num()).arg(s.aspect.den()));
            o.insert(QStringLiteral("pixelFormat"), s.pixelFormat);
            o.insert(QStringLiteral("interlaced"), s.interlaced);
            o.insert(QStringLiteral("frames"), static_cast<qint64>(s.length));
        } else if (s.kind == StreamKind::Audio) {
            o.insert(QStringLiteral("sampleRate"), s.sampleRate);
            o.insert(QStringLiteral("channels"), s.channels);
            o.insert(QStringLiteral("sampleFormat"), s.sampleFormat);
            if (s.bitsPerSample > 0) o.insert(QStringLiteral("bits"), s.bitsPerSample);
            o.insert(QStringLiteral("samples"), static_cast<qint64>(s.length));
        }
        list.append(o);
    }
    QJsonObject o{{QStringLiteral("path"), path},
                  {QStringLiteral("format"), format},
                  {QStringLiteral("duration"), durationSeconds},
                  {QStringLiteral("streams"), list}};
    if (!startTimecode.isEmpty()) o.insert(QStringLiteral("timecode"), startTimecode);
    if (!reelName.isEmpty()) o.insert(QStringLiteral("reel"), reelName);
    return o;
}

// Reads a file's format, metadata and streams without decoding any frames (except to count those of
// formats without an index, see lengthIsGuessed()). The file and stream metadata are merged:
// QuickTime keeps the timecode and reel on a track, MXF the file package's UMID on its stream.
namespace {

std::optional<MediaInfo> probeUnchecked(const QString& path, QString* error) {
    Input in = openInput(path, error);
    if (!in) return std::nullopt;
    MediaInfo info;
    info.path = path;
    info.format = QString::fromUtf8(in->iformat->name);
    info.formatLongName = in->iformat->long_name ? QString::fromUtf8(in->iformat->long_name) : info.format;
    if (in->duration > 0) info.durationSeconds = static_cast<double>(in->duration) / AV_TIME_BASE;
    info.startTimecode = metadata(in->metadata, "timecode");
    info.reelName = metadata(in->metadata, "reel_name");
    for (const AVDictionaryEntry* e = nullptr; (e = av_dict_get(in->metadata, "", e, AV_DICT_IGNORE_SUFFIX));) {
        info.metadata.insert(QString::fromUtf8(e->key), QString::fromUtf8(e->value));
    }
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream* st = in->streams[i];
        info.streams.push_back(describe(in.get(), st));
        if (info.streams.back().kind == StreamKind::Video && lengthIsGuessed(in.get())) {
            info.streams.back().length = countPackets(in.get(), static_cast<int>(i));
        }
        // QuickTime keeps the timecode and reel on its timecode track.
        if (info.startTimecode.isEmpty()) info.startTimecode = metadata(st->metadata, "timecode");
        if (info.reelName.isEmpty()) info.reelName = metadata(st->metadata, "reel_name");
        // MXF: the file source package of the stream (OP-Atom has one), next
        // to the format's material_package_umid.
        if (!info.metadata.contains(QStringLiteral("file_package_umid"))) {
            if (const QString umid = metadata(st->metadata, "file_package_umid"); !umid.isEmpty()) {
                info.metadata.insert(QStringLiteral("file_package_umid"), umid);
            }
        }
    }
    if (const auto seq = ImageSequence::fromSpec(path)) {
        // The run is what was chosen, not every numbered file after it.
        for (StreamInfo& s : info.streams) {
            if (s.kind != StreamKind::Video) continue;
            s.length = seq->count;
            s.frameRate = seq->rate;
        }
        info.durationSeconds = static_cast<double>(seq->count) * static_cast<double>(seq->rate.den()) /
                               static_cast<double>(seq->rate.num());
    }
    return info;
}

} // namespace

std::optional<MediaInfo> probe(const QString& path, QString* error) {
    // A damaged file's numbers can be past what Rational holds (it throws rather than wrap): the
    // file is then unreadable, not a crash.
    try {
        return probeUnchecked(path, error);
    } catch (const std::exception& e) {
        if (error) *error = QStringLiteral("%1: %2").arg(path, QString::fromUtf8(e.what()));
        return std::nullopt;
    }
}

// ---------------------------------------------------------------- ImageSequence

namespace {

// The extensions isNumberedStill() takes as image formats that can make a sequence.
const QStringList& stillExtensions() {
    static const QStringList e{QStringLiteral("dpx"), QStringLiteral("exr"), QStringLiteral("tif"),
                               QStringLiteral("tiff"), QStringLiteral("png"), QStringLiteral("jpg"),
                               QStringLiteral("jpeg"), QStringLiteral("tga"), QStringLiteral("bmp"),
                               QStringLiteral("cin")};
    return e;
}

} // namespace

QString ImageSequence::fileAt(std::int64_t index) const {
    // The pattern holds one %0Nd.
    const qsizetype at = pattern.indexOf(QLatin1Char('%'));
    const qsizetype d = pattern.indexOf(QLatin1Char('d'), at);
    if (at < 0 || d < 0) return pattern;
    const int width = pattern.mid(at + 2, d - at - 2).toInt();
    return pattern.left(at) + QStringLiteral("%1").arg(first + index, width, 10, QLatin1Char('0')) + pattern.mid(d + 1);
}

// The file name before its number, with trailing separators ("._- ") removed.
QString ImageSequence::name() const {
    const QString file = pattern.mid(pattern.lastIndexOf(QLatin1Char('/')) + 1);
    QString stem = file.left(file.indexOf(QLatin1Char('%')));
    while (!stem.isEmpty() && QStringLiteral("._- ").contains(stem.back())) stem.chop(1);
    return stem;
}

QString ImageSequence::spec() const {
    // Joined, not arg(): the pattern's own %04d would be taken for a marker.
    return QStringLiteral("seq:") + pattern +
           QStringLiteral("?first=%1&count=%2&rate=%3/%4").arg(first).arg(count).arg(rate.num()).arg(rate.den());
}

// Parses "seq:<pattern>?first=..&count=..&rate=n/d". The query is the text after the last '?', so
// the pattern may hold one. Nothing if it is not a spec or the run is empty.
std::optional<ImageSequence> ImageSequence::fromSpec(const QString& spec) {
    if (!spec.startsWith(QLatin1String("seq:"))) return std::nullopt;
    const qsizetype q = spec.lastIndexOf(QLatin1Char('?'));
    if (q < 0) return std::nullopt;
    ImageSequence s;
    s.pattern = spec.mid(4, q - 4);
    for (const QString& kv : spec.mid(q + 1).split(QLatin1Char('&'))) {
        const QString key = kv.section(QLatin1Char('='), 0, 0);
        const QString value = kv.section(QLatin1Char('='), 1);
        if (key == QLatin1String("first")) s.first = value.toLongLong();
        if (key == QLatin1String("count")) s.count = value.toLongLong();
        if (key == QLatin1String("rate")) {
            const std::int64_t num = value.section(QLatin1Char('/'), 0, 0).toLongLong();
            const std::int64_t den = value.section(QLatin1Char('/'), 1).toLongLong();
            if (num > 0 && den > 0) s.rate = Rational(num, den);
        }
    }
    if (s.pattern.isEmpty() || s.count <= 0) return std::nullopt;
    return s;
}

bool isSequenceSpec(const QString& path) {
    return path.startsWith(QLatin1String("seq:"));
}

bool isNumberedStill(const QString& path) {
    const QString file = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
    const qsizetype dot = file.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || !stillExtensions().contains(file.mid(dot + 1).toLower())) return false;
    return file.at(dot - 1).isDigit();
}

// ---------------------------------------------------------------- VideoReader

// One open video stream: the demuxer, a decoder and the last decoded frame. Frame numbers count
// from the first frame shown at the stream's edit rate (`rate`); toPts() and toFrame() convert
// between them and the stream's timestamps. Not thread-safe: FrameCache serialises its use.
struct VideoReader::Impl {
    Input fmt;
    AVStream* stream = nullptr;
    AVCodecContext* dec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    StreamInfo info;
    // The edit rate, frames per second (videoRate()).
    AVRational rate{25, 1};
    std::int64_t next = -1; // the frame the decoder gives next, -1: unknown
    bool eof = false;

    ~Impl() {
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&dec);
    }

    // The first frame's timestamp, in the stream's time base (set in open()).
    std::int64_t first = 0; // the first frame's time
    std::int64_t toPts(std::int64_t n) const {
        return first + av_rescale_q(n, inverse(rate), stream->time_base);
    }
    // The nearest frame number for a timestamp, so rounded timestamps still find their frame.
    std::int64_t toFrame(std::int64_t pts) const {
        return av_rescale_q_rnd(pts - first, stream->time_base, inverse(rate),
                                static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
    }

    // Seeks to the keyframe at or before frame n (the stream's start for frame 0, or if that seek fails)
    // and empties the decoder. Which frame decoding gives next is then unknown.
    void seek(std::int64_t n) {
        avcodec_flush_buffers(dec);
        if (n <= 0 || av_seek_frame(fmt.get(), stream->index, toPts(n), AVSEEK_FLAG_BACKWARD) < 0) {
            av_seek_frame(fmt.get(), stream->index, streamStart(stream), AVSEEK_FLAG_BACKWARD);
        }
        eof = false;
        next = -1;
    }

    // The next decoded frame, its number in `number`; false at the end.
    bool decodeNext(std::int64_t& number) {
        for (;;) {
            // FFmpeg's send/receive loop: take a frame if the decoder has one; otherwise feed it the next
            // packet of our stream (others are dropped), or, at the end of the file, a null packet that drains
            // what it still holds until it returns AVERROR_EOF.
            const int rc = avcodec_receive_frame(dec, frame);
            if (rc == 0 && frame->hw_frames_ctx && !takeFromDevice(frame)) return false;
            if (rc == 0) {
                // The frame's number from its timestamp; without one, the one after the last.
                const std::int64_t pts = frame->best_effort_timestamp != kNoPts ? frame->best_effort_timestamp : frame->pts;
                number = pts != kNoPts ? toFrame(pts) : (next >= 0 ? next : 0);
                next = number + 1;
                return true;
            }
            if (rc == AVERROR_EOF) return false;
            if (eof) {
                avcodec_send_packet(dec, nullptr);
                continue;
            }
            // Feed the decoder.
            for (;;) {
                const int read = av_read_frame(fmt.get(), packet);
                if (read < 0) {
                    eof = true;
                    avcodec_send_packet(dec, nullptr);
                    break;
                }
                if (packet->stream_index == stream->index) {
                    avcodec_send_packet(dec, packet);
                    av_packet_unref(packet);
                    break;
                }
                av_packet_unref(packet);
            }
        }
    }

    // Decodes frame `number` into `frame`.
    bool locate(std::int64_t number);
    bool rewind();
    QString path;

    // The current frame as an RGBA8888 QImage at `size` (its own if empty), through a cached swscale
    // context. The source's matrix and range are set explicitly so pictures look the same whatever the
    // file leaves unspecified.
    QImage convert(QSize size) {
        const int w = size.isValid() && !size.isEmpty() ? size.width() : frame->width;
        const int h = size.isValid() && !size.isEmpty() ? size.height() : frame->height;
        sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), w, h,
                                   AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!sws) return {};
        // HD and larger is Rec. 709, unless the file says otherwise.
        int space = SWS_CS_DEFAULT;
        if (frame->colorspace == AVCOL_SPC_BT709 || (frame->colorspace == AVCOL_SPC_UNSPECIFIED && frame->height > 576)) {
            space = SWS_CS_ITU709;
        } else if (frame->colorspace == AVCOL_SPC_SMPTE170M || frame->colorspace == AVCOL_SPC_BT470BG) {
            space = SWS_CS_SMPTE170M;
        }
        const int srcFull = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
        sws_setColorspaceDetails(sws, sws_getCoefficients(space), srcFull, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0,
                                 1 << 16, 1 << 16);
        QImage image(w, h, QImage::Format_RGBA8888);
        std::uint8_t* dst[4] = {image.bits(), nullptr, nullptr, nullptr};
        int dstStride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
        return image;
    }
};

VideoReader::VideoReader() = default;
VideoReader::~VideoReader() = default;

// Opens the stream, works out its length (counted when the format only guesses it, the chosen run
// for an image sequence) and decodes the first frame to learn where frame 0 starts, then seeks back.
// On failure the reader stays as it was.
bool VideoReader::open(const QString& path, int streamIndex, QString* error) {
    auto d = std::make_unique<Impl>();
    d->fmt = openInput(path, error);
    if (!d->fmt) return false;
    if (streamIndex < 0) streamIndex = firstStream(d->fmt.get(), AVMEDIA_TYPE_VIDEO);
    if (streamIndex < 0 || static_cast<unsigned>(streamIndex) >= d->fmt->nb_streams ||
        d->fmt->streams[streamIndex]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        setError(error, QStringLiteral("no video stream"));
        return false;
    }
    d->path = path;
    d->stream = d->fmt->streams[streamIndex];
    d->dec = openDecoder(d->stream, error);
    if (!d->dec) return false;
    d->rate = videoRate(d->stream);
    d->info = describe(d->fmt.get(), d->stream);
    if (lengthIsGuessed(d->fmt.get())) {
        // Counted on a file of its own: some demuxers (raw DV) do not read
        // on properly after reaching the end and seeking back.
        if (Input counter = openInput(path, nullptr)) d->info.length = countPackets(counter.get(), streamIndex);
    }
    if (const auto seq = ImageSequence::fromSpec(path)) {
        d->info.length = seq->count; // the run chosen, not every file after it
        d->info.frameRate = seq->rate;
    }
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    // Frame 0 is the first one shown, which can start after the stream
    // does (B-frames in AVI and MPEG-TS): its time is where counting starts.
    d->first = streamStart(d->stream);
    d->next = -1;
    std::int64_t number = 0;
    if (d->decodeNext(number)) {
        const std::int64_t pts = d->frame->best_effort_timestamp != kNoPts ? d->frame->best_effort_timestamp
                                                                           : d->frame->pts;
        if (pts != kNoPts) d->first = pts;
    }
    d->seek(0);
    d_ = std::move(d);
    return true;
}

bool VideoReader::isOpen() const {
    return d_ != nullptr;
}

const StreamInfo& VideoReader::info() const {
    return d_->info;
}

std::int64_t VideoReader::frameCount() const {
    return d_ ? d_->info.length : 0;
}

bool VideoReader::Impl::rewind() {
    // A fresh start: some formats (MPEG-TS, AVI) do not seek back to their
    // first frames exactly.
    const int index = stream->index;
    Input again = openInput(path, nullptr);
    if (!again || index >= static_cast<int>(again->nb_streams)) return false;
    AVCodecContext* fresh = openDecoder(again->streams[index], nullptr);
    if (!fresh) return false;
    avcodec_free_context(&dec);
    dec = fresh;
    fmt = std::move(again);
    stream = fmt->streams[index];
    eof = false;
    next = -1;
    return true;
}

// Decodes frame `number` into `frame`: carries on when it is the next one, otherwise seeks to the
// keyframe before it and decodes forward, discarding earlier frames. If the seek lands after the
// frame, or the frame never comes, it starts over from the beginning once (rewind()) and decodes
// every frame up to it. False past the end or if the frame cannot be found.
bool VideoReader::Impl::locate(std::int64_t number) {
    if (number < 0 || (info.length > 0 && number >= info.length)) return false;
    // The next frame continues decoding; anything else seeks first.
    if (next != number) seek(number);
    std::int64_t got = -1;
    bool rewound = false;
    for (;;) {
        while (decodeNext(got)) {
            if (got < number) continue;
            if (got > number && !rewound) break; // the seek landed after it
            return true;
        }
        // Not found where the seek went, or passed: from the start, frame by
        // frame.
        if (rewound || !rewind()) break;
        rewound = true;
    }
    next = -1;
    return false;
}

QByteArray VideoReader::captionPair(std::int64_t number) {
    if (!d_ || !d_->locate(number)) return {};
    const AVFrameSideData* side = av_frame_get_side_data(d_->frame, AV_FRAME_DATA_A53_CC);
    if (!side) return {};
    // cc_data triplets (ATSC A/53 Part 4): marker bits, cc_valid and cc_type in the first byte, then two bytes; type 0
    // is NTSC field 1, which CEA-608's CC1 rides on.
    for (std::size_t i = 0; i + 2 < side->size; i += 3) {
        const std::uint8_t head = side->data[i];
        if ((head & 0x04) && (head & 0x03) == 0) {
            return QByteArray(reinterpret_cast<const char*>(side->data + i + 1), 2);
        }
    }
    return {};
}

QImage VideoReader::frame(std::int64_t number, QSize size) {
    if (!d_ || !d_->locate(number)) return {};
    return d_->convert(size);
}

QString VideoReader::frameMd5(std::int64_t number) {
    if (!d_ || !d_->locate(number)) return {};
    // As FFmpeg's framemd5 muxer hashes a decoded picture: its planes
    // packed one after another, rows without padding.
    const AVFrame* f = d_->frame;
    const auto format = static_cast<AVPixelFormat>(f->format);
    const int size = av_image_get_buffer_size(format, f->width, f->height, 1);
    if (size <= 0) return {};
    QByteArray bytes(size, Qt::Uninitialized);
    if (av_image_copy_to_buffer(reinterpret_cast<std::uint8_t*>(bytes.data()), size, f->data, f->linesize, format,
                                f->width, f->height, 1) < 0) {
        return {};
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

// Three ways, so the values do not depend on the FFmpeg version where it matters: planar RGB at its
// own size is converted here with the Rec. 709 matrix; planar legal-range Y'CbCr at its own size is
// upsampled here (planarYuvTo444()); anything else, or a different size, goes through swscale.
bool VideoReader::planes(std::int64_t number, QSize size, std::vector<std::uint16_t>* y, std::vector<std::uint16_t>* cb,
                         std::vector<std::uint16_t>* cr) {
    if (!d_ || !d_->locate(number)) return false;
    const AVFrame* f = d_->frame;
    const int w = size.isValid() && !size.isEmpty() ? size.width() : f->width;
    const int h = size.isValid() && !size.isEmpty() ? size.height() : f->height;
    const auto format = static_cast<AVPixelFormat>(f->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    if (desc && (desc->flags & AV_PIX_FMT_FLAG_RGB) && (desc->flags & AV_PIX_FMT_FLAG_PLANAR) && !(desc->flags & AV_PIX_FMT_FLAG_FLOAT) &&
        desc->comp[0].depth > 8 && desc->comp[0].depth <= 16 && w == f->width && h == f->height) {
        // Planar RGB (DNx 444) becomes Rec. 709 legal-range Y'CbCr here,
        // exactly, so the comparison does not depend on swscale's RGB
        // path, which differs between FFmpeg versions.
        const int depth = desc->comp[0].depth;
        const double full = static_cast<double>((1 << depth) - 1);
        const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        y->assign(n, 0);
        cb->assign(n, 0);
        cr->assign(n, 0);
        // The descriptor's components are R, G and B, wherever their planes
        // are (gbrp: G, B, R).
        const int rPlane = desc->comp[0].plane;
        const int gPlane = desc->comp[1].plane;
        const int bPlane = desc->comp[2].plane;
        auto sample = [&](int plane, int x, int row) {
            const auto* line = reinterpret_cast<const std::uint16_t*>(f->data[plane] + static_cast<std::ptrdiff_t>(row) * f->linesize[plane]);
            return static_cast<double>(line[x]) / full;
        };
        for (int row = 0; row < h; ++row) {
            for (int x = 0; x < w; ++x) {
                const double r = sample(rPlane, x, row);
                const double g = sample(gPlane, x, row);
                const double b = sample(bPlane, x, row);
                const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                const double pb = (b - luma) / 1.8556;
                const double pr = (r - luma) / 1.5748;
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                // Legal range, 16-bit: 16..235 and 16..240 scaled by 256.
                (*y)[i] = static_cast<std::uint16_t>(std::lround(std::clamp((16.0 + 219.0 * luma) * 256.0, 0.0, 65535.0)));
                (*cb)[i] = static_cast<std::uint16_t>(std::lround(std::clamp((128.0 + 224.0 * pb) * 256.0, 0.0, 65535.0)));
                (*cr)[i] = static_cast<std::uint16_t>(std::lround(std::clamp((128.0 + 224.0 * pr) * 256.0, 0.0, 65535.0)));
            }
        }
        return true;
    }
    // Planar Y'CbCr at its own size: done here when planarYuvTo444() takes the format.
    if (w == f->width && h == f->height) {
        const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        y->assign(n, 0);
        cb->assign(n, 0);
        cr->assign(n, 0);
        if (planarYuvTo444(f, y->data(), cb->data(), cr->data(), w)) return true;
    }
    // Otherwise swscale, to 16-bit little-endian 4:4:4 at the requested size.
    SwsContext* sws = sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), w, h,
                                     AV_PIX_FMT_YUV444P16LE, SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                                     nullptr, nullptr, nullptr);
    if (!sws) return false;
    // RGB pictures become Rec. 709 Y'CbCr; Y'CbCr ones keep their values.
    sws_setColorspaceDetails(sws, sws_getCoefficients(SWS_CS_ITU709), fullRange(f) ? 1 : 0,
                             sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    y->assign(n, 0);
    cb->assign(n, 0);
    cr->assign(n, 0);
    std::uint8_t* dst[4] = {reinterpret_cast<std::uint8_t*>(y->data()), reinterpret_cast<std::uint8_t*>(cb->data()),
                            reinterpret_cast<std::uint8_t*>(cr->data()), nullptr};
    const int stride[4] = {w * 2, w * 2, w * 2, 0};
    sws_scale(sws, f->data, f->linesize, 0, f->height, dst, stride);
    sws_freeContext(sws);
    return true;
}

double Psnr::worst() const {
    return std::min({y, cb, cr});
}

// Frame A against frame B scaled to A's raster. Identical planes give 100 dB rather than infinity.
std::optional<Psnr> comparePictures(VideoReader& a, std::int64_t frameA, VideoReader& b, std::int64_t frameB) {
    std::vector<std::uint16_t> ay, acb, acr, by, bcb, bcr;
    if (!a.planes(frameA, {}, &ay, &acb, &acr)) return std::nullopt;
    const QSize size(a.info().width, a.info().height);
    if (!b.planes(frameB, size, &by, &bcb, &bcr)) return std::nullopt;
    auto psnr = [](const std::vector<std::uint16_t>& p, const std::vector<std::uint16_t>& q) {
        double sum = 0;
        for (std::size_t i = 0; i < p.size() && i < q.size(); ++i) {
            const double d = static_cast<double>(p[i]) - static_cast<double>(q[i]);
            sum += d * d;
        }
        const double mse = sum / static_cast<double>(std::max<std::size_t>(p.size(), 1));
        if (mse <= 0) return 100.0;
        return 10.0 * std::log10(65535.0 * 65535.0 / mse);
    };
    Psnr r;
    r.y = psnr(ay, by);
    r.cb = psnr(acb, bcb);
    r.cr = psnr(acr, bcr);
    return r;
}

// ---------------------------------------------------------------- AudioReader

// One open audio stream, decoded and converted to interleaved float at the stream's own rate and
// channel count (no resampling: Resampler and AudioMix do that). Decoded samples wait in `pending`
// so consecutive requests continue without seeking.
struct AudioReader::Impl {
    Input fmt;
    AVStream* stream = nullptr;
    AVCodecContext* dec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* swr = nullptr;
    StreamInfo info;
    int channels = 0;
    int rate = 0;
    // pendingStart is the sample number of pending's first sample; -1 after a seek, until the first
    // decoded frame says where it starts.
    std::vector<float> pending;  // decoded, interleaved
    std::int64_t pendingStart = -1;
    bool eof = false;

    ~Impl() {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&dec);
    }

    // The sample number just after what is pending.
    std::int64_t pendingEnd() const {
        return pendingStart + static_cast<std::int64_t>(pending.size()) / channels;
    }

    // Seeks to the packet at or before `sample` and drops what is pending; decodeMore() then learns the
    // position from the first frame's timestamp, so the samples before `sample` are skipped by samples().
    void seek(std::int64_t sample) {
        avcodec_flush_buffers(dec);
        const std::int64_t pts = streamStart(stream) + av_rescale_q(sample, AVRational{1, rate}, stream->time_base);
        av_seek_frame(fmt.get(), stream->index, pts, AVSEEK_FLAG_BACKWARD);
        pending.clear();
        pendingStart = -1;
        eof = false;
    }

    // Decodes one more frame into `pending`; false at the end.
    bool decodeMore() {
        for (;;) {
            // The same send/receive loop as VideoReader::Impl::decodeNext(). Each frame is converted with
            // swresample and appended to `pending`; the first frame after a seek sets pendingStart from its
            // timestamp, later frames follow on.
            const int rc = avcodec_receive_frame(dec, frame);
            if (rc == 0) {
                std::int64_t start = pendingStart >= 0 ? pendingEnd() : 0;
                if (pendingStart < 0 && frame->pts != kNoPts) {
                    start = av_rescale_q(frame->pts - streamStart(stream), stream->time_base, AVRational{1, rate});
                }
                const auto before = pending.size();
                pending.resize(before + static_cast<std::size_t>(frame->nb_samples) * static_cast<std::size_t>(channels));
                std::uint8_t* out[1] = {reinterpret_cast<std::uint8_t*>(pending.data() + before)};
                const int got = swr_convert(swr, out, frame->nb_samples, const_cast<const std::uint8_t**>(frame->extended_data),
                                            frame->nb_samples);
                pending.resize(before + static_cast<std::size_t>(std::max(got, 0)) * static_cast<std::size_t>(channels));
                if (pendingStart < 0) pendingStart = start;
                return true;
            }
            if (rc == AVERROR_EOF) return false;
            if (eof) {
                avcodec_send_packet(dec, nullptr);
                continue;
            }
            for (;;) {
                const int read = av_read_frame(fmt.get(), packet);
                if (read < 0) {
                    eof = true;
                    avcodec_send_packet(dec, nullptr);
                    break;
                }
                if (packet->stream_index == stream->index) {
                    avcodec_send_packet(dec, packet);
                    av_packet_unref(packet);
                    break;
                }
                av_packet_unref(packet);
            }
        }
    }
};

AudioReader::AudioReader() = default;
AudioReader::~AudioReader() = default;

// Opens the stream and a swresample context that changes only the sample format, to interleaved
// 32-bit float, keeping the rate and the channel layout.
bool AudioReader::open(const QString& path, int streamIndex, QString* error) {
    auto d = std::make_unique<Impl>();
    d->fmt = openInput(path, error);
    if (!d->fmt) return false;
    if (streamIndex < 0) streamIndex = firstStream(d->fmt.get(), AVMEDIA_TYPE_AUDIO);
    if (streamIndex < 0 || static_cast<unsigned>(streamIndex) >= d->fmt->nb_streams ||
        d->fmt->streams[streamIndex]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        setError(error, QStringLiteral("no audio stream"));
        return false;
    }
    d->stream = d->fmt->streams[streamIndex];
    d->dec = openDecoder(d->stream, error);
    if (!d->dec) return false;
    d->channels = d->dec->ch_layout.nb_channels;
    d->rate = d->dec->sample_rate;
    if (d->channels <= 0 || d->rate <= 0) {
        setError(error, QStringLiteral("the audio stream has no channels or rate"));
        return false;
    }
    int rc = swr_alloc_set_opts2(&d->swr, &d->dec->ch_layout, AV_SAMPLE_FMT_FLT, d->rate, &d->dec->ch_layout,
                                 d->dec->sample_fmt, d->rate, 0, nullptr);
    if (rc < 0 || swr_init(d->swr) < 0) {
        setError(error, QStringLiteral("cannot convert the audio samples"));
        return false;
    }
    d->info = describe(d->fmt.get(), d->stream);
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    d->pendingStart = 0;
    d_ = std::move(d);
    return true;
}

bool AudioReader::isOpen() const {
    return d_ != nullptr;
}

const StreamInfo& AudioReader::info() const {
    return d_->info;
}

int AudioReader::channels() const {
    return d_ ? d_->channels : 0;
}

int AudioReader::sampleRate() const {
    return d_ ? d_->rate : 0;
}

// Seeks only when `start` is not inside (or just after) what is pending, then decodes until the
// request is covered or the stream ends. What is returned is removed from `pending`.
std::vector<float> AudioReader::samples(std::int64_t start, int count) {
    if (!d_ || count <= 0 || start < 0) return {};
    Impl& d = *d_;
    // Reuse what is decoded when the request starts inside it.
    if (d.pendingStart < 0 || start < d.pendingStart || start > d.pendingEnd()) {
        d.seek(start);
    }
    while (d.pendingStart < 0 || d.pendingEnd() < start + count) {
        if (!d.decodeMore()) break;
        // Drop what lies before the request, keeping memory small.
        if (d.pendingStart >= 0 && d.pendingEnd() <= start) {
            d.pendingStart = d.pendingEnd();
            d.pending.clear();
        }
    }
    if (d.pendingStart < 0 || start >= d.pendingEnd()) return {};
    const auto ch = static_cast<std::size_t>(d.channels);
    const auto from = static_cast<std::size_t>(start - d.pendingStart) * ch;
    const auto available = d.pending.size() - from;
    const auto n = std::min(available, static_cast<std::size_t>(count) * ch);
    std::vector<float> out(d.pending.begin() + static_cast<std::ptrdiff_t>(from),
                           d.pending.begin() + static_cast<std::ptrdiff_t>(from + n));
    // Keep what follows for the next request.
    d.pending.erase(d.pending.begin(), d.pending.begin() + static_cast<std::ptrdiff_t>(from + n));
    d.pendingStart = start + static_cast<std::int64_t>(n / ch);
    return out;
}

// ---------------------------------------------------------------- MediaWriter

namespace {

// The encoder's preferred pixel format: the first it lists (avcodec_get_supported_config() from
// libavcodec 61.13, which deprecates AVCodec::pix_fmts).
AVPixelFormat firstPixelFormat(const AVCodec* codec) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* list = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &list, &n) >= 0 && list && n > 0) {
        return static_cast<const AVPixelFormat*>(list)[0];
    }
    return AV_PIX_FMT_YUV420P;
#else
    return codec->pix_fmts ? codec->pix_fmts[0] : AV_PIX_FMT_YUV420P;
#endif
}

// The pixel format asked for when the encoder takes it, else the one it takes that loses least of it
// (avcodec_find_best_pix_fmt_of_list: depth and chroma first). VideoToolbox's ProRes, for one, takes
// its own 10-bit 4:2:2 layout (p210) rather than yuv422p10le.
AVPixelFormat closestPixelFormat(const AVCodec* codec, AVPixelFormat wanted) {
    const AVPixelFormat* list = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* supported = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &supported, &n) >= 0) {
        list = static_cast<const AVPixelFormat*>(supported);
    }
#else
    list = codec->pix_fmts;
#endif
    if (!list || wanted == AV_PIX_FMT_NONE) return wanted;
    for (const AVPixelFormat* f = list; *f != AV_PIX_FMT_NONE; ++f) {
        if (*f == wanted) return wanted;
    }
    return avcodec_find_best_pix_fmt_of_list(list, wanted, 0, nullptr);
}

// Likewise, the encoder's first sample format.
AVSampleFormat firstSampleFormat(const AVCodec* codec) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    const void* list = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &list, &n) >= 0 && list && n > 0) {
        return static_cast<const AVSampleFormat*>(list)[0];
    }
    return AV_SAMPLE_FMT_S16;
#else
    return codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_S16;
#endif
}

bool encoderOpens(const AVCodec* codec) {
    static std::mutex lock;
    static QHash<QString, bool> known;
    const QString name = QString::fromUtf8(codec->name);
    const std::lock_guard<std::mutex> hold(lock);
    const auto it = known.constFind(name);
    if (it != known.constEnd()) return *it;
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    bool ok = false;
    if (ctx) {
        if (codec->type == AVMEDIA_TYPE_VIDEO) {
            ctx->width = 320;
            ctx->height = 180;
            ctx->pix_fmt = firstPixelFormat(codec);
            ctx->time_base = AVRational{1, 25};
            ctx->framerate = AVRational{25, 1};
            ctx->bit_rate = 1000000;
        } else {
            ctx->sample_rate = 48000;
            av_channel_layout_default(&ctx->ch_layout, 2);
            ctx->sample_fmt = firstSampleFormat(codec);
            ctx->time_base = AVRational{1, 48000};
            ctx->bit_rate = 128000;
        }
        ok = avcodec_open2(ctx, codec, nullptr) >= 0;
        avcodec_free_context(&ctx);
    }
    known.insert(name, ok);
    return ok;
}

} // namespace

// One output file: the muxer and up to one video and one audio encoder. vpts counts frames (the video
// time base is 1/edit rate), apts samples (1/sample rate). Audio is buffered in `abuffer` until it
// fills an encoder frame of aframeSize samples.
struct MediaWriter::Impl {
    QByteArray caption; // setNextCaption()'s pair for the next video frame
    AVFormatContext* out = nullptr;
    AVCodecContext* venc = nullptr;
    AVCodecContext* aenc = nullptr;
    AVStream* vst = nullptr;
    AVStream* ast = nullptr;
    SwsContext* sws = nullptr;
    SwsContext* sws444 = nullptr; // to Y'CbCr 4:4:4, for RGB written exactly
    std::vector<std::uint16_t> yuv444; // its planes
    SwrContext* swr = nullptr;
    AVFrame* vframe = nullptr;
    AVFrame* aframe = nullptr;
    AVPacket* packet = nullptr;
    std::int64_t vpts = 0;
    std::int64_t apts = 0;
    std::vector<float> abuffer; // interleaved, waiting for a full encoder frame
    int aframeSize = 0;
    bool headerWritten = false;
    bool rec709 = false;
    QString error;
    QString path;        // for the OP-Atom relabelling after the trailer
    bool opAtom = false;
    MobId materialPackage;
    MobId filePackage;

    ~Impl() {
        sws_freeContext(sws);
        sws_freeContext(sws444);
        swr_free(&swr);
        av_frame_free(&vframe);
        av_frame_free(&aframe);
        av_packet_free(&packet);
        avcodec_free_context(&venc);
        avcodec_free_context(&aenc);
        if (out) {
            if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
            avformat_free_context(out);
        }
    }

    bool fail(const QString& text) {
        error = text;
        return false;
    }

    // Writes every packet the encoder has ready, its timestamps moved to the stream's time base.
    bool drain(AVCodecContext* enc, AVStream* st) {
        for (;;) {
            const int rc = avcodec_receive_packet(enc, packet);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) return true;
            if (rc < 0) return fail(errorText(rc));
            av_packet_rescale_ts(packet, enc->time_base, st->time_base);
            packet->stream_index = st->index;
            const int w = av_interleaved_write_frame(out, packet);
            if (w < 0) return fail(errorText(w));
        }
    }

    // The video frame, with setNextCaption()'s pair as A/53 caption side data ("cc_data": a valid field 1 triplet).
    bool encodeVideo() {
        av_frame_remove_side_data(vframe, AV_FRAME_DATA_A53_CC);
        if (caption.size() == 2) {
            if (AVFrameSideData* side = av_frame_new_side_data(vframe, AV_FRAME_DATA_A53_CC, 3)) {
                side->data[0] = 0xFC;
                side->data[1] = static_cast<std::uint8_t>(caption.at(0));
                side->data[2] = static_cast<std::uint8_t>(caption.at(1));
            }
            caption.clear();
        }
        return encode(venc, vst, vframe);
    }

    // Sends a frame (null: flush the encoder) and writes what comes out.
    bool encode(AVCodecContext* enc, AVStream* st, AVFrame* f) {
        const int rc = avcodec_send_frame(enc, f);
        if (rc < 0 && rc != AVERROR_EOF) return fail(errorText(rc));
        return drain(enc, st);
    }

    // Converts the first `n` buffered samples to the encoder's sample format, encodes them as one frame
    // and removes them from the buffer.
    bool encodeAudio(int n) {
        av_frame_make_writable(aframe);
        aframe->nb_samples = n;
        const std::uint8_t* in[1] = {reinterpret_cast<const std::uint8_t*>(abuffer.data())};
        const int got = swr_convert(swr, aframe->data, n, in, n);
        if (got < 0) return fail(QStringLiteral("cannot convert the audio samples"));
        aframe->nb_samples = got;
        aframe->pts = apts;
        apts += got;
        abuffer.erase(abuffer.begin(), abuffer.begin() + static_cast<std::ptrdiff_t>(n) * aenc->ch_layout.nb_channels);
        return encode(aenc, ast, aframe);
    }
};

MediaWriter::MediaWriter() : d_(std::make_unique<Impl>()) {}
MediaWriter::~MediaWriter() = default;

QString MediaWriter::lastError() const {
    return d_->error;
}

namespace {

// `bytes` bytes of b from `at` as a big-endian number (MXF's byte order).
std::uint64_t bigEndian(const QByteArray& b, qsizetype at, int bytes) {
    std::uint64_t v = 0;
    for (int i = 0; i < bytes; ++i) v = (v << 8) | static_cast<std::uint8_t>(b.at(at + i));
    return v;
}

// A KLV's BER length at `at`; `at` moves past it. -1 when it is not one.
qint64 berLength(const QByteArray& b, qsizetype& at) {
    if (at >= b.size()) return -1;
    const auto first = static_cast<std::uint8_t>(b.at(at++));
    if (!(first & 0x80)) return first;
    const int bytes = first & 0x7f;
    if (bytes < 1 || bytes > 8 || at + bytes > b.size()) return -1;
    const auto v = bigEndian(b, at, bytes);
    at += bytes;
    return static_cast<qint64>(v);
}

// Two fixes to what FFmpeg's OP-Atom muxer writes, in each partition pack
// and the header metadata after it (the random index pack at the end lists
// the partitions); the essence is not touched.
// * It clip-wraps the essence (one KLV for the whole of it, as Avid's media
//   is) but labels VC-3 (DNx), ProRes, MPEG-2 (XDCAM), DV and BWF essence containers
//   frame-wrapped (SMPTE ST 2019-4, RDD 44 and 382: byte 15, 01 frame, 02 clip;
//   ST 381's MPEG and ST 383's DV: byte 16). Readers that go
//   by the label, such as bmx (ROADMAP Phase 2 gate 2), then cannot read
//   the essence.
// * Its package UMIDs are random in every file: one stem, the material
//   package ending 00 and the file package 01. A clip's track files must
//   share their material package (the master clip's MobID), so the given
//   MobIDs replace them (docs/spec/native-model.md).
bool fixOpAtom(const QString& path, const MobId& materialPackage, const MobId& filePackage) {
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return false;
    // The random index pack is at the end of the file; its last four bytes give its length. Its entries
    // (body SID and byte offset, 12 bytes each) list every partition.
    const qint64 size = f.size();
    if (size < 4 || !f.seek(size - 4)) return false;
    const auto ripLength = static_cast<qint64>(bigEndian(f.read(4), 0, 4));
    if (ripLength < 20 || ripLength > size || !f.seek(size - ripLength)) return false;
    const QByteArray rip = f.read(ripLength);
    static const QByteArray kRipKey = QByteArray::fromHex("060e2b34020501010d01020101110100");
    if (!rip.startsWith(kRipKey)) return false;
    qsizetype at = 16;
    const qint64 entries = berLength(rip, at);
    if (entries < 4) return false;
    static const QByteArray kPrefix = QByteArray::fromHex("060e2b34040101");
    static const QByteArray kMiddle = QByteArray::fromHex("0d01030102");
    QList<qint64> offsets;
    for (qsizetype e = at; e + 12 <= at + entries - 4 && e + 12 <= rip.size(); e += 12) {
        offsets << static_cast<qint64>(bigEndian(rip, e + 4, 8));
    }
    std::sort(offsets.begin(), offsets.end());
    for (qsizetype n = 0; n < offsets.size(); ++n) {
        const qint64 offset = offsets.at(n);
        const qint64 next = n + 1 < offsets.size() ? offsets.at(n + 1) : size - ripLength;
        if (offset < 0 || offset >= size || next <= offset || !f.seek(offset)) return false;
        // The partition pack (key, length, then fixed fields: header byte
        // count at 32), and the header metadata after it. That starts after
        // the pack's fill (to the KLV alignment grid), which the byte count
        // leaves out: a partition with metadata is read to the next one (in
        // FFmpeg's OP-Atom the essence has a partition of its own).
        QByteArray head = f.read(64);
        qsizetype p = 16;
        const qint64 packLength = berLength(head, p);
        if (packLength < 40 || p + 40 > head.size()) return false;
        const auto headerBytes = static_cast<qint64>(bigEndian(head, p + 32, 8));
        const qint64 length = headerBytes > 0 ? next - offset : p + packLength;
        if (length > 64 * 1024 * 1024 || offset + length > size || !f.seek(offset)) return false;
        QByteArray region = f.read(length);
        bool changed = false;
        // FFmpeg's UMIDs: 06 0a 2b 34 01 01 01 05 01 01 0d 00 13, then the
        // instance and material numbers, the last byte the package's kind.
        static const QByteArray kFfmpegUmid = QByteArray::fromHex("060a2b340101010501010d0013");
        if (!materialPackage.isNull() || !filePackage.isNull()) {
            const qsizetype first = region.indexOf(kFfmpegUmid);
            if (headerBytes > 0 && first < 0) return false; // not FFmpeg's layout: say so
            if (first >= 0 && first + 32 <= region.size()) {
                const QByteArray stem = region.mid(first, 31);
                const QByteArray ours[2] = {
                    QByteArray(reinterpret_cast<const char*>(materialPackage.bytes().data()), 32),
                    QByteArray(reinterpret_cast<const char*>(filePackage.bytes().data()), 32)};
                for (int kind = 0; kind < 2; ++kind) {
                    if (ours[kind].count('\0') == 32) continue;
                    const QByteArray theirs = stem + char(kind);
                    for (qsizetype i = region.indexOf(theirs); i >= 0; i = region.indexOf(theirs, i + 32)) {
                        region.replace(i, 32, ours[kind]);
                        changed = true;
                    }
                }
            }
        }
        // The essence container labels: 06 0e 2b 34 04 01 01 .. 0d 01 03 01 02, then 11 (VC-3), 1c (ProRes) or 06 (BWF),
        // mapping byte 01 (frame-wrapped) and 00; the mapping byte becomes 02 (clip-wrapped).
        for (qsizetype i = region.indexOf(kPrefix); i >= 0 && i + 16 <= region.size(); i = region.indexOf(kPrefix, i + 1)) {
            const auto kind = static_cast<std::uint8_t>(region.at(i + 13));
            if (region.mid(i + 8, 5) == kMiddle && (kind == 0x11 || kind == 0x1c || kind == 0x06) && region.at(i + 14) == 0x01 &&
                region.at(i + 15) == 0x00) {
                region[i + 14] = 0x02;
                changed = true;
            }
            // MPEG elementary streams (XDCAM; SMPTE ST 381): 04, the stream id 60-6f, then the wrapping last, 01 frame
            // and 02 clip.
            // DV (SMPTE ST 383): 02, the DV kind, then the wrapping last, 01 frame and 02 clip.
            if (region.mid(i + 8, 5) == kMiddle && kind == 0x02 && region.at(i + 15) == 0x01) {
                region[i + 15] = 0x02;
                changed = true;
            }
            const auto stream = static_cast<std::uint8_t>(region.at(i + 14));
            if (region.mid(i + 8, 5) == kMiddle && kind == 0x04 && (stream & 0xf0) == 0x60 && region.at(i + 15) == 0x01) {
                region[i + 15] = 0x02;
                changed = true;
            }
        }
        if (changed && (!f.seek(offset) || f.write(region) != region.size())) return false;
    }
    return true;
}

} // namespace

// Sets up the muxer and the encoders from the settings and writes the header. Formats that want
// global headers (MOV, MXF) get them from the encoders. The metadata keys are FFmpeg's ("timecode",
// "reel_name", MXF's "material_package_name" and "comment_*"); the muxer keeps what it knows.
bool MediaWriter::open(const QString& path, const WriterSettings& s, QString* error) {
    d_ = std::make_unique<Impl>();
    Impl& d = *d_;
    auto failed = [&](const QString& text) {
        d.error = text;
        setError(error, text);
        return false;
    };
    const QByteArray file = path.toUtf8();
    const QByteArray format = s.format.toUtf8();
    d.path = path;
    d.opAtom = s.format == QLatin1String("mxf_opatom");
    d.materialPackage = s.materialPackage;
    d.filePackage = s.filePackage;
    if (avformat_alloc_output_context2(&d.out, nullptr, format.isEmpty() ? nullptr : format.constData(), file.constData()) < 0 ||
        !d.out) {
        return failed(QStringLiteral("unknown format %1").arg(s.format));
    }
    if (!s.startTimecode.isEmpty()) av_dict_set(&d.out->metadata, "timecode", s.startTimecode.toUtf8().constData(), 0);
    if (!s.reelName.isEmpty()) av_dict_set(&d.out->metadata, "reel_name", s.reelName.toUtf8().constData(), 0);
    for (const auto& [key, value] : s.metadata) {
        av_dict_set(&d.out->metadata, key.toUtf8().constData(), value.toUtf8().constData(), 0);
    }
    const bool globalHeader = (d.out->oformat->flags & AVFMT_GLOBALHEADER) != 0;

    // The video encoder; the profile goes to its private options (DNxHR's "dnxhr_hq", ...).
    if (!s.videoCodec.isEmpty()) {
        const AVCodec* codec = avcodec_find_encoder_by_name(s.videoCodec.toUtf8().constData());
        if (!codec) return failed(QStringLiteral("no encoder %1").arg(s.videoCodec));
        // The operating system's encoders are always allowed; FFmpeg's own only for an allowed codec, judged by the
        // codec as well as the encoder's name, as libx264 is H.264 (docs/spec/release-codecs.md, encoderAllowed).
        if (!encoderAllowed(codec)) return failed(QStringLiteral("%1 is not available in this build").arg(s.videoCodec));
        // Set up and opened with `threads` (0: automatic). An encoder whose per-thread buffers do not fit in
        // the memory there is (DNx on a many-core machine running other jobs: "Cannot allocate memory") is
        // opened again with one thread (BUILDING.md, Troubleshooting).
        auto openEncoder = [&](int threads) {
            if (d.venc) avcodec_free_context(&d.venc);
            d.venc = avcodec_alloc_context3(codec);
            if (!d.venc) return AVERROR(ENOMEM);
            d.venc->width = s.width;
            d.venc->height = s.height;
            d.venc->time_base = inverse(toAv(s.frameRate));
            d.venc->framerate = toAv(s.frameRate);
            d.venc->pix_fmt = s.pixelFormat.isEmpty()
                                  ? firstPixelFormat(codec)
                                  : closestPixelFormat(codec, av_get_pix_fmt(s.pixelFormat.toUtf8().constData()));
            d.venc->sample_aspect_ratio = AVRational{1, 1};
            if (std::abs(s.pixelAspect - 1.0) > 1e-6 && s.pixelAspect > 0) {
                d.venc->sample_aspect_ratio = av_d2q(s.pixelAspect, 1000);
            }
            // Rec. 709 for HD and larger, and for DNx, which declares 709 at any
            // size; Rec. 601 otherwise. The stream says which, so readers agree.
            d.rec709 = s.height > 576 || s.videoCodec == QLatin1String("dnxhd");
            d.venc->colorspace = d.rec709 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
            d.venc->color_primaries = d.rec709 ? AVCOL_PRI_BT709 : AVCOL_PRI_SMPTE170M;
            d.venc->color_trc = d.rec709 ? AVCOL_TRC_BT709 : AVCOL_TRC_SMPTE170M;
            if (!s.colorTransfer.isEmpty()) {
                const int trc = av_color_transfer_from_name(s.colorTransfer.toUtf8().constData());
                if (trc >= 0) d.venc->color_trc = static_cast<AVColorTransferCharacteristic>(trc);
            }
            d.venc->color_range = s.fullRange ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
            if (s.bitRate > 0) d.venc->bit_rate = s.bitRate;
            d.venc->thread_count = threads;
            if (globalHeader) d.venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (!s.videoProfile.isEmpty()) av_opt_set(d.venc->priv_data, "profile", s.videoProfile.toUtf8().constData(), 0);
            // FFmpeg's VideoToolbox encoders ask for the hardware encoder alone unless allow_sw is set; a Mac without a
            // ProRes engine (the M1, M2 and M3 without Pro or Max) then fails to open them ("Generic error in an
            // external library"). Apple's software encoder is the OS's too.
            // DV sites its chroma top-left (IEC 61834, SMPTE 314M) and says so to the encoder, which warns otherwise.
            if (codec->id == AV_CODEC_ID_DVVIDEO) d.venc->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
            if (QByteArray(codec->name).endsWith("_videotoolbox")) av_opt_set(d.venc->priv_data, "allow_sw", "1", 0);
            // On the context and its encoder both: "g", "bf", "maxrate", "minrate" and "bufsize" are the context's,
            // "intra_vlc" or OpenEXR's "format" the encoder's own (set on priv_data alone, the context's were lost:
            // movie export's XDCAM got no GOP or 50 Mb/s rate before 11.1c).
            for (const auto& [key, value] : s.videoOptions) {
                av_opt_set(d.venc, key.toUtf8().constData(), value.toUtf8().constData(), AV_OPT_SEARCH_CHILDREN);
            }
            if (s.interlaced) {
                d.venc->field_order = s.bottomFieldFirst ? AV_FIELD_BB : AV_FIELD_TT;
                d.venc->flags |= AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME;
            }
            if (s.quality > 0) {
                d.venc->flags |= AV_CODEC_FLAG_QSCALE;
                d.venc->global_quality = FF_QP2LAMBDA * s.quality;
            }
            return avcodec_open2(d.venc, codec, nullptr);
        };
        int rc = openEncoder(0);
        if (rc == AVERROR(ENOMEM)) rc = openEncoder(1);
        if (rc < 0) return failed(QStringLiteral("cannot open the %1 encoder: %2").arg(s.videoCodec, errorText(rc)));
        d.vst = avformat_new_stream(d.out, nullptr);
        avcodec_parameters_from_context(d.vst->codecpar, d.venc);
        // The ProRes profile in the stream's parameters, which the MXF muxer labels the essence by (and fails with -1,
        // "Operation not permitted", without): FFmpeg's encoder sets it, VideoToolbox's keeps it in its own option only.
        if (codec->id == AV_CODEC_ID_PRORES && d.vst->codecpar->profile < 0) {
            static const char* const kProres[] = {"proxy", "lt", "standard", "hq", "4444", "xq"};
            for (int i = 0; i < 6; ++i) {
                if (s.videoProfile == QLatin1String(kProres[i])) d.vst->codecpar->profile = i; // AV_PROFILE_PRORES_*
            }
        }
        d.vst->time_base = d.venc->time_base;
        d.vst->avg_frame_rate = d.venc->framerate;
        if (!s.startTimecode.isEmpty()) av_dict_set(&d.vst->metadata, "timecode", s.startTimecode.toUtf8().constData(), 0);
        d.vframe = av_frame_alloc();
        d.vframe->format = d.venc->pix_fmt;
        d.vframe->width = s.width;
        d.vframe->height = s.height;
        if (av_frame_get_buffer(d.vframe, 0) < 0) return failed(QStringLiteral("out of memory"));
        // Interlaced pictures say which field comes first: MPEG-2 writes its top_field_first from the frame, not from the
        // context's field order (which goes into the container).
        if (s.interlaced) {
#ifdef AV_FRAME_FLAG_INTERLACED
            d.vframe->flags |= AV_FRAME_FLAG_INTERLACED;
            if (!s.bottomFieldFirst) d.vframe->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
#else
            d.vframe->interlaced_frame = 1;
            d.vframe->top_field_first = s.bottomFieldFirst ? 0 : 1;
#endif
        }
    }
    // The audio encoder, and a swresample context from interleaved float to its sample format.
    if (!s.audioCodec.isEmpty()) {
        const AVCodec* codec = avcodec_find_encoder_by_name(s.audioCodec.toUtf8().constData());
        if (!codec) return failed(QStringLiteral("no encoder %1").arg(s.audioCodec));
        // As for video: the operating system's AAC (aac_at) is allowed in every build.
        if (!encoderAllowed(codec)) return failed(QStringLiteral("%1 is not available in this build").arg(s.audioCodec));
        d.aenc = avcodec_alloc_context3(codec);
        d.aenc->sample_rate = s.sampleRate;
        av_channel_layout_default(&d.aenc->ch_layout, s.channels);
        d.aenc->sample_fmt = firstSampleFormat(codec);
        d.aenc->time_base = AVRational{1, s.sampleRate};
        if (globalHeader) d.aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const int rc = avcodec_open2(d.aenc, codec, nullptr);
        if (rc < 0) return failed(QStringLiteral("cannot open the %1 encoder: %2").arg(s.audioCodec, errorText(rc)));
        d.ast = avformat_new_stream(d.out, nullptr);
        avcodec_parameters_from_context(d.ast->codecpar, d.aenc);
        d.ast->time_base = d.aenc->time_base;
        // PCM takes any frame size; one frame of video at 25 fps otherwise.
        d.aframeSize = d.aenc->frame_size > 0 ? d.aenc->frame_size : s.sampleRate / 25;
        AVChannelLayout layout{};
        av_channel_layout_default(&layout, s.channels);
        if (swr_alloc_set_opts2(&d.swr, &d.aenc->ch_layout, d.aenc->sample_fmt, s.sampleRate, &layout, AV_SAMPLE_FMT_FLT,
                                s.sampleRate, 0, nullptr) < 0 ||
            swr_init(d.swr) < 0) {
            return failed(QStringLiteral("cannot convert the audio samples"));
        }
        d.aframe = av_frame_alloc();
        d.aframe->format = d.aenc->sample_fmt;
        av_channel_layout_copy(&d.aframe->ch_layout, &d.aenc->ch_layout);
        d.aframe->sample_rate = s.sampleRate;
        d.aframe->nb_samples = d.aframeSize;
        if (av_frame_get_buffer(d.aframe, 0) < 0) return failed(QStringLiteral("out of memory"));
    }
    if (!d.venc && !d.aenc) return failed(QStringLiteral("nothing to write"));
    // The file itself, unless the muxer writes its own; then the header with the muxer's options.
    if (!(d.out->oformat->flags & AVFMT_NOFILE)) {
        const int rc = avio_open(&d.out->pb, file.constData(), AVIO_FLAG_WRITE);
        if (rc < 0) return failed(errorText(rc));
    }
    AVDictionary* options = nullptr;
    for (const auto& [key, value] : s.formatOptions) {
        av_dict_set(&options, key.toUtf8().constData(), value.toUtf8().constData(), 0);
    }
    const int rc = avformat_write_header(d.out, &options);
    av_dict_free(&options);
    if (rc < 0) return failed(QStringLiteral("cannot write the header: %1").arg(errorText(rc)));
    d.headerWritten = true;
    d.packet = av_packet_alloc();
    return true;
}

// Converts any QImage to RGBA and lets swscale scale it to the raster and convert it to the encoder's
// pixel format, with the stream's matrix. Each frame is the next one: pts counts from 0.
void MediaWriter::setNextCaption(const QByteArray& pair) {
    d_->caption = pair.left(2);
}

bool MediaWriter::writeVideo(const QImage& image) {
    Impl& d = *d_;
    if (!d.venc) return d.fail(QStringLiteral("no video stream"));
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    d.sws = sws_getCachedContext(d.sws, rgba.width(), rgba.height(), AV_PIX_FMT_RGBA, d.venc->width, d.venc->height,
                                 d.venc->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
    if (!d.sws) return d.fail(QStringLiteral("cannot convert the picture"));
    const int space = d.rec709 ? SWS_CS_ITU709 : SWS_CS_SMPTE170M;
    // RGB is full range; Y'CbCr legal range.
    const bool fullOut = isRgbFormat(d.venc->pix_fmt) || d.venc->color_range == AVCOL_RANGE_JPEG;
    sws_setColorspaceDetails(d.sws, sws_getCoefficients(SWS_CS_DEFAULT), 1, sws_getCoefficients(space), fullOut ? 1 : 0, 0,
                             1 << 16, 1 << 16);
    av_frame_make_writable(d.vframe);
    const std::uint8_t* src[4] = {rgba.constBits(), nullptr, nullptr, nullptr};
    const int srcStride[4] = {static_cast<int>(rgba.bytesPerLine()), 0, 0, 0};
    sws_scale(d.sws, src, srcStride, 0, rgba.height(), d.vframe->data, d.vframe->linesize);
    d.vframe->colorspace = d.venc->colorspace;
    d.vframe->color_primaries = d.venc->color_primaries;
    d.vframe->color_trc = d.venc->color_trc;
    d.vframe->color_range = d.venc->color_range;
    // A fixed quality is read from each frame.
    if (d.venc->flags & AV_CODEC_FLAG_QSCALE) d.vframe->quality = d.venc->global_quality;
    d.vframe->pts = d.vpts++;
    return d.encodeVideo();
}


// Takes the reader's decoded frame (at full bit depth, not via QImage) and draws it into `target`
// on a black raster: for transcoding, where the picture is fitted into the project's raster. RGB
// output (DNx 444) from Y'CbCr sources is converted here; everything else through swscale.
bool MediaWriter::writeVideoFrom(VideoReader& reader, std::int64_t number, QRect target) {
    Impl& d = *d_;
    if (!d.venc) return d.fail(QStringLiteral("no video stream"));
    if (!reader.d_ || !reader.d_->locate(number)) return false;
    const AVFrame* src = reader.d_->frame;
    const auto format = d.venc->pix_fmt;
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    if (!desc) return d.fail(QStringLiteral("unknown pixel format"));
    if (target.isEmpty()) target = QRect(0, 0, d.venc->width, d.venc->height);
    // Even, inside the raster: chroma is shared by pairs of pixels.
    target = target.intersected(QRect(0, 0, d.venc->width, d.venc->height));
    target.setRect(target.x() & ~1, target.y() & ~1, target.width() & ~1, target.height() & ~1);
    if (target.isEmpty()) return d.fail(QStringLiteral("nothing to draw into"));
    // Black first, so the bars outside the target stay black.
    av_frame_make_writable(d.vframe);
    const bool rgbOut = isRgbFormat(format);
    std::ptrdiff_t lines[4] = {d.vframe->linesize[0], d.vframe->linesize[1], d.vframe->linesize[2],
                               d.vframe->linesize[3]};
    av_image_fill_black(d.vframe->data, lines, format, rgbOut ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG, d.venc->width,
                        d.venc->height);
    // Where the target's corner is in each plane.
    std::uint8_t* dst[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int plane = 0; plane < 4 && d.vframe->data[plane]; ++plane) {
        int step = 1;
        for (int c = 0; c < desc->nb_components; ++c) {
            if (desc->comp[c].plane == plane) step = desc->comp[c].step;
        }
        const bool chroma = !rgbOut && (plane == 1 || plane == 2);
        const int x = chroma ? target.x() >> desc->log2_chroma_w : target.x();
        const int y = chroma ? target.y() >> desc->log2_chroma_h : target.y();
        dst[plane] = d.vframe->data[plane] + static_cast<std::ptrdiff_t>(y) * d.vframe->linesize[plane] +
                     static_cast<std::ptrdiff_t>(x) * step;
    }
    const AVPixFmtDescriptor* srcDesc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(src->format));
    const bool exactRgb = rgbOut && srcDesc && !(srcDesc->flags & AV_PIX_FMT_FLAG_RGB) && !fullRange(src) &&
                          (desc->flags & AV_PIX_FMT_FLAG_PLANAR) && desc->comp[0].depth > 8 && desc->comp[0].depth <= 16;
    if (exactRgb) {
        // Y'CbCr to RGB (DNx 444) done here, the 4:4:4 step and the matrix,
        // the same everywhere: swscale's RGB results differ between FFmpeg
        // versions. It is used only to scale.
        const int w = target.width();
        const int h = target.height();
        const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        d.yuv444.resize(3 * n);
        const bool exact = w == src->width && h == src->height &&
                           planarYuvTo444(src, d.yuv444.data(), d.yuv444.data() + n, d.yuv444.data() + 2 * n, w);
        if (!exact) {
            d.sws444 = sws_getCachedContext(d.sws444, src->width, src->height, static_cast<AVPixelFormat>(src->format), w, h,
                                            AV_PIX_FMT_YUV444P16LE, SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                                            nullptr, nullptr, nullptr);
            if (!d.sws444) return d.fail(QStringLiteral("cannot convert the picture"));
            std::uint8_t* planes[4] = {reinterpret_cast<std::uint8_t*>(d.yuv444.data()),
                                       reinterpret_cast<std::uint8_t*>(d.yuv444.data() + n),
                                       reinterpret_cast<std::uint8_t*>(d.yuv444.data() + 2 * n), nullptr};
            const int strides[4] = {w * 2, w * 2, w * 2, 0};
            sws_scale(d.sws444, src->data, src->linesize, 0, src->height, planes, strides);
        }
        // Rec. 709 or 601, as the encoder declares.
        const bool bt709 = colourSpaceOf(src) == SWS_CS_ITU709;
        const double kr = bt709 ? 0.2126 : 0.299;
        const double kb = bt709 ? 0.0722 : 0.114;
        const double kg = 1.0 - kr - kb;
        const double full = static_cast<double>((1 << desc->comp[0].depth) - 1);
        const int planeOf[3] = {desc->comp[0].plane, desc->comp[1].plane, desc->comp[2].plane}; // R, G, B
        for (int row = 0; row < h; ++row) {
            std::uint16_t* out[3];
            for (int c = 0; c < 3; ++c) {
                out[c] = reinterpret_cast<std::uint16_t*>(dst[planeOf[c]] +
                                                          static_cast<std::ptrdiff_t>(row) * d.vframe->linesize[planeOf[c]]);
            }
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                const double luma = (d.yuv444[i] / 256.0 - 16.0) / 219.0;
                const double pb = (d.yuv444[n + i] / 256.0 - 128.0) / 224.0;
                const double pr = (d.yuv444[2 * n + i] / 256.0 - 128.0) / 224.0;
                const double r = luma + 2.0 * (1.0 - kr) * pr;
                const double b = luma + 2.0 * (1.0 - kb) * pb;
                const double g = (luma - kr * r - kb * b) / kg;
                const double rgb[3] = {r, g, b};
                for (int c = 0; c < 3; ++c) {
                    out[c][x] = static_cast<std::uint16_t>(std::lround(std::clamp(rgb[c], 0.0, 1.0) * full));
                }
            }
        }
    // Otherwise swscale converts and scales straight into the target's corner of each plane.
    } else {
        d.sws = sws_getCachedContext(d.sws, src->width, src->height, static_cast<AVPixelFormat>(src->format),
                                     target.width(), target.height(), format,
                                     SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT, nullptr, nullptr, nullptr);
        if (!d.sws) return d.fail(QStringLiteral("cannot convert the picture"));
        sws_setColorspaceDetails(d.sws, sws_getCoefficients(colourSpaceOf(src)), fullRange(src) ? 1 : 0,
                                 sws_getCoefficients(d.rec709 ? SWS_CS_ITU709 : SWS_CS_SMPTE170M), rgbOut ? 1 : 0, 0,
                                 1 << 16, 1 << 16);
        sws_scale(d.sws, src->data, src->linesize, 0, src->height, dst, d.vframe->linesize);
    }
    d.vframe->colorspace = d.venc->colorspace;
    d.vframe->color_primaries = d.venc->color_primaries;
    d.vframe->color_trc = d.venc->color_trc;
    d.vframe->color_range = rgbOut ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    d.vframe->pts = d.vpts++;
    return d.encodeVideo();
}

// Buffers the samples and encodes every full encoder frame; the rest waits for more or for finish().
bool MediaWriter::writeAudio(const float* samples, int count) {
    Impl& d = *d_;
    if (!d.aenc) return d.fail(QStringLiteral("no audio stream"));
    const int ch = d.aenc->ch_layout.nb_channels;
    d.abuffer.insert(d.abuffer.end(), samples, samples + static_cast<std::ptrdiff_t>(count) * ch);
    while (static_cast<int>(d.abuffer.size() / static_cast<std::size_t>(ch)) >= d.aframeSize) {
        if (!d.encodeAudio(d.aframeSize)) return false;
    }
    return true;
}

// Encodes what is buffered, flushes both encoders, writes the trailer and closes the file. For
// mxf_opatom it then rewrites the labels and package IDs in place (fixOpAtom()), which needs the file
// complete and closed. False with the reason in *error and lastError().
bool MediaWriter::finish(QString* error) {
    Impl& d = *d_;
    bool ok = d.headerWritten;
    if (ok && d.aenc) {
        const int left = static_cast<int>(d.abuffer.size() / static_cast<std::size_t>(d.aenc->ch_layout.nb_channels));
        if (left > 0) {
            // A codec with a fixed frame size gets a last, shorter frame
            // only if it allows one; PCM always does.
            ok = d.encodeAudio(left);
        }
        ok = ok && d.encode(d.aenc, d.ast, nullptr);
    }
    if (ok && d.venc) ok = d.encode(d.venc, d.vst, nullptr);
    if (d.headerWritten) {
        const int rc = av_write_trailer(d.out);
        if (rc < 0 && ok) ok = d.fail(errorText(rc));
        d.headerWritten = false;
    }
    if (!ok) setError(error, d.error);
    // Close the file now.
    if (d.out && !(d.out->oformat->flags & AVFMT_NOFILE)) avio_closep(&d.out->pb);
    if (ok && d.opAtom && !fixOpAtom(d.path, d.materialPackage, d.filePackage)) {
        ok = d.fail(QStringLiteral("cannot finish the MXF's labels and package IDs"));
        setError(error, d.error);
    }
    return ok;
}

// ---------------------------------------------------------------- Resampler

// A swresample context between two rates, interleaved float on both sides.
struct Resampler::Impl {
    SwrContext* swr = nullptr;
    int channels = 0;
    int from = 0;
    int to = 0;
    ~Impl() { swr_free(&swr); }

    // As many samples out as are ready; a null `samples` drains what swresample holds.
    std::vector<float> convert(const float* samples, int count) {
        std::vector<float> out;
        if (!swr) return out;
        const int room = swr_get_out_samples(swr, count);
        if (room <= 0) return out;
        out.resize(static_cast<std::size_t>(room) * static_cast<std::size_t>(channels));
        std::uint8_t* o[1] = {reinterpret_cast<std::uint8_t*>(out.data())};
        const std::uint8_t* i[1] = {reinterpret_cast<const std::uint8_t*>(samples)};
        const int got = swr_convert(swr, o, room, samples ? i : nullptr, samples ? count : 0);
        out.resize(static_cast<std::size_t>(std::max(got, 0)) * static_cast<std::size_t>(channels));
        return out;
    }
};

Resampler::Resampler(int channels, int fromRate, int toRate, int filterSize) : d_(std::make_unique<Impl>()) {
    d_->channels = channels;
    d_->from = fromRate;
    d_->to = toRate;
    if (channels <= 0 || fromRate <= 0 || toRate <= 0) return;
    AVChannelLayout layout{};
    av_channel_layout_default(&layout, channels);
    if (swr_alloc_set_opts2(&d_->swr, &layout, AV_SAMPLE_FMT_FLT, toRate, &layout, AV_SAMPLE_FMT_FLT, fromRate, 0,
                            nullptr) < 0 ||
        av_opt_set_int(d_->swr, "filter_size", std::clamp(filterSize, 4, 256), 0) < 0 || swr_init(d_->swr) < 0) {
        swr_free(&d_->swr);
    }
    av_channel_layout_uninit(&layout);
}

Resampler::~Resampler() = default;

bool Resampler::isValid() const {
    return d_->swr != nullptr;
}

std::vector<float> Resampler::push(const float* samples, int count) {
    if (count <= 0) return {};
    return d_->convert(samples, count);
}

std::vector<float> Resampler::flush() {
    return d_->convert(nullptr, 0);
}

// ---------------------------------------------------------------- copyVideoFrames

// Remuxes packets: the input's codec parameters are copied to the output stream and each packet of
// the stream from the `first`-th on is written with new timestamps, one frame apart from 0. That only
// gives exact frames for intra-only codecs; the caller (consolidateClip(), managed.cpp) checks. For
// mxf_opatom the closed file is then fixed as MediaWriter's is (fixOpAtom()), with the settings' package IDs.
bool copyVideoFrames(const QString& from, int stream, std::int64_t first, std::int64_t count, const QString& to,
                     const WriterSettings& settings, QString* error) {
    Input in = openInput(from, error);
    if (!in) return false;
    if (stream < 0) stream = av_find_best_stream(in.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0 || stream >= static_cast<int>(in->nb_streams)) {
        setError(error, QStringLiteral("%1 has no picture").arg(from));
        return false;
    }
    const AVStream* ist = in->streams[stream];
    AVFormatContext* out = nullptr;
    const QByteArray file = to.toUtf8();
    const QByteArray format = settings.format.toUtf8();
    if (avformat_alloc_output_context2(&out, nullptr, format.isEmpty() ? nullptr : format.constData(), file.constData()) <
            0 ||
        !out) {
        setError(error, QStringLiteral("unknown format %1").arg(settings.format));
        return false;
    }
    struct Closer {
        AVFormatContext* out;
        ~Closer() {
            if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
            avformat_free_context(out);
        }
    } closer{out};
    auto failed = [&](const QString& text) {
        setError(error, text);
        return false;
    };
    if (!settings.startTimecode.isEmpty()) {
        av_dict_set(&out->metadata, "timecode", settings.startTimecode.toUtf8().constData(), 0);
    }
    if (!settings.reelName.isEmpty()) av_dict_set(&out->metadata, "reel_name", settings.reelName.toUtf8().constData(), 0);
    for (const auto& [key, value] : settings.metadata) {
        av_dict_set(&out->metadata, key.toUtf8().constData(), value.toUtf8().constData(), 0);
    }
    AVStream* ost = avformat_new_stream(out, nullptr);
    if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0) return failed(QStringLiteral("out of memory"));
    // Let the muxer pick its own tag for the codec: the input container's may not fit.
    ost->codecpar->codec_tag = 0;
    const AVRational rate = settings.sourceRate && settings.sourceRate->num() > 0
                                ? AVRational{static_cast<int>(settings.sourceRate->num()), static_cast<int>(settings.sourceRate->den())}
                                : videoRate(ist);
    ost->time_base = inverse(rate);
    ost->avg_frame_rate = rate;
    if (!(out->oformat->flags & AVFMT_NOFILE)) {
        const int rc = avio_open(&out->pb, file.constData(), AVIO_FLAG_WRITE);
        if (rc < 0) return failed(errorText(rc));
    }
    AVDictionary* options = nullptr;
    for (const auto& [key, value] : settings.formatOptions) {
        av_dict_set(&options, key.toUtf8().constData(), value.toUtf8().constData(), 0);
    }
    int rc = avformat_write_header(out, &options);
    av_dict_free(&options);
    if (rc < 0) return failed(QStringLiteral("cannot write the header: %1").arg(errorText(rc)));
    // Packets in order, counted: each one is a frame.
    AVPacket* packet = av_packet_alloc();
    std::int64_t index = 0;
    std::int64_t written = 0;
    bool ok = true;
    while (written < count) {
        rc = av_read_frame(in.get(), packet);
        if (rc < 0) break;
        if (packet->stream_index == stream) {
            if (index >= first) {
                packet->stream_index = ost->index;
                packet->pts = packet->dts = written;
                packet->duration = 1;
                packet->pos = -1;
                av_packet_rescale_ts(packet, AVRational{rate.den, rate.num}, ost->time_base);
                if (av_interleaved_write_frame(out, packet) < 0) {
                    ok = false;
                    break;
                }
                ++written;
            }
            ++index;
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    rc = av_write_trailer(out);
    if (!ok || rc < 0) return failed(QStringLiteral("cannot write %1").arg(to));
    if (written < count) return failed(QStringLiteral("%1 ends before frame %2").arg(from).arg(first + count));
    // As MediaWriter::finish(): OP-Atom gets its labels and the given package IDs once the file is closed.
    if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
    if (settings.format == QLatin1String("mxf_opatom") && !fixOpAtom(to, settings.materialPackage, settings.filePackage)) {
        return failed(QStringLiteral("cannot finish the MXF's labels and package IDs"));
    }
    return true;
}

// ---------------------------------------------------------------- codec provider

namespace {

// The codec names of ext::CodecQuery mapped to FFmpeg's.
QString ffmpegName(const std::string& name) {
    const QString n = QString::fromStdString(name).toLower();
    if (n == QLatin1String("dnxhr")) return QStringLiteral("dnxhd");
    if (n == QLatin1String("avc-intra") || n == QLatin1String("xavc")) return QStringLiteral("h264");
    if (n.startsWith(QLatin1String("xdcam"))) return QStringLiteral("mpeg2video");
    return n;
}

// An ext::Decoder over an FFmpeg decoder, for modules that hand us packets (docs/EXTENSIONS.md).
// Decoded frames are copied out plane by plane: video planes at their stride, audio one plane per
// channel when planar, else one.
class FFmpegDecoder : public ext::Decoder {
public:
    explicit FFmpegDecoder(AVCodecContext* ctx) : ctx_(ctx), frame_(av_frame_alloc()), packet_(av_packet_alloc()) {}
    ~FFmpegDecoder() override {
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&ctx_);
    }
    ext::CodecStatus send(const ext::Packet& p) override {
        av_packet_unref(packet_);
        if (av_new_packet(packet_, static_cast<int>(p.data.size())) < 0) return ext::CodecStatus::Error;
        std::memcpy(packet_->data, p.data.data(), p.data.size());
        packet_->pts = p.pts;
        if (p.keyframe) packet_->flags |= AV_PKT_FLAG_KEY;
        const int rc = avcodec_send_packet(ctx_, packet_);
        if (rc == AVERROR(EAGAIN)) return ext::CodecStatus::NeedMoreInput;
        return rc < 0 ? ext::CodecStatus::Error : ext::CodecStatus::Ok;
    }
    ext::CodecStatus receive(ext::DecodedFrame& out) override {
        const int rc = avcodec_receive_frame(ctx_, frame_);
        if (rc == AVERROR(EAGAIN)) return ext::CodecStatus::NeedMoreInput;
        if (rc == AVERROR_EOF) return ext::CodecStatus::EndOfStream;
        if (rc < 0) return ext::CodecStatus::Error;
        out.pts = frame_->pts;
        out.planes.clear();
        if (ctx_->codec_type == AVMEDIA_TYPE_VIDEO) {
            out.kind = ext::MediaKind::Video;
            out.width = frame_->width;
            out.height = frame_->height;
            const auto pf = static_cast<AVPixelFormat>(frame_->format);
            out.pixelFormat = av_get_pix_fmt_name(pf);
            const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pf);
            const int planes = av_pix_fmt_count_planes(pf);
            for (int i = 0; i < planes; ++i) {
                const int h = (i == 1 || i == 2) && desc ? AV_CEIL_RSHIFT(frame_->height, desc->log2_chroma_h) : frame_->height;
                ext::FramePlane plane;
                plane.stride = frame_->linesize[i];
                plane.data.assign(frame_->data[i], frame_->data[i] + static_cast<std::ptrdiff_t>(plane.stride) * h);
                out.planes.push_back(std::move(plane));
            }
        } else {
            out.kind = ext::MediaKind::Audio;
            out.sampleCount = frame_->nb_samples;
            out.pixelFormat = av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame_->format));
            const int size = av_samples_get_buffer_size(nullptr, frame_->ch_layout.nb_channels, frame_->nb_samples,
                                                        static_cast<AVSampleFormat>(frame_->format), 1);
            const bool planar = av_sample_fmt_is_planar(static_cast<AVSampleFormat>(frame_->format)) != 0;
            const int count = planar ? frame_->ch_layout.nb_channels : 1;
            for (int i = 0; i < count; ++i) {
                ext::FramePlane plane;
                plane.stride = size / count;
                plane.data.assign(frame_->extended_data[i], frame_->extended_data[i] + plane.stride);
                out.planes.push_back(std::move(plane));
            }
        }
        av_frame_unref(frame_);
        return ext::CodecStatus::Ok;
    }
    ext::CodecStatus flush() override {
        avcodec_send_packet(ctx_, nullptr);
        return ext::CodecStatus::Ok;
    }

private:
    AVCodecContext* ctx_;
    AVFrame* frame_;
    AVPacket* packet_;
};

// FFmpeg as a codec provider in the registry (src/ext). It decodes whatever FFmpeg and the codec
// policy allow; it does not encode yet.
class FFmpegProvider : public ext::CodecProvider {
public:
    ext::ProviderInfo info() const override {
        ext::ProviderInfo i;
        i.id = "org.ffmpeg.libavcodec";
        i.displayName = "FFmpeg";
        i.category = ext::Category::VideoCodec;
        i.tier = ext::LicenseTier::OpenSource;
        i.vendor = "FFmpeg";
        i.version = av_version_info();
        return i;
    }
    bool canDecode(const ext::CodecQuery& q) const override {
        const QString name = ffmpegName(q.codec);
        return avcodec_find_decoder_by_name(name.toUtf8().constData()) != nullptr && codecAllowed(name);
    }
    bool canEncode(const ext::CodecQuery& q) const override {
        const QString name = ffmpegName(q.codec);
        return avcodec_find_encoder_by_name(name.toUtf8().constData()) != nullptr && codecAllowed(name);
    }
    std::unique_ptr<ext::Decoder> createDecoder(const ext::CodecQuery& q) override {
        if (!canDecode(q)) return nullptr;
        const AVCodec* codec = avcodec_find_decoder_by_name(ffmpegName(q.codec).toUtf8().constData());
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        ctx->width = q.width;
        ctx->height = q.height;
        if (q.editRate.num() > 0) ctx->pkt_timebase = inverse(toAv(q.editRate));
        ctx->thread_count = 0;
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            return nullptr;
        }
        return std::make_unique<FFmpegDecoder>(ctx);
    }
    std::unique_ptr<ext::Encoder> createEncoder(const ext::CodecQuery&) override {
        // Files are written through MediaWriter; a packet-level encoder
        // interface comes when a module needs one.
        return nullptr;
    }
};

} // namespace

void registerProviders(ext::ProviderRegistry& registry) {
    registry.add(std::make_shared<FFmpegProvider>());
    // CLAP audio plug-ins (docs/spec/clap-host.md); the plug-ins themselves are scanned at start-up.
    registry.add(std::make_shared<ClapHostProvider>());
    // OFX image effect plug-ins (docs/spec/ofx-host.md); scanned at start-up too.
    registry.add(std::make_shared<OfxHostProvider>());
    // Export to Device > XDCAM onto disks seen as folders (docs/spec/export-device.md).
    registry.add(std::make_shared<XdcamDiskProvider>());
    // The Digital Cut tool's tapes kept in folders (docs/spec/digital-cut.md).
    registry.add(std::make_shared<FolderTapeDeckProvider>());
    // Speech to text for transcripts through whisper.cpp's whisper-cli (docs/spec/transcripts.md).
    registry.add(std::make_shared<WhisperProvider>());
    // Audio CD tracks the OS shows only as ".cda" files (docs/spec/audio-cd.md).
    registerAudioCdProviders(registry);
}

bool pixelFormatHasAlpha(const QString& pixelFormat) {
    const AVPixelFormat pf = av_get_pix_fmt(pixelFormat.toUtf8().constData());
    if (pf == AV_PIX_FMT_NONE) return false;
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pf);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
}

} // namespace lumenis::media

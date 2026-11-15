// Movie export (7.1a; lumenis/media/movie_export.h, docs/spec/movie-export.md): the compression table, fitting
// frames to the output raster, and the writing loop over MediaWriter. Frames and sound are interleaved one frame at a
// time, the sound resampled to 44.1 kHz when asked.
#include "lumenis/media/movie_export.h"

#include "lumenis/media/media.h"
#include "lumenis/media/pict.h"
#include "lumenis/media/playback.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPainter>

#include <algorithm>
#include <cstring>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace lumenis::media {

const QList<MovieCompression>& movieCompressions() {
    // Ours: the guide names the menus (Codec Family, Compression, Color Depth, p.895) and HEVC's Main and Main 10
    // (p.899), not the entries. DNxHR and ProRes go in MOV only; H.264, HEVC and AV1 in both.
    static const QList<MovieCompression> all{[] {
    QList<MovieCompression> list{
        {QStringLiteral("dnxhr_lb"), QStringLiteral("Avid DNxHR"), QStringLiteral("Avid DNxHR LB"), QStringLiteral("dnxhd"),
         QStringLiteral("dnxhr_lb"), QStringLiteral("yuv422p"), 8},
        {QStringLiteral("dnxhr_sq"), QStringLiteral("Avid DNxHR"), QStringLiteral("Avid DNxHR SQ"), QStringLiteral("dnxhd"),
         QStringLiteral("dnxhr_sq"), QStringLiteral("yuv422p"), 8},
        {QStringLiteral("dnxhr_hq"), QStringLiteral("Avid DNxHR"), QStringLiteral("Avid DNxHR HQ"), QStringLiteral("dnxhd"),
         QStringLiteral("dnxhr_hq"), QStringLiteral("yuv422p"), 8},
        {QStringLiteral("dnxhr_hqx"), QStringLiteral("Avid DNxHR"), QStringLiteral("Avid DNxHR HQX"), QStringLiteral("dnxhd"),
         QStringLiteral("dnxhr_hqx"), QStringLiteral("yuv422p10le"), 10},
        {QStringLiteral("dnxhr_444"), QStringLiteral("Avid DNxHR"), QStringLiteral("Avid DNxHR 444"), QStringLiteral("dnxhd"),
         QStringLiteral("dnxhr_444"), QStringLiteral("yuv444p10le"), 10},
        {QStringLiteral("prores_proxy"), QStringLiteral("Apple ProRes"), QStringLiteral("Apple ProRes 422 Proxy"),
         QStringLiteral("prores_ks"), QStringLiteral("proxy"), QStringLiteral("yuv422p10le"), 10},
        {QStringLiteral("prores_lt"), QStringLiteral("Apple ProRes"), QStringLiteral("Apple ProRes 422 LT"), QStringLiteral("prores_ks"),
         QStringLiteral("lt"), QStringLiteral("yuv422p10le"), 10},
        {QStringLiteral("prores_422"), QStringLiteral("Apple ProRes"), QStringLiteral("Apple ProRes 422"), QStringLiteral("prores_ks"),
         QStringLiteral("standard"), QStringLiteral("yuv422p10le"), 10},
        {QStringLiteral("prores_hq"), QStringLiteral("Apple ProRes"), QStringLiteral("Apple ProRes 422 HQ"), QStringLiteral("prores_ks"),
         QStringLiteral("hq"), QStringLiteral("yuv422p10le"), 10},
        {QStringLiteral("prores_4444"), QStringLiteral("Apple ProRes"), QStringLiteral("Apple ProRes 4444"), QStringLiteral("prores_ks"),
         QStringLiteral("4444"), QStringLiteral("yuv444p10le"), 10},
        {QStringLiteral("h264"), QStringLiteral("H.264"), QStringLiteral("H.264"), QStringLiteral("libx264"), QString(),
         QStringLiteral("yuv420p"), 8, true, true, true},
        {QStringLiteral("hevc_main"), QStringLiteral("HEVC"), QStringLiteral("H.265/HEVC Main Profile"), QStringLiteral("libx265"),
         QString(), QStringLiteral("yuv420p"), 8, true, true, true},
        {QStringLiteral("hevc_main10"), QStringLiteral("HEVC"), QStringLiteral("H.265/HEVC Main 10 Profile"), QStringLiteral("libx265"),
         QString(), QStringLiteral("yuv420p10le"), 10, true, true, true},
        {QStringLiteral("av1"), QStringLiteral("AV1"), QStringLiteral("AV1"), QStringLiteral("libsvtav1"), QString(),
         QStringLiteral("yuv420p"), 8, true, true, true},
        {QStringLiteral("mjpeg"), QStringLiteral("Motion JPEG"), QStringLiteral("Motion JPEG"), QStringLiteral("mjpeg"), QString(),
         QStringLiteral("yuv422p"), 8},
        {QStringLiteral("png"), QStringLiteral("PNG"), QStringLiteral("PNG"), QStringLiteral("png"), QString(), QStringLiteral("rgb24"), 8},
        {QStringLiteral("uncompressed_8"), QStringLiteral("Uncompressed"), QStringLiteral("Uncompressed 8-bit 4:2:2"),
         QStringLiteral("rawvideo"), QString(), QStringLiteral("uyvy422"), 8},
        {QStringLiteral("uncompressed_10"), QStringLiteral("Uncompressed"), QStringLiteral("Uncompressed 10-bit 4:2:2"),
         QStringLiteral("v210"), QString(), QStringLiteral("yuv422p10le"), 10},
    };
    // DNxHR and ProRes go in MXF OP1a too ("Avid DNx as MXF OP1a wrapped media", p.888; ProRes "in OP-Atom, MXF
    // OP1a, and MOV containers", p.904).
    for (MovieCompression& c : list) c.op1a = c.family == QLatin1String("Avid DNxHR") || c.family == QLatin1String("Apple ProRes");
    auto mxfOnly = [&](const char* id, const char* family, const char* label, const char* encoder, const char* pixelFormat, int depth,
                       int mbps, QSize raster, QList<QPair<QString, QString>> options, bool xdcam, bool legacy) {
        MovieCompression c{QString::fromLatin1(id), QString::fromLatin1(family), QString::fromLatin1(label), QString::fromLatin1(encoder),
                           QString(), QString::fromLatin1(pixelFormat), depth, false, false, false};
        c.op1a = !legacy;
        c.xdcam = xdcam;
        c.legacy = legacy;
        c.fixedMbps = mbps;
        c.raster = raster;
        c.options = std::move(options);
        list << c;
    };
    // XDCAM HD (p.884: "For HD projects, select XDCAM-50, XDCAM-35, XDCAM-25, or XDCAM-17"): MPEG-2 long GOP, the
    // 50 Mb/s one 4:2:2 at constant rate, the others 4:2:0, XDCAM-25 and -17 at 1440 x 1080 (Sony's formats; ours: a
    // GOP of 12 with two B frames for each).
    auto gop = [](const char* mbps, bool constant) {
        QList<QPair<QString, QString>> o{{QStringLiteral("g"), QStringLiteral("12")}, {QStringLiteral("bf"), QStringLiteral("2")},
                                         {QStringLiteral("intra_vlc"), QStringLiteral("1")},
                                         {QStringLiteral("maxrate"), QString::fromLatin1(mbps) + QLatin1Char('M')},
                                         {QStringLiteral("bufsize"), QStringLiteral("17825792")}};
        if (constant) o << QPair<QString, QString>{QStringLiteral("minrate"), QString::fromLatin1(mbps) + QLatin1Char('M')};
        return o;
    };
    mxfOnly("xdcam_50", "XDCAM HD", "XDCAM-50", "mpeg2video", "yuv422p", 8, 50, {}, gop("50", true), true, false);
    mxfOnly("xdcam_35", "XDCAM HD", "XDCAM-35", "mpeg2video", "yuv420p", 8, 35, {}, gop("35", false), true, false);
    mxfOnly("xdcam_25", "XDCAM HD", "XDCAM-25", "mpeg2video", "yuv420p", 8, 25, QSize(1440, 1080), gop("25", true), true, false);
    mxfOnly("xdcam_17", "XDCAM HD", "XDCAM-17", "mpeg2video", "yuv420p", 8, 18, QSize(1440, 1080), gop("18", false), true, false);
    // Panasonic AVC Long-GOP G6, G12, G25 and G50 (p.887-888): H.264 at 6, 12, 25 and 50 Mb/s (ours: G6 and G12 4:2:0
    // 8-bit, G25 and G50 4:2:2 10-bit, as Panasonic's cameras record them; a GOP of 12).
    const QList<QPair<QString, QString>> avc{{QStringLiteral("g"), QStringLiteral("12")}};
    mxfOnly("avc_g6", "AVC Long-GOP", "AVC Long-GOP G6", "libx264", "yuv420p", 8, 6, {}, avc, false, false);
    mxfOnly("avc_g12", "AVC Long-GOP", "AVC Long-GOP G12", "libx264", "yuv420p", 8, 12, {}, avc, false, false);
    mxfOnly("avc_g25", "AVC Long-GOP", "AVC Long-GOP G25", "libx264", "yuv422p10le", 10, 25, {}, avc, false, false);
    mxfOnly("avc_g50", "AVC Long-GOP", "AVC Long-GOP G50", "libx264", "yuv422p10le", 10, 50, {}, avc, false, false);
    // JPEG 2000, FFmpeg's own encoder, in MXF OP1a (legacy) (p.1174).
    mxfOnly("j2k", "JPEG 2000", "JPEG 2000", "jpeg2000", "yuv422p10le", 10, 0, {}, {}, false, true);
    return list;
}()};
    return all;
}

const MovieCompression* findMovieCompression(const QString& id) {
    for (const MovieCompression& c : movieCompressions()) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

QString movieEncoder(const MovieCompression& c) {
    if (encoderAvailable(c.encoder)) return c.encoder;
    // The codec behind the encoder ("libx264" is H.264), by FFmpeg's id, or by the family when FFmpeg lacks it.
    const AVCodec* own = avcodec_find_encoder_by_name(c.encoder.toUtf8().constData());
    QString codec = own ? QString::fromUtf8(avcodec_get_name(own->id)) : QString();
    if (codec.isEmpty()) {
        codec = c.family == QLatin1String("H.264") ? QStringLiteral("h264")
                : c.family == QLatin1String("HEVC") ? QStringLiteral("hevc")
                : c.family == QLatin1String("Apple ProRes") ? QStringLiteral("prores")
                                                             : QString();
    }
    return codec.isEmpty() ? QString() : osEncoderFor(codec);
}

bool movieCompressionAvailable(const MovieCompression& c) {
    return !movieEncoder(c).isEmpty();
}

bool movieCompressionFits(const MovieCompression& c, MovieContainer container) {
    switch (container) {
    case MovieContainer::Mov: return c.mov;
    case MovieContainer::Mp4: return c.mp4;
    case MovieContainer::Mxf: return c.op1a;
    case MovieContainer::Xdcam: return c.xdcam;
    case MovieContainer::MxfLegacy: return c.legacy;
    }
    return false;
}

QImage fitMoviePicture(const QImage& picture, QSize out, MovieScaling scaling) {
    if (picture.isNull() || out.isEmpty()) return picture;
    if (picture.size() == out) return picture;
    QImage frame(out, QImage::Format_RGB32);
    frame.fill(Qt::black);
    QPainter p(&frame);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QSizeF size = picture.size();
    switch (scaling) {
    case MovieScaling::Stretch: size = out; break;
    case MovieScaling::Letterbox: size.scale(out, Qt::KeepAspectRatio); break;
    case MovieScaling::CenterCrop: size.scale(out, Qt::KeepAspectRatioByExpanding); break;
    case MovieScaling::CenterKeepSize: break;
    }
    const QRectF target((out.width() - size.width()) / 2.0, (out.height() - size.height()) / 2.0, size.width(), size.height());
    p.drawImage(target, picture);
    p.end();
    return frame;
}

namespace {

// The sample that starts frame `frame` at 48 kHz.
std::int64_t sampleOf(std::int64_t frame, const Rational& rate) {
    return framesToSamples(frame, rate);
}

// Interleaves out[channel] into one buffer of `channels` channels.
std::vector<float> interleave(const std::vector<std::vector<float>>& planes, int channels, int count) {
    std::vector<float> out(static_cast<std::size_t>(channels) * static_cast<std::size_t>(std::max(count, 0)), 0.0f);
    for (int c = 0; c < channels && c < static_cast<int>(planes.size()); ++c) {
        const std::vector<float>& plane = planes[static_cast<std::size_t>(c)];
        const int n = std::min(count, static_cast<int>(plane.size()));
        for (int i = 0; i < n; ++i) out[static_cast<std::size_t>(i * channels + c)] = plane[static_cast<std::size_t>(i)];
    }
    return out;
}

} // namespace

bool exportMovie(const QString& path, const MovieSource& source, const MovieExportOptions& o,
                 const std::function<bool(double)>& progress, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        QFile::remove(path);
        return false;
    };
    const bool video = o.video && source.picture;
    const bool audio = o.audio && source.sound && o.audioChannels > 0;
    if ((!video && !audio) || source.frames <= 0) return fail(QObject::tr("There is nothing to export."));

    WriterSettings w;
    const bool mxf = o.container == MovieContainer::Mxf || o.container == MovieContainer::Xdcam || o.container == MovieContainer::MxfLegacy;
    w.format = mxf ? QStringLiteral("mxf") : o.container == MovieContainer::Mov ? QStringLiteral("mov") : QStringLiteral("mp4");
    w.frameRate = source.frameRate;
    w.startTimecode = o.startTimecode;
    if (video) {
        const MovieCompression* c = findMovieCompression(o.compression);
        if (!c) return fail(QObject::tr("Unknown compression %1.").arg(o.compression));
        if (!movieCompressionFits(*c, o.container)) {
            if (mxf) return fail(QObject::tr("%1 cannot go in this kind of MXF file.").arg(c->label));
            return fail(QObject::tr("%1 cannot go in an MP4 file; choose MOV, or H.264, HEVC or AV1.").arg(c->label));
        }
        if (!movieCompressionAvailable(*c)) return fail(QObject::tr("%1 is not available in this build.").arg(c->label));
        QSize size = o.size.isValid() && !o.size.isEmpty() ? o.size : source.raster;
        // A resolution with a raster of its own (XDCAM-25 and -17) has it, the picture squeezed into it.
        if (c->raster.isValid() && !c->raster.isEmpty()) size = c->raster;
        if (size.isEmpty()) return fail(QObject::tr("The picture has no size."));
        // Chroma subsampled formats need even sizes (ours: rounded up).
        size = QSize(size.width() + size.width() % 2, size.height() + size.height() % 2);
        w.videoCodec = movieEncoder(*c);
        w.videoProfile = c->profile;
        w.pixelFormat = c->pixelFormat;
        w.width = size.width();
        w.height = size.height();
        // JPEG is full range whatever the Color Levels.
        w.fullRange = o.fullRange || c->encoder == QLatin1String("mjpeg");
        if (c->bitRate && o.bitRateMbps > 0) w.bitRate = static_cast<std::int64_t>(o.bitRateMbps) * 1000000;
        if (c->fixedMbps > 0) w.bitRate = static_cast<std::int64_t>(c->fixedMbps) * 1000000;
        w.videoOptions << c->options;
    }
    // MXF's sound is 48 kHz (ours: as our managed MXF media, docs/spec/audio-settings.md).
    int rate = o.sampleRate == 44100 && !mxf ? 44100 : 48000;
    if (audio) {
        w.channels = o.audioChannels;
        if (o.container == MovieContainer::Mp4 || (o.aac && !mxf)) {
            if (codecAllowed(QStringLiteral("aac"))) {
                w.audioCodec = QStringLiteral("aac");
            } else if (const QString os = osEncoderFor(QStringLiteral("aac")); !os.isEmpty()) {
                // The operating system's AAC (AudioToolbox, Media Foundation; docs/spec/release-codecs.md).
                w.audioCodec = os;
            } else if (avcodec_find_encoder_by_name("libopus")) {
                // Without the codecs awaiting legal review an MP4 carries Opus (ours), which is 48 kHz only.
                w.audioCodec = QStringLiteral("libopus");
                rate = 48000;
            } else {
                return fail(QObject::tr("AAC is not available in this build."));
            }
        } else {
            w.audioCodec = o.bitDepth == 16 ? QStringLiteral("pcm_s16le") : QStringLiteral("pcm_s24le");
        }
        w.sampleRate = rate;
    }

    MediaWriter writer;
    QString why;
    if (!writer.open(path, w, &why)) return fail(why);
    std::unique_ptr<Resampler> resampler;
    if (audio && rate != 48000) resampler = std::make_unique<Resampler>(o.audioChannels, 48000, rate);
    const QSize outSize(w.width, w.height);
    std::vector<std::vector<float>> planes;
    for (std::int64_t f = 0; f < source.frames; ++f) {
        if (video) {
            const QImage frame = fitMoviePicture(source.picture(f), outSize, o.scaling);
            if (frame.isNull() || !writer.writeVideo(frame)) {
                writer.finish();
                return fail(QObject::tr("Frame %1 could not be written: %2").arg(f).arg(writer.lastError()));
            }
        }
        if (audio) {
            const std::int64_t from = sampleOf(f, source.frameRate);
            const int count = static_cast<int>(sampleOf(f + 1, source.frameRate) - from);
            source.sound(from, count, planes);
            std::vector<float> samples = interleave(planes, o.audioChannels, count);
            if (resampler) samples = resampler->push(samples.data(), count);
            const int n = static_cast<int>(samples.size() / static_cast<std::size_t>(o.audioChannels));
            if (n > 0 && !writer.writeAudio(samples.data(), n)) {
                writer.finish();
                return fail(writer.lastError());
            }
        }
        if (progress && !progress(static_cast<double>(f + 1) / static_cast<double>(source.frames))) {
            writer.finish();
            return fail(QObject::tr("The export was cancelled."));
        }
    }
    if (resampler) {
        const std::vector<float> rest = resampler->flush();
        const int n = static_cast<int>(rest.size() / static_cast<std::size_t>(o.audioChannels));
        if (n > 0) writer.writeAudio(rest.data(), n);
    }
    if (!writer.finish(&why)) return fail(why);
    return true;
}

// ---------------------------------------------------------------- stills and sound files (7.1b)

QString stillSuffix(StillFormat format) {
    switch (format) {
    case StillFormat::Png: return QStringLiteral("png");
    case StillFormat::Jpeg: return QStringLiteral("jpg");
    case StillFormat::Tiff: return QStringLiteral("tif");
    case StillFormat::Bmp: return QStringLiteral("bmp");
    case StillFormat::Pict: return QStringLiteral("pct");
    case StillFormat::Dpx: return QStringLiteral("dpx");
    case StillFormat::OpenExr: return QStringLiteral("exr");
    }
    return QStringLiteral("png");
}

QImage legalRangePicture(const QImage& picture) {
    QImage out = picture.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            auto squeeze = [](int v) { return 16 + (v * 219 + 127) / 255; };
            line[x] = qRgb(squeeze(qRed(line[x])), squeeze(qGreen(line[x])), squeeze(qBlue(line[x])));
        }
    }
    return out;
}

QString numberedStillPath(const QString& path, std::int64_t number, int digits) {
    const QFileInfo f(path);
    return f.dir().filePath(
        QStringLiteral("%1.%2.%3").arg(f.completeBaseName()).arg(number, std::clamp(digits, 1, 9), 10, QLatin1Char('0')).arg(f.suffix()));
}

bool setDpxHeaderInfo(const QString& path, const QString& inputDevice, double frameRate, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(QObject::tr("%1 cannot be read.").arg(path));
    QByteArray data = file.readAll();
    file.close();
    if (data.size() < 1664) return fail(QObject::tr("%1 is not a DPX file.").arg(path));
    const bool big = data.startsWith("SDPX");
    if (!big && !data.startsWith("XPDS")) return fail(QObject::tr("%1 is not a DPX file.").arg(path));
    auto u32 = [&](int at) {
        quint32 v = 0;
        for (int i = 0; i < 4; ++i) {
            const quint32 b = static_cast<quint8>(data[at + i]);
            v |= big ? b << (8 * (3 - i)) : b << (8 * i);
        }
        return v;
    };
    auto put32 = [&](int at, quint32 v) {
        for (int i = 0; i < 4; ++i) data[at + i] = static_cast<char>(big ? (v >> (8 * (3 - i))) & 0xff : (v >> (8 * i)) & 0xff);
    };
    // The industry headers (film, 256 bytes, and television, 128 bytes) after the generic headers, when missing: the
    // picture moves 384 bytes on, and the image offset, the file size, the industry header size and each image
    // element's data offset follow it.
    const quint32 imageOffset = u32(4);
    if (frameRate > 0 && imageOffset < 2048) {
        if (imageOffset != 1664) return fail(QObject::tr("%1 has a DPX header this export cannot extend.").arg(path));
        data.insert(1664, QByteArray(384, '\0'));
        put32(4, 2048);
        put32(16, static_cast<quint32>(data.size()));
        put32(28, 384);
        // Image header: orientation (2), element count (2), pixels per line (4), lines (4), then 8 elements of 72
        // bytes, each with its data offset 28 bytes in. The element count is a 16-bit field.
        auto u16 = [&](int at) {
            const quint32 a = static_cast<quint8>(data[at]), b = static_cast<quint8>(data[at + 1]);
            return big ? (a << 8) | b : (b << 8) | a;
        };
        const quint32 count = std::min<quint32>(u16(770), 8);
        for (quint32 e = 0; e < count; ++e) {
            const int at = static_cast<int>(780 + e * 72 + 28);
            const quint32 offset = u32(at);
            if (offset >= 1664 && offset != 0xffffffffu) put32(at, offset + 384);
        }
        // Undefined film and television fields are all ones (SMPTE 268M); the frame rates are set below.
        std::fill(data.begin() + 1664, data.begin() + 2048, static_cast<char>(0xff));
        // Text fields of the film header are zeros, not ones.
        std::fill(data.begin() + 1664, data.begin() + 1664 + 48, '\0');
    }
    if (!inputDevice.isEmpty()) {
        QByteArray name = inputDevice.toLatin1().left(32);
        name.append(QByteArray(32 - name.size(), '\0'));
        data.replace(1556, 32, name);
    }
    if (frameRate > 0) {
        float rate = static_cast<float>(frameRate);
        quint32 bits = 0;
        std::memcpy(&bits, &rate, sizeof bits);
        put32(1724, bits); // film header: frame rate of original
        put32(1940, bits); // television header: temporal sampling rate
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size())
        return fail(QObject::tr("%1 cannot be written.").arg(path));
    return true;
}

bool writeStill(const QString& path, const QImage& picture, const StillOptions& o, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        QFile::remove(path);
        return false;
    };
    if (picture.isNull()) return fail(QObject::tr("There is no picture to export."));
    const QSize size = o.size.isValid() && !o.size.isEmpty() ? o.size : picture.size();
    QImage frame = fitMoviePicture(picture, size, o.scaling);
    if (o.legalRange) frame = legalRangePicture(frame);
    if (o.format == StillFormat::Pict) {
        // Our own PICT writer (src/media/pict.*).
        if (!writePict(frame, path, false)) return fail(QObject::tr("The PICT file could not be written."));
        return true;
    }
    WriterSettings w;
    w.format = QStringLiteral("image2");
    w.formatOptions = {{QStringLiteral("update"), QStringLiteral("1")}};
    w.width = frame.width();
    w.height = frame.height();
    switch (o.format) {
    case StillFormat::Png:
        w.videoCodec = QStringLiteral("png");
        w.pixelFormat = o.depth >= 16 ? QStringLiteral("rgb48be") : QStringLiteral("rgb24");
        break;
    case StillFormat::Jpeg:
        w.videoCodec = QStringLiteral("mjpeg");
        w.pixelFormat = QStringLiteral("yuv444p");
        w.fullRange = true; // JPEG is full range
        w.quality = std::clamp(31 - (std::clamp(o.quality, 1, 100) * 30) / 100, 1, 31);
        break;
    case StillFormat::Tiff:
        w.videoCodec = QStringLiteral("tiff");
        w.pixelFormat = o.depth >= 16 ? QStringLiteral("rgb48le") : QStringLiteral("rgb24");
        break;
    case StillFormat::Bmp:
        w.videoCodec = QStringLiteral("bmp");
        w.pixelFormat = QStringLiteral("bgr24");
        break;
    case StillFormat::Dpx:
        w.videoCodec = QStringLiteral("dpx");
        w.pixelFormat = o.depth >= 16 ? QStringLiteral("rgb48le") : o.depth >= 10 ? QStringLiteral("gbrp10le") : QStringLiteral("rgb24");
        break;
    case StillFormat::OpenExr:
        w.videoCodec = QStringLiteral("exr");
        w.pixelFormat = QStringLiteral("gbrpf32le");
        w.videoOptions = {{QStringLiteral("format"), o.depth >= 32 ? QStringLiteral("float") : QStringLiteral("half")}};
        break;
    case StillFormat::Pict: break;
    }
    MediaWriter writer;
    QString why;
    if (!writer.open(path, w, &why)) return fail(why);
    if (!writer.writeVideo(frame)) {
        writer.finish();
        return fail(writer.lastError());
    }
    if (!writer.finish(&why)) return fail(why);
    if (o.format == StillFormat::Dpx && (!o.dpxInputDevice.isEmpty() || o.dpxFrameRate > 0) &&
        !setDpxHeaderInfo(path, o.dpxInputDevice, o.dpxFrameRate, &why))
        return fail(why);
    return true;
}

bool exportAudioFile(const QString& path, const MovieSource& source, const AudioFileOptions& o,
                     const std::function<bool(double)>& progress, QString* error) {
    auto fail = [&](const QString& text) {
        if (error) *error = text;
        QFile::remove(path);
        return false;
    };
    if (!source.sound || source.frames <= 0 || o.channels <= 0) return fail(QObject::tr("There is nothing to export."));
    static const QList<int> rates{32000, 44100, 48000, 88200, 96000};
    const int rate = rates.contains(o.sampleRate) ? o.sampleRate : 48000;
    WriterSettings w;
    w.format = o.aiff ? QStringLiteral("aiff") : QStringLiteral("wav");
    w.audioCodec = o.aiff ? (o.bitDepth == 16 ? QStringLiteral("pcm_s16be") : QStringLiteral("pcm_s24be"))
                          : (o.bitDepth == 16 ? QStringLiteral("pcm_s16le") : QStringLiteral("pcm_s24le"));
    w.sampleRate = rate;
    w.channels = o.channels;
    MediaWriter writer;
    QString why;
    if (!writer.open(path, w, &why)) return fail(why);
    std::unique_ptr<Resampler> resampler;
    if (rate != 48000) resampler = std::make_unique<Resampler>(o.channels, 48000, rate);
    std::vector<std::vector<float>> planes;
    for (std::int64_t f = 0; f < source.frames; ++f) {
        const std::int64_t from = sampleOf(f, source.frameRate);
        const int count = static_cast<int>(sampleOf(f + 1, source.frameRate) - from);
        source.sound(from, count, planes);
        std::vector<float> samples = interleave(planes, o.channels, count);
        if (resampler) samples = resampler->push(samples.data(), count);
        const int n = static_cast<int>(samples.size() / static_cast<std::size_t>(o.channels));
        if (n > 0 && !writer.writeAudio(samples.data(), n)) {
            writer.finish();
            return fail(writer.lastError());
        }
        if (progress && !progress(static_cast<double>(f + 1) / static_cast<double>(source.frames))) {
            writer.finish();
            return fail(QObject::tr("The export was cancelled."));
        }
    }
    if (resampler) {
        const std::vector<float> rest = resampler->flush();
        const int n = static_cast<int>(rest.size() / static_cast<std::size_t>(o.channels));
        if (n > 0) writer.writeAudio(rest.data(), n);
    }
    if (!writer.finish(&why)) return fail(why);
    return true;
}

} // namespace lumenis::media

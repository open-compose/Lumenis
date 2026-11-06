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
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace lumenis::media {

const QList<MovieCompression>& movieCompressions() {
    // Ours: the guide names the menus (Codec Family, Compression, Color Depth, p.895) and HEVC's Main and Main 10
    // (p.899), not the entries. DNxHR and ProRes go in MOV only; H.264, HEVC and AV1 in both.
    static const QList<MovieCompression> all{
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
    return all;
}

const MovieCompression* findMovieCompression(const QString& id) {
    for (const MovieCompression& c : movieCompressions()) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

bool movieCompressionAvailable(const MovieCompression& c) {
    const AVCodec* codec = avcodec_find_encoder_by_name(c.encoder.toUtf8().constData());
    return codec && codecAllowed(QString::fromUtf8(avcodec_get_name(codec->id)));
}

bool movieCompressionFits(const MovieCompression& c, MovieContainer container) {
    return container == MovieContainer::Mov ? c.mov : c.mp4;
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
    w.format = o.container == MovieContainer::Mov ? QStringLiteral("mov") : QStringLiteral("mp4");
    w.frameRate = source.frameRate;
    w.startTimecode = o.startTimecode;
    if (video) {
        const MovieCompression* c = findMovieCompression(o.compression);
        if (!c) return fail(QObject::tr("Unknown compression %1.").arg(o.compression));
        if (!movieCompressionFits(*c, o.container))
            return fail(QObject::tr("%1 cannot go in an MP4 file; choose MOV, or H.264, HEVC or AV1.").arg(c->label));
        if (!movieCompressionAvailable(*c)) return fail(QObject::tr("%1 is not available in this build.").arg(c->label));
        QSize size = o.size.isValid() && !o.size.isEmpty() ? o.size : source.raster;
        if (size.isEmpty()) return fail(QObject::tr("The picture has no size."));
        // Chroma subsampled formats need even sizes (ours: rounded up).
        size = QSize(size.width() + size.width() % 2, size.height() + size.height() % 2);
        w.videoCodec = c->encoder;
        w.videoProfile = c->profile;
        w.pixelFormat = c->pixelFormat;
        w.width = size.width();
        w.height = size.height();
        // JPEG is full range whatever the Color Levels.
        w.fullRange = o.fullRange || c->encoder == QLatin1String("mjpeg");
        if (c->bitRate && o.bitRateMbps > 0) w.bitRate = static_cast<std::int64_t>(o.bitRateMbps) * 1000000;
    }
    int rate = o.sampleRate == 44100 ? 44100 : 48000;
    if (audio) {
        w.channels = o.audioChannels;
        if (o.container == MovieContainer::Mp4 || o.aac) {
            if (codecAllowed(QStringLiteral("aac"))) {
                w.audioCodec = QStringLiteral("aac");
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

QString numberedStillPath(const QString& path, std::int64_t number) {
    const QFileInfo f(path);
    return f.dir().filePath(QStringLiteral("%1.%2.%3").arg(f.completeBaseName()).arg(number, 7, 10, QLatin1Char('0')).arg(f.suffix()));
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

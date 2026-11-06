// Movie export (7.1a; lumenis/media/movie_export.h, docs/spec/movie-export.md): fitting frames to the output raster
// by each Source Scaling, and MOV and MP4 files written and read back: their codecs, sizes, frame counts, sound,
// timecode and colour range; compressions that do not fit the container or the build; a cancel removing the file.
// 7.1b: stills in every Graphic format, DPX and OpenEXR read back, legal range, sequence file names, WAVE and AIFF-C.
#include "lumenis/media/media.h"
#include "lumenis/media/movie_export.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <cstring>

using namespace lumenis;
using namespace lumenis::media;

namespace {

// Ten frames of 64 x 36, a red left half and a blue right half, and a 1 kHz tone in every channel.
MovieSource source(std::int64_t frames = 10) {
    MovieSource s;
    s.frames = frames;
    s.frameRate = Rational(25, 1);
    s.raster = QSize(64, 36);
    s.picture = [](std::int64_t) {
        QImage image(64, 36, QImage::Format_RGB32);
        image.fill(Qt::blue);
        for (int y = 0; y < 36; ++y) {
            for (int x = 0; x < 32; ++x) image.setPixel(x, y, qRgb(255, 0, 0));
        }
        return image;
    };
    s.sound = [](std::int64_t at, int count, std::vector<std::vector<float>>& out) {
        out.assign(2, std::vector<float>(static_cast<std::size_t>(count)));
        for (int i = 0; i < count; ++i) {
            const float v = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(at + i) / 48000.0));
            out[0][static_cast<std::size_t>(i)] = v;
            out[1][static_cast<std::size_t>(i)] = v;
        }
    };
    return s;
}

bool near(QRgb a, QRgb b, int tolerance = 24) {
    return std::abs(qRed(a) - qRed(b)) <= tolerance && std::abs(qGreen(a) - qGreen(b)) <= tolerance &&
           std::abs(qBlue(a) - qBlue(b)) <= tolerance;
}

} // namespace

class TestMovieExport : public QObject {
    Q_OBJECT

private slots:
    void scaling() {
        QImage wide(160, 90, QImage::Format_RGB32);
        wide.fill(Qt::white);
        // Into a square raster: stretched fills it; letterboxed has black above and below; cropped fills it; kept
        // at its size it overflows to the sides, centred.
        const QSize square(100, 100);
        QImage s = fitMoviePicture(wide, square, MovieScaling::Stretch);
        QCOMPARE(s.size(), square);
        QVERIFY(near(s.pixel(50, 2), qRgb(255, 255, 255)));
        s = fitMoviePicture(wide, square, MovieScaling::Letterbox);
        QVERIFY(near(s.pixel(50, 2), qRgb(0, 0, 0)));
        QVERIFY(near(s.pixel(50, 50), qRgb(255, 255, 255)));
        QVERIFY(near(s.pixel(2, 50), qRgb(255, 255, 255)));
        s = fitMoviePicture(wide, square, MovieScaling::CenterCrop);
        QVERIFY(near(s.pixel(50, 2), qRgb(255, 255, 255)));
        s = fitMoviePicture(wide, square, MovieScaling::CenterKeepSize);
        QVERIFY(near(s.pixel(50, 2), qRgb(0, 0, 0)));     // 90 rows centred in 100: black at the top
        QVERIFY(near(s.pixel(50, 50), qRgb(255, 255, 255)));
        // The same size is the picture itself.
        QCOMPARE(fitMoviePicture(wide, wide.size(), MovieScaling::Stretch), wide);
    }

    void compressionTable() {
        QVERIFY(findMovieCompression(QStringLiteral("dnxhr_hq")));
        QVERIFY(!findMovieCompression(QStringLiteral("nothing")));
        const MovieCompression* dnx = findMovieCompression(QStringLiteral("dnxhr_hq"));
        QVERIFY(movieCompressionFits(*dnx, MovieContainer::Mov));
        QVERIFY(!movieCompressionFits(*dnx, MovieContainer::Mp4));
        const MovieCompression* hevc = findMovieCompression(QStringLiteral("hevc_main10"));
        QCOMPARE(hevc->depth, 10);
        QVERIFY(movieCompressionFits(*hevc, MovieContainer::Mp4));
        // A codec awaiting legal review is available exactly when the build allows those codecs.
        if (movieCompressionAvailable(*dnx)) QVERIFY(patentCodecsAllowed());
        QVERIFY(movieCompressionAvailable(*findMovieCompression(QStringLiteral("png"))));
    }

    void movWithPcm() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.mov"));
        MovieExportOptions o;
        o.compression = QStringLiteral("png");
        o.bitDepth = 24;
        o.startTimecode = QStringLiteral("01:00:00:00");
        QString error;
        QVERIFY2(exportMovie(path, source(), o, {}, &error), qPrintable(error));
        const auto info = probe(path);
        QVERIFY(info);
        QCOMPARE(info->startTimecode, QStringLiteral("01:00:00:00"));
        const StreamInfo* v = info->firstVideo();
        QVERIFY(v);
        QCOMPARE(v->codec, QStringLiteral("png"));
        QCOMPARE(v->width, 64);
        QCOMPARE(v->height, 36);
        QCOMPARE(v->length, 10);
        QCOMPARE(info->audio().size(), std::size_t(1));
        const StreamInfo* a = info->audio().front();
        QCOMPARE(a->codec, QStringLiteral("pcm_s24le"));
        QCOMPARE(a->channels, 2);
        QCOMPARE(a->sampleRate, 48000);
        // The frame read back: red left, blue right.
        VideoReader reader;
        QVERIFY(reader.open(path));
        const QImage f = reader.frame(3);
        QVERIFY(near(f.pixel(8, 18), qRgb(255, 0, 0)));
        QVERIFY(near(f.pixel(56, 18), qRgb(0, 0, 255)));
        // The sound: 0.4 s at 48 kHz, the tone's level.
        AudioReader sound;
        QVERIFY(sound.open(path));
        const std::vector<float> samples = sound.samples(0, 19200);
        QCOMPARE(samples.size(), std::size_t(19200 * 2));
        float peak = 0.0f;
        for (float x : samples) peak = std::max(peak, std::abs(x));
        QVERIFY2(std::abs(peak - 0.5f) < 0.01f, qPrintable(QString::number(static_cast<double>(peak))));
    }

    // MXF OP1a (7.2a, p.886-890): FFmpeg's OP1a muxer, the operational pattern label in the header partition, picture
    // and 24-bit sound, the timecode; which compressions go in which MXF export; XDCAM-25's own raster.
    void mxfOp1a() {
        const MovieCompression* dnx = findMovieCompression(QStringLiteral("dnxhr_lb"));
        const MovieCompression* xd = findMovieCompression(QStringLiteral("xdcam_25"));
        const MovieCompression* j2k = findMovieCompression(QStringLiteral("j2k"));
        QVERIFY(dnx && xd && j2k);
        QVERIFY(movieCompressionFits(*dnx, MovieContainer::Mxf));
        QVERIFY(!movieCompressionFits(*dnx, MovieContainer::Xdcam));
        QVERIFY(movieCompressionFits(*xd, MovieContainer::Mxf) && movieCompressionFits(*xd, MovieContainer::Xdcam));
        QVERIFY(!movieCompressionFits(*xd, MovieContainer::Mov));
        QVERIFY(movieCompressionFits(*j2k, MovieContainer::MxfLegacy) && !movieCompressionFits(*j2k, MovieContainer::Mxf));
        QTemporaryDir dir;
        auto op1a = [](const QString& file) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly)) return false;
            // The OP1a label: 06.0E.2B.34.04.01.01.01.0D.01.02.01.01.01 (SMPTE 378M).
            return f.read(65536).contains(QByteArray::fromHex("060e2b34040101010d0102010101"));
        };
        MovieExportOptions o;
        o.container = MovieContainer::Xdcam;
        o.compression = QStringLiteral("xdcam_25");
        o.bitDepth = 24;
        o.startTimecode = QStringLiteral("10:00:00:00");
        QString error;
        const QString xdcam = dir.filePath(QStringLiteral("xdcam.mxf"));
        QVERIFY2(exportMovie(xdcam, source(), o, {}, &error), qPrintable(error));
        auto info = probe(xdcam);
        QVERIFY(info && info->firstVideo());
        QCOMPARE(info->firstVideo()->codec, QStringLiteral("mpeg2video"));
        QCOMPARE(info->firstVideo()->width, 1440);
        QCOMPARE(info->firstVideo()->height, 1080);
        QCOMPARE(info->startTimecode, QStringLiteral("10:00:00:00"));
        QCOMPARE(info->audio().size(), std::size_t(1));
        QCOMPARE(info->audio().front()->codec, QStringLiteral("pcm_s24le"));
        QCOMPARE(info->audio().front()->sampleRate, 48000);
        QVERIFY(op1a(xdcam));
        // DNxHR does not go in an XDCAM file.
        o.compression = QStringLiteral("dnxhr_lb");
        QVERIFY(!exportMovie(dir.filePath(QStringLiteral("no.mxf")), source(), o, {}, &error));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("no.mxf"))));
        // JPEG 2000 in MXF OP1a (legacy), 16-bit sound at 48 kHz whatever was asked.
        o.container = MovieContainer::MxfLegacy;
        o.compression = QStringLiteral("j2k");
        o.bitDepth = 16;
        o.sampleRate = 44100;
        const QString legacy = dir.filePath(QStringLiteral("j2k.mxf"));
        QVERIFY2(exportMovie(legacy, source(4), o, {}, &error), qPrintable(error));
        info = probe(legacy);
        QVERIFY(info && info->firstVideo());
        QCOMPARE(info->firstVideo()->codec, QStringLiteral("jpeg2000"));
        QCOMPARE(info->audio().front()->codec, QStringLiteral("pcm_s16le"));
        QCOMPARE(info->audio().front()->sampleRate, 48000);
        VideoReader reader;
        QVERIFY(reader.open(legacy));
        const QImage f = reader.frame(1);
        QVERIFY(near(f.pixel(8, 18), qRgb(255, 0, 0)));
        QVERIFY(near(f.pixel(56, 18), qRgb(0, 0, 255)));
        // DNxHR in MXF OP1a where the build has it.
        if (movieCompressionAvailable(*dnx)) {
            o.container = MovieContainer::Mxf;
            o.compression = QStringLiteral("dnxhr_lb");
            MovieSource big = source(3);
            big.raster = QSize(256, 144);
            const QString dnxFile = dir.filePath(QStringLiteral("dnx.mxf"));
            QVERIFY2(exportMovie(dnxFile, big, o, {}, &error), qPrintable(error));
            info = probe(dnxFile);
            QCOMPARE(info->firstVideo()->codec, QStringLiteral("dnxhd"));
            QCOMPARE(info->firstVideo()->length, 3);
            QVERIFY(op1a(dnxFile));
        }
    }

    void sizesAndRates() {
        // Another raster, letterboxed; 16-bit sound at 44.1 kHz; video only and audio only.
        QTemporaryDir dir;
        MovieExportOptions o;
        o.compression = QStringLiteral("uncompressed_8");
        o.size = QSize(64, 64);
        o.sampleRate = 44100;
        o.bitDepth = 16;
        QString error;
        const QString path = dir.filePath(QStringLiteral("a.mov"));
        QVERIFY2(exportMovie(path, source(25), o, {}, &error), qPrintable(error));
        auto info = probe(path);
        QCOMPARE(info->firstVideo()->height, 64);
        QCOMPARE(info->audio().front()->sampleRate, 44100);
        QCOMPARE(info->audio().front()->codec, QStringLiteral("pcm_s16le"));
        AudioReader sound;
        QVERIFY(sound.open(path));
        // One second of sound; the resampler's tail may be a few samples short of it.
        QVERIFY(sound.samples(0, 44100).size() >= std::size_t(44000 * 2));
        VideoReader reader;
        QVERIFY(reader.open(path));
        const QImage f = reader.frame(0);
        QVERIFY(near(f.pixel(8, 4), qRgb(0, 0, 0)));   // the letterbox
        QVERIFY(near(f.pixel(8, 32), qRgb(255, 0, 0)));

        o.audio = false;
        const QString videoOnly = dir.filePath(QStringLiteral("v.mov"));
        QVERIFY(exportMovie(videoOnly, source(), o, {}, &error));
        QVERIFY(probe(videoOnly)->audio().empty());
        o.audio = true;
        o.video = false;
        const QString audioOnly = dir.filePath(QStringLiteral("s.mov"));
        QVERIFY(exportMovie(audioOnly, source(), o, {}, &error));
        QVERIFY(!probe(audioOnly)->firstVideo());
        o.audio = false;
        QVERIFY(!exportMovie(dir.filePath(QStringLiteral("n.mov")), source(), o, {}, &error));
    }

    void mp4() {
        QTemporaryDir dir;
        MovieExportOptions o;
        o.container = MovieContainer::Mp4;
        // DNxHR does not go in an MP4 file.
        o.compression = QStringLiteral("dnxhr_hq");
        QString error;
        const QString path = dir.filePath(QStringLiteral("out.mp4"));
        QVERIFY(!exportMovie(path, source(), o, {}, &error));
        QVERIFY2(error.contains(QStringLiteral("MP4")), qPrintable(error));
        QVERIFY(!QFileInfo::exists(path));
        // H.264 where the build has it, else AV1; the sound is AAC, or Opus without AAC.
        const MovieCompression* h264 = findMovieCompression(QStringLiteral("h264"));
        o.compression = movieCompressionAvailable(*h264) ? QStringLiteral("h264") : QStringLiteral("av1");
        if (!movieCompressionAvailable(*findMovieCompression(o.compression))) QSKIP("no H.264 or AV1 encoder");
        QVERIFY2(exportMovie(path, source(), o, {}, &error), qPrintable(error));
        const auto info = probe(path);
        QCOMPARE(info->firstVideo()->codec, o.compression == QLatin1String("h264") ? QStringLiteral("h264") : QStringLiteral("av1"));
        QCOMPARE(info->firstVideo()->length, 10);
        QCOMPARE(info->audio().front()->codec, patentCodecsAllowed() ? QStringLiteral("aac") : QStringLiteral("opus"));
    }

    void fullRange() {
        // Color Levels: full range in the stream, read back with the same colours either way.
        QTemporaryDir dir;
        MovieExportOptions o;
        o.compression = QStringLiteral("uncompressed_10");
        o.audio = false;
        o.fullRange = true;
        QString error;
        const QString path = dir.filePath(QStringLiteral("full.mov"));
        QVERIFY2(exportMovie(path, source(), o, {}, &error), qPrintable(error));
        VideoReader reader;
        QVERIFY(reader.open(path));
        QVERIFY(near(reader.frame(0).pixel(8, 18), qRgb(255, 0, 0)));
    }

    void stills() {
        QTemporaryDir dir;
        const QImage picture = source().picture(0);
        struct Case {
            StillFormat format;
            int depth;
        };
        for (const Case& c : {Case{StillFormat::Png, 8}, Case{StillFormat::Png, 16}, Case{StillFormat::Jpeg, 8}, Case{StillFormat::Tiff, 8},
                              Case{StillFormat::Tiff, 16}, Case{StillFormat::Bmp, 8}, Case{StillFormat::Dpx, 10}, Case{StillFormat::Dpx, 16},
                              Case{StillFormat::OpenExr, 16}, Case{StillFormat::OpenExr, 32}}) {
            StillOptions o;
            o.format = c.format;
            o.depth = c.depth;
            const QString path = dir.filePath(QStringLiteral("still%1.%2").arg(c.depth).arg(stillSuffix(c.format)));
            QString error;
            QVERIFY2(writeStill(path, picture, o, &error), qPrintable(path + QLatin1Char(' ') + error));
            VideoReader reader;
            QVERIFY2(reader.open(path), qPrintable(path));
            const QImage back = reader.frame(0);
            QCOMPARE(back.size(), QSize(64, 36));
            QVERIFY2(near(back.pixel(8, 18), qRgb(255, 0, 0), 30) && near(back.pixel(56, 18), qRgb(0, 0, 255), 30), qPrintable(path));
        }
        // PICT through our own writer; its file starts after a 512-byte header.
        StillOptions pict;
        pict.format = StillFormat::Pict;
        const QString pct = dir.filePath(QStringLiteral("still.pct"));
        QVERIFY(writeStill(pct, picture, pict));
        QVERIFY(QFileInfo(pct).size() > 512);
        // Another size by Crop/Pad: the picture 1:1 centred on black.
        StillOptions padded;
        padded.size = QSize(100, 100);
        padded.scaling = MovieScaling::CenterKeepSize;
        const QString pad = dir.filePath(QStringLiteral("pad.png"));
        QVERIFY(writeStill(pad, picture, padded));
        VideoReader reader;
        QVERIFY(reader.open(pad));
        QCOMPARE(reader.frame(0).size(), QSize(100, 100));
        QVERIFY(near(reader.frame(0).pixel(2, 2), qRgb(0, 0, 0)));
        // Keep as Legal Range: white is 235.
        QImage white(8, 8, QImage::Format_RGB32);
        white.fill(Qt::white);
        QCOMPARE(qRed(legalRangePicture(white).pixel(0, 0)), 235);
        white.fill(Qt::black);
        QCOMPARE(qRed(legalRangePicture(white).pixel(0, 0)), 16);
        QCOMPARE(numberedStillPath(QStringLiteral("/out/Cut.dpx"), 12), QStringLiteral("/out/Cut.0000012.dpx"));
        // Digits in File Name: "a value of 2 would be name.01.dpx ... If the count reached 99 an additional digit is
        // added, name.100.dpx" (p.891).
        QCOMPARE(numberedStillPath(QStringLiteral("/out/name.dpx"), 1, 2), QStringLiteral("/out/name.01.dpx"));
        QCOMPARE(numberedStillPath(QStringLiteral("/out/name.dpx"), 100, 2), QStringLiteral("/out/name.100.dpx"));

        // DPX's Input Device (field 38) and FPS in the header (p.891): the film and television headers added after
        // FFmpeg's 1664 bytes, the picture still read back.
        for (int depth : {10, 16}) {
            StillOptions dpx;
            dpx.format = StillFormat::Dpx;
            dpx.depth = depth;
            dpx.dpxInputDevice = QStringLiteral("Cutthulhu Telecine");
            dpx.dpxFrameRate = 23.976;
            const QString file = dir.filePath(QStringLiteral("header%1.dpx").arg(depth));
            QString error;
            QVERIFY2(writeStill(file, picture, dpx, &error), qPrintable(error));
            QFile f(file);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray data = f.readAll();
            const bool big = data.startsWith("SDPX");
            auto u32 = [&](int at) {
                quint32 v = 0;
                for (int i = 0; i < 4; ++i) {
                    const quint32 b = static_cast<quint8>(data[at + i]);
                    v |= big ? b << (8 * (3 - i)) : b << (8 * i);
                }
                return v;
            };
            QCOMPARE(u32(4), quint32(2048));
            QCOMPARE(u32(16), static_cast<quint32>(data.size()));
            QCOMPARE(data.mid(1556, 18), QByteArray("Cutthulhu Telecine"));
            float rate = 0;
            const quint32 bits = u32(1724);
            std::memcpy(&rate, &bits, sizeof rate);
            QVERIFY(std::abs(rate - 23.976f) < 0.001f);
            VideoReader back;
            QVERIFY2(back.open(file), qPrintable(file));
            QVERIFY(near(back.frame(0).pixel(8, 18), qRgb(255, 0, 0), 30) && near(back.frame(0).pixel(56, 18), qRgb(0, 0, 255), 30));
        }
    }

    void audioFiles() {
        QTemporaryDir dir;
        AudioFileOptions o;
        o.bitDepth = 24;
        QString error;
        const QString wav = dir.filePath(QStringLiteral("mix.wav"));
        QVERIFY2(exportAudioFile(wav, source(25), o, {}, &error), qPrintable(error));
        auto info = probe(wav);
        QVERIFY(info);
        QCOMPARE(info->audio().front()->codec, QStringLiteral("pcm_s24le"));
        QCOMPARE(info->audio().front()->channels, 2);
        QCOMPARE(info->audio().front()->sampleRate, 48000);
        QVERIFY(!info->firstVideo());
        AudioReader sound;
        QVERIFY(sound.open(wav));
        QCOMPARE(sound.samples(0, 48000).size(), std::size_t(48000 * 2));

        o.aiff = true;
        o.bitDepth = 16;
        o.sampleRate = 96000;
        o.channels = 1;
        const QString aif = dir.filePath(QStringLiteral("mix.aif"));
        QVERIFY2(exportAudioFile(aif, source(25), o, {}, &error), qPrintable(error));
        info = probe(aif);
        QCOMPARE(info->audio().front()->codec, QStringLiteral("pcm_s16be"));
        QCOMPARE(info->audio().front()->channels, 1);
        QCOMPARE(info->audio().front()->sampleRate, 96000);
    }

    void cancel() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("c.mov"));
        MovieExportOptions o;
        o.compression = QStringLiteral("png");
        QString error;
        QVERIFY(!exportMovie(path, source(), o, [](double done) { return done < 0.5; }, &error));
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(error.contains(QStringLiteral("cancelled")));
    }
};

QTEST_GUILESS_MAIN(TestMovieExport)
#include "tst_movie_export.moc"

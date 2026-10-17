#include "lumenis/ext/codec.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>

using namespace lumenis;
using namespace lumenis::media;

namespace {

// Frame n is a flat colour that changes with n, within broadcast-legal
// levels: DNxHD and others clip what lies outside them.
QColor colourOf(int n) {
    return QColor(40 + (n * 23) % 176, 40 + (175 - n * 11 % 176), 40 + (n * 57) % 176);
}

QImage picture(int n, int w, int h) {
    QImage image(w, h, QImage::Format_RGBA8888);
    image.fill(colourOf(n));
    return image;
}

// The colour at the centre, compared within a tolerance.
bool near(const QImage& image, const QColor& want, int tolerance) {
    if (image.isNull()) return false;
    const QColor got = image.pixelColor(image.width() / 2, image.height() / 2);
    const bool ok = std::abs(got.red() - want.red()) <= tolerance && std::abs(got.green() - want.green()) <= tolerance &&
                    std::abs(got.blue() - want.blue()) <= tolerance;
    if (!ok) qWarning() << "got" << got << "want" << want;
    return ok;
}

bool writeVideo(const QString& path, const WriterSettings& s, int frames) {
    MediaWriter w;
    QString error;
    if (!w.open(path, s, &error)) {
        qWarning() << "open:" << error;
        return false;
    }
    for (int i = 0; i < frames; ++i) {
        if (!w.writeVideo(picture(i, s.width, s.height))) {
            qWarning() << "write:" << w.lastError();
            return false;
        }
    }
    if (!w.finish(&error)) {
        qWarning() << "finish:" << error;
        return false;
    }
    return true;
}

} // namespace

class TestMedia : public QObject {
    Q_OBJECT

private slots:
    void version() {
        QVERIFY(ffmpegVersion().startsWith(QStringLiteral("FFmpeg ")));
    }

    void codecPolicy() {
        // docs/EXTENSIONS.md: the dagger codecs wait for counsel in official
        // builds; development builds allow them.
        QVERIFY(isPatentCodec(QStringLiteral("dnxhd")));
        QVERIFY(isPatentCodec(QStringLiteral("H264")));
        QVERIFY(!isPatentCodec(QStringLiteral("mpeg2video")));
        QVERIFY(codecAllowed(QStringLiteral("ffv1")));
        QCOMPARE(patentCodecsAllowed(), bool(LUMENIS_PATENT_CODECS));
        QCOMPARE(codecAllowed(QStringLiteral("prores")), bool(LUMENIS_PATENT_CODECS));
    }

    void losslessRoundTrip() {
        // FFV1 in QuickTime, RGB: the colours come back exactly.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ffv1.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 48;
        s.frameRate = Rational(25);
        s.startTimecode = QStringLiteral("10:00:00:00");
        QVERIFY(writeVideo(path, s, 12));

        const auto info = probe(path);
        QVERIFY(info);
        QVERIFY(info->format.contains(QStringLiteral("mov")));
        const StreamInfo* v = info->firstVideo();
        QVERIFY(v);
        QCOMPARE(v->codec, QStringLiteral("ffv1"));
        QCOMPARE(v->width, 64);
        QCOMPARE(v->height, 48);
        QCOMPARE(v->frameRate, Rational(25));
        QCOMPARE(v->length, 12);
        QVERIFY(v->decodable);
        QCOMPARE(info->startTimecode, QStringLiteral("10:00:00:00"));
        QCOMPARE(info->toJson().value(QStringLiteral("timecode")).toString(), QStringLiteral("10:00:00:00"));

        VideoReader reader;
        QVERIFY(reader.open(path));
        QCOMPARE(reader.frameCount(), 12);
        // In order, then out of order: every frame is exact.
        for (int n : {0, 1, 2, 7, 3, 11, 5, 6}) {
            QVERIFY2(near(reader.frame(n), colourOf(n), 0), qPrintable(QString::number(n)));
        }
        QVERIFY(reader.frame(12).isNull()); // past the end
        // Scaled on request.
        QCOMPARE(reader.frame(4, QSize(32, 24)).size(), QSize(32, 24));
    }

    void longGopSeeking() {
        // MPEG-2 Long GOP (XDCAM-like): frames between keyframes need
        // decoding from the keyframe before them.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("mpeg2.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("mpeg2video");
        s.pixelFormat = QStringLiteral("yuv422p");
        s.width = 128;
        s.height = 96;
        s.frameRate = Rational(30000, 1001);
        s.bitRate = 8'000'000;
        QVERIFY(writeVideo(path, s, 40));
        VideoReader reader;
        QVERIFY(reader.open(path));
        QCOMPARE(reader.info().frameRate, Rational(30000, 1001));
        QCOMPARE(reader.frameCount(), 40);
        for (int n : {17, 3, 39, 18, 19, 0, 25}) {
            QVERIFY2(near(reader.frame(n), colourOf(n), 12), qPrintable(QString::number(n)));
        }
    }

    void audioSamples() {
        // 24-bit PCM in WAV: a 440 Hz left channel, 880 Hz right.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tone.wav"));
        WriterSettings s;
        s.format = QStringLiteral("wav");
        s.audioCodec = QStringLiteral("pcm_s24le");
        s.sampleRate = 48000;
        s.channels = 2;
        auto value = [](int ch, std::int64_t i) {
            const double f = ch == 0 ? 440.0 : 880.0;
            return 0.5 * std::sin(2 * std::numbers::pi * f * static_cast<double>(i) / 48000.0);
        };
        {
            MediaWriter w;
            QVERIFY(w.open(path, s));
            std::vector<float> chunk;
            for (std::int64_t i = 0; i < 48000; ++i) {
                chunk.push_back(static_cast<float>(value(0, i)));
                chunk.push_back(static_cast<float>(value(1, i)));
                if (chunk.size() == 2 * 1000) {
                    QVERIFY(w.writeAudio(chunk.data(), 1000));
                    chunk.clear();
                }
            }
            QVERIFY(w.finish());
        }
        const auto info = probe(path);
        QVERIFY(info);
        const auto audio = info->audio();
        QCOMPARE(audio.size(), std::size_t(1));
        QCOMPARE(audio[0]->sampleRate, 48000);
        QCOMPARE(audio[0]->channels, 2);
        QCOMPARE(audio[0]->length, 48000);

        AudioReader reader;
        QVERIFY(reader.open(path));
        QCOMPARE(reader.channels(), 2);
        QCOMPARE(reader.sampleRate(), 48000);
        // Anywhere, in any order.
        for (std::int64_t start : {std::int64_t(24000), std::int64_t(100), std::int64_t(24100), std::int64_t(47950)}) {
            const std::vector<float> got = reader.samples(start, 100);
            const std::size_t expected = start + 100 > 48000 ? static_cast<std::size_t>(48000 - start) : 100;
            QCOMPARE(got.size(), expected * 2);
            for (std::size_t i = 0; i < expected; ++i) {
                const auto at = start + static_cast<std::int64_t>(i);
                QVERIFY(std::abs(static_cast<double>(got[2 * i]) - value(0, at)) < 1e-5);
                QVERIFY(std::abs(static_cast<double>(got[2 * i + 1]) - value(1, at)) < 1e-5);
            }
        }
        QVERIFY(reader.samples(48000, 10).empty());
    }

    void dnxhrInMxfOpAtom() {
        // Avid's own media: DNxHR in MXF OP-Atom (ROADMAP Phase 2).
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("V01.mxf"));
        WriterSettings s;
        s.format = QStringLiteral("mxf_opatom");
        s.videoCodec = QStringLiteral("dnxhd");
        s.videoProfile = QStringLiteral("dnxhr_lb");
        s.pixelFormat = QStringLiteral("yuv422p");
        s.width = 640;
        s.height = 360;
        s.frameRate = Rational(25);
        s.startTimecode = QStringLiteral("01:00:00:00");
        QVERIFY(writeVideo(path, s, 10));
        const auto info = probe(path);
        QVERIFY(info);
        QCOMPARE(info->format, QStringLiteral("mxf"));
        const StreamInfo* v = info->firstVideo();
        QVERIFY(v);
        QCOMPARE(v->codec, QStringLiteral("dnxhd"));
        QVERIFY(v->profile.contains(QStringLiteral("LB"), Qt::CaseInsensitive));
        QCOMPARE(v->length, 10);
        QCOMPARE(info->startTimecode, QStringLiteral("01:00:00:00"));
        VideoReader reader;
        QVERIFY(reader.open(path));
        for (int n : {9, 0, 4}) QVERIFY2(near(reader.frame(n), colourOf(n), 14), qPrintable(QString::number(n)));
    }

    void provider() {
        // FFmpeg is a codec provider in the registry (docs/EXTENSIONS.md).
        ext::ProviderRegistry registry;
        registerProviders(registry);
        auto codecs = registry.usable<ext::CodecProvider>();
        QCOMPARE(codecs.size(), std::size_t(1));
        QCOMPARE(codecs.front()->info().id, std::string("org.ffmpeg.libavcodec"));
        ext::CodecQuery q;
        q.codec = "mpeg2video";
        QVERIFY(codecs.front()->canDecode(q));
        q.codec = "dnxhr";
        QCOMPARE(codecs.front()->canDecode(q), patentCodecsAllowed());
        q.codec = "no-such-codec";
        QVERIFY(!codecs.front()->canDecode(q));
    }

    void linkVideoClip() {
        // A 25 fps QuickTime file linked into a 25 fps project (p.317-318).
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Harbor Wide.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        s.startTimecode = QStringLiteral("10:00:00:00");
        QVERIFY(writeVideo(path, s, 12));
        const auto info = probe(path);
        QVERIFY(info);
        QVERIFY(isLinkable(*info));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto item = linkClip(*info, o);
        QVERIFY(item);
        QCOMPARE(item->kind, model::BinItemKind::MasterClip);
        QCOMPARE(item->name, QStringLiteral("Harbor Wide"));
        QCOMPARE(item->durationFrames, std::int64_t{12});
        QCOMPARE(item->fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:00"));
        QCOMPARE(item->fields.value(QStringLiteral("End")), QStringLiteral("10:00:00:12"));
        QCOMPARE(item->fields.value(QStringLiteral("Tracks")), QStringLiteral("V1"));
        QCOMPARE(item->fields.value(QStringLiteral("Video")), QStringLiteral("FFV1"));
        QCOMPARE(item->fields.value(QStringLiteral("FPS")), QStringLiteral("25"));
        QCOMPARE(item->fields.value(QStringLiteral("Format")), QStringLiteral("36p/25"));
        QCOMPARE(item->fields.value(QStringLiteral("Color Space")), QStringLiteral("RGB"));
        QCOMPARE(item->fields.value(QStringLiteral("Source File")), QStringLiteral("Harbor Wide.mov"));
        QCOMPARE(item->fields.value(QStringLiteral("Plug-In")), linkPluginName());
        QVERIFY(isLinked(*item));

        // The link survives the bin file.
        model::Bin bin;
        bin.items << *item;
        const QString binPath = dir.filePath(QStringLiteral("b.cthbin"));
        QVERIFY(model::BinFile::write(binPath, bin));
        const auto back = model::BinFile::read(binPath);
        QVERIFY(back);
        const auto link = linkOf(back->items.first());
        QVERIFY(link);
        QCOMPARE(link->path, QFileInfo(path).absoluteFilePath());
        QCOMPARE(link->frames, std::int64_t{12});

        // Its frames, exactly; past the end, the last one.
        FrameCache cache;
        QVERIFY(cache.isOnline(*item));
        for (int n : {0, 7, 3, 11}) QVERIFY2(near(cache.frame(*item, n), colourOf(n), 0), qPrintable(QString::number(n)));
        QVERIFY(near(cache.frame(*item, 40), colourOf(11), 0));
        const QImage small = cache.frame(*item, 5, QSize(32, 32));
        QCOMPARE(small.size(), QSize(32, 18));

        // The same file in a 50 fps project: each file frame shows twice.
        o.editRate = Rational(50);
        const auto fast = linkClip(*info, o);
        QVERIFY(fast);
        QCOMPARE(fast->durationFrames, std::int64_t{24});
        QCOMPARE(fast->fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:00"));
        QVERIFY(near(cache.frame(*fast, 9), colourOf(4), 0));

        // Offline once the file is gone.
        cache.clear();
        QVERIFY(QFile::remove(path));
        QVERIFY(!cache.isOnline(*item));
        QVERIFY(cache.frame(*item, 0).isNull());
    }

    void linkAudioClip() {
        // A WAV links as an audio clip that ends on the frame holding its
        // last sample (p.329): 1.01 s at 25 fps is 26 frames.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("room tone.wav"));
        WriterSettings s;
        s.format = QStringLiteral("wav");
        s.audioCodec = QStringLiteral("pcm_s24le");
        s.sampleRate = 48000;
        s.channels = 2;
        {
            MediaWriter w;
            QVERIFY(w.open(path, s));
            const std::vector<float> silence(2 * 48480, 0.0f);
            QVERIFY(w.writeAudio(silence.data(), 48480));
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        int skipped = -1;
        const QString notMedia = dir.filePath(QStringLiteral("notes.txt"));
        {
            QFile f(notMedia);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("not media");
        }
        const QList<model::BinItem> items = linkFiles({path, notMedia}, o, &skipped);
        QCOMPARE(items.size(), 1);
        QCOMPARE(skipped, 1);
        const model::BinItem& item = items.first();
        QCOMPARE(item.kind, model::BinItemKind::AudioClip);
        QCOMPARE(item.durationFrames, std::int64_t{26});
        QCOMPARE(item.fields.value(QStringLiteral("Tracks")), QStringLiteral("A1-2"));
        QCOMPARE(item.fields.value(QStringLiteral("Audio SR")), QStringLiteral("48000"));
        QCOMPARE(item.fields.value(QStringLiteral("Audio Bit Depth")), QStringLiteral("24 bit"));
        QCOMPARE(item.fields.value(QStringLiteral("Audio Format")), QStringLiteral("PCM"));
        QCOMPARE(item.fields.value(QStringLiteral("Start")), QStringLiteral("00:00:00:00"));
        FrameCache cache;
        QVERIFY(cache.frame(item, 0).isNull()); // no picture
    }

    void errors() {
        QString error;
        QVERIFY(!probe(QStringLiteral("/no/such/file.mov"), &error));
        QVERIFY(!error.isEmpty());
        VideoReader reader;
        QVERIFY(!reader.open(QStringLiteral("/no/such/file.mov"), -1, &error));
        QVERIFY(reader.frame(0).isNull());
        MediaWriter w;
        WriterSettings s;
        s.format = QStringLiteral("no-such-format");
        s.videoCodec = QStringLiteral("ffv1");
        QVERIFY(!w.open(QStringLiteral("x.bin"), s, &error));
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"

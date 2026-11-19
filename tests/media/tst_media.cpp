#include "lumenis/ext/codec.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/color_transform.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/movie_export.h"
#include "lumenis/media/playback.h"
#include "lumenis/media/relink.h"
#include "lumenis/model/color_adapter.h"
#include "lumenis/model/frame_flex.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QPainter>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <thread>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
// Old Windows macros that clash with names here.
#undef near
#undef far
#undef small
#endif

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
        // builds; development builds allow them. DNxHD/DNxHR are allowed in
        // releases (the owner's decision, 2026-11-15).
        QVERIFY(!isPatentCodec(QStringLiteral("dnxhd")));
        QVERIFY(isPatentCodec(QStringLiteral("H264")));
        QVERIFY(!isPatentCodec(QStringLiteral("mpeg2video")));
        QVERIFY(codecAllowed(QStringLiteral("ffv1")));
        // The build's setting, unless LUMENIS_PATENT_CODECS_OFF gives the release policy (docs/spec/release-codecs.md).
        QCOMPARE(patentCodecsAllowed(), bool(LUMENIS_PATENT_CODECS) && qEnvironmentVariableIntValue("LUMENIS_PATENT_CODECS_OFF") == 0);
        QCOMPARE(codecAllowed(QStringLiteral("prores")), patentCodecsAllowed());
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

    // A user's report: H.264 played and scrubbed badly on an M1. Every frame that was not the next one seeked back to
    // the keyframe and decoded the group of pictures again. Long-GOP media now decodes on to a later frame before the
    // next keyframe and keeps its last frames, so stepping and reverse play cost about one decode per frame.
    void longGopDecodesOnce() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("gop.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("mpeg2video");
        s.pixelFormat = QStringLiteral("yuv420p");
        s.width = 128;
        s.height = 96;
        s.frameRate = Rational(25);
        s.bitRate = 4'000'000;
        s.videoOptions = {{QStringLiteral("g"), QStringLiteral("50")}, {QStringLiteral("bf"), QStringLiteral("2")}};
        QVERIFY(writeVideo(path, s, 150));
        VideoReader reader;
        QVERIFY(reader.open(path));
        QCOMPARE(reader.frameCount(), 150);
        QCOMPARE(reader.decodedFrames(), 0);
        auto check = [&](int n) { return near(reader.frame(n), colourOf(n), 12); };
        // Frame 60 is ten after the keyframe at 50.
        QVERIFY(check(60));
        const std::int64_t toSixty = reader.decodedFrames();
        QVERIFY2(toSixty <= 16, qPrintable(QString::number(toSixty)));
        // Three on decodes three, not the group again.
        QVERIFY(check(63));
        QVERIFY2(reader.decodedFrames() - toSixty <= 4, qPrintable(QString::number(reader.decodedFrames() - toSixty)));
        // Reverse play from 63 to 20: kept frames, then one decode of the first group.
        const std::int64_t before = reader.decodedFrames();
        for (int n = 62; n >= 20; --n) QVERIFY2(check(n), qPrintable(QString::number(n)));
        const std::int64_t reverse = reader.decodedFrames() - before;
        QVERIFY2(reverse <= 80, qPrintable(QString::number(reverse))); // about 1150 seeking for each frame
        // Past the next keyframe it seeks, as before, and frames are still right in any order.
        const std::int64_t far = reader.decodedFrames();
        QVERIFY(check(140));
        QVERIFY2(reader.decodedFrames() - far <= 45, qPrintable(QString::number(reader.decodedFrames() - far)));
        for (int n : {141, 139, 99, 100, 101, 0, 149, 49, 50, 75, 74, 76}) QVERIFY2(check(n), qPrintable(QString::number(n)));
        // The frames' hashes are the decoder's, kept or not.
        const QString kept = reader.frameMd5(75);
        VideoReader fresh;
        QVERIFY(fresh.open(path));
        QCOMPARE(fresh.frameMd5(75), kept);
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
        // The essence is clip-wrapped, and labelled so (FFmpeg labels it
        // frame-wrapped; readers that go by the label, such as bmx, then
        // fail: ROADMAP Phase 2 gate 2).
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(!bytes.contains(QByteArray::fromHex("0d01030102110100")));
        QVERIFY(bytes.contains(QByteArray::fromHex("0d01030102110200")));
    }

    void managedMedia() {
        // Import/Transcode and Consolidate into Avid MediaFiles (p.349-356):
        // MXF OP-Atom, a file per track.
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Dock Interview.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        s.audioCodec = QStringLiteral("pcm_s24le");
        s.sampleRate = 48000;
        s.channels = 2;
        s.startTimecode = QStringLiteral("10:00:00:00");
        {
            MediaWriter w;
            QVERIFY(w.open(source, s));
            // Left 0.5, right -0.25: a constant level is easy to check.
            std::vector<float> sound;
            for (int i = 0; i < 1920; ++i) sound.insert(sound.end(), {0.5f, -0.25f});
            for (int i = 0; i < 12; ++i) {
                QVERIFY(w.writeVideo(picture(i, 64, 36)));
                QVERIFY(w.writeAudio(sound.data(), 1920));
            }
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        QCOMPARE(clip->fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));

        const QString drive = dir.filePath(QStringLiteral("Media 1"));
        QVERIFY(QDir().mkpath(drive));
        MediaCreation c;
        c.raster = QSize(256, 144);
        c.resolution = *findResolution(QStringLiteral("dnx_lb"));
        c.videoDrive = drive;
        c.project = QStringLiteral("Harbor");
        int calls = 0;
        c.progress = [&](double) {
            ++calls;
            return true;
        };
        QString error;
        const auto made = transcodeClip(*clip, 2, 10, c, &error);
        QVERIFY2(made, qPrintable(error));
        QVERIFY(calls > 0);
        QCOMPARE(made->name, clip->name);
        QCOMPARE(made->kind, model::BinItemKind::MasterClip);
        QCOMPARE(made->durationFrames, std::int64_t{8});
        QCOMPARE(made->fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:02"));
        QCOMPARE(made->fields.value(QStringLiteral("End")), QStringLiteral("10:00:00:10"));
        QCOMPARE(made->fields.value(QStringLiteral("Tracks")), QStringLiteral("V1 A1-2"));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx LB"));
        QCOMPARE(made->fields.value(QStringLiteral("Source File")), QStringLiteral("Dock Interview.mov"));
        QVERIFY(!made->fields.contains(QStringLiteral("Plug-In")));
        const auto link = linkOf(*made);
        QVERIFY(link && link->isManaged());
        QCOMPARE(link->audioPaths.size(), 2);
        // In the MXF "1" folder, one file per track.
        const QString folder = QDir(drive).filePath(QStringLiteral("Avid MediaFiles/MXF/1"));
        QCOMPARE(QDir(folder).entryList({QStringLiteral("*.mxf")}, QDir::Files).size(), 3);
        QCOMPARE(QFileInfo(link->path).absolutePath(), QFileInfo(folder).absoluteFilePath());
        const auto v1 = probe(link->path);
        QVERIFY(v1);
        QCOMPARE(v1->format, QStringLiteral("mxf"));
        QCOMPARE(v1->firstVideo()->width, 256);
        QCOMPARE(v1->firstVideo()->length, 8);
        QCOMPARE(v1->startTimecode, QStringLiteral("10:00:00:02"));
        const auto file = readMediaFile(link->audioPaths.at(1));
        QVERIFY(file);
        QCOMPARE(file->mediaId, link->mediaId);
        // Its source chain (docs/spec/native-model.md): the linked clip's
        // file and import sources; the new clip's files, one MobID each,
        // from the same import source; and the MXF names the master clip
        // (in every track file) and the file by those MobIDs.
        using model::ClipSource;
        QCOMPARE(clip->sources.size(), 4); // V1, A1, A2 of one file, then the import
        QCOMPARE(clip->sources.at(0).mob, clip->sources.at(2).mob);
        const ClipSource& imported = clip->sources.last();
        QCOMPARE(imported.kind, ClipSource::Kind::Import);
        QCOMPARE(imported.name, QStringLiteral("Dock Interview.mov"));
        QCOMPARE(imported.startTimecode, QStringLiteral("10:00:00:00"));
        QVERIFY(!made->mobId.isNull());
        QCOMPARE(made->sources.size(), 4);
        QStringList tracks;
        QSet<QString> mobs;
        for (int i = 0; i < 3; ++i) {
            const ClipSource& fileSource = made->sources.at(i);
            QCOMPARE(fileSource.kind, ClipSource::Kind::File);
            QCOMPARE(fileSource.from, imported.mob);
            tracks << fileSource.track;
            mobs << QString::fromStdString(fileSource.mob.toString());
            const auto info = probe(fileSource.path);
            QVERIFY(info);
            auto umid = [&](const char* key) {
                return MobId::fromString(info->metadata.value(QString::fromLatin1(key)).mid(2).toStdString());
            };
            QCOMPARE(umid("material_package_umid"), made->mobId);
            QCOMPARE(umid("file_package_umid"), fileSource.mob);
        }
        QCOMPARE(tracks, (QStringList{QStringLiteral("V1"), QStringLiteral("A1"), QStringLiteral("A2")}));
        QCOMPARE(mobs.size(), 3);
        QCOMPARE(made->sources.last(), imported);
        // Linking the managed media takes its MobIDs from the MXF.
        const auto relinked = linkClip(*probe(made->sources.at(0).path), o);
        QVERIFY(relinked);
        QCOMPARE(relinked->mobId, made->mobId);
        QCOMPARE(relinked->sources.first().mob, made->sources.first().mob);
        QCOMPARE(file->track, QStringLiteral("A2"));
        QCOMPARE(file->clipName, clip->name);
        QCOMPARE(file->project, QStringLiteral("Harbor"));

        // The frames and sound of frames 2-9.
        FrameCache cache;
        QVERIFY(cache.isOnline(*made));
        for (int n : {0, 5, 7}) QVERIFY2(near(cache.frame(*made, n), colourOf(n + 2), 14), qPrintable(QString::number(n)));
        for (int t = 0; t < 2; ++t) {
            AudioReader a;
            QVERIFY(a.open(link->audioFile(t)));
            QCOMPARE(a.channels(), 1);
            QCOMPARE(a.sampleRate(), 48000);
            QCOMPARE(a.info().length, 8 * 1920);
            const std::vector<float> got = a.samples(5000, 10);
            QCOMPARE(got.size(), std::size_t(10));
            QVERIFY(std::abs(got[3] - (t == 0 ? 0.5f : -0.25f)) < 1e-4f);
        }

        // Cancelled: nothing is left behind.
        c.progress = [](double) { return false; };
        QVERIFY(!transcodeClip(*clip, 0, -1, c, &error));
        QCOMPARE(QDir(folder).entryList({QStringLiteral("*.mxf")}, QDir::Files).size(), 3);
        c.progress = {};

        // Consolidate copies DNx frame for frame; the linked FFV1 must be
        // transcoded.
        QVERIFY(!canConsolidate(*clip));
        QVERIFY(canConsolidate(*made));
        const QString drive2 = dir.filePath(QStringLiteral("Media 2"));
        QVERIFY(QDir().mkpath(drive2));
        c.videoDrive = drive2;
        const auto copied = consolidateClip(*made, 1, 5, c, &error);
        QVERIFY2(copied, qPrintable(error));
        QCOMPARE(copied->durationFrames, std::int64_t{4});
        QCOMPARE(copied->fields.value(QStringLiteral("Start")), QStringLiteral("10:00:00:03"));
        QCOMPARE(copied->fields.value(QStringLiteral("Video")), QStringLiteral("Avid DNx LB"));
        for (int n : {0, 3}) QCOMPARE(cache.frame(*copied, n), cache.frame(*made, n + 1));
        // The copied picture's MXF names the new clip and its file by their
        // MobIDs too, as the sound's do, and is labelled clip-wrapped.
        for (const ClipSource& fileSource : copied->sources) {
            if (fileSource.kind != ClipSource::Kind::File) continue;
            const auto info = probe(fileSource.path);
            QVERIFY(info);
            auto umid = [&](const char* key) {
                return MobId::fromString(info->metadata.value(QString::fromLatin1(key)).mid(2).toStdString());
            };
            QCOMPARE(umid("material_package_umid"), copied->mobId);
            QCOMPARE(umid("file_package_umid"), fileSource.mob);
        }

        // The media database finds the files where they are now.
        MediaDatabase db;
        db.scan({drive, drive2});
        QCOMPARE(db.fileCount(), 6);
        QVERIFY(QFileInfo::exists(QDir(folder).filePath(MediaDatabase::indexName())));
        const auto copiedSet = db.setOf(copied->mobId);
        QVERIFY(copiedSet);
        QCOMPARE(copiedSet->mediaId, linkOf(*copied)->mediaId);
        QCOMPARE(copiedSet->track(QStringLiteral("V1")).material, copied->mobId);
        const QString moved = QDir(drive2).filePath(QStringLiteral("Avid MediaFiles/MXF/2"));
        QVERIFY(QDir().mkpath(moved));
        // Closed first: Windows renames no open file.
        cache.clear();
        for (const QString& f : link->files()) QVERIFY(QFile::rename(f, QDir(moved).filePath(QFileInfo(f).fileName())));
        model::BinItem offline = *made;
        QVERIFY(!cache.isOnline(offline));
        db.clear();
        db.scan({drive, drive2});
        QCOMPARE(db.fileCount(), 6);
        QVERIFY(db.resolve(offline));
        QVERIFY(cache.isOnline(offline));
        QCOMPARE(QFileInfo(linkOf(offline)->path).absolutePath(), QFileInfo(moved).absoluteFilePath());
        // New media goes in the highest-numbered folder.
        QCOMPARE(QFileInfo(mxfFolder(drive2)).fileName(), QStringLiteral("2"));

        // Sound at 44.1 kHz becomes 48 kHz, exactly as long as its frames.
        const QString wav = dir.filePath(QStringLiteral("cd track.wav"));
        WriterSettings ws;
        ws.format = QStringLiteral("wav");
        ws.audioCodec = QStringLiteral("pcm_s16le");
        ws.sampleRate = 44100;
        ws.channels = 1;
        {
            MediaWriter w;
            QVERIFY(w.open(wav, ws));
            const std::vector<float> level(44100, 0.3f);
            QVERIFY(w.writeAudio(level.data(), 44100));
            QVERIFY(w.finish());
        }
        const auto audioClip = linkClip(*probe(wav), o);
        QVERIFY(audioClip);
        c.bitDepth = 16;
        const auto music = transcodeClip(*audioClip, 0, -1, c, &error);
        QVERIFY2(music, qPrintable(error));
        QCOMPARE(music->kind, model::BinItemKind::AudioClip);
        QCOMPARE(music->fields.value(QStringLiteral("Audio Bit Depth")), QStringLiteral("16 bit"));
        AudioReader a;
        QVERIFY(a.open(linkOf(*music)->path));
        QCOMPARE(a.info().length, music->durationFrames * 1920);
        QVERIFY(std::abs(a.samples(20000, 1)[0] - 0.3f) < 2e-3f);
    }

    // Apple ProRes as managed media (p.1240): made where an encoder may be used (VideoToolbox on macOS, FFmpeg's in builds
    // with LUMENIS_PATENT_CODECS), in MXF OP-Atom labelled clip-wrapped as it is written (fixOpAtom), and ranked
    // beside the comparable DNx level when relinking by quality.
    void transcodeProRes() {
        // The ranks hold whether or not this build can make ProRes.
        QCOMPARE(resolutionOfProbed(QStringLiteral("prores"), QStringLiteral("HQ"))->quality,
                 knownResolution(QStringLiteral("dnx_hqx"))->quality);
        QCOMPARE(knownResolution(QStringLiteral("Apple ProRes 422 Proxy"))->quality,
                 knownResolution(QStringLiteral("Avid DNx LB"))->quality);
        QVERIFY(knownResolution(QStringLiteral("prores_4444xq"))->quality > knownResolution(QStringLiteral("dnx_444"))->quality);
        const Resolution* hq = findResolution(QStringLiteral("prores_hq"));
        if (!hq) QSKIP("No ProRes encoder may be used in this build (docs/EXTENSIONS.md)");
        QVERIFY(!resolutionEncoder(*hq).isEmpty());
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Harbor Wide.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(source, s, 10));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        MediaCreation c;
        c.raster = QSize(256, 144);
        c.resolution = *hq;
        c.videoDrive = dir.path();
        QString error;
        const auto made = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("Apple ProRes 422 HQ"));
        const auto link = linkOf(*made);
        QVERIFY(link && link->isManaged());
        const auto v1 = probe(link->path);
        QVERIFY(v1 && v1->firstVideo());
        QCOMPARE(v1->format, QStringLiteral("mxf"));
        QCOMPARE(v1->firstVideo()->codec, QStringLiteral("prores"));
        QCOMPARE(v1->firstVideo()->profile, QStringLiteral("HQ"));
        QCOMPARE(v1->firstVideo()->length, 10);
        // The essence container label says clip-wrapped (RDD 44: ProRes 1c, mapping 02), as the essence is.
        QFile f(link->path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.contains(QByteArray::fromHex("060e2b340401010d0d010301021c0200")));
        QVERIFY(!bytes.contains(QByteArray::fromHex("060e2b340401010d0d010301021c0100")));
        // It plays: the picture comes back.
        FrameCache cache;
        QVERIFY(near(cache.frame(*made, 4), colourOf(4), 14));
        // Copied frame for frame, as DNx media is (Consolidate).
        QVERIFY(canConsolidate(*made));
    }

    // The Import setting's Image tab (p.1194-1195; 11.4f1): where an imported picture goes in the raster for each Image
    // Size Adjustment, and Scale from Full Range to Legal Range, through an import's transcode.
    void importImageOptions() {
        using Size = MediaCreation::ImageSize;
        const QSize hd(1920, 1080);
        // Fit keeps the shape; Current fills the frame; Do not resize keeps a smaller picture's pixels, centred.
        QCOMPARE(importedPictureRect(QSize(400, 400), 1.0, hd, Size::Fit), QRectF(420, 0, 1080, 1080));
        QCOMPARE(importedPictureRect(QSize(400, 400), 1.0, hd, Size::Current), QRectF(0, 0, 1920, 1080));
        QCOMPARE(importedPictureRect(QSize(400, 400), 1.0, hd, Size::NoResize), QRectF(760, 340, 400, 400));
        QCOMPARE(importedPictureRect(QSize(3840, 2160), 1.0, hd, Size::NoResize), QRectF(0, 0, 1920, 1080));
        // Crop/Pad: 720 x 480 padded by six lines into 486, and 486 cropped four lines at the top, two at the bottom.
        QCOMPARE(importedPictureRect(QSize(720, 480), 0.9, QSize(720, 486), Size::CropPad), QRectF(0, 4, 720, 480));
        QCOMPARE(importedPictureRect(QSize(720, 486), 0.9, QSize(720, 480), Size::CropPad), QRectF(0, -4, 720, 486));
        // Full range to legal: 0 and 255 become 16 and 235.
        QImage levels(2, 1, QImage::Format_RGBA8888);
        levels.setPixelColor(0, 0, QColor(0, 0, 0));
        levels.setPixelColor(1, 0, QColor(255, 255, 255));
        const QImage legal = fullToLegalRange(levels);
        QCOMPARE(legal.pixelColor(0, 0), QColor(16, 16, 16));
        QCOMPARE(legal.pixelColor(1, 0), QColor(235, 235, 235));

        // Through a transcode: a small white picture kept at its size in the middle, at legal white.
        const auto lb = findResolution(QStringLiteral("dnx_lb"));
        if (!lb) QSKIP("No DNx encoder in this FFmpeg");
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("logo.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        {
            MediaWriter w;
            QVERIFY(w.open(source, s));
            QImage white(64, 36, QImage::Format_RGB32);
            white.fill(Qt::white);
            for (int f = 0; f < 3; ++f) QVERIFY(w.writeVideo(white));
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        MediaCreation c;
        c.raster = QSize(256, 144);
        c.resolution = *lb;
        c.videoDrive = dir.path();
        c.imageSize = Size::NoResize;
        c.fullToLegal = true;
        QString error;
        const auto made = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        FrameCache cache;
        const QImage frame = cache.frame(*made, 1);
        QVERIFY(!frame.isNull());
        const QColor middle = frame.pixelColor(frame.width() / 2, frame.height() / 2);
        const QColor corner = frame.pixelColor(4, 4);
        QVERIFY2(std::abs(middle.red() - 235) <= 8, qPrintable(QString::number(middle.red())));
        QVERIFY2(corner.red() <= 24, qPrintable(QString::number(corner.red())));
        // The picture is 64 pixels wide: a quarter of the way in is outside it.
        QVERIFY(frame.pixelColor(frame.width() / 4 - 8, frame.height() / 2).red() <= 24);
    }

    // XDCAM HD 50 and XDCAM EX 35 as managed media (p.1252-1269; 11.1c1): offered only in the project formats the guide's
    // tables give, MPEG-2 long GOP at its rate (the encoder's options reach the context: a 50 Mb/s file is that big),
    // interlaced in interlaced projects, and refused for a format it is not made for.
    void transcodeXdcam() {
        if (!findResolution(QStringLiteral("xdcam_hd50"))) QSKIP("No MPEG-2 encoder in this FFmpeg");
        auto offered = [](const QString& id) {
            const QList<Resolution> list = resolutions();
            return std::any_of(list.begin(), list.end(), [&](const Resolution& r) { return r.id == id; });
        };
        setProjectFormat(QSize(3840, 2160), Rational(25), false);
        QVERIFY(!offered(QStringLiteral("xdcam_hd50")));
        QVERIFY(offered(QStringLiteral("dnx_hq")));
        setProjectFormat(QSize(1280, 720), Rational(24000, 1001), false);
        QVERIFY(!offered(QStringLiteral("xdcam_hd50")));
        QVERIFY(offered(QStringLiteral("xdcam_ex35")));
        setProjectFormat(QSize(1920, 1080), Rational(25), true);
        QVERIFY(offered(QStringLiteral("xdcam_hd50")));

        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Market.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(source, s, 25));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        MediaCreation c;
        c.raster = QSize(1920, 1080);
        c.resolution = *findResolution(QStringLiteral("xdcam_hd50"));
        c.videoDrive = dir.path();
        QString error;
        const auto made = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("XDCAM HD 50Mbits"));
        const auto link = linkOf(*made);
        QVERIFY(link);
        const auto v1 = probe(link->path);
        QVERIFY(v1 && v1->firstVideo());
        QCOMPARE(v1->firstVideo()->codec, QStringLiteral("mpeg2video"));
        QCOMPARE(v1->firstVideo()->profile, QStringLiteral("4:2:2"));
        QCOMPARE(v1->firstVideo()->length, 25);
        QVERIFY(v1->firstVideo()->interlaced);
        QVERIFY(v1->firstVideo()->topFieldFirst);
        // Labelled clip-wrapped as it is written (SMPTE ST 381: MPEG, stream 60, wrapping 02).
        {
            QFile f(link->path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray bytes = f.readAll();
            QVERIFY(bytes.contains(QByteArray::fromHex("0d01030102046002")));
            QVERIFY(!bytes.contains(QByteArray::fromHex("0d01030102046001")));
        }
        // One second at a constant 50 Mb/s: about 6 MB (FFmpeg's default rate, as before the fix, would be 200 kb/s).
        QVERIFY2(QFileInfo(link->path).size() > 4'000'000, qPrintable(QString::number(QFileInfo(link->path).size())));
        FrameCache cache;
        QVERIFY(near(cache.frame(*made, 12), colourOf(12), 16));
        // Not for a UHD project.
        c.raster = QSize(3840, 2160);
        QVERIFY(!transcodeClip(*clip, 0, -1, c, &error));
        QVERIFY(error.contains(QStringLiteral("XDCAM HD 50Mbits")));
        setProjectFormat({}, Rational(), false);
    }

    // The release builds' codec policy (Phase 12.2a; docs/spec/release-codecs.md), through LUMENIS_PATENT_CODECS_OFF:
    // DNx managed media as before, FFmpeg's own H.264 refused, H.264 decoded by the operating system where it has a
    // decoder (VideoToolbox on the Mac runner) and refused with the reason where not, movie export through the OS's
    // H.264 encoder where there is one.
    void releaseCodecPolicy() {
        QTemporaryDir dir;
        // An H.264 movie made while FFmpeg's own encoder may still be used (where this FFmpeg has it).
        const QString h264 = dir.filePath(QStringLiteral("h264.mov"));
        bool haveH264 = false;
        if (encoderAvailable(QStringLiteral("libx264"))) {
            WriterSettings s;
            s.format = QStringLiteral("mov");
            s.videoCodec = QStringLiteral("libx264");
            s.pixelFormat = QStringLiteral("yuv420p");
            s.width = 64;
            s.height = 36;
            s.frameRate = Rational(25);
            haveH264 = writeVideo(h264, s, 10);
        }
        qputenv("LUMENIS_PATENT_CODECS_OFF", "1");
        const auto restore = qScopeGuard([] { qunsetenv("LUMENIS_PATENT_CODECS_OFF"); });
        QVERIFY(!patentCodecsAllowed());
        QVERIFY(codecAllowed(QStringLiteral("dnxhd"))); // the owner's decision, 2026-11-15
        QVERIFY(!codecAllowed(QStringLiteral("h264")));
        QVERIFY(!codecAllowed(QStringLiteral("aac")));
        QVERIFY(!encoderAvailable(QStringLiteral("libx264")));
        QVERIFY(!encoderAvailable(QStringLiteral("prores_ks")));
        QVERIFY(encoderAvailable(QStringLiteral("dnxhd")));
        QVERIFY(isOsCodec(QStringLiteral("h264_videotoolbox")) && isOsCodec(QStringLiteral("aac_at")) && isOsCodec(QStringLiteral("hevc_mf")));
        QVERIFY(!isOsCodec(QStringLiteral("libx264")));
        // No AVC-Intra (x264) in a release.
        QVERIFY(!findResolution(QStringLiteral("avc_intra100")));

        // DNx managed media is made and read as before.
        const QString source = dir.filePath(QStringLiteral("Market.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(source, s, 5));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        MediaCreation c;
        c.raster = QSize(1920, 1080);
        c.resolution = defaultResolution();
        c.videoDrive = dir.path();
        QString error;
        const auto made = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        FrameCache cache;
        QVERIFY(near(cache.frame(*made, 2), colourOf(2), 16));

        // H.264: the operating system's decoder or none, never FFmpeg's own.
        if (haveH264) {
            const auto info = probe(h264);
            QVERIFY(info && info->firstVideo());
            QCOMPARE(info->firstVideo()->decodable, decodingAvailable(QStringLiteral("h264")));
            VideoReader reader;
            QString why;
            if (reader.open(h264, -1, &why)) {
                const QImage frame = reader.frame(3);
                QVERIFY(!frame.isNull());
                QVERIFY(near(frame, colourOf(3), 24)); // decoded by the OS
            } else {
                // No OS decoder (the reason names it), or one that opens and cannot decode this stream (a runner's
                // GPU-less D3D11VA): either way a reason, and never FFmpeg's own decoding.
                QVERIFY(!why.isEmpty());
                if (!decodingAvailable(QStringLiteral("h264"))) QVERIFY2(why.contains(QStringLiteral("operating system")), qPrintable(why));
            }
        }
        // Movie export's H.264 is the OS's encoder where there is one, or not offered.
        const MovieCompression* avc = findMovieCompression(QStringLiteral("h264"));
        QVERIFY(avc);
        const QString encoder = movieEncoder(*avc);
        QVERIFY2(encoder.isEmpty() || isOsCodec(encoder), qPrintable(encoder));
        QCOMPARE(movieCompressionAvailable(*avc), !encoder.isEmpty());
        QCOMPARE(encoder, osEncoderFor(QStringLiteral("h264")));
    }

    // The DNxHD bit-rate resolutions (p.1121; 11.1d): named by their rate at the project's frame rate, offered in the
    // formats that rate belongs to, made at it (interlaced in 1080i, the 1440 x 1080 thin raster for -TR), and linked
    // DNxHD media named the same.
    void transcodeDnxhd() {
        auto offered = [](const QString& name) {
            const QList<Resolution> list = resolutions();
            return std::any_of(list.begin(), list.end(), [&](const Resolution& r) { return r.name == name; });
        };
        setProjectFormat(QSize(1920, 1080), Rational(25), false);
        for (const char* name : {"DNxHD 36", "DNxHD 120", "DNxHD 185", "DNxHD 185x"}) QVERIFY2(offered(QLatin1String(name)), name);
        for (const char* name : {"DNxHD 145", "DNxHD 220", "DNxHD 175", "DNxHD 120-TR"}) QVERIFY2(!offered(QLatin1String(name)), name);
        QVERIFY(offered(QStringLiteral("Avid DNx HQ"))); // DNxHR stays in every format
        setProjectFormat(QSize(1920, 1080), Rational(30000, 1001), true);
        for (const char* name : {"DNxHD 145", "DNxHD 220", "DNxHD 220x", "DNxHD 145-TR"}) QVERIFY2(offered(QLatin1String(name)), name);
        QVERIFY(!offered(QStringLiteral("DNxHD 45"))); // 1080p only
        setProjectFormat(QSize(1280, 720), Rational(24000, 1001), false);
        QVERIFY(offered(QStringLiteral("DNxHD 90x")));
        setProjectFormat(QSize(3840, 2160), Rational(25), false);
        QVERIFY(!offered(QStringLiteral("DNxHD 120")));

        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Market.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(source, s, 25));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        // DNxHD 185x in a 1080i/25 project: 10-bit, interlaced, at 185 Mb/s.
        setProjectFormat(QSize(1920, 1080), Rational(25), true);
        MediaCreation c;
        c.raster = QSize(1920, 1080);
        c.resolution = *findResolution(QStringLiteral("DNxHD 185x"));
        c.videoDrive = dir.path();
        QString error;
        const auto made = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("DNxHD 185x"));
        const auto link = linkOf(*made);
        QVERIFY(link);
        const auto v1 = probe(link->path);
        QVERIFY(v1 && v1->firstVideo());
        const StreamInfo& video = *v1->firstVideo();
        QCOMPARE(video.codec, QStringLiteral("dnxhd"));
        QCOMPARE(video.profile, QStringLiteral("DNXHD"));
        QCOMPARE(video.bitsPerSample, 10);
        QVERIFY(video.interlaced);
        QVERIFY2(std::abs(static_cast<double>(video.bitRate) / 185e6 - 1.0) < 0.08, qPrintable(QString::number(video.bitRate)));
        // Linked as it is, the file is named for its rate.
        LinkOptions lo;
        lo.editRate = Rational(25);
        const auto relinked = linkClip(*v1, lo);
        QVERIFY(relinked);
        QCOMPARE(relinked->fields.value(QStringLiteral("Video")), QStringLiteral("DNxHD 185x"));
        // DNxHD 120-TR: stored 1440 x 1080, pixels 4:3 wide, named back the same.
        c.resolution = *findResolution(QStringLiteral("DNxHD 120-TR"));
        const auto thin = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(thin, qPrintable(error));
        const auto tr = probe(linkOf(*thin)->path);
        QVERIFY(tr && tr->firstVideo());
        QCOMPARE(tr->firstVideo()->width, 1440);
        QCOMPARE(linkClip(*tr, lo)->fields.value(QStringLiteral("Video")), QStringLiteral("DNxHD 120-TR"));
        // Not in a 1080p/25 project.
        setProjectFormat(QSize(1920, 1080), Rational(25), false);
        QVERIFY(!transcodeClip(*clip, 0, -1, c, &error));
        setProjectFormat({}, Rational(), false);
    }

    // AVC-Intra 100 and 50 (p.1252-1270; 11.1b): x264's classes in the formats the tables give, 100 in the full raster
    // (4:2:2 10-bit, High 4:2:2 Intra) and 50 in the thin one (1440 x 1080, 960 x 720), named back from their profile.
    void transcodeAvcIntra() {
        if (!findResolution(QStringLiteral("avc_intra100"))) QSKIP("No libx264 in this build (LUMENIS_PATENT_CODECS)");
        auto offered = [](const QString& id) {
            const QList<Resolution> list = resolutions();
            return std::any_of(list.begin(), list.end(), [&](const Resolution& r) { return r.id == id; });
        };
        setProjectFormat(QSize(1920, 1080), Rational(30000, 1001), true);
        QVERIFY(offered(QStringLiteral("avc_intra100")));
        QVERIFY(offered(QStringLiteral("avc_intra50")));
        setProjectFormat(QSize(3840, 2160), Rational(25), false);
        QVERIFY(!offered(QStringLiteral("avc_intra100")));
        setProjectFormat(QSize(1920, 1080), Rational(30000, 1001), true);

        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Market.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(30000, 1001);
        QVERIFY(writeVideo(source, s, 10));
        LinkOptions o;
        o.editRate = Rational(30000, 1001);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        MediaCreation c;
        c.raster = QSize(1920, 1080);
        c.resolution = *findResolution(QStringLiteral("avc_intra100"));
        c.videoDrive = dir.path();
        QString error;
        const auto made = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("AVC-Intra 100"));
        const auto v1 = probe(linkOf(*made)->path);
        QVERIFY(v1 && v1->firstVideo());
        QCOMPARE(v1->firstVideo()->codec, QStringLiteral("h264"));
        QCOMPARE(v1->firstVideo()->profile, QStringLiteral("High 4:2:2 Intra"));
        QCOMPARE(v1->firstVideo()->width, 1920);
        QVERIFY(v1->firstVideo()->interlaced);
        QCOMPARE(v1->firstVideo()->length, 10);
        FrameCache cache;
        QVERIFY(near(cache.frame(*made, 5), colourOf(5), 16));
        QCOMPARE(linkClip(*v1, o)->fields.value(QStringLiteral("Video")), QStringLiteral("AVC-Intra 100"));
        // AVC-Intra 50: the thin raster.
        c.resolution = *findResolution(QStringLiteral("avc_intra50"));
        const auto thin = transcodeClip(*clip, 0, -1, c, &error);
        QVERIFY2(thin, qPrintable(error));
        const auto v50 = probe(linkOf(*thin)->path);
        QVERIFY(v50 && v50->firstVideo());
        QCOMPARE(v50->firstVideo()->width, 1440);
        QCOMPARE(v50->firstVideo()->profile, QStringLiteral("High 10 Intra"));
        QCOMPARE(linkClip(*v50, o)->fields.value(QStringLiteral("Video")), QStringLiteral("AVC-Intra 50"));
        QVERIFY(near(cache.frame(*thin, 5), colourOf(5), 16));
        setProjectFormat({}, Rational(), false);
    }

    // DV, DVCPRO HD and XDCAM HD 35 as managed media (p.1252-1274; 11.1c2): offered in the formats the tables give, by
    // scan too; stored in their own rasters (DVCPRO HD's 1280 x 1080 at 1080i/59.94, DV's 480 lines for NTSC) with the
    // pixels' shape in the stream and the clip, labelled clip-wrapped, and shown filling the project frame.
    void transcodeDv() {
        if (!findResolution(QStringLiteral("dvcpro_hd"))) QSKIP("No DV encoder in this FFmpeg");
        auto offered = [](const QString& id) {
            const QList<Resolution> list = resolutions();
            return std::any_of(list.begin(), list.end(), [&](const Resolution& r) { return r.id == id; });
        };
        const Rational ntsc(30000, 1001);
        setProjectFormat(QSize(1920, 1080), ntsc, true, Rational(16, 9));
        QVERIFY(offered(QStringLiteral("dvcpro_hd")));
        QVERIFY(!offered(QStringLiteral("dv25_411")));
        QVERIFY(!offered(QStringLiteral("xdcam_hd35")));
        setProjectFormat(QSize(1920, 1080), ntsc, false, Rational(16, 9));
        QVERIFY(!offered(QStringLiteral("dvcpro_hd"))); // 1080p 29.97 is not DVCPRO HD's
        setProjectFormat(QSize(720, 576), Rational(25), false, Rational(4, 3));
        QVERIFY(offered(QStringLiteral("dv25_420")));
        QVERIFY(offered(QStringLiteral("dv50")));
        setProjectFormat(QSize(1440, 1080), Rational(25), true, Rational(16, 9));
        QVERIFY(offered(QStringLiteral("xdcam_hd35")));
        QVERIFY(offered(QStringLiteral("dvcpro_hd")));
        QVERIFY(!offered(QStringLiteral("xdcam_hd50")));
        QCOMPARE(resolutionRaster(*findResolution(QStringLiteral("dvcpro_hd")), QSize(1920, 1080), Rational(25)), QSize(1440, 1080));

        QTemporaryDir dir;
        auto sourceAt = [&](const QString& name, Rational rate) {
            const QString path = dir.filePath(name);
            WriterSettings s;
            s.format = QStringLiteral("mov");
            s.videoCodec = QStringLiteral("ffv1");
            s.pixelFormat = QStringLiteral("bgr0");
            s.width = 64;
            s.height = 36;
            s.frameRate = rate;
            if (!writeVideo(path, s, 15)) return std::optional<model::BinItem>();
            LinkOptions o;
            o.editRate = rate;
            return linkClip(*probe(path), o);
        };
        auto clipWrapped = [](const QString& path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return false;
            const QByteArray bytes = f.readAll();
            const QByteArray dv = QByteArray::fromHex("060e2b34040101010d0103010202");
            const qsizetype at = bytes.indexOf(dv);
            return at >= 0 && bytes.at(at + 15) == 0x02 && !bytes.contains(dv + bytes.mid(at + 14, 1) + char(0x01));
        };
        QString error;
        // DVCPRO HD in a 1080i/59.94 project: 1280 x 1080, pixels 3:2.
        setProjectFormat(QSize(1920, 1080), ntsc, true, Rational(16, 9));
        const auto hd = sourceAt(QStringLiteral("Harbour.mov"), ntsc);
        QVERIFY(hd);
        MediaCreation c;
        c.raster = QSize(1920, 1080);
        c.resolution = *findResolution(QStringLiteral("dvcpro_hd"));
        c.videoDrive = dir.path();
        const auto made = transcodeClip(*hd, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("DVCPro HD"));
        QCOMPARE(made->fields.value(QStringLiteral("Pixel Aspect Ratio")), QStringLiteral("1.5"));
        const auto link = linkOf(*made);
        QVERIFY(link);
        const auto v1 = probe(link->path);
        QVERIFY(v1 && v1->firstVideo());
        QCOMPARE(v1->firstVideo()->codec, QStringLiteral("dvvideo"));
        QCOMPARE(QSize(v1->firstVideo()->width, v1->firstVideo()->height), QSize(1280, 1080));
        QCOMPARE(v1->firstVideo()->aspect, Rational(16, 9));
        QVERIFY(clipWrapped(link->path));
        FrameCache cache;
        QCOMPARE(cache.frame(*made, 7).size(), QSize(1280, 1080)); // as stored
        const QImage shown = cache.frame(*made, 7, QSize(1920, 1080));
        QCOMPARE(shown.size(), QSize(1920, 1080));
        // Filling the frame: no pillars at its edges.
        QVERIFY(near(shown.copy(8, 540, 1, 1), colourOf(7), 20));
        QVERIFY(near(shown.copy(1910, 540, 1, 1), colourOf(7), 20));

        // DV25 in an NTSC project: 720 x 480, the frame's 5th to 484th lines.
        setProjectFormat(QSize(720, 486), ntsc, true, Rational(4, 3));
        c.raster = QSize(720, 486);
        c.resolution = *findResolution(QStringLiteral("dv25_411"));
        const auto sd = transcodeClip(*hd, 0, -1, c, &error);
        QVERIFY2(sd, qPrintable(error));
        const auto sdProbe = probe(linkOf(*sd)->path);
        QVERIFY(sdProbe && sdProbe->firstVideo());
        QCOMPARE(QSize(sdProbe->firstVideo()->width, sdProbe->firstVideo()->height), QSize(720, 480));
        QCOMPARE(sdProbe->firstVideo()->pixelFormat, QStringLiteral("yuv411p"));
        QVERIFY(clipWrapped(linkOf(*sd)->path));
        const QImage ntscFrame = cache.frame(*sd, 7, QSize(720, 486));
        QCOMPARE(ntscFrame.size(), QSize(720, 486));
        QCOMPARE(qGray(ntscFrame.pixel(360, 1)), 0); // the four lines DV has not
        QVERIFY(near(ntscFrame.copy(360, 243, 1, 1), colourOf(7), 24));
        // DV25 4:2:0 is PAL's alone.
        c.resolution = *findResolution(QStringLiteral("dv25_420"));
        QVERIFY(!transcodeClip(*hd, 0, -1, c, &error));
        QVERIFY(error.contains(QStringLiteral("DV25 420")));

        // XDCAM HD 35 in a 1440 x 1080 1080i/50 project.
        setProjectFormat(QSize(1440, 1080), Rational(25), true, Rational(16, 9));
        const auto pal = sourceAt(QStringLiteral("Quay.mov"), Rational(25));
        QVERIFY(pal);
        c.raster = QSize(1440, 1080);
        c.resolution = *findResolution(QStringLiteral("xdcam_hd35"));
        const auto xd = transcodeClip(*pal, 0, -1, c, &error);
        QVERIFY2(xd, qPrintable(error));
        const auto xdProbe = probe(linkOf(*xd)->path);
        QVERIFY(xdProbe && xdProbe->firstVideo());
        QCOMPARE(xdProbe->firstVideo()->codec, QStringLiteral("mpeg2video"));
        QCOMPARE(QSize(xdProbe->firstVideo()->width, xdProbe->firstVideo()->height), QSize(1440, 1080));
        QCOMPARE(xdProbe->firstVideo()->pixelFormat, QStringLiteral("yuv420p"));
        setProjectFormat({}, Rational(), false);
    }

    // MPEG IMX as managed media (p.1240, p.1272-1274; 11.1c3): offered in NTSC 30i and PAL 25i; MPEG-2 4:2:2 intra frames
    // at a constant rate in D-10's 720 x 608 (PAL) or 720 x 512 (NTSC) with black blanking lines above the picture, which
    // is shown filling the project frame; NTSC's lower field first.
    void transcodeImx() {
        if (!findResolution(QStringLiteral("mpeg_imx50"))) QSKIP("No MPEG-2 encoder in this FFmpeg");
        auto offered = [](const QString& id) {
            const QList<Resolution> list = resolutions();
            return std::any_of(list.begin(), list.end(), [&](const Resolution& r) { return r.id == id; });
        };
        setProjectFormat(QSize(720, 576), Rational(25), false, Rational(4, 3));
        QVERIFY(!offered(QStringLiteral("mpeg_imx50"))); // 25p is not in the tables
        setProjectFormat(QSize(1920, 1080), Rational(25), true, Rational(16, 9));
        QVERIFY(!offered(QStringLiteral("mpeg_imx30")));

        QTemporaryDir dir;
        auto sourceAt = [&](const QString& name, Rational rate) {
            const QString path = dir.filePath(name);
            WriterSettings s;
            s.format = QStringLiteral("mov");
            s.videoCodec = QStringLiteral("ffv1");
            s.pixelFormat = QStringLiteral("bgr0");
            s.width = 48;
            s.height = 36;
            s.frameRate = rate;
            if (!writeVideo(path, s, 25)) return std::optional<model::BinItem>();
            LinkOptions o;
            o.editRate = rate;
            return linkClip(*probe(path), o);
        };
        QString error;
        // PAL: 720 x 608, upper field first, 50 Mb/s constant (a second of it about 6 MB).
        setProjectFormat(QSize(720, 576), Rational(25), true, Rational(4, 3));
        QVERIFY(offered(QStringLiteral("mpeg_imx50")));
        const auto pal = sourceAt(QStringLiteral("Pier.mov"), Rational(25));
        QVERIFY(pal);
        MediaCreation c;
        c.raster = QSize(720, 576);
        c.resolution = *findResolution(QStringLiteral("mpeg_imx50"));
        c.videoDrive = dir.path();
        const auto made = transcodeClip(*pal, 0, -1, c, &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->fields.value(QStringLiteral("Video")), QStringLiteral("MPEG50"));
        const auto link = linkOf(*made);
        QVERIFY(link);
        const auto v1 = probe(link->path);
        QVERIFY(v1 && v1->firstVideo());
        QCOMPARE(v1->firstVideo()->codec, QStringLiteral("mpeg2video"));
        QCOMPARE(v1->firstVideo()->profile, QStringLiteral("4:2:2"));
        QCOMPARE(QSize(v1->firstVideo()->width, v1->firstVideo()->height), QSize(720, 608));
        QVERIFY(v1->firstVideo()->interlaced);
        QVERIFY(v1->firstVideo()->topFieldFirst);
        QVERIFY2(QFileInfo(link->path).size() > 4'000'000, qPrintable(QString::number(QFileInfo(link->path).size())));
        FrameCache cache;
        const QImage stored = cache.frame(*made, 9);
        QCOMPARE(stored.size(), QSize(720, 608));
        QVERIFY(qGray(stored.pixel(360, 8)) < 24); // the blanking lines
        const QImage shown = cache.frame(*made, 9, QSize(720, 576));
        QCOMPARE(shown.size(), QSize(720, 576));
        // The 48 x 36 source fills 720 x 540 of the frame (by its pixels, rows 18 to 557), as it was written: the
        // blanking lines are not shown, nor do they push the picture down.
        QVERIFY(near(shown.copy(360, 24, 1, 1), colourOf(9), 24));
        QVERIFY(near(shown.copy(360, 551, 1, 1), colourOf(9), 24));
        QVERIFY(qGray(shown.pixel(360, 6)) < 24 && qGray(shown.pixel(360, 570)) < 24);

        // NTSC: 720 x 512, lower field first.
        setProjectFormat(QSize(720, 486), Rational(30000, 1001), true, Rational(4, 3));
        const auto ntsc = sourceAt(QStringLiteral("Dock.mov"), Rational(30000, 1001));
        QVERIFY(ntsc);
        c.raster = QSize(720, 486);
        c.resolution = *findResolution(QStringLiteral("mpeg_imx30"));
        const auto nt = transcodeClip(*ntsc, 0, -1, c, &error);
        QVERIFY2(nt, qPrintable(error));
        const auto ntProbe = probe(linkOf(*nt)->path);
        QVERIFY(ntProbe && ntProbe->firstVideo());
        QCOMPARE(QSize(ntProbe->firstVideo()->width, ntProbe->firstVideo()->height), QSize(720, 512));
        QVERIFY(ntProbe->firstVideo()->interlaced);
        QVERIFY(!ntProbe->firstVideo()->topFieldFirst);
        QVERIFY(near(cache.frame(*nt, 9, QSize(720, 486)).copy(360, 243, 1, 1), colourOf(9), 24));
        setProjectFormat({}, Rational(), false);
    }

    // Transcode's "Apply source transformations > Color encoding" (p.544; 8.5c2): the clip's colour adapter goes into the
    // new media and the new clip and segments have none; off, the new clip keeps the encoding.
    void transcodeColorEncoding() {
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Graded.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(source, s, 10));
        LinkOptions o;
        o.editRate = Rational(25);
        auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        model::ColorEncoding e;
        e.transformations = {QStringLiteral("Gamma 2.6 to Linear")};
        model::setColorEncoding(*clip, e);
        model::Sequence sequence = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = clip->id;
        a.binId = QStringLiteral("bin1");
        a.sourceTrack = QStringLiteral("V1");
        a.sourceIn = 2;
        a.length = 5;
        a.color = e;
        sequence.track(QStringLiteral("V1"))->segments = {a};
        model::BinItem seq;
        seq.id = model::newItemId();
        seq.kind = model::BinItemKind::Sequence;
        seq.name = QStringLiteral("Cut 1");
        seq.editRate = Rational(25);
        sequence.storeIn(seq);
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip->id) return *clip;
            return std::nullopt;
        };
        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Transcode;
        options.handles = 0;
        options.creation.raster = QSize(256, 144);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        options.creation.videoDrive = dir.path();
        // Off: the adapters are bypassed; the new clip keeps the encoding, the segment its adapter.
        ConsolidateResult r = consolidate({seq}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.added.size(), 1);
        QVERIFY(model::colorEncodingOf(r.added.first()) == e);
        QVERIFY(model::Sequence::fromItem(r.changed.first()).track(QStringLiteral("V1"))->segments.at(0).color == e);
        FrameCache cache;
        const QColor plain = colourOf(4);
        QVERIFY(near(cache.frame(r.added.first(), 2), plain, 14));
        // On: the colours are in the media.
        options.applyColorEncoding = true;
        r = consolidate({seq}, QStringLiteral("bin1"), lookup, {QStringLiteral("Graded.new.01")}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(!model::colorEncodingOf(r.added.first()));
        QVERIFY(!model::Sequence::fromItem(r.changed.first()).track(QStringLiteral("V1"))->segments.at(0).color);
        const auto graded = media::transformColor(
            {static_cast<double>(plain.redF()), static_cast<double>(plain.greenF()), static_cast<double>(plain.blueF())}, e.transformations);
        const QColor want = QColor::fromRgbF(static_cast<float>(graded[0]), static_cast<float>(graded[1]), static_cast<float>(graded[2]));
        QVERIFY(near(cache.frame(r.added.first(), 2), want, 14));
    }

    // The Consolidate/Transcode options of 11.4a1 (p.351-356): Delete original media files when done, Relink selected
    // clips to target drive before skipping, Convert to Project Frame Rate, Framing (Frame Flex) and Convert Audio Sample
    // Rate.
    // Ours, for the offer after linking long-GOP files: which codecs are long GOP, and Transcode with clipsTakeMedia,
    // where the clip itself takes the new media (its MobID, no ".new" or ".old" clip).
    void transcodeClipsTakeMedia() {
        QVERIFY(isLongGopCodec(QStringLiteral("h264")));
        QVERIFY(isLongGopCodec(QStringLiteral("hevc")));
        QVERIFY(isLongGopCodec(QStringLiteral("mpeg2video")));
        QVERIFY(!isLongGopCodec(QStringLiteral("dnxhd")));
        QVERIFY(!isLongGopCodec(QStringLiteral("prores")));
        QVERIFY(!isLongGopCodec(QStringLiteral("ffv1")));
        QVERIFY(!isLongGopCodec(QStringLiteral("pcm_s16le")));
        QVERIFY(!isLongGopCodec(QStringLiteral("nonsense")));
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Long.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("mpeg2video");
        s.pixelFormat = QStringLiteral("yuv420p");
        s.width = 128;
        s.height = 96;
        s.frameRate = Rational(25);
        s.bitRate = 4'000'000;
        s.videoOptions = {{QStringLiteral("g"), QStringLiteral("25")}};
        QVERIFY(writeVideo(source, s, 30));
        QVERIFY(isLongGopFile(source));
        LinkOptions o;
        o.editRate = Rational(25);
        auto linked = linkClip(*probe(source), o);
        QVERIFY(linked);
        linked->name = QStringLiteral("Long");
        linked->color = QStringLiteral("#3264c8");
        const QString drive = dir.filePath(QStringLiteral("A"));
        QVERIFY(QDir().mkpath(drive));
        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Transcode;
        options.creation.raster = QSize(256, 144);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        options.creation.videoDrive = drive;
        options.clipsTakeMedia = true;
        const ClipLookup none = [](const QString&, const QString&) { return std::optional<model::BinItem>(); };
        const ConsolidateResult r = consolidate({*linked}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.changed.size(), 1);
        const model::BinItem& clip = r.changed.first();
        QCOMPARE(clip.id, linked->id);
        QCOMPARE(clip.name, QStringLiteral("Long"));
        QCOMPARE(clip.color, QStringLiteral("#3264c8"));
        QCOMPARE(model::mobIdOf(clip), model::mobIdOf(*linked)); // the new media name the clip
        const auto link = linkOf(clip);
        QVERIFY(link && link->isManaged());
        QVERIFY(!isLongGopFile(link->file()));
        QVERIFY(QFileInfo::exists(source)); // the linked file stays
        FrameCache cache;
        QVERIFY(near(cache.frame(clip, 12), colourOf(12), 16));
    }

    void consolidateOptions() {
        QTemporaryDir dir;
        // A 25 fps clip with sound at 44.1 kHz, its left half red and its right half blue.
        const QString source = dir.filePath(QStringLiteral("Halves.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        s.audioCodec = QStringLiteral("pcm_s16le");
        s.sampleRate = 44100;
        s.channels = 1;
        {
            MediaWriter w;
            QVERIFY(w.open(source, s));
            const std::vector<float> sound(1764, 0.25f);
            for (int i = 0; i < 50; ++i) {
                QImage image(64, 36, QImage::Format_RGBA8888);
                image.fill(Qt::blue);
                QPainter p(&image);
                p.fillRect(0, 0, 32, 36, Qt::red);
                p.end();
                QVERIFY(w.writeVideo(image));
                QVERIFY(w.writeAudio(sound.data(), 1764));
            }
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        auto linked = linkClip(*probe(source), o);
        QVERIFY(linked);
        const QString driveA = dir.filePath(QStringLiteral("A"));
        const QString driveB = dir.filePath(QStringLiteral("B"));
        QVERIFY(QDir().mkpath(driveA) && QDir().mkpath(driveB));
        const ClipLookup none = [](const QString&, const QString&) { return std::optional<model::BinItem>(); };
        auto colourAt = [](const model::BinItem& clip, std::int64_t frame, double x) {
            FrameCache cache;
            const QImage image = cache.frame(clip, frame);
            return image.isNull() ? QColor() : image.pixelColor(static_cast<int>(x * image.width()), image.height() / 2);
        };
        auto redder = [](const QColor& c) { return c.red() > 180 && c.blue() < 80; };
        auto bluer = [](const QColor& c) { return c.blue() > 180 && c.red() < 80; };

        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Transcode;
        options.creation.raster = QSize(256, 144);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        options.creation.videoDrive = driveA;
        options.creation.sampleRate = 48000;

        // Keep Source's Frame Rate (the default) and Convert Audio Sample Rate to 48 kHz: MXF.
        ConsolidateResult r = consolidate({*linked}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        const model::BinItem managed = r.added.first();
        QCOMPARE(managed.editRate, Rational(25));
        QCOMPARE(managed.durationFrames, std::int64_t{50});
        QCOMPARE(managed.fields.value(QStringLiteral("Audio SR")), QStringLiteral("48000"));
        QVERIFY(bluer(colourAt(managed, 5, 0.75)));

        // Convert to Project Frame Rate: two seconds at 24 fps, frames taken by time; the sound not converted keeps 44.1.
        options.convertToProjectRate = true;
        options.projectRate = Rational(24);
        options.convertSampleRate = false;
        r = consolidate({*linked}, QStringLiteral("bin1"), none, {managed.name}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        const model::BinItem retimed = r.added.first();
        QCOMPARE(retimed.editRate, Rational(24));
        QCOMPARE(retimed.durationFrames, std::int64_t{48});
        QCOMPARE(retimed.fields.value(QStringLiteral("Audio SR")), QStringLiteral("44100"));
        QVERIFY(linkOf(retimed)->audioPaths.first().endsWith(QStringLiteral(".wav")));
        options.convertToProjectRate = false;
        options.convertSampleRate = true;

        // Framing: the box on the left half fills the frame, so its right side is red too; without it, blue.
        model::FrameFlex flex;
        flex.imageSize = QSize(64, 36);
        flex.size = 0.5;
        flex.x = -16;
        model::BinItem framed = *linked;
        model::setFrameFlex(framed, flex);
        options.applyFraming = true;
        r = consolidate({framed}, QStringLiteral("bin1"), none, {managed.name, retimed.name}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(redder(colourAt(r.added.first(), 5, 0.75)));
        QVERIFY(!model::frameFlexOf(r.added.first()));
        options.applyFraming = false;
        r = consolidate({framed}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY(bluer(colourAt(r.added.first(), 5, 0.75)));

        // Relink selected clips to target drive before skipping: the managed clip's copy on B is found and linked to.
        options.mode = ConsolidateOptions::Mode::Consolidate;
        options.keepMastersLinked = true;
        options.creation.videoDrive = driveB;
        r = consolidate({managed}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        const Link onB = *linkOf(r.added.first());
        options.skipOnTarget = true;
        options.relinkBeforeSkipping = true;
        options.mediaOnTarget = [&](const model::BinItem& clip, const QString& drive) -> std::optional<Link> {
            if (clip.id == managed.id && drive == driveB) return onB;
            return std::nullopt;
        };
        r = consolidate({managed}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.changed.size(), 1);
        QCOMPARE(linkOf(r.changed.first())->path, onB.path);
        QCOMPARE(r.skipped.size(), 1);
        // Without it, the clip is copied (its media is not on B by its own link).
        options.relinkBeforeSkipping = false;
        options.skipOnTarget = false;

        // Delete original media files when done: the clip takes the copy, no ".old", its old files listed to delete.
        const QStringList originals = linkOf(managed)->files();
        options.deleteOriginals = true;
        options.keepMastersLinked = true; // overridden: the clip must take the copy
        r = consolidate({managed}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.changed.size(), 1);
        QVERIFY(linkOf(r.changed.first())->path.startsWith(driveB));
        QCOMPARE(r.changed.first().mobId, managed.mobId);
        QCOMPARE(QSet<QString>(r.deleteFiles.begin(), r.deleteFiles.end()), QSet<QString>(originals.begin(), originals.end()));
        QCOMPARE(deleteMediaFiles(r.deleteFiles).size(), originals.size());
        for (const QString& f : originals) QVERIFY(!QFileInfo::exists(f));
        QVERIFY(bluer(colourAt(r.changed.first(), 5, 0.75)));
        // Linked media is never deleted: the user's own file.
        r = consolidate({*linked}, QStringLiteral("bin1"), none, {}, options);
        QVERIFY(r.deleteFiles.isEmpty());
        QVERIFY(QFileInfo::exists(source));
    }

    // Consolidate all clips in a group edit (p.351; 11.4a2): a sequence's group segment remakes the angle it shows, or
    // every angle with the option, over the group frames used with handles; a new group plays the new clips and the
    // segment uses it, the same pictures at the same frames.
    void consolidateGroupEdits() {
        QTemporaryDir dir;
        // Two angles in managed DNx (Consolidate copies DNx), B's frames numbered from 100.
        auto angleClip = [&](const QString& name, int base) -> std::optional<model::BinItem> {
            const QString path = dir.filePath(name + QStringLiteral(".mov"));
            WriterSettings s;
            s.format = QStringLiteral("mov");
            s.videoCodec = QStringLiteral("ffv1");
            s.pixelFormat = QStringLiteral("bgr0");
            s.width = 64;
            s.height = 36;
            s.frameRate = Rational(25);
            MediaWriter w;
            if (!w.open(path, s)) return std::nullopt;
            for (int i = 0; i < 40; ++i) {
                if (!w.writeVideo(picture(base + i, 64, 36))) return std::nullopt;
            }
            if (!w.finish()) return std::nullopt;
            LinkOptions o;
            o.editRate = Rational(25);
            auto linked = linkClip(*probe(path), o);
            if (!linked) return std::nullopt;
            linked->name = name;
            MediaCreation c;
            c.raster = QSize(256, 144);
            c.resolution = *findResolution(QStringLiteral("dnx_lb"));
            c.videoDrive = dir.filePath(QStringLiteral("Media"));
            QDir().mkpath(c.videoDrive);
            return transcodeClip(*linked, 0, -1, c);
        };
        const auto a = angleClip(QStringLiteral("CamA"), 0);
        const auto b = angleClip(QStringLiteral("CamB"), 100);
        QVERIFY(a && b);
        model::GroupClip group;
        group.angles = {model::GroupAngle{QStringLiteral("bin1"), a->id, *a, 0}, model::GroupAngle{QStringLiteral("bin1"), b->id, *b, 5}};
        model::BinItem groupItem;
        groupItem.id = model::newItemId();
        groupItem.mobId = MobId::generate();
        groupItem.kind = model::BinItemKind::Group;
        groupItem.name = QStringLiteral("Interview");
        groupItem.editRate = Rational(25);
        model::storeGroup(groupItem, group, 30);

        model::Sequence sequence = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = groupItem.id;
        seg.binId = QStringLiteral("bin1");
        seg.sourceTrack = QStringLiteral("V1");
        seg.sourceIn = 10;
        seg.length = 5;
        seg.groupAngle = 1;
        sequence.track(QStringLiteral("V1"))->segments = {seg};
        model::BinItem seqItem;
        seqItem.id = model::newItemId();
        seqItem.kind = model::BinItemKind::Sequence;
        seqItem.name = QStringLiteral("Cut");
        seqItem.editRate = Rational(25);
        sequence.storeIn(seqItem);
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == groupItem.id) return groupItem;
            return std::nullopt;
        };
        auto shownAt = [](const model::BinItem& g, const model::Segment& s, std::int64_t frame) {
            FrameCache cache;
            return cache.frame(model::clipForSegment(g, s), frame);
        };

        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Consolidate;
        options.handles = 2;
        options.creation.raster = QSize(256, 144);
        options.creation.videoDrive = dir.filePath(QStringLiteral("Target"));
        QDir().mkpath(options.creation.videoDrive);

        // Without the option: B alone (shown), frames 13 to 22 of it (group frames 8-17 with its offset of 5).
        ConsolidateResult r = consolidate({seqItem}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.added.size(), 2);
        const model::BinItem newB = r.added.at(0);
        const model::BinItem newGroup = r.added.at(1);
        QCOMPARE(newB.name, QStringLiteral("CamB.new.01"));
        QCOMPARE(newB.durationFrames, std::int64_t{9});
        QCOMPARE(newGroup.name, QStringLiteral("Interview.new.01"));
        QCOMPARE(newGroup.kind, model::BinItemKind::Group);
        const auto remade = model::groupOf(newGroup);
        QVERIFY(remade);
        QCOMPARE(remade->angles.at(0).clipId, a->id);
        QCOMPARE(remade->angles.at(1).clipId, newB.id);
        QCOMPARE(remade->angles.at(1).offset, std::int64_t{5 - 13});
        const model::Segment after = model::Sequence::fromItem(r.changed.first()).track(QStringLiteral("V1"))->segments.first();
        QCOMPARE(after.clipId, newGroup.id);
        QCOMPARE(after.sourceIn, std::int64_t{10});
        for (const std::int64_t f : {10, 12, 14}) {
            QVERIFY(near(shownAt(newGroup, after, f), colourOf(100 + 5 + static_cast<int>(f)), 14));
            QVERIFY(near(shownAt(newGroup, after, f), shownAt(groupItem, seg, f).pixelColor(32, 18), 14));
        }

        // With it: A too, over the same group frames (8-17, its offset 0).
        options.groupEdits = true;
        r = consolidate({seqItem}, QStringLiteral("bin1"), lookup, {newB.name, newGroup.name}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.added.size(), 3);
        const auto both = model::groupOf(r.added.at(2));
        QVERIFY(both);
        QCOMPARE(r.added.at(0).durationFrames, std::int64_t{9});
        QCOMPARE(both->angles.at(0).offset, std::int64_t{-8});
        model::Segment onA = model::Sequence::fromItem(r.changed.first()).track(QStringLiteral("V1"))->segments.first();
        onA.groupAngle = 0;
        QVERIFY(near(shownAt(r.added.at(2), onA, 12), colourOf(12), 14));

        // A group selected in the bin: every angle whole with the option, else its selected angle.
        r = consolidate({groupItem}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.added.size(), 3);
        QCOMPARE(r.added.at(0).durationFrames, std::int64_t{40});
        QCOMPARE(model::groupOf(r.added.at(2))->angles.at(1).offset, std::int64_t{5});
        options.groupEdits = false;
        r = consolidate({groupItem}, QStringLiteral("bin1"), lookup, {}, options);
        QCOMPARE(r.added.size(), 2);
        QCOMPARE(model::groupOf(r.added.at(1))->angles.at(1).clipId, b->id);
    }

    // The Relink dialog's options of 11.4a3 (p.362-364): Relink by a column (Camroll), by Aux TC, Allow relinking to
    // offline items, Relink to a video format, and Tracks to Relink, on a sequence's segments.
    void relinkOptions() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("R1.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        s.audioCodec = QStringLiteral("pcm_s16le");
        s.channels = 1;
        {
            MediaWriter w;
            QVERIFY(w.open(path, s));
            const std::vector<float> sound(1920, 0.1f);
            for (int i = 0; i < 30; ++i) QVERIFY(w.writeVideo(picture(i, 64, 36)) && w.writeAudio(sound.data(), 1920));
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const auto linked = linkClip(*probe(path), o);
        QVERIFY(linked);
        auto clipLike = [&](const QString& name, const QString& start) {
            model::BinItem c = *linked;
            c.id = model::newItemId();
            c.mobId = MobId::generate();
            c.name = name;
            c.durationFrames = 30;
            c.fields.insert(QStringLiteral("Start"), start);
            c.fields.insert(QStringLiteral("Tracks"), QStringLiteral("V1 A1"));
            c.fields.insert(QStringLiteral("Format"), QStringLiteral("1080i/29.97"));
            return c;
        };
        // The cut's clip: camroll R7, Start 01:00:00:00, Aux TC1 02:00:00:00; its segment uses frames 5-15 on V1 and A1.
        model::BinItem used = clipLike(QStringLiteral("Take 1"), QStringLiteral("01:00:00:00"));
        used.fields.insert(QStringLiteral("Camroll"), QStringLiteral("R7"));
        used.fields.insert(QStringLiteral("Auxiliary TC1"), QStringLiteral("02:00:00:00"));
        model::Sequence sequence = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = used.id;
        seg.binId = QStringLiteral("b");
        seg.sourceTrack = QStringLiteral("V1");
        seg.sourceIn = 5;
        seg.length = 10;
        sequence.track(QStringLiteral("V1"))->segments = {seg};
        seg.sourceTrack = QStringLiteral("A1");
        sequence.ensureTrack(QStringLiteral("A1"))->segments = {seg};
        model::BinItem cut;
        cut.id = model::newItemId();
        cut.kind = model::BinItemKind::Sequence;
        cut.name = QStringLiteral("Cut");
        cut.editRate = Rational(25);
        sequence.storeIn(cut);
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == used.id) return used;
            return std::nullopt;
        };
        MediaDatabase db;
        auto relinkedTo = [&](const RelinkOptions& r, const QList<model::BinItem>& candidates, const QString& track) {
            QList<BinClip> c;
            for (const model::BinItem& i : candidates) c.append({QStringLiteral("b"), i});
            RelinkOptions options = r;
            options.newSequences = false;
            const RelinkResult result = relinkManaged({cut}, db, c, lookup, options, {});
            if (result.changed.isEmpty()) return QString();
            const QString id = model::Sequence::fromItem(result.changed.first()).track(track)->segments.first().clipId;
            return id == used.id ? QStringLiteral("unchanged") : id;
        };
        // The track kept its clip: nothing relinked, or only another track.
        auto stays = [&](const RelinkOptions& r, const QList<model::BinItem>& candidates, const QString& track) {
            const QString id = relinkedTo(r, candidates, track);
            return id.isEmpty() || id == QStringLiteral("unchanged");
        };

        // By Camroll: another name, the same roll and timecode.
        model::BinItem roll = clipLike(QStringLiteral("Roll copy"), QStringLiteral("01:00:00:00"));
        roll.fields.insert(QStringLiteral("Camroll"), QStringLiteral("R7"));
        roll.fields.insert(QStringLiteral("Source File"), QStringLiteral("Other.mov"));
        RelinkOptions r;
        QVERIFY(stays(r, {roll}, QStringLiteral("V1"))); // by source name: no match
        r.by = RelinkOptions::By::Column;
        r.column = QStringLiteral("Camroll");
        QCOMPARE(relinkedTo(r, {roll}, QStringLiteral("V1")), roll.id);
        r = RelinkOptions{};

        // By Aux TC1: the candidate's own Aux TC1 holds the material at 02:00:00:00, its Start elsewhere.
        model::BinItem aux = clipLike(QStringLiteral("Take 1"), QStringLiteral("05:00:00:00"));
        aux.fields.insert(QStringLiteral("Auxiliary TC1"), QStringLiteral("02:00:00:00"));
        r.by = RelinkOptions::By::Name;
        QVERIFY(stays(r, {aux}, QStringLiteral("V1")));
        r.timecode = QStringLiteral("Auxiliary TC1");
        QCOMPARE(relinkedTo(r, {aux}, QStringLiteral("V1")), aux.id);
        r.timecode.clear();

        // Allow relinking to offline items: a clip without media only then.
        model::BinItem offline = clipLike(QStringLiteral("Take 1"), QStringLiteral("01:00:00:00"));
        offline.link = {};
        QVERIFY(stays(r, {offline}, QStringLiteral("V1")));
        r.offlineItems = true;
        QCOMPARE(relinkedTo(r, {offline}, QStringLiteral("V1")), offline.id);
        r.offlineItems = false;

        // Relink to: an HD clip is not an SD format; the project's format is its raster and rate.
        model::BinItem hd = clipLike(QStringLiteral("Take 1"), QStringLiteral("01:00:00:00"));
        r.format = RelinkOptions::Format::Sd;
        QVERIFY(stays(r, {hd}, QStringLiteral("V1")));
        r.format = RelinkOptions::Format::Hd;
        QCOMPARE(relinkedTo(r, {hd}, QStringLiteral("V1")), hd.id);
        r.format = RelinkOptions::Format::Project;
        r.projectRaster = QSize(1920, 1080);
        r.projectRate = Rational(25);
        QCOMPARE(relinkedTo(r, {hd}, QStringLiteral("V1")), hd.id);
        r.projectRate = Rational(30000, 1001);
        QVERIFY(stays(r, {hd}, QStringLiteral("V1")));
        r.format = RelinkOptions::Format::Any;

        // Tracks to Relink: without audio, A1 keeps its clip while V1 moves.
        r.relinkAudio = false;
        QCOMPARE(relinkedTo(r, {hd}, QStringLiteral("V1")), hd.id);
        QCOMPARE(relinkedTo(r, {hd}, QStringLiteral("A1")), QStringLiteral("unchanged"));
        r.relinkAudio = true;
        r.relinkVideo = false;
        QCOMPARE(relinkedTo(r, {hd}, QStringLiteral("A1")), hd.id);
        QCOMPARE(relinkedTo(r, {hd}, QStringLiteral("V1")), QStringLiteral("unchanged"));
    }

    void consolidateSequences() {
        // A sequence keeps only the parts of its clips it uses, with handles,
        // in new ".new.01" master clips; its segments use them (p.349).
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Wide.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(source, s, 40));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        QCOMPARE(defaultHandles(Rational(25)), std::int64_t{50});
        QCOMPARE(defaultHandles(Rational(30000, 1001)), std::int64_t{60});
        QCOMPARE(defaultHandles(Rational(24000, 1001)), std::int64_t{24});
        QCOMPARE(numberedName(QStringLiteral("Wide"), QStringLiteral(".new"), {QStringLiteral("Wide.new.01")}),
                 QStringLiteral("Wide.new.02"));

        model::Sequence sequence = model::Sequence::withDefaultTracks();
        model::Segment a;
        a.clipId = clip->id;
        a.binId = QStringLiteral("bin1");
        a.name = clip->name;
        a.sourceTrack = QStringLiteral("V1");
        a.sourceIn = 10;
        a.length = 5;
        model::Segment b = a;
        b.sourceIn = 20;
        b.length = 4;
        sequence.track(QStringLiteral("V1"))->segments = {a, b};
        model::BinItem seq;
        seq.id = model::newItemId();
        seq.kind = model::BinItemKind::Sequence;
        seq.name = QStringLiteral("Cut 1");
        seq.editRate = Rational(25);
        sequence.storeIn(seq);

        ConsolidateOptions options;
        options.mode = ConsolidateOptions::Mode::Transcode;
        options.handles = 3;
        options.creation.raster = QSize(256, 144);
        options.creation.resolution = *findResolution(QStringLiteral("dnx_lb"));
        options.creation.videoDrive = dir.path();
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip->id) return *clip;
            return std::nullopt;
        };
        // Consolidate cannot copy FFV1.
        options.mode = ConsolidateOptions::Mode::Consolidate;
        ConsolidateResult r = consolidate({seq}, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.skipped.size(), 1);

        options.mode = ConsolidateOptions::Mode::Transcode;
        r = consolidate({seq}, QStringLiteral("bin1"), lookup, {clip->name}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.added.size(), 1);
        const model::BinItem made = r.added.first();
        QCOMPARE(made.name, QStringLiteral("Wide.new.01"));
        // Frames 7 to 27: 10-24 used, 3 frames either side.
        QCOMPARE(made.durationFrames, std::int64_t{20});
        QCOMPARE(r.changed.size(), 1);
        const model::Sequence after = model::Sequence::fromItem(r.changed.first());
        const auto& segs = after.track(QStringLiteral("V1"))->segments;
        QCOMPARE(segs.size(), 2);
        QCOMPARE(segs.at(0).clipId, made.id);
        QCOMPARE(segs.at(0).sourceIn, std::int64_t{3});
        QCOMPARE(segs.at(1).sourceIn, std::int64_t{13});
        FrameCache cache;
        QVERIFY(near(cache.frame(made, segs.at(1).sourceIn), colourOf(20), 14));

        // Create new sequence(s): the old one is left alone.
        options.newSequences = true;
        r = consolidate({seq}, QStringLiteral("bin1"), lookup, {clip->name, made.name}, options);
        QCOMPARE(r.changed.size(), 0);
        QCOMPARE(r.added.size(), 2);
        QCOMPARE(r.added.at(0).name, QStringLiteral("Wide.new.02"));
        QCOMPARE(r.added.at(1).name, QStringLiteral("Cut 1.new.01"));
        QCOMPARE(r.added.at(1).kind, model::BinItemKind::Sequence);

        // A master clip consolidated, keeping its media: ".old" keeps the
        // old, the clip takes the copy.
        const model::BinItem managed = made;
        options.keepMastersLinked = false;
        options.mode = ConsolidateOptions::Mode::Consolidate;
        const QString other = dir.filePath(QStringLiteral("Other"));
        QVERIFY(QDir().mkpath(other));
        options.creation.videoDrive = other;
        r = consolidate({managed}, QStringLiteral("bin1"), lookup, {managed.name}, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.changed.size(), 1);
        QCOMPARE(r.added.size(), 1);
        QCOMPARE(r.added.first().name, QStringLiteral("Wide.new.01.old.01"));
        QCOMPARE(r.added.first().link, managed.link);
        QVERIFY(linkOf(r.changed.first())->path.startsWith(other));
        // Skip media already on the target drive.
        options.skipOnTarget = true;
        r = consolidate(r.changed, QStringLiteral("bin1"), lookup, {}, options);
        QVERIFY(r.added.isEmpty());
        QCOMPARE(r.skipped.size(), 1);
    }

    void relinkManagedMedia() {
        // Relink > Managed Media (p.361-364): clips find media of the same
        // source and timecode, by the relink method.
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("Dock.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        s.audioCodec = QStringLiteral("pcm_s16le");
        s.channels = 1;
        s.startTimecode = QStringLiteral("10:00:00:00");
        {
            MediaWriter w;
            QVERIFY(w.open(source, s));
            for (int i = 0; i < 20; ++i) {
                // Each frame's sound is its number, to tell where it starts.
                const std::vector<float> level(1920, static_cast<float>(i) / 40.0f);
                QVERIFY(w.writeVideo(picture(i, 64, 36)));
                QVERIFY(w.writeAudio(level.data(), 1920));
            }
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        MediaCreation c;
        c.raster = QSize(256, 144);
        c.videoDrive = dir.path();
        c.project = QStringLiteral("Harbor");
        c.resolution = *findResolution(QStringLiteral("dnx_lb"));
        const auto whole = transcodeClip(*clip, 0, -1, c);
        QVERIFY(whole);
        c.resolution = *findResolution(QStringLiteral("dnx_sq"));
        const auto part = transcodeClip(*clip, 5, 15, c);
        QVERIFY(part);
        MediaDatabase db;
        db.scan({dir.path()});
        QCOMPARE(db.fileCount(), 4);
        QCOMPARE(db.sets().size(), 2);
        QCOMPARE(db.projects(), QStringList{"Harbor"});
        const auto info = db.set(linkOf(*part)->mediaId);
        QVERIFY(info);
        QCOMPARE(info->any().source, QStringLiteral("Dock.mov"));
        QCOMPARE(info->any().frames, std::int64_t{10});
        QCOMPARE(info->track(QStringLiteral("V1")).resolution, QStringLiteral("Avid DNx SQ"));
        QCOMPARE(info->any().start, QStringLiteral("10:00:00:05"));

        // The part's clip: Highest Quality keeps its own (SQ); Most
        // Compressed takes the whole LB media from frame 5.
        RelinkOptions r;
        auto link = findMedia(*part, db, r);
        QVERIFY(link);
        QCOMPARE(link->mediaId, linkOf(*part)->mediaId);
        r.method = RelinkOptions::Method::MostCompressed;
        link = findMedia(*part, db, r);
        QVERIFY(link);
        QCOMPARE(link->mediaId, linkOf(*whole)->mediaId);
        QCOMPARE(link->first, std::int64_t{5});
        model::BinItem relinked = *part;
        relinked.link = link->toJson();
        FrameCache cache;
        QVERIFY(near(cache.frame(relinked, 0), colourOf(5), 14));
        QVERIFY(near(cache.frame(relinked, 9), colourOf(14), 14));
        const auto pieces = audioPieces(relinked, {});
        QCOMPARE(pieces.size(), std::size_t(1));
        QCOMPARE(pieces.front().sourceStart, std::int64_t{5 * 1920});
        // Specific Resolution: nothing as good as HQ.
        r.method = RelinkOptions::Method::Specific;
        r.resolution = QStringLiteral("dnx_hq");
        QVERIFY(!findMedia(*part, db, r));
        r.quality = RelinkOptions::Quality::AtMost;
        link = findMedia(*part, db, r);
        QVERIFY(link);
        QCOMPARE(link->mediaId, linkOf(*part)->mediaId);
        // Another source name matches nothing; ignoring the extension, a
        // differently named file of the same name does.
        model::BinItem other = *part;
        other.fields.insert(QStringLiteral("Source File"), QStringLiteral("Dock.mxf"));
        r = RelinkOptions{};
        QVERIFY(!findMedia(other, db, r));
        r.ignoreExtension = true;
        QVERIFY(findMedia(other, db, r));
        r = RelinkOptions{};
        r.currentProjectOnly = true;
        r.project = QStringLiteral("Other");
        QVERIFY(!findMedia(*part, db, r));
        // The part's material in a 50 fps clip: 10:00:00:10 for 20 frames
        // is the 25 fps media's frames 5-14. Only with Allow relink across
        // rates (p.363), and then converted to the media's frames.
        model::BinItem fifty = *part;
        fifty.editRate = Rational(50);
        fifty.durationFrames = 20;
        fifty.fields.insert(QStringLiteral("Start"), QStringLiteral("10:00:00:10"));
        r = RelinkOptions{};
        r.method = RelinkOptions::Method::MostCompressed;
        QVERIFY(!findMedia(fifty, db, r));
        r.acrossRates = true;
        link = findMedia(fifty, db, r);
        QVERIFY(link);
        QCOMPARE(link->mediaId, linkOf(*whole)->mediaId);
        QCOMPARE(link->first, std::int64_t{5});
        // Past the media's end at the other rate: no match.
        fifty.durationFrames = 32;
        QVERIFY(!findMedia(fifty, db, r));

        // No match: the clip stays, or is unlinked.
        r = RelinkOptions{};
        RelinkResult result = relinkManaged({other}, db, {}, {}, r, {});
        QCOMPARE(result.notRelinked, QStringList{other.name});
        QVERIFY(result.changed.isEmpty());
        r.unlinkWithoutMatch = true;
        result = relinkManaged({other}, db, {}, {}, r, {});
        QCOMPARE(result.changed.size(), 1);
        QVERIFY(isUnlinked(result.changed.first()));

        // A sequence of the LB clip's frames 6-8 relinks to the SQ clip,
        // as a copy named ".relinked".
        model::Sequence sequence = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = whole->id;
        seg.binId = QStringLiteral("b");
        seg.sourceTrack = QStringLiteral("V1");
        seg.sourceIn = 6;
        seg.length = 3;
        sequence.track(QStringLiteral("V1"))->segments = {seg};
        model::BinItem cut;
        cut.id = model::newItemId();
        cut.kind = model::BinItemKind::Sequence;
        cut.name = QStringLiteral("Cut");
        cut.editRate = Rational(25);
        sequence.storeIn(cut);
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == whole->id) return *whole;
            if (id == part->id) return *part;
            return std::nullopt;
        };
        r = RelinkOptions{};
        result = relinkManaged({cut}, db, {{QStringLiteral("b"), *whole}, {QStringLiteral("b"), *part}}, lookup, r,
                               {QStringLiteral("Cut")});
        QCOMPARE(result.added.size(), 1);
        QCOMPARE(result.added.first().name, QStringLiteral("Cut.relinked"));
        const model::Sequence relinkedCut = model::Sequence::fromItem(result.added.first());
        const auto& segs = relinkedCut.track(QStringLiteral("V1"))->segments;
        QCOMPARE(segs.first().clipId, part->id);
        QCOMPARE(segs.first().sourceIn, std::int64_t{1});

        // Delete Media removes managed files only.
        cache.clear();
        const QStringList files = linkOf(*whole)->files();
        QCOMPARE(deleteMediaFiles(files + QStringList{source}, &db), files);
        QVERIFY(QFileInfo::exists(source));
        QCOMPARE(db.fileCount(), 2);
        QVERIFY(!cache.isOnline(*whole));
        // Files deleted behind its back are gone after the next scan.
        for (const QString& f : linkOf(*part)->files()) QVERIFY(QFile::remove(f));
        db.scan({dir.path()});
        QCOMPARE(db.fileCount(), 0);
    }

    void transcodeRoundTrips() {
        // ROADMAP Phase 2 gate 5: transcoding keeps the picture to each
        // resolution's expected PSNR in Y'CbCr, and the timecode and tape.
        QTemporaryDir dir;
        // A lossless 10-bit Y'CbCr source with detail: gradients, edges,
        // circles and fine noise.
        const QString source = dir.filePath(QStringLiteral("Chart.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("yuv422p10le");
        s.width = 1280;
        s.height = 720;
        s.frameRate = Rational(25);
        s.startTimecode = QStringLiteral("07:59:50:00");
        constexpr int kFrames = 4;
        // The same chart as 10-bit RGB, for DNx 444 without a change of
        // colour space.
        const QString rgbSource = dir.filePath(QStringLiteral("Chart RGB.mov"));
        WriterSettings rgb = s;
        rgb.pixelFormat = QStringLiteral("gbrp10le");
        {
            MediaWriter w;
            QVERIFY(w.open(source, s));
            MediaWriter wr;
            QVERIFY(wr.open(rgbSource, rgb));
            for (int f = 0; f < kFrames; ++f) {
                QImage image(1280, 720, QImage::Format_RGBA8888);
                QPainter p(&image);
                QLinearGradient g(0, 0, 1280, 720);
                g.setColorAt(0, QColor(30, 40, 200));
                g.setColorAt(0.5, QColor(220, 200, 40));
                g.setColorAt(1, QColor(20, 160, 60));
                p.fillRect(image.rect(), g);
                p.setPen(QPen(Qt::white, 3));
                for (int i = 0; i < 12; ++i) p.drawEllipse(QPoint(100 + i * 95 + f * 4, 360), 40 + i * 4, 40 + i * 4);
                // Hard edges, as titles have (no fonts in a test without a
                // GUI application).
                p.fillRect(QRect(0, 600, 1280, 60), QColor(16, 16, 16));
                for (int i = 0; i < 40; ++i) p.fillRect(QRect(20 + i * 31 + f, 610, 3 + i % 9, 40), QColor(235, 235, 235));
                p.end();
                quint32 seed = 1234u + static_cast<quint32>(f);
                for (int y = 0; y < 720; y += 2) {
                    auto* line = reinterpret_cast<quint8*>(image.scanLine(y));
                    for (int x = 0; x < 1280 * 4; ++x) {
                        if (x % 4 == 3) continue;
                        seed = seed * 1664525u + 1013904223u;
                        line[x] = static_cast<quint8>(std::clamp(static_cast<int>(line[x]) + static_cast<int>(seed >> 29) - 4, 0, 255));
                    }
                }
                QVERIFY(w.writeVideo(image));
                QVERIFY(wr.writeVideo(image));
            }
            QVERIFY(w.finish());
            QVERIFY(wr.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        auto clip = linkClip(*probe(source), o);
        QVERIFY(clip);
        clip->fields.insert(QStringLiteral("Tape"), QStringLiteral("A007"));
        // The PSNR each resolution keeps at least: ours, measured on this
        // chart (44.3, 48.6, 50.0, 52.9 and, RGB, 53.0 dB with FFmpeg 6.1)
        // less some 3-4 dB for other FFmpeg versions.
        // FFmpeg's arm64 build decodes DNxHR 444 with a less exact IDCT
        // (the same as x86's idct=int; no option changes it there): the
        // file is the same byte for byte, but its pictures come back at
        // about 42 dB on the Mac (BUILDING.md, Troubleshooting).
#if defined(Q_PROCESSOR_ARM_64)
        constexpr double k444 = 40.0;
#else
        constexpr double k444 = 49.0;
#endif
        const QList<QPair<QString, double>> expected{{QStringLiteral("dnx_lb"), 40.0},
                                                    {QStringLiteral("dnx_sq"), 45.0},
                                                    {QStringLiteral("dnx_hq"), 46.0},
                                                    {QStringLiteral("dnx_hqx"), 49.0},
                                                    {QStringLiteral("dnx_444"), k444}};
        double previous = 0;
        // RGB to DNx 444 first: the codec alone, without a change of colour
        // space.
        {
            auto rgbClip = linkClip(*probe(rgbSource), o);
            QVERIFY(rgbClip);
            MediaCreation c;
            c.raster = QSize(1280, 720);
            c.resolution = *findResolution(QStringLiteral("dnx_444"));
            c.videoDrive = dir.filePath(QStringLiteral("rgb"));
            QVERIFY(QDir().mkpath(c.videoDrive));
            QString error;
            const auto made = transcodeClip(*rgbClip, 0, -1, c, &error);
            QVERIFY2(made, qPrintable(error));
            VideoReader from;
            QVERIFY(from.open(rgbSource));
            VideoReader to;
            QVERIFY(to.open(linkOf(*made)->path));
            double worst = 1000;
            for (int f = 0; f < kFrames; ++f) {
                const auto psnr = comparePictures(from, f, to, f);
                QVERIFY(psnr);
                worst = std::min(worst, psnr->worst());
            }
            qInfo("dnx_444 from RGB: worst plane PSNR %.2f dB (at least 49.0)", worst);
            QVERIFY2(worst >= 49.0, qPrintable(QStringLiteral("dnx_444 from RGB: %1 dB").arg(worst)));
        }
        VideoReader original;
        QVERIFY(original.open(source));
        for (const auto& [id, minimum] : expected) {
            MediaCreation c;
            c.raster = QSize(1280, 720);
            c.resolution = *findResolution(id);
            c.videoDrive = dir.filePath(id);
            QVERIFY(QDir().mkpath(c.videoDrive));
            QString error;
            const auto made = transcodeClip(*clip, 0, -1, c, &error);
            QVERIFY2(made, qPrintable(error));
            const auto link = linkOf(*made);
            const auto info = probe(link->path);
            QVERIFY(info);
            QCOMPARE(info->startTimecode, QStringLiteral("07:59:50:00"));
            QCOMPARE(info->reelName, QStringLiteral("A007"));
            QCOMPARE(made->fields.value(QStringLiteral("Tape")), QStringLiteral("A007"));
            QCOMPARE(made->fields.value(QStringLiteral("Start")), QStringLiteral("07:59:50:00"));
            VideoReader copy;
            QVERIFY(copy.open(link->path));
            double worst = 1000;
            Psnr planes{1000, 1000, 1000};
            for (int f = 0; f < kFrames; ++f) {
                const auto psnr = comparePictures(original, f, copy, f);
                QVERIFY(psnr);
                worst = std::min(worst, psnr->worst());
                planes = {std::min(planes.y, psnr->y), std::min(planes.cb, psnr->cb), std::min(planes.cr, psnr->cr)};
            }
            qInfo("%s (%s): worst plane PSNR %.2f dB (at least %.1f); Y' %.2f, Cb %.2f, Cr %.2f", qPrintable(id),
                  qPrintable(copy.info().pixelFormat), worst, minimum, planes.y, planes.cb, planes.cr);
            QVERIFY2(worst >= minimum, qPrintable(QStringLiteral("%1: %2 dB").arg(id).arg(worst)));
            // Each Y'CbCr resolution up keeps more (444 is RGB: converting
            // from the 4:2:2 source costs its own rounding).
            if (!c.resolution.rgb) {
                QVERIFY2(worst > previous - 0.5,
                         qPrintable(QStringLiteral("%1 is no better than the one below").arg(id)));
                previous = worst;
            }
        }
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
        // DNx is allowed in every build (docs/spec/release-codecs.md); ProRes
        // decodes only with the patent codecs on.
        q.codec = "dnxhr";
        QVERIFY(codecs.front()->canDecode(q));
        q.codec = "prores";
        QCOMPARE(codecs.front()->canDecode(q), patentCodecsAllowed());
        q.codec = "no-such-codec";
        QVERIFY(!codecs.front()->canDecode(q));
    }

    void subclips() {
        // A subclip of a linked clip (p.407): "<master>.Sub.NN", linked to
        // its master by MobID, its media the master's from the IN point.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("RedSky.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        s.startTimecode = QStringLiteral("01:00:00:00");
        QVERIFY(writeVideo(path, s, 12));
        LinkOptions o;
        o.editRate = Rational(25);
        auto master = linkClip(*probe(path), o);
        QVERIFY(master);
        model::Bin bin;
        bin.items << *master;
        const QDateTime when = QDateTime::currentDateTimeUtc();
        const auto sub = makeSubclip(bin, *master, 3, 9, when);
        QVERIFY(sub);
        QCOMPARE(sub->kind, model::BinItemKind::Subclip);
        QCOMPARE(sub->name, QStringLiteral("RedSky.Sub.01"));
        QCOMPARE(sub->durationFrames, std::int64_t{6});
        QCOMPARE(sub->fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:03"));
        QCOMPARE(sub->fields.value(QStringLiteral("End")), QStringLiteral("01:00:00:09"));
        QCOMPARE(sub->sources.size(), 1);
        const model::ClipSource& from = sub->sources.first();
        QCOMPARE(from.kind, model::ClipSource::Kind::Clip);
        QCOMPARE(from.mob, model::mobIdOf(*master));
        QCOMPARE(from.offset, std::int64_t{3});
        QVERIFY(model::mobIdOf(*sub) != model::mobIdOf(*master));
        // Frame 0 of the subclip is the master's frame 3.
        const auto link = linkOf(*sub);
        QVERIFY(link);
        QCOMPARE(link->fileFrame(0, sub->editRate), std::int64_t{3});
        VideoReader reader;
        QVERIFY(reader.open(link->path));
        QVERIFY(near(reader.frame(link->fileFrame(2, sub->editRate)), colourOf(5), 14));
        // The next one is .Sub.02; none of an empty range.
        bin.items << *sub;
        QCOMPARE(makeSubclip(bin, *master, 0, 4, when)->name, QStringLiteral("RedSky.Sub.02"));
        QVERIFY(!makeSubclip(bin, *master, 7, 7, when));
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

    void imageSequences() {
        // Numbered stills link as one clip (p.325-326): one file brings its
        // run; several make a clip of each run among them.
        QTemporaryDir dir;
        auto still = [&](const QString& name, int n) {
            QImage image(32, 18, QImage::Format_RGB32);
            image.fill(colourOf(n));
            QVERIFY(image.save(dir.filePath(name), "PNG"));
        };
        for (int i = 1; i <= 10; ++i) still(QStringLiteral("Kermit_%1.png").arg(i, 4, 10, QLatin1Char('0')), i);
        for (int i = 1; i <= 3; ++i) still(QStringLiteral("Oscar.%1.png").arg(i, 3, 10, QLatin1Char('0')), i);
        still(QStringLiteral("Bert_01.png"), 1);
        still(QStringLiteral("Bert_03.png"), 3);
        const Rational rate(25);
        auto k = [&](int n) { return dir.filePath(QStringLiteral("Kermit_%1.png").arg(n, 4, 10, QLatin1Char('0'))); };

        QStringList one = groupStills({k(5)}, rate);
        QCOMPARE(one.size(), 1);
        auto seq = ImageSequence::fromSpec(one.first());
        QVERIFY(seq);
        QCOMPARE(seq->first, 1);
        QCOMPARE(seq->count, 10);
        QCOMPARE(seq->name(), QStringLiteral("Kermit"));
        QCOMPARE(seq->fileAt(9), k(10));

        const QStringList runs = groupStills({k(2), k(3), k(7), k(8), k(9)}, rate);
        QCOMPARE(runs.size(), 2);
        QCOMPARE(ImageSequence::fromSpec(runs.at(1))->first, 7);
        QCOMPARE(ImageSequence::fromSpec(runs.at(1))->count, 3);

        QStringList all;
        for (const QFileInfo& fi : QDir(dir.path()).entryInfoList(QDir::Files, QDir::Name)) all << fi.absoluteFilePath();
        const QStringList grouped = groupStills(all, rate);
        // Bert_01 and Bert_03 are not a run: two stills.
        QCOMPARE(grouped.size(), 4);
        QVERIFY(!isSequenceSpec(grouped.at(0)) && !isSequenceSpec(grouped.at(1)));
        QCOMPARE(ImageSequence::fromSpec(grouped.at(3))->name(), QStringLiteral("Oscar"));

        LinkOptions o;
        o.editRate = rate;
        const QList<model::BinItem> items = linkFiles({k(4)}, o);
        QCOMPARE(items.size(), 1);
        model::BinItem clip = items.first();
        QCOMPARE(clip.name, QStringLiteral("Kermit"));
        QCOMPARE(clip.durationFrames, std::int64_t{10});
        QCOMPARE(clip.fields.value(QStringLiteral("Source File")), QStringLiteral("Kermit_0001.png"));
        FrameCache cache;
        for (int n : {0, 6, 9}) QVERIFY2(near(cache.frame(clip, n), colourOf(n + 1), 0), qPrintable(QString::number(n)));

        // Unlink Media (p.368): offline until relinked.
        QVERIFY(unlinkClip(clip));
        QVERIFY(isUnlinked(clip));
        QVERIFY(!cache.isOnline(clip));
        QVERIFY(cache.frame(clip, 0).isNull());

        // Relink > Linked Media (p.364-365): the files moved to another
        // folder are found by name.
        QTemporaryDir moved;
        QDir().mkpath(moved.filePath(QStringLiteral("deep/er")));
        for (int i = 1; i <= 10; ++i) {
            QVERIFY(QFile::rename(k(i), moved.filePath(QStringLiteral("deep/er/Kermit_%1.png").arg(i, 4, 10, QLatin1Char('0')))));
        }
        const QHash<QString, QString> index = indexFolder(moved.path());
        const QString found = locateMedia(clip, index);
        QVERIFY(isSequenceSpec(found));
        QVERIFY(relinkClip(clip, found));
        QVERIFY(!isUnlinked(clip));
        QVERIFY(cache.isOnline(clip));
        QVERIFY(near(cache.frame(clip, 2), colourOf(3), 0));
        QCOMPARE(clip.fields.value(QStringLiteral("Source Path")), QFileInfo(moved.filePath(QStringLiteral("deep/er"))).absoluteFilePath());
        // Media that does not fit the clip is refused.
        QString error;
        QVERIFY(!relinkClip(clip, dir.filePath(QStringLiteral("Oscar.001.png")), &error));
        QVERIFY(!error.isEmpty());
    }

    void audioMixAndPlayer() {
        // Playback's mix (2.3): pieces of channels placed, panned and mixed
        // to stereo at 48 kHz; files at other rates are resampled.
        QTemporaryDir dir;
        auto tone = [&](const QString& name, int rate, int channels, auto value, int seconds) {
            WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = QStringLiteral("pcm_f32le");
            s.sampleRate = rate;
            s.channels = channels;
            MediaWriter w;
            QVERIFY(w.open(dir.filePath(name), s));
            std::vector<float> data;
            for (int i = 0; i < rate * seconds; ++i) {
                for (int c = 0; c < channels; ++c) data.push_back(value(c, i));
            }
            QVERIFY(w.writeAudio(data.data(), rate * seconds));
            QVERIFY(w.finish());
        };
        // Left channel counts up slowly, right is constant.
        tone(QStringLiteral("stereo.wav"), 48000, 2, [](int c, int i) { return c == 0 ? static_cast<float>(i % 1000) / 1000.0f : 0.25f; }, 1);
        tone(QStringLiteral("slow.wav"), 24000, 1, [](int, int) { return 0.5f; }, 1);

        float l = 0;
        float r = 0;
        panGains(-100, 0.0, &l, &r);
        QVERIFY(std::abs(l - 1.0f) < 1e-6f && std::abs(r) < 1e-6f);
        panGains(0, 0.0, &l, &r);
        QVERIFY(std::abs(l - r) < 1e-6f && std::abs(l - std::sqrt(0.5f)) < 1e-6f);
        panGains(100, -6.0, &l, &r);
        QVERIFY(std::abs(l) < 1e-6f && std::abs(r - 0.5012f) < 1e-3f);

        std::vector<AudioPiece> pieces;
        AudioPiece a;
        a.path = dir.filePath(QStringLiteral("stereo.wav"));
        a.channel = 0;
        a.sourceStart = 100;
        a.at = 1000;
        a.length = 2000;
        a.left = 1.0f;
        a.right = 0.0f;
        pieces.push_back(a);
        AudioPiece b = a;
        b.channel = 1;
        b.left = 0.0f;
        b.right = 1.0f;
        pieces.push_back(b);
        AudioPiece c;
        c.path = dir.filePath(QStringLiteral("slow.wav"));
        c.at = 5000;
        c.length = 4000;
        pieces.push_back(c);
        AudioMix mix(pieces);
        std::vector<float> out(2 * 6000);
        mix.render(0, 6000, out.data());
        QCOMPARE(out[2 * 999], 0.0f);                           // before the piece
        QVERIFY(std::abs(out[2 * 1000] - 0.1f) < 1e-6f);        // sample 100 of the left channel
        QVERIFY(std::abs(out[2 * 1500] - 0.6f) < 1e-6f);        // sample 600
        QVERIFY(std::abs(out[2 * 1500 + 1] - 0.25f) < 1e-6f);   // the right channel, panned right
        QCOMPARE(out[2 * 3000], 0.0f);                          // after it
        QVERIFY(std::abs(out[2 * 5500] - 0.5f) < 1e-4f);        // 24 kHz, resampled
        QVERIFY(std::abs(out[2 * 5500 + 1] - 0.5f) < 1e-4f);

        // The player renders ahead; what is taken is the clock.
        AudioPlayer player;
        AudioPlayer::PlayOptions quiet;
        quiet.device = false;
        QVERIFY(player.start(std::make_shared<AudioMix>(pieces), 1000, quiet));
        std::vector<float> got(2 * 600);
        int taken = 0;
        for (int tries = 0; tries < 500 && taken < 600; ++tries) {
            taken += player.pull(got.data() + 2 * taken, 600 - taken);
            if (taken < 600) QTest::qWait(2);
        }
        QCOMPARE(taken, 600);
        QCOMPARE(player.played(), std::int64_t{600});
        QVERIFY(std::abs(got[0] - 0.1f) < 1e-6f);
        QVERIFY(std::abs(got[2 * 500] - 0.6f) < 1e-6f);
        player.stop();
        QVERIFY(!player.isPlaying());
        auto take = [&](int count) {
            std::vector<float> buffer(2 * static_cast<std::size_t>(count));
            int n = 0;
            for (int tries = 0; tries < 500 && n < count; ++tries) {
                n += player.pull(buffer.data() + 2 * n, count - n);
                if (n < count) QTest::qWait(2);
            }
            return buffer;
        };
        // Twice normal speed (p.675): every other program sample, averaged:
        // left samples 100+2i and 101+2i of the ramp.
        AudioPlayer::PlayOptions fast = quiet;
        fast.speed = 2;
        QVERIFY(player.start(std::make_shared<AudioMix>(pieces), 1000, fast));
        got = take(300);
        QVERIFY(std::abs(got[0] - 0.1005f) < 1e-5f);
        QVERIFY(std::abs(got[2 * 200] - 0.5005f) < 1e-5f);
        player.stop();
        // Backwards from program sample 1600: samples 699, 698, ... of the
        // ramp.
        AudioPlayer::PlayOptions back = quiet;
        back.speed = -1;
        QVERIFY(player.start(std::make_shared<AudioMix>(pieces), 1600, back));
        got = take(300);
        QVERIFY(std::abs(got[0] - 0.699f) < 1e-6f);
        QVERIFY(std::abs(got[2 * 99] - 0.6f) < 1e-6f);
        player.stop();
        // A digital scrub's burst: 100 samples, then silence.
        AudioPlayer::PlayOptions burst = quiet;
        burst.limit = 100;
        QVERIFY(player.start(std::make_shared<AudioMix>(pieces), 1000, burst));
        got = take(300);
        QVERIFY(std::abs(got[2 * 99] - 0.199f) < 1e-6f);
        QCOMPARE(got[2 * 100], 0.0f);
        QCOMPARE(got[2 * 250], 0.0f);
        player.stop();
        // A sound card if there is one; none is fine too.
        if (player.start(std::make_shared<AudioMix>(pieces), 0)) {
            qInfo() << "audio output:" << player.deviceName();
            player.stop();
        }
    }

    void encodersOnTheirOwnThread() {
        // Media Foundation's encoders open only outside a single-threaded COM apartment, which Qt makes of the GUI
        // thread on Windows, so the writer calls them on a thread of its own (EncoderThread in media.cpp; the owner's
        // report, 2026-11-16). LUMENIS_ENCODER_THREAD=1 sends every encoder there, so this runs everywhere.
        qputenv("LUMENIS_ENCODER_THREAD", "1");
        const auto restore = qScopeGuard([] { qunsetenv("LUMENIS_ENCODER_THREAD"); });
        QTemporaryDir dir;
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("dnxhd");
        s.videoProfile = QStringLiteral("dnxhr_lb");
        s.width = 256;
        s.height = 144;
        s.frameRate = Rational(25);
        s.audioCodec = QStringLiteral("pcm_s16le");
        s.sampleRate = 48000;
        s.channels = 2;
        const QString path = dir.filePath(QStringLiteral("threaded.mov"));
        {
            MediaWriter w;
            QString error;
            QVERIFY2(w.open(path, s, &error), qPrintable(error));
            std::vector<float> tone(2 * 1920, 0.25f);
            for (int i = 0; i < 10; ++i) {
                QVERIFY2(w.writeVideo(picture(i, s.width, s.height)), qPrintable(w.lastError()));
                QVERIFY2(w.writeAudio(tone.data(), 1920), qPrintable(w.lastError()));
            }
            QVERIFY2(w.finish(&error), qPrintable(error));
        }
        VideoReader reader;
        QVERIFY(reader.open(path));
        QCOMPARE(reader.frameCount(), std::int64_t(10));
        QVERIFY(near(reader.frame(4), colourOf(4), 12));
        // A writer given up before finish() frees its encoders on their thread.
        {
            MediaWriter w;
            QVERIFY(w.open(dir.filePath(QStringLiteral("dropped.mov")), s));
            QVERIFY(w.writeVideo(picture(0, s.width, s.height)));
        }
        // And the start-up check of an encoder runs there too.
        QVERIFY(encoderAvailable(QStringLiteral("dnxhd")));

#if defined(Q_OS_WIN)
        // The real case: H.264 from Media Foundation written from a single-threaded apartment, as the GUI thread is.
        // A new thread, as GitHub's runners make the main thread multithreaded; skipped where Windows has no H.264
        // encoder (Windows Server, the runners).
        qunsetenv("LUMENIS_ENCODER_THREAD");
        if (osEncoderFor(QStringLiteral("h264")).isEmpty()) {
            qInfo() << "no Media Foundation H.264 here:" << osEncoderProblem(QStringLiteral("h264"));
            return;
        }
        bool written = false;
        QString why;
        std::thread sta([&] {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            WriterSettings h;
            h.format = QStringLiteral("mp4");
            h.videoCodec = QStringLiteral("h264_mf");
            h.width = 320;
            h.height = 180;
            h.frameRate = Rational(25);
            h.bitRate = 2000000;
            MediaWriter w;
            written = w.open(dir.filePath(QStringLiteral("sta.mp4")), h, &why);
            for (int i = 0; written && i < 25; ++i) written = w.writeVideo(picture(i, h.width, h.height));
            if (written) written = w.finish(&why);
            if (!written && why.isEmpty()) why = w.lastError();
            if (SUCCEEDED(hr)) CoUninitialize();
        });
        sta.join();
        QVERIFY2(written, qPrintable(why));
#endif
    }

    void soundLeavesComAlone() {
        // Qt's Windows platform plugin keeps COM (single-threaded) on the
        // main thread and releases WinRT objects through it as the
        // application goes. miniaudio before 0.11.25 called CoUninitialize
        // after its own CoInitializeEx had failed there, taking Qt's
        // reference: a crash on exit after any playback (BUILDING.md).
#if defined(Q_OS_WIN)
        // The main thread is usually single-threaded already (Qt's plugin);
        // on GitHub's Windows runners something has made it multithreaded
        // first (RPC_E_CHANGED_MODE), so take a reference to whichever
        // apartment it has: the check is the same.
        DWORD model = COINIT_APARTMENTTHREADED;
        HRESULT first = CoInitializeEx(nullptr, model);
        if (first == RPC_E_CHANGED_MODE) {
            model = COINIT_MULTITHREADED;
            first = CoInitializeEx(nullptr, model);
        }
        QVERIFY2(first == S_OK || first == S_FALSE, qPrintable(QString::number(quint32(first), 16)));
        {
            QTemporaryDir dir;
            WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = QStringLiteral("pcm_f32le");
            s.sampleRate = 48000;
            s.channels = 2;
            MediaWriter w;
            QVERIFY(w.open(dir.filePath(QStringLiteral("quiet.wav")), s));
            std::vector<float> silence(2 * 4800, 0.0f);
            QVERIFY(w.writeAudio(silence.data(), 4800));
            QVERIFY(w.finish());
            AudioPiece piece;
            piece.path = dir.filePath(QStringLiteral("quiet.wav"));
            piece.length = 4800;
            AudioPlayer player;
            // A sound card if there is one; opening none goes through the
            // same calls.
            if (player.start(std::make_shared<AudioMix>(std::vector<AudioPiece>{piece}), 0)) player.stop();
        }
        // Still initialised: this adds a reference (S_FALSE), not a new
        // apartment (S_OK).
        const HRESULT again = CoInitializeEx(nullptr, model);
        if (SUCCEEDED(again)) CoUninitialize();
        CoUninitialize();
        QCOMPARE(again, S_FALSE);
#else
        QSKIP("COM is Windows only");
#endif
    }

    void audioOfClipsAndSequences() {
        // What plays (p.395, p.627): a clip's channels as tracks, odd left
        // and even right; a sequence's monitored audio segments with their
        // gain and pan.
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("two.wav"));
        {
            WriterSettings s;
            s.format = QStringLiteral("wav");
            s.audioCodec = QStringLiteral("pcm_s16le");
            s.channels = 2;
            MediaWriter w;
            QVERIFY(w.open(wav, s));
            const std::vector<float> silence(2 * 48000, 0.0f);
            QVERIFY(w.writeAudio(silence.data(), 48000));
            QVERIFY(w.finish());
        }
        LinkOptions o;
        o.editRate = Rational(25);
        const QList<model::BinItem> linked = linkFiles({wav}, o);
        QCOMPARE(linked.size(), 1);
        const model::BinItem clip = linked.first();
        QCOMPARE(framesToSamples(25, Rational(25)), std::int64_t{48000});
        QCOMPARE(framesToSamples(1, Rational(30000, 1001)), std::int64_t{1601}); // 1601.6, down

        std::vector<AudioPiece> pieces = audioPieces(clip, {});
        QCOMPARE(pieces.size(), std::size_t{2});
        QCOMPARE(pieces[0].channel, 0);
        QCOMPARE(pieces[1].channel, 1);
        QCOMPARE(pieces[0].length, std::int64_t{48000});
        QVERIFY(pieces[0].left > 0.99f && pieces[0].right < 0.01f); // A1 left
        QVERIFY(pieces[1].right > 0.99f && pieces[1].left < 0.01f); // A2 right
        // Default Pan for mono tracks: Centered (p.1147).
        const std::vector<AudioPiece> centred = audioPieces(clip, {}, {}, false);
        QVERIFY(std::abs(centred[0].left - centred[0].right) < 1e-6f);
        QVERIFY(std::abs(centred[1].left - centred[1].right) < 1e-6f);
        // Only the monitored tracks play.
        pieces = audioPieces(clip, {}, [](const QString& t) { return t == QLatin1String("A2"); });
        QCOMPARE(pieces.size(), std::size_t{1});
        QCOMPARE(pieces[0].channel, 1);

        // A sequence: A1 holds the clip's A2 from frame 5 for 10 frames,
        // after 3 frames of filler, at -6 dB panned centre-left.
        model::Sequence seq = model::Sequence::withDefaultTracks();
        model::Segment seg;
        seg.clipId = clip.id;
        seg.binId = QStringLiteral("bin");
        seg.sourceTrack = QStringLiteral("A2");
        seg.sourceIn = 5;
        seg.length = 10;
        seg.gain = -6.0;
        seg.pan = -50;
        seq.splice(QStringLiteral("A1"), 0, {model::Segment::filler(3), seg});
        model::BinItem item;
        item.kind = model::BinItemKind::Sequence;
        item.editRate = Rational(25);
        seq.storeIn(item);
        const ClipLookup lookup = [&](const QString&, const QString& id) -> std::optional<model::BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        pieces = audioPieces(item, lookup);
        QCOMPARE(pieces.size(), std::size_t{1});
        QCOMPARE(pieces[0].channel, 1);
        QCOMPARE(pieces[0].at, framesToSamples(3, Rational(25)));
        QCOMPARE(pieces[0].length, framesToSamples(10, Rational(25)));
        QCOMPARE(pieces[0].sourceStart, framesToSamples(5, Rational(25)));
        float l = 0;
        float r = 0;
        panGains(-50, -6.0, &l, &r);
        QCOMPARE(pieces[0].left, l);
        QCOMPARE(pieces[0].right, r);
        // An unmonitored track is silent; an unlinked clip too.
        QVERIFY(audioPieces(item, lookup, [](const QString&) { return false; }).empty());
        model::BinItem offline = clip;
        QVERIFY(unlinkClip(offline));
        QVERIFY(audioPieces(offline, {}).empty());
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

        // Sound that says it has no channels, and a picture that cannot be
        // decoded: nothing to link (not a crash).
        MediaInfo info;
        info.path = QStringLiteral("/no/such/odd.mov");
        StreamInfo picture;
        picture.kind = StreamKind::Video;
        picture.frameRate = Rational(25);
        picture.decodable = false;
        StreamInfo sound;
        sound.index = 1;
        sound.kind = StreamKind::Audio;
        sound.sampleRate = 48000;
        sound.channels = 0;
        sound.decodable = true;
        info.streams = {picture, sound};
        error.clear();
        QVERIFY(!linkClip(info, LinkOptions{}, &error));
        QVERIFY(!error.isEmpty());
    }

    // A frame cache does not keep a file it could not open: once the file
    // is there, its frames are.
    void frameCacheRetriesMissingFiles() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Later.mov"));
        WriterSettings s;
        s.format = QStringLiteral("mov");
        s.videoCodec = QStringLiteral("ffv1");
        s.pixelFormat = QStringLiteral("bgr0");
        s.width = 64;
        s.height = 36;
        s.frameRate = Rational(25);
        QVERIFY(writeVideo(path, s, 4));
        LinkOptions o;
        o.editRate = Rational(25);
        const auto clip = linkClip(*probe(path), o);
        QVERIFY(clip);
        const QString away = dir.filePath(QStringLiteral("Away.mov"));
        QVERIFY(QFile::rename(path, away));
        FrameCache cache;
        QVERIFY(cache.frame(*clip, 1).isNull());
        QVERIFY(QFile::rename(away, path));
        QVERIFY(near(cache.frame(*clip, 1), colourOf(1), 14));
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"

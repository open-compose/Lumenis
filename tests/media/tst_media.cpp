#include "lumenis/ext/codec.h"
#include "lumenis/ext/provider.h"
#include "lumenis/media/link.h"
#include "lumenis/media/consolidate.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/playback.h"
#include "lumenis/media/relink.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QPainter>
#include <QTest>

#include <cmath>
#include <numbers>

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

    void managedMedia() {
        // Import/Transcode and Consolidate into Avid MediaFiles (p.348-356):
        // MXF OP-Atom, a file per track.
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
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

        // The media database finds the files where they are now.
        MediaDatabase db;
        db.scan({drive, drive2});
        QCOMPARE(db.fileCount(), 6);
        QVERIFY(QFileInfo::exists(QDir(folder).filePath(MediaDatabase::indexName())));
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

    void consolidateSequences() {
        // A sequence keeps only the parts of its clips it uses, with handles,
        // in new ".new.01" master clips; its segments use them (p.349).
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
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
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
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
    }

    void transcodeRoundTrips() {
        // ROADMAP Phase 2 gate 5: transcoding keeps the picture to each
        // resolution's expected PSNR in Y'CbCr, and the timecode and tape.
        if (!patentCodecsAllowed()) QSKIP("DNxHR is left out of this build (docs/EXTENSIONS.md)");
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

    void soundLeavesComAlone() {
        // Qt's Windows platform plugin keeps COM (single-threaded) on the
        // main thread and releases WinRT objects through it as the
        // application goes. miniaudio before 0.11.25 called CoUninitialize
        // after its own CoInitializeEx had failed there, taking Qt's
        // reference: a crash on exit after any playback (BUILDING.md).
#if defined(Q_OS_WIN)
        const HRESULT first = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        QVERIFY(first == S_OK || first == S_FALSE);
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
        const HRESULT again = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"

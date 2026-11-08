// Dynamic Media Folders' data and processing (Editing Guide p.371-376; 8.4b, docs/spec/background-services.md):
// folders and profiles through JSON, a folder's new entries, bin names, the AIS columns, and the work on one entry
// (link, Copy to Folder with Auto Relink, Transcode).
#include "lumenis/media/dynamic_media_folders.h"
#include "lumenis/media/media.h"
#include "lumenis/media/movie_export.h"
#include "lumenis/model/multichannel.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <vector>

using namespace lumenis;
using namespace lumenis::media;

namespace {

// A stereo 24-bit WAV of `seconds`, with a Broadcast WAV time reference when not negative.
bool writeWav(const QString& path, double seconds, std::int64_t timeReference = -1, int channels = 1) {
    WriterSettings s;
    s.format = QStringLiteral("wav");
    s.audioCodec = QStringLiteral("pcm_s24le");
    s.channels = channels;
    s.sampleRate = 48000;
    if (timeReference >= 0) {
        s.formatOptions = {{QStringLiteral("write_bext"), QStringLiteral("1")}};
        s.metadata = {{QStringLiteral("time_reference"), QString::number(timeReference)}};
    }
    MediaWriter w;
    if (!w.open(path, s)) return false;
    const auto n = static_cast<int>(48000 * seconds);
    std::vector<float> v(static_cast<std::size_t>(n * channels), 0.25f);
    return w.writeAudio(v.data(), n) && w.finish();
}

void touch(const QString& path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

} // namespace

class TestDmf : public QObject {
    Q_OBJECT

private slots:
    // Every field survives the file; unknown or missing values fall back to the defaults ("Create a new bin" is the
    // default, p.373).
    void dataRoundTrip() {
        DynamicMediaFolders d;
        DmfProfile p;
        p.name = QStringLiteral("Dailies");
        p.link.bin = DmfLinkSettings::Bin::Active;
        p.link.naming = DmfLinkSettings::Naming::Specify;
        p.link.binName = QStringLiteral("Day 1");
        p.link.multichannel = true;
        p.link.trackFormats = QStringLiteral("Stereo A1A2");
        p.link.reel = DmfLinkSettings::Reel::FolderName;
        p.link.reelLevel = 3;
        p.link.frameCount = DmfLinkSettings::FrameCount::FileName;
        p.link.timecode = DmfLinkSettings::Timecode::FileName;
        p.link.defaultFps = Rational(25, 1);
        DmfAction copy;
        copy.copyTo = QStringLiteral("/mnt/share/day1");
        copy.autoRelink = false;
        DmfAction transcode;
        transcode.kind = DmfAction::Kind::Transcode;
        transcode.resolution = QStringLiteral("dnx_lb");
        transcode.sampleRate = 48000;
        transcode.bitDepth = 16;
        p.actions = {copy, transcode};
        d.storeProfile(p);
        QVERIFY(d.addFolder(QStringLiteral("/media/card/")));
        QVERIFY(!d.addFolder(QStringLiteral("/media/card")));
        d.folder(QStringLiteral("/media/card"))->profile = p.name;
        d.folder(QStringLiteral("/media/card"))->enabled = true;
        d.folder(QStringLiteral("/media/card"))->done = {QStringLiteral("A001.mov")};

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("sub/Dynamic_Media_Folders.json"));
        QVERIFY(d.save(file));
        const DynamicMediaFolders back = DynamicMediaFolders::load(file);
        QCOMPARE(back.profiles.size(), 1);
        QVERIFY(back.profiles[0] == p);
        QCOMPARE(back.folders.size(), 1);
        QVERIFY(back.folders[0] == d.folders[0]);
        QCOMPARE(back.folders[0].path, QStringLiteral("/media/card"));

        // Storing a profile of the same name replaces it.
        p.actions.clear();
        d.storeProfile(p);
        QCOMPARE(d.profiles.size(), 1);
        QVERIFY(d.profile(p.name)->actions.isEmpty());
        d.removeFolder(QStringLiteral("/media/card"));
        QVERIFY(d.folders.isEmpty());

        const DmfLinkSettings defaults = DmfLinkSettings::fromJson({});
        QCOMPARE(defaults.bin, DmfLinkSettings::Bin::New);
        QCOMPARE(defaults.plugin, QStringLiteral("Autodetect"));
        QCOMPARE(DmfAction::kindName(DmfAction::Kind::CopyToFolder), QStringLiteral("Copy to Folder"));
        QCOMPARE(DynamicMediaFolders::load(dir.filePath(QStringLiteral("missing.json"))).folders.size(), 0);
    }

    // The folder's files and folders not yet done, without hidden ones; a folder's files at any depth.
    void entries() {
        QTemporaryDir dir;
        touch(dir.filePath(QStringLiteral("b.mov")));
        touch(dir.filePath(QStringLiteral("a.wav")));
        touch(dir.filePath(QStringLiteral(".DS_Store")));
        QDir(dir.path()).mkpath(QStringLiteral("Reel1/sub"));
        touch(dir.filePath(QStringLiteral("Reel1/sub/f2.dpx")));
        touch(dir.filePath(QStringLiteral("Reel1/f1.dpx")));
        DmfFolder f;
        f.path = dir.path();
        QCOMPARE(newDmfEntries(f), (QStringList{QStringLiteral("Reel1"), QStringLiteral("a.wav"), QStringLiteral("b.mov")}));
        f.done = {QStringLiteral("a.wav")};
        QCOMPARE(newDmfEntries(f), (QStringList{QStringLiteral("Reel1"), QStringLiteral("b.mov")}));
        const QStringList files = dmfEntryFiles(dir.filePath(QStringLiteral("Reel1")));
        QCOMPARE(files.size(), 2);
        QVERIFY(files[0].endsWith(QLatin1String("Reel1/f1.dpx")));
        QCOMPARE(dmfEntryFiles(dir.filePath(QStringLiteral("b.mov"))).size(), 1);
    }

    // Default: "<project> <n>", the first number free; Specify; Volume gives some name.
    void binNames() {
        DmfLinkSettings link;
        QCOMPARE(dmfBinName(link, QStringLiteral("/tmp"), QStringLiteral("Doc"), {QStringLiteral("Doc 1")}), QStringLiteral("Doc 2"));
        link.naming = DmfLinkSettings::Naming::Specify;
        link.binName = QStringLiteral("  Rushes ");
        QCOMPARE(dmfBinName(link, QStringLiteral("/tmp"), QStringLiteral("Doc"), {}), QStringLiteral("Rushes"));
        link.naming = DmfLinkSettings::Naming::Volume;
        QVERIFY(!dmfBinName(link, QDir::tempPath(), QStringLiteral("Doc"), {}).isEmpty());
    }

    void timecodeNames() {
        const Rational rate(25, 1);
        QCOMPARE(timecodeFromName(QStringLiteral("/x/A001_10_00_01_05.mov"), rate), std::optional<std::int64_t>(((10 * 3600) + 1) * 25 + 5));
        QCOMPARE(timecodeFromName(QStringLiteral("/x/T01-01000012.wav"), rate), std::optional<std::int64_t>(3600 * 25 + 12));
        QVERIFY(!timecodeFromName(QStringLiteral("/x/A001_0001000.dpx"), rate));
        // Frames past the rate are no timecode.
        QVERIFY(!timecodeFromName(QStringLiteral("/x/clip_00_00_00_30.mov"), rate));
    }

    // A folder of numbered DPX files is one clip; Labroll, Camroll and DPX come from its first file (p.373-374).
    void dpxFolderAsOneClip() {
        QTemporaryDir dir;
        const QString reel = dir.filePath(QStringLiteral("Roll_B/A001"));
        QVERIFY(QDir().mkpath(reel));
        QImage picture(64, 36, QImage::Format_RGB32);
        picture.fill(Qt::red);
        StillOptions o;
        o.format = StillFormat::Dpx;
        o.depth = 10;
        o.dpxInputDevice = QStringLiteral("Scanner 2");
        for (int i = 0; i < 3; ++i) {
            QVERIFY(writeStill(QStringLiteral("%1/A001_%2.dpx").arg(reel).arg(1000 + i, 7, 10, QLatin1Char('0')), picture, o));
        }
        QCOMPARE(dpxInputDevice(reel + QStringLiteral("/A001_0001000.dpx")), QStringLiteral("Scanner 2"));

        DmfProfile p;
        p.link.frameCount = DmfLinkSettings::FrameCount::FileName;
        DmfProcessOptions options;
        options.link.editRate = Rational(24, 1);
        DmfResult r = processDmfEntry(reel, p, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.clips.size(), 1);
        const model::BinItem& clip = r.clips[0];
        QCOMPARE(clip.durationFrames, std::int64_t{3});
        QCOMPARE(clip.fields.value(QStringLiteral("Labroll")), QStringLiteral("A001"));
        QCOMPARE(clip.fields.value(QStringLiteral("Camroll")), QStringLiteral("Scanner 2"));
        QCOMPARE(clip.fields.value(QStringLiteral("DPX")), QStringLiteral("A0010001000"));

        // The reel from the folder two up, the count from 1.
        p.link.reel = DmfLinkSettings::Reel::FolderName;
        p.link.reelLevel = 2;
        p.link.frameCount = DmfLinkSettings::FrameCount::FromOne;
        r = processDmfEntry(reel, p, options);
        QCOMPARE(r.clips.size(), 1);
        QCOMPARE(r.clips[0].fields.value(QStringLiteral("Labroll")), QStringLiteral("Roll_B"));
        QCOMPARE(r.clips[0].fields.value(QStringLiteral("DPX")), QStringLiteral("Roll_B0000001"));
    }

    // Start from the file name or the BWF time stamp; Multichannel's Track Formats; Copy to Folder with Auto Relink
    // leaves the clip on the copy.
    void copyAndRelink() {
        QTemporaryDir dir;
        const QString watched = dir.filePath(QStringLiteral("watched"));
        QVERIFY(QDir().mkpath(watched));
        const QString wav = watched + QStringLiteral("/Take_01_00_00_00.wav");
        QVERIFY(writeWav(wav, 1.0, 48000 * 10, 2));

        DmfProfile p;
        p.link.timecode = DmfLinkSettings::Timecode::FileName;
        p.link.multichannel = true;
        p.link.trackFormats = QStringLiteral("Stereo A1A2");
        DmfAction copy;
        copy.copyTo = dir.filePath(QStringLiteral("copies"));
        p.actions = {copy};
        DmfProcessOptions options;
        options.link.editRate = Rational(25, 1);
        DmfResult r = processDmfEntry(wav, p, options);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.clips.size(), 1);
        QCOMPARE(r.clips[0].fields.value(QStringLiteral("Start")), QStringLiteral("01:00:00:00"));
        QCOMPARE(model::trackFormatsText(model::trackFormatsOf(r.clips[0])), QStringLiteral("Stereo A1A2"));
        const QString copied = dir.filePath(QStringLiteral("copies/Take_01_00_00_00.wav"));
        QVERIFY(QFileInfo::exists(copied));
        QCOMPARE(linkOf(r.clips[0])->path, copied);

        // Without Auto Relink the clip stays on the original; the BWF time stamp (10 s) sets Start.
        p.link.timecode = DmfLinkSettings::Timecode::Embedded;
        p.link.audioStartTime = true;
        p.actions[0].autoRelink = false;
        r = processDmfEntry(wav, p, options);
        QCOMPARE(r.clips.size(), 1);
        QCOMPARE(linkOf(r.clips[0])->path, wav);
        QCOMPARE(r.clips[0].fields.value(QStringLiteral("Start")), QStringLiteral("00:00:10:00"));

        // A file that is not media is an error, nothing linked.
        touch(watched + QStringLiteral("/notes.txt"));
        r = processDmfEntry(watched + QStringLiteral("/notes.txt"), p, options);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.clips.isEmpty());
    }

    // Transcode makes a ".new" clip on the action's drive at its sample rate; the linked clip stays as it was.
    void transcode() {
        QTemporaryDir dir;
        const QString wav = dir.filePath(QStringLiteral("VO.wav"));
        QVERIFY(writeWav(wav, 0.5));
        DmfProfile p;
        DmfAction t;
        t.kind = DmfAction::Kind::Transcode;
        t.videoDrive = dir.filePath(QStringLiteral("drive"));
        QVERIFY(QDir().mkpath(t.videoDrive));
        t.bitDepth = 16;
        p.actions = {t};
        DmfProcessOptions options;
        options.link.editRate = Rational(25, 1);
        options.creation.raster = QSize(256, 144);
        int calls = 0;
        const DmfResult r = processDmfEntry(wav, p, options, [&](double f) {
            ++calls;
            return f >= 0 && f <= 1;
        });
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.clips.size(), 2);
        QCOMPARE(linkOf(r.clips[0])->path, wav);
        QVERIFY(r.clips[1].name.startsWith(QLatin1String("VO.new")));
        const auto made = linkOf(r.clips[1]);
        QVERIFY(made && made->isManaged());
        QVERIFY(made->audioFile(0).startsWith(t.videoDrive));
        QVERIFY(calls > 0);

        // Cancelled through progress: nothing comes back.
        const DmfResult stopped = processDmfEntry(wav, p, options, [](double) { return false; });
        QCOMPARE(stopped.error, QStringLiteral("Cancelled"));
        QVERIFY(stopped.clips.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDmf)
#include "tst_dmf.moc"

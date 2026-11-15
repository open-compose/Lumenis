// File > Output > Export to File as MOV and MP4 (7.1a; docs/spec/movie-export.md, Editing Guide p.868-869, p.893-897):
// a sequence from the Record monitor written as a movie with the Export setting's options, its picture and sound read
// back; Use Marks and the sequence's timecode; a master clip exported through a sequence of its own; Video Only.
// 7.1b: a Graphic still of the Record monitor's frame, the frames with markers named by their comments, a DPX image
// sequence and a WAVE file. 7.2a-b: XDCAM MXF OP1a, DPX's options, and Export to Device > XDCAM onto a disk seen as
// a folder; AS-11 without segments; an AS-02 Export Volume.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/media/as11.h"
#include "lumenis/media/dcp.h"
#include "lumenis/media/movie_export.h"
#include "lumenis/media/xdcam_disk.h"
#include "lumenis/model/export_volume.h"
#include "lumenis/model/marker.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/export_device_dialog.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/export_drag.h"
#include "lumenis/ui/export_volume_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// Frame n of the source movie: its own grey, so that frames can be told apart after the export.
QColor colourOf(int n) {
    return QColor(n * 20, n * 20, n * 20);
}

// A 10-frame 64 x 36 FFV1 movie at 25 fps with stereo sound at 0.25, from 01:00:00:00.
bool writeMovie(const QString& path) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 36;
    s.frameRate = Rational(25);
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.channels = 2;
    s.startTimecode = QStringLiteral("01:00:00:00");
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    const std::vector<float> sound(2 * 1920, 0.25f);
    for (int i = 0; i < 10; ++i) {
        QImage image(64, 36, QImage::Format_RGBA8888);
        image.fill(colourOf(i));
        if (!w.writeVideo(image) || !w.writeAudio(sound.data(), 1920)) return false;
    }
    return w.finish();
}

int greyAt(const QString& path, std::int64_t frame) {
    media::VideoReader reader;
    if (!reader.open(path)) return -1;
    const QImage image = reader.frame(frame);
    return image.isNull() ? -1 : qGray(image.pixel(image.width() / 2, image.height() / 2));
}

} // namespace

class TestMovieExportUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    // DCP (7.5, docs/spec/dcp.md): Export As DCP writes a package folder named for the file chosen, through
    // asdcp-wrap (a stand-in script here, POSIX shells only) and the DCP tab's choices.
    // Export from a monitor and by drag and drop (p.868-869; 11.4b): with no bin selection, Export to File takes the
    // active monitor's sequence and its marks; a bin drag, dropped outside the application, exports with the Export
    // setting into the General setting's Temporary File Directory and hands over the files; ALE cannot be dragged.
    void exportFromMonitorAndByDragging() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A006.mov"));
        QVERIFY(writeMovie(movie));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Reel"));
        project.settings.name = QStringLiteral("Reel");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.linkFiles(binId, {movie}).size(), 1);
        const QString clipId = bin->bin().items.first().id;
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), bin->bin().items.first());
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const QString sequenceName = composer.state(MonitorId::Record).current()->item.name;
        window.binContainer().saveAll();
        Settings& settings = window.settings();
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("mov"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("movieCompression"), QStringLiteral("dnxhr_lb"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("useMarks"), true);

        // From the Record monitor, its IN and OUT: frames 2 to 5.
        window.binContainer().viewFor(bin)->selectIds({});
        composer.setActiveMonitor(MonitorId::Record);
        composer.state(MonitorId::Record).setPosition(2);
        composer.state(MonitorId::Record).markIn();
        composer.state(MonitorId::Record).setPosition(5);
        composer.state(MonitorId::Record).markOut();
        QString offered;
        window.setExportHook([&](ExportAsDialog& dialog) {
            offered = QFileInfo(dialog.path()).completeBaseName();
            dialog.setPath(dir.filePath(QStringLiteral("FromMonitor.mov")));
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QCOMPARE(offered, sequenceName);
        auto frames = [](const QString& path) -> std::int64_t {
            const auto info = media::probe(path);
            if (!info) return -1;
            for (const media::StreamInfo& st : info->streams) {
                if (st.width > 0) return st.length;
            }
            return -1;
        };
        QCOMPARE(frames(dir.filePath(QStringLiteral("FromMonitor.mov"))), std::int64_t{4});
        // With a clip selected in the bin, clicking the monitor exports the monitor's sequence; a new bin selection after
        // the click, the bin's.
        BinView* view = window.binContainer().viewFor(bin);
        view->selectIds({clipId});
        emit composer.monitorClicked(MonitorId::Record);
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QCOMPARE(offered, sequenceName);
        const QString second = dir.filePath(QStringLiteral("A007.mov"));
        QVERIFY(writeMovie(second));
        const QStringList secondIds = window.linkFiles(binId, {second});
        QCOMPARE(secondIds.size(), 1);
        view->selectIds(secondIds);
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QCOMPARE(offered, bin->bin().item(secondIds.first())->name);

        // Dragged from the bin and dropped on a folder: the clip, whole (no marks of its own), in the temporary folder.
        const QString temp = dir.filePath(QStringLiteral("Temp"));
        settings.setValue(QStringLiteral("General"), QStringLiteral("tempDirectory"), temp);
        ExportDragData drag(binId, {clipId});
        QVERIFY(drag.hasFormat(QStringLiteral("text/uri-list")));
        const QList<QUrl> files = drag.exportedFiles();
        QCOMPARE(files.size(), 1);
        const QString made = files.first().toLocalFile();
        QVERIFY2(made.startsWith(QDir(temp).absolutePath()), qPrintable(made));
        QCOMPARE(QFileInfo(made).suffix(), QStringLiteral("mov"));
        QCOMPARE(frames(made), std::int64_t{10});
        QCOMPARE(drag.exportedFiles(), files); // made once

        // "You cannot use the drag-and-drop method to export ALE" (p.868).
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("ale"));
        ExportDragData log(binId, {clipId});
        QVERIFY(log.exportedFiles().isEmpty());
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("aaf"));
    }

    void exportDcp() {
#ifdef Q_OS_WIN
        QSKIP("the stand-in asdcp-wrap is a POSIX shell script");
#else
        if (!media::dcpEncoderAvailable()) QSKIP("FFmpeg has no OpenJPEG encoder (BUILDING.md, \"DCP\")");
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A004.mov"));
        QVERIFY(writeMovie(movie));
        const QString stub = dir.filePath(QStringLiteral("asdcp-wrap"));
        {
            QFile f(stub);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\nfor out; do :; done\necho \"$@\" > \"$out.args\"\necho MXF > \"$out\"\n");
            f.close();
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
        }
        qputenv("LUMENIS_ASDCP_WRAP", stub.toLocal8Bit());
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Reel"));
        project.settings.name = QStringLiteral("Reel");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.linkFiles(binId, {movie}).size(), 1);
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), bin->bin().items.first());
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const QString sequenceId = composer.state(MonitorId::Record).current()->item.id;
        window.binContainer().saveAll();
        Settings& settings = window.settings();
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("dcp"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dcpTitle"), QStringLiteral("Reel One"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dcpContainer"), QStringLiteral("full"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dcpSound"), QStringLiteral("stereo"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("useMarks"), false);
        window.binContainer().viewFor(bin)->selectIds({sequenceId});
        window.setExportHook([&](ExportAsDialog& dialog) {
            dialog.setPath(dir.filePath(QStringLiteral("ReelOne.dcp")));
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        qunsetenv("LUMENIS_ASDCP_WRAP");
        const QDir package(dir.filePath(QStringLiteral("ReelOne")));
        QVERIFY(package.exists(QStringLiteral("ASSETMAP.xml")));
        const QStringList cpls = package.entryList({QStringLiteral("cpl_*.xml")});
        QCOMPARE(cpls.size(), 1);
        QFile cpl(package.filePath(cpls.first()));
        QVERIFY(cpl.open(QIODevice::ReadOnly));
        const QByteArray xml = cpl.readAll();
        QVERIFY(xml.contains("<ContentTitleText>Reel One</ContentTitleText>"));
        QVERIFY(xml.contains("<EditRate>25 1</EditRate>"));
        QVERIFY(xml.contains("<ScreenAspectRatio>2048 1080</ScreenAspectRatio>"));
        QVERIFY(xml.contains("<IntrinsicDuration>10</IntrinsicDuration>"));
        // Stereo: no 5.1 channel label for the sound.
        QFile args(package.filePath(QStringLiteral("sound.mxf.args")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        QVERIFY(!args.readAll().contains("5.1"));
#endif
    }

    void exportSequenceAndClip() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("A003.mov"));
        QVERIFY(writeMovie(movie));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Harbor"));
        project.settings.name = QStringLiteral("Harbor");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 256;
        project.settings.format.height = 144;
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);
        QCOMPARE(window.linkFiles(binId, {movie}).size(), 1);
        const model::BinItem clip = bin->bin().items.first();
        // Frames 2 to 5 of the clip spliced into a new sequence.
        Composer& composer = window.composer();
        composer.loadDefault(bin->nodeId(), clip);
        composer.state(MonitorId::Source).setPosition(2);
        composer.state(MonitorId::Source).markIn();
        composer.state(MonitorId::Source).setPosition(5);
        composer.state(MonitorId::Source).markOut();
        QVERIFY(window.timeline().perform(QStringLiteral("edit.splice_in")));
        const model::LoadedClip* record = composer.state(MonitorId::Record).current();
        QVERIFY(record);
        const QString sequenceId = record->item.id;
        window.binContainer().saveAll();

        Settings& settings = window.settings();
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("mov"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("movieCompression"), QStringLiteral("png"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("useMarks"), false);
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({sequenceId});
        const QString out = dir.filePath(QStringLiteral("Cut.mov"));
        QString proposed;
        window.setExportHook([&](ExportAsDialog& dialog) {
            proposed = dialog.pathEdit()->text();
            dialog.setPath(out);
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY2(proposed.endsWith(QLatin1String(".mov")), qPrintable(proposed));
        auto info = media::probe(out);
        QVERIFY(info);
        // The four frames of the sequence, the sequence's timecode, its stereo sound.
        QCOMPARE(info->firstVideo()->codec, QStringLiteral("png"));
        QCOMPARE(info->firstVideo()->length, 4);
        QCOMPARE(info->firstVideo()->width, 256);
        QCOMPARE(greyAt(out, 0), colourOf(2).red());
        QCOMPARE(greyAt(out, 3), colourOf(5).red());
        QVERIFY(!info->startTimecode.isEmpty());
        QCOMPARE(info->audio().size(), std::size_t(1));
        QCOMPARE(info->audio().front()->channels, 2);
        media::AudioReader sound;
        QVERIFY(sound.open(out));
        const std::vector<float> samples = sound.samples(0, 1920);
        QVERIFY(!samples.empty());
        QVERIFY(std::abs(samples[1000]) > 0.05f);

        // Use Marks: the Record monitor's IN and OUT (p.868).
        settings.setValue(QStringLiteral("Export"), QStringLiteral("useMarks"), true);
        composer.state(MonitorId::Record).setPosition(1);
        composer.state(MonitorId::Record).markIn();
        composer.state(MonitorId::Record).setPosition(2);
        composer.state(MonitorId::Record).markOut();
        const QString marked = dir.filePath(QStringLiteral("Marked.mov"));
        window.setExportHook([&](ExportAsDialog& dialog) {
            dialog.setPath(marked);
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        info = media::probe(marked);
        QVERIFY(info);
        QCOMPARE(info->firstVideo()->length, 2);
        QCOMPARE(greyAt(marked, 0), colourOf(3).red());

        // A master clip through a sequence of its own, at its own timecode; Video only.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("useMarks"), false);
        settings.setValue(QStringLiteral("Export"), QStringLiteral("movieTracks"), QStringLiteral("video"));
        view->selectIds({clip.id});
        const QString whole = dir.filePath(QStringLiteral("Clip.mov"));
        window.setExportHook([&](ExportAsDialog& dialog) {
            dialog.setPath(whole);
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        info = media::probe(whole);
        QVERIFY(info);
        QCOMPARE(info->firstVideo()->length, 10);
        QVERIFY(info->audio().empty());
        QCOMPARE(info->startTimecode, QStringLiteral("01:00:00:00"));
        QCOMPARE(greyAt(whole, 7), colourOf(7).red());

        // An MP4 cannot hold PNG: nothing is written and the Console says why.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("mp4"));
        const QString mp4 = dir.filePath(QStringLiteral("Clip.mp4"));
        window.setExportHook([&](ExportAsDialog& dialog) {
            dialog.setPath(mp4);
            return true;
        });
        commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        QVERIFY(!QFileInfo::exists(mp4));

        auto exportTo = [&](const QString& to) {
            window.setExportHook([to](ExportAsDialog& dialog) {
                dialog.setPath(to);
                return true;
            });
            commands.action(QStringLiteral("file.output.export_to_file"))->trigger();
        };
        // Graphic without Sequential Files: the frame at the Record monitor's position (p.868).
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("graphic"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("graphicFormat"), QStringLiteral("png"));
        view->selectIds({sequenceId});
        composer.state(MonitorId::Record).clearBoth();
        composer.state(MonitorId::Record).setPosition(2);
        const QString still = dir.filePath(QStringLiteral("Frame.png"));
        exportTo(still);
        QCOMPARE(greyAt(still, 0), colourOf(4).red());

        // Markers only, named by their comments (p.1186): the clip's two markers.
        model::BinItem marked2 = *bin->bin().item(clip.id);
        model::Marker wide;
        wide.frame = 2;
        wide.comment = QStringLiteral("Wide");
        model::Marker close;
        close.frame = 6;
        close.comment = QStringLiteral("Close");
        model::setMarkers(marked2, {wide, close});
        QVERIFY(bin->replaceItem(marked2));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("markersOnly"), true);
        settings.setValue(QStringLiteral("Export"), QStringLiteral("useComments"), true);
        view->selectIds({clip.id});
        exportTo(dir.filePath(QStringLiteral("Markers.png")));
        QCOMPARE(greyAt(dir.filePath(QStringLiteral("Wide.png")), 0), colourOf(2).red());
        QCOMPARE(greyAt(dir.filePath(QStringLiteral("Close.png")), 0), colourOf(6).red());
        settings.setValue(QStringLiteral("Export"), QStringLiteral("markersOnly"), false);

        // DPX: the sequence's four frames as numbered files (ours).
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("dpx"));
        view->selectIds({sequenceId});
        exportTo(dir.filePath(QStringLiteral("Cut.dpx")));
        for (int i = 0; i < 4; ++i) QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Cut.%1.dpx").arg(i, 7, 10, QLatin1Char('0')))));
        QCOMPARE(greyAt(dir.filePath(QStringLiteral("Cut.0000003.dpx")), 0), colourOf(5).red());
        // In a subfolder, from Start Frame 20 with 3 digits (p.891).
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dpxSubfolder"), QStringLiteral("Reel 1"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dpxStartFrame"), 20);
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dpxDigits"), 3);
        exportTo(dir.filePath(QStringLiteral("Cut.dpx")));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("Reel 1/Cut.020.dpx"))));
        QCOMPARE(greyAt(dir.filePath(QStringLiteral("Reel 1/Cut.023.dpx")), 0), colourOf(5).red());
        settings.setValue(QStringLiteral("Export"), QStringLiteral("dpxSubfolder"), QString());

        // Audio: a WAVE file of the sequence's sound.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("audio"));
        const QString wav = dir.filePath(QStringLiteral("Cut.wav"));
        exportTo(wav);
        info = media::probe(wav);
        QVERIFY(info);
        QCOMPARE(info->audio().front()->channels, 2);
        QVERIFY(!info->firstVideo());

        // XDCAM MXF OP1a (p.886-887, p.1174): the Video Format XDCAM-50, the sequence's four frames and its stereo mix.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("xdcam"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("xdcamFormat"), QStringLiteral("xdcam_50"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("mxfBitDepth"), QStringLiteral("16"));
        const QString mxf = dir.filePath(QStringLiteral("Cut.mxf"));
        exportTo(mxf);
        info = media::probe(mxf);
        QVERIFY(info && info->firstVideo());
        QCOMPARE(info->firstVideo()->codec, QStringLiteral("mpeg2video"));
        QCOMPARE(info->firstVideo()->length, 4);
        QCOMPARE(info->audio().front()->codec, QStringLiteral("pcm_s16le"));
        QCOMPARE(info->audio().front()->channels, 2);

        // AS-11 (p.891-893) needs programme segments from spanned markers: none, no file.
        settings.setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("as11"));
        settings.setValue(QStringLiteral("Export"), QStringLiteral("mxfCompression"), QStringLiteral("xdcam_50"));
        const QString as11 = dir.filePath(QStringLiteral("Programme.mxf"));
        view->selectIds({sequenceId});
        exportTo(as11);
        QVERIFY(!QFileInfo::exists(as11));

        // Export to Device > P2 without a P2 device: the options dimmed, nothing exported (p.901).
        bool dimmed = false;
        window.setExportDeviceHook([&](ExportDeviceDialog& dialog) {
            dimmed = !dialog.hasTargets();
            return true;
        });
        QCOMPARE(window.exportToDevice(QStringLiteral("p2")), 0);
        QVERIFY(dimmed);

        // Export to Device > XDCAM (p.883-884): the Record monitor's sequence onto a disk seen as a folder, named
        // C0001.MXF, then C0002.MXF; the Video Format chosen in the dialog.
        providers.add(std::make_shared<media::XdcamDiskProvider>());
        const QString disk = dir.filePath(QStringLiteral("XDCAM Disk"));
        QVERIFY(QDir().mkpath(disk));
        media::setXdcamDiskFolders({disk});
        window.setExportDeviceHook([&](ExportDeviceDialog& dialog) {
            return dialog.hasTargets() && dialog.chooseTarget(QDir::toNativeSeparators(QFileInfo(disk).absoluteFilePath())) &&
                   dialog.chooseFormat(QStringLiteral("xdcam_35"));
        });
        QCOMPARE(window.exportToDevice(QStringLiteral("xdcam")), 1);
        QCOMPARE(window.exportToDevice(QStringLiteral("xdcam")), 1);
        info = media::probe(QDir(disk).filePath(QStringLiteral("Clip/C0002.MXF")));
        QVERIFY(info && info->firstVideo());
        QCOMPARE(info->firstVideo()->codec, QStringLiteral("mpeg2video"));
        QCOMPARE(info->firstVideo()->length, 4);
        QVERIFY(QFileInfo::exists(QDir(disk).filePath(QStringLiteral("Clip/C0001.MXF"))));
        QCOMPARE(media::nextXdcamClipName(disk), QStringLiteral("C0003.MXF"));

        // A disk whose clips have four audio tracks takes no two-track clip (p.884).
        const QString other = dir.filePath(QStringLiteral("Other Disk"));
        QVERIFY(QDir().mkpath(other + QStringLiteral("/Clip")));
        media::MovieSource four;
        four.frames = 2;
        four.raster = QSize(256, 144);
        four.picture = [](std::int64_t) {
            QImage black(256, 144, QImage::Format_RGB32);
            black.fill(Qt::black);
            return black;
        };
        four.sound = [](std::int64_t, int count, std::vector<std::vector<float>>& planes) {
            planes.assign(4, std::vector<float>(static_cast<std::size_t>(count), 0.0f));
        };
        media::MovieExportOptions o;
        o.container = media::MovieContainer::Xdcam;
        o.compression = QStringLiteral("xdcam_50");
        o.audioChannels = 4;
        QVERIFY(media::exportMovie(other + QStringLiteral("/Clip/C0009.MXF"), four, o));
        media::setXdcamDiskFolders({other});
        window.setExportDeviceHook([](ExportDeviceDialog& dialog) { return dialog.hasTargets() && dialog.target().audioChannels == 4; });
        QCOMPARE(window.exportToDevice(QStringLiteral("xdcam")), 0);
        QVERIFY(!QFileInfo::exists(other + QStringLiteral("/Clip/C0010.MXF")));
        media::setXdcamDiskFolders({});

        // File > New > Volume for Export (p.902-904): the Export Volume bin takes a copy of the sequence, Not Committed,
        // and refuses the clip; Properties changes the compression; Commit writes the bundle where bmxtranswrap is.
        window.setExportVolumeHook([&](ExportVolumeDialog& dialog) {
            dialog.setPath(dir.filePath(QStringLiteral("Volumes")));
            dialog.setName(QStringLiteral("Delivery"));
            return true;
        });
        const QString volumeBin = window.newVolumeForExport();
        QVERIFY(!volumeBin.isEmpty());
        BinDocument* volumeDoc = window.binContainer().document(volumeBin);
        QVERIFY(volumeDoc);
        const auto volume = model::exportVolumeOf(volumeDoc->bin());
        QVERIFY(volume);
        QCOMPARE(volume->type, QStringLiteral("AS-02"));
        QCOMPARE(volume->compression, QStringLiteral("j2k"));
        QCOMPARE(QDir::cleanPath(volume->folder()), QDir::cleanPath(dir.filePath(QStringLiteral("Volumes/Delivery"))));
        BinView* volumeView = window.binContainer().viewFor(volumeDoc);
        QVERIFY(volumeView && volumeView->findChild<QWidget*>(QStringLiteral("exportVolumeBar")));
        QVERIFY(volumeView->findChild<QWidget*>(QStringLiteral("exportVolumeBar"))->isVisibleTo(volumeView));
        QVERIFY(!view->findChild<QWidget*>(QStringLiteral("exportVolumeBar"))->isVisibleTo(view));
        const QStringList added = window.binContainer().transferItems(bin->nodeId(), {clip.id, sequenceId}, volumeBin, false);
        QCOMPARE(added.size(), 1);
        QVERIFY(bin->bin().item(sequenceId)); // a copy: the sequence stays
        const model::BinItem* copied = volumeDoc->bin().item(added.first());
        QVERIFY(copied && copied->kind == model::BinItemKind::Sequence);
        QCOMPARE(copied->fields.value(model::kMediaStatusHeading), model::kNotCommitted);
        const QString copiedName = copied->name;
        window.setExportVolumeHook([](ExportVolumeDialog& dialog) {
            dialog.setCompression(QStringLiteral("dnxhr_lb"));
            return true;
        });
        QVERIFY(window.editExportVolume(volumeBin));
        if (media::movieCompressionAvailable(*media::findMovieCompression(QStringLiteral("dnxhr_lb"))))
            QCOMPARE(model::exportVolumeOf(volumeDoc->bin())->compression, QStringLiteral("dnxhr_lb"));
        if (media::bmxTranswrapPath().isEmpty()) {
            QCOMPARE(window.commitExportVolume(volumeBin), 0);
            QCOMPARE(volumeDoc->bin().item(added.first())->fields.value(model::kMediaStatusHeading), model::kNotCommitted);
        } else {
            QCOMPARE(window.commitExportVolume(volumeBin), 1);
            QCOMPARE(volumeDoc->bin().item(added.first())->fields.value(model::kMediaStatusHeading), model::kCommitted);
            QVERIFY(QFileInfo(QDir(volume->folder()).filePath(copiedName)).isDir());
        }
    }
};

QTEST_MAIN(TestMovieExportUi)
#include "tst_movie_export_ui.moc"

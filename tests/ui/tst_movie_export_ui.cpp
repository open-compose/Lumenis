// File > Output > Export to File as MOV and MP4 (7.1a; docs/spec/movie-export.md, Editing Guide p.868-869, p.893-897):
// a sequence from the Record monitor written as a movie with the Export setting's options, its picture and sound read
// back; Use Marks and the sequence's timecode; a master clip exported through a sequence of its own; Video Only.
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/export_dialog.h"
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
    }
};

QTEST_MAIN(TestMovieExportUi)
#include "tst_movie_export_ui.moc"

// Reformatting and FrameFlex in the application (Editing Guide p.475-478, p.527-535; 8.5a, docs/spec/frameflex.md):
// linked clips take the Link setting's Reformat attribute, the Source monitor and edited segments show a clip's
// FrameFlex box, Refresh Sequence brings changed attributes into a sequence; and a failed Export to File says why
// (owner's report, 2026-11-08).
#include "lumenis/ext/provider.h"
#include "lumenis/media/media.h"
#include "lumenis/model/frame_flex.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/export_dialog.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"

#include <QAction>
#include <QDir>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

void settle() {
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
}

// A 64 x 32 movie, the left half red and the right half blue.
bool writeHalves(const QString& path, int frames) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = 64;
    s.height = 32;
    s.frameRate = Rational(25);
    media::MediaWriter w;
    if (!w.open(path, s)) return false;
    QImage image(64, 32, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    for (int y = 0; y < 32; ++y) {
        for (int x = 32; x < 64; ++x) image.setPixelColor(x, y, Qt::blue);
    }
    for (int i = 0; i < frames; ++i) {
        if (!w.writeVideo(image)) return false;
    }
    return w.finish();
}

bool isRed(QColor c) { return c.red() > 180 && c.blue() < 80; }
bool isBlue(QColor c) { return c.blue() > 180 && c.red() < 80; }
bool isBlack(QColor c) { return c.red() < 40 && c.green() < 40 && c.blue() < 40; }

} // namespace

class TestFrameFlexUi : public QObject {
    Q_OBJECT

private slots:
    void reformatAndFrameFlex() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("halves.mov"));
        QVERIFY(writeHalves(movie, 10));
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        project.settings.format.editRate = Rational(25);
        project.settings.format.width = 128;
        project.settings.format.height = 128;
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // "If you are linking to clips, then the default is set to Center Keep Size" (p.475): the 64 x 32 clip sits in
        // the middle of the 128 x 128 frame.
        QStringList ids = window.linkFiles(binId, {movie});
        QCOMPARE(ids.size(), 1);
        model::BinItem clip = *bin->bin().item(ids.first());
        QCOMPARE(model::cellText(clip, QLatin1String(model::kReformatHeading)), QStringLiteral("Center Keep Size"));
        Composer& composer = window.composer();
        composer.loadDefault(binId, clip);
        QImage shown = composer.pictureAt(MonitorId::Source, 0, QSize(128, 128));
        QVERIFY(isBlack(shown.pixelColor(5, 64)));
        QVERIFY(isRed(shown.pixelColor(40, 64)));
        QVERIFY(isBlue(shown.pixelColor(88, 64)));
        QVERIFY(isBlack(shown.pixelColor(64, 30)));

        // The Reformatting Options column changed in the bin: Stretch fills the frame.
        QVERIFY(bin->setField(clip.id, QLatin1String(model::kReformatHeading), QStringLiteral("Stretch")));
        clip = *bin->bin().item(clip.id);
        composer.loadDefault(binId, clip);
        shown = composer.pictureAt(MonitorId::Source, 0, QSize(128, 128));
        QVERIFY(isRed(shown.pixelColor(5, 5)));
        QVERIFY(isBlue(shown.pixelColor(122, 122)));

        // Edited into a sequence, the segment carries the adapter.
        composer.moveTo(MonitorId::Source, 0);
        commands.action(QStringLiteral("edit.splice_in"))->trigger();
        settle();
        QVERIFY(composer.state(MonitorId::Record).current());
        auto segment = [&] {
            return model::Sequence::fromItem(composer.state(MonitorId::Record).current()->item).track(QStringLiteral("V1"))->segments.at(0);
        };
        QVERIFY(segment().spatial);
        QCOMPARE(segment().spatial->reformat, model::Reformat::Stretch);

        // A FrameFlex box on the right half, set on the clip after the edit: the sequence keeps the old framing until
        // Refresh Sequence > Aspect Ratio and Reformatting Options (p.531).
        model::FrameFlex flex;
        flex.x = 16;
        flex.size = 0.5;
        flex.frameAspect = 1.0;
        model::setFrameFlex(clip, flex);
        bin->replaceItem(clip);
        shown = composer.pictureAt(MonitorId::Record, 0, QSize(128, 128));
        QVERIFY(isRed(shown.pixelColor(5, 64)));
        composer.setActiveMonitor(MonitorId::Record);
        commands.action(QStringLiteral("clip.refresh_sequence.aspect_ratio_reformatting"))->trigger();
        settle();
        QVERIFY(segment().spatial && segment().spatial->frameFlex);
        shown = composer.pictureAt(MonitorId::Record, 0, QSize(128, 128));
        QVERIFY(isBlue(shown.pixelColor(5, 64)));
        QVERIFY(isBlue(shown.pixelColor(122, 64)));
        // The Source monitor shows the clip's new framing too.
        composer.loadDefault(binId, clip);
        QVERIFY(isBlue(composer.pictureAt(MonitorId::Source, 0, QSize(128, 128)).pixelColor(5, 64)));
    }

    // Export to File's failure goes into a message, not only the Console (owner's report, 2026-11-08).
    void failedExportSaysWhy() {
        QTemporaryDir dir;
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Film"));
        project.settings.name = QStringLiteral("Film");
        QDir().mkpath(project.folder);
        window.setProject(project);
        settle();
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        // An empty sequence as a movie: "There is nothing to export."
        model::BinItem seq;
        seq.id = model::newItemId();
        seq.kind = model::BinItemKind::Sequence;
        seq.name = QStringLiteral("Empty");
        model::Sequence::withDefaultTracks().storeIn(seq);
        bin->addItem(seq);
        BinView* view = window.binContainer().viewFor(bin);
        QVERIFY(view);
        view->selectIds({seq.id});
        window.settings().setValue(QStringLiteral("Export"), QStringLiteral("exportAs"), QStringLiteral("mov"));
        window.setExportHook([&](ExportAsDialog& d) {
            d.setPath(dir.filePath(QStringLiteral("Empty.mov")));
            return true;
        });
        window.exportToFile();
        settle();
        auto* box = window.findChild<QMessageBox*>(QStringLiteral("exportFailed"));
        QVERIFY(box);
        QVERIFY2(box->informativeText().contains(QStringLiteral("Empty")), qPrintable(box->informativeText()));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("Empty.mov"))));
        box->close();
    }
};

QTEST_MAIN(TestFrameFlexUi)
#include "tst_frameflex_ui.moc"

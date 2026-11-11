// Graphics with alpha on import (9.2b; docs/spec/graphics-import.md; Editing Guide p.217-218, p.1196, p.1231): the fill
// and matte of a picture with alpha (Invert, Do not invert, Dilate Fill), which files have alpha, and in the application
// a PNG with alpha as a Matte Key clip keyed over what lies below, or as a master clip when the alpha is ignored, and PICT
// files with and without alpha.
#include "lumenis/ext/provider.h"
#include "lumenis/media/graphic_import.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/pict.h"
#include "lumenis/model/matte_key_clip.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

// Opaque red on the left half, transparent on the right.
QImage halfRed(QSize size = QSize(64, 36)) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width() / 2; ++x) image.setPixelColor(x, y, QColor(255, 0, 0, 255));
    }
    return image;
}

const model::BinItem* itemNamed(const BinDocument& doc, const QString& name, std::optional<model::BinItemKind> kind = {}) {
    for (const model::BinItem& i : doc.bin().items) {
        if (i.name == name && (!kind || i.kind == *kind)) return &i;
    }
    return nullptr;
}

} // namespace

class TestGraphicsImport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void pictures() {
        const QImage rgba = halfRed();
        // Invert on import: the file's alpha is white = opaque, as the matte keeps it.
        const QImage matte = media::mattePicture(rgba, media::AlphaHandling::Invert);
        QCOMPARE(QColor(matte.pixel(4, 4)), QColor(Qt::white));
        QCOMPARE(QColor(matte.pixel(60, 4)), QColor(Qt::black));
        // Do not invert: black = opaque in the file.
        const QImage kept = media::mattePicture(rgba, media::AlphaHandling::NoInvert);
        QCOMPARE(QColor(kept.pixel(4, 4)), QColor(Qt::black));
        QCOMPARE(QColor(kept.pixel(60, 4)), QColor(Qt::white));
        // The fill is opaque; Dilate Fill bleeds the edge's colour one pixel into the transparent side.
        const QImage plain = media::fillPicture(rgba, false);
        QCOMPARE(QColor(plain.pixel(4, 4)), QColor(Qt::red));
        QCOMPARE(QColor(plain.pixel(32, 4)), QColor(Qt::black));
        const QImage dilated = media::fillPicture(rgba, true);
        QCOMPARE(QColor(dilated.pixel(32, 4)), QColor(Qt::red));
        QCOMPARE(QColor(dilated.pixel(34, 4)), QColor(Qt::black));
        QCOMPARE(media::alphaHandlingOf(QStringLiteral("noInvert")), media::AlphaHandling::NoInvert);
        QCOMPARE(media::alphaHandlingOf(QStringLiteral("whatever")), media::AlphaHandling::Ignore);

        // Which files have alpha.
        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("Logo.png"));
        QVERIFY(rgba.save(png));
        const auto source = media::alphaSourceOf(png);
        QVERIFY(source);
        QCOMPARE(source->frames, std::int64_t(1));
        QCOMPARE(qAlpha(source->frame(0).pixel(60, 4)), 0);
        const QString jpg = dir.filePath(QStringLiteral("Logo.jpg"));
        QVERIFY(rgba.convertToFormat(QImage::Format_RGB32).save(jpg));
        QVERIFY(!media::alphaSourceOf(jpg));
        const QString pict = dir.filePath(QStringLiteral("Logo.pic"));
        QVERIFY(media::isPictFile(pict));
        QVERIFY(!media::isPictFile(png));
        QVERIFY(media::writePict(rgba, pict, true));
        QVERIFY(media::alphaSourceOf(pict));
        const QString opaquePict = dir.filePath(QStringLiteral("Plain.pict"));
        QVERIFY(media::writePict(rgba, opaquePict, false));
        QVERIFY(!media::alphaSourceOf(opaquePict));
        QVERIFY(media::pixelFormatHasAlpha(QStringLiteral("rgba")));
        QVERIFY(media::pixelFormatHasAlpha(QStringLiteral("yuva420p")));
        QVERIFY(!media::pixelFormatHasAlpha(QStringLiteral("yuv420p")));
        QVERIFY(!media::pixelFormatHasAlpha(QStringLiteral("nonsense")));
    }

    void importing() {
        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("Logo.png"));
        QVERIFY(halfRed().save(png));
        const QString pict = dir.filePath(QStringLiteral("Badge.pic"));
        QVERIFY(media::writePict(halfRed(), pict, true));
        const QString plainPict = dir.filePath(QStringLiteral("Plain.pic"));
        QVERIFY(media::writePict(halfRed(), plainPict, false));
        const QString drive = dir.filePath(QStringLiteral("Media"));
        QVERIFY(QDir().mkpath(drive));
        media::setExtraMediaDrives({drive});
        ext::ProviderRegistry providers;
        CommandRegistry commands;
        commands.loadBuiltin();
        MainWindow window(commands, providers);
        window.resize(1200, 760);
        window.show();
        window.settings().setUser(dir.filePath(QStringLiteral("Users")), QStringLiteral("ed"));
        model::ProjectEntry project;
        project.folder = dir.filePath(QStringLiteral("Graphics"));
        project.settings.name = QStringLiteral("Graphics");
        project.settings.format.editRate = Rational(25);
        QDir().mkpath(project.folder);
        window.setProject(project);
        QCoreApplication::processEvents();
        window.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("importVideoDrive"), drive);
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("frameImportDuration"), 2);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // Invert on import: the PNG is a Matte Key clip of two seconds, with its fill and matte precomputes.
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("alphaChannel"), QStringLiteral("invert"));
        QStringList ids = window.importFiles(binId, {png});
        QCOMPARE(ids.size(), 1);
        const model::BinItem key = *bin->bin().item(ids.first());
        QVERIFY(model::isMatteKeyClip(key));
        QCOMPARE(key.name, QStringLiteral("Logo"));
        QCOMPARE(key.durationFrames, std::int64_t(50));
        QCOMPARE(key.fields.value(QStringLiteral("Source File")), QStringLiteral("Logo.png"));
        const model::MatteKeyMedia media = model::matteKeyMediaOf(*model::matteKeyClipSegment(key)->effect);
        QCOMPARE(media.frames, std::int64_t(1));
        QCOMPARE(media.binId, binId);
        QVERIFY(bin->bin().item(media.fill) && bin->bin().item(media.matte));
        QCOMPARE(bin->bin().item(media.fill)->kind, model::BinItemKind::RenderedEffect);
        // In the Source monitor over black: the red half keyed, the transparent half shows what lies below.
        Composer& composer = window.composer();
        composer.loadDefault(binId, key);
        const QImage shown = composer.pictureAt(MonitorId::Source, 30, QSize(64, 36));
        QVERIFY(!shown.isNull());
        const QColor left(shown.pixel(8, 18)), right(shown.pixel(56, 18));
        QVERIFY2(left.red() > 180 && left.green() < 60, qPrintable(left.name()));
        QVERIFY2(right.red() < 40, qPrintable(right.name()));

        // The Frame view shows the fill.
        BinView* view = window.binContainer().activeView();
        QVERIFY(view);
        const QImage thumb = view->frameView()->picture(key.id, QSize(64, 36), true);
        QVERIFY(!thumb.isNull());
        QVERIFY(QColor(thumb.pixel(8, 18)).red() > 180);

        // Ignore: one opaque master clip.
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("alphaChannel"), QStringLiteral("ignore"));
        ids = window.importFiles(binId, {png});
        QCOMPARE(ids.size(), 1);
        QCOMPARE(bin->bin().item(ids.first())->kind, model::BinItemKind::MasterClip);
        QVERIFY(!model::isMatteKeyClip(*bin->bin().item(ids.first())));

        // PICT files, which FFmpeg does not read: with alpha a Matte Key clip, without a master clip.
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("alphaChannel"), QStringLiteral("invert"));
        ids = window.importFiles(binId, {pict, plainPict});
        QCOMPARE(ids.size(), 2);
        QVERIFY(model::isMatteKeyClip(*bin->bin().item(ids.at(0))));
        QCOMPARE(bin->bin().item(ids.at(0))->name, QStringLiteral("Badge"));
        const model::BinItem& plain = *bin->bin().item(ids.at(1));
        QCOMPARE(plain.kind, model::BinItemKind::MasterClip);
        QCOMPARE(plain.durationFrames, std::int64_t(50));
        QCOMPARE(plain.fields.value(QStringLiteral("Source File")), QStringLiteral("Plain.pic"));
        composer.loadDefault(binId, plain);
        const QImage plainShown = composer.pictureAt(MonitorId::Source, 10, QSize(64, 36));
        QVERIFY(QColor(plainShown.pixel(8, 18)).red() > 180);
        QVERIFY(itemNamed(*bin, QStringLiteral("Badge.fill"), model::BinItemKind::RenderedEffect));
        media::setExtraMediaDrives({});
    }

    // A movie with alpha is a Matte Key clip of its frames, without its sound (p.1196).
    void movieWithAlpha() {
        QTemporaryDir dir;
        const QString movie = dir.filePath(QStringLiteral("Bug.mov"));
        {
            media::WriterSettings s;
            s.format = QStringLiteral("mov");
            s.videoCodec = QStringLiteral("png");
            s.pixelFormat = QStringLiteral("rgba");
            s.width = 64;
            s.height = 36;
            s.frameRate = Rational(25);
            media::MediaWriter w;
            if (!w.open(movie, s)) QSKIP("no PNG encoder in this FFmpeg");
            for (int i = 0; i < 6; ++i) QVERIFY(w.writeVideo(halfRed()));
            QVERIFY(w.finish());
        }
        const auto source = media::alphaSourceOf(movie);
        QVERIFY(source);
        QCOMPARE(source->frames, std::int64_t(6));
        QCOMPARE(qAlpha(source->frame(2).pixel(60, 4)), 0);
        QCOMPARE(qAlpha(source->frame(2).pixel(4, 4)), 255);
    }
};

QTEST_MAIN(TestGraphicsImport)
#include "tst_graphics_import.moc"

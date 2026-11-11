// Multilayer Photoshop files on import (9.2e; docs/spec/graphics-import.md "Multilayer Photoshop files"; Editing Guide
// p.218-221): our reader of Photoshop's layers (media/psd.h), raw and PackBits channels, the colour modes it refuses;
// and the import's Sequence of Layers, Select Layers, Flattened Image and Cancel, with a master clip for the background,
// Matte Key clips with the layers' Opacity as their Foreground Level, hidden layers too, and the 24-track sequence.
#include "lumenis/ext/provider.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/media/psd.h"
#include "lumenis/model/matte_key_clip.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/theme.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace lumenis;
using namespace lumenis::ui;

namespace {

media::PsdLayer layer(const QString& name, QRect rect, QColor colour, bool transparency, int opacity = 255, bool visible = true) {
    media::PsdLayer l;
    l.name = name;
    l.rect = rect;
    l.transparency = transparency;
    l.opacity = opacity;
    l.visible = visible;
    l.image = QImage(rect.size(), QImage::Format_RGBA8888);
    l.image.fill(colour);
    return l;
}

// A collage as p.218-219 describes it: a background picture, a picture over it, a hidden one and a line of "text".
media::PsdFile collage() {
    media::PsdFile f;
    f.size = QSize(64, 36);
    f.layers << layer(QStringLiteral("Background"), QRect(0, 0, 64, 36), QColor(10, 20, 200), false);
    media::PsdLayer photo = layer(QStringLiteral("Photo"), QRect(8, 4, 20, 12), QColor(200, 100, 0), true);
    photo.image.setPixelColor(0, 0, QColor(200, 100, 0, 0)); // a transparent corner
    f.layers << photo;
    f.layers << layer(QStringLiteral("Hidden"), QRect(30, 10, 10, 10), QColor(0, 255, 0), true, 255, false);
    f.layers << layer(QStringLiteral("Titre é"), QRect(-4, 28, 40, 6), QColor(255, 255, 255, 200), true, 128);
    return f;
}

bool write(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

} // namespace

class TestPsdImport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { applyTheme(*qApp); }

    void reader() {
        const media::PsdFile made = collage();
        for (bool rle : {false, true}) {
            QString error;
            const auto read = media::readPsdData(media::psdData(made, rle), &error);
            QVERIFY2(read, qPrintable(error));
            QCOMPARE(read->size, QSize(64, 36));
            QCOMPARE(read->layers.size(), made.layers.size());
            for (qsizetype i = 0; i < made.layers.size(); ++i) {
                const media::PsdLayer& a = made.layers.at(i);
                const media::PsdLayer& b = read->layers.at(i);
                QCOMPARE(b.name, a.name); // the Unicode name ("luni")
                QCOMPARE(b.rect, a.rect);
                QCOMPARE(b.opacity, a.opacity);
                QCOMPARE(b.visible, a.visible);
                QCOMPARE(b.transparency, a.transparency);
                QCOMPARE(b.image, a.image);
            }
        }
        // A layer as a canvas-sized picture: transparent outside it, cut at the canvas's edge.
        const QImage title = media::psdLayerPicture(made, made.layers.at(3));
        QCOMPARE(title.size(), QSize(64, 36));
        QCOMPARE(title.pixelColor(0, 30).alpha(), 200);
        QCOMPARE(title.pixelColor(50, 30).alpha(), 0);
        QCOMPARE(media::importableLayers(made).size(), 4);

        // A flattened file has no layers; other colour modes (CMYK, 4) and depths are refused.
        media::PsdFile flat;
        flat.size = QSize(4, 4);
        const auto none = media::readPsdData(media::psdData(flat));
        QVERIFY(none);
        QVERIFY(none->layers.isEmpty());
        QByteArray cmyk = media::psdData(made);
        cmyk[25] = 4;
        QString error;
        QVERIFY(!media::readPsdData(cmyk, &error));
        QVERIFY(error.contains(QLatin1String("RGB or grayscale")));
        QVERIFY(!media::readPsdData(QByteArray("8BPS")));
        QVERIFY(!media::readPsdData(media::psdData(made).left(300)));
        QVERIFY(media::isPsdFile(QStringLiteral("x/Collage.PSD")));
    }

    void importing() {
        QTemporaryDir dir;
        const QString psd = dir.filePath(QStringLiteral("Collage.psd"));
        QVERIFY(write(psd, media::psdData(collage(), true)));
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
        window.settings().setValue(QStringLiteral("Import"), QStringLiteral("frameImportDuration"), 1);
        const QString binId = window.binContainer().newBin();
        BinDocument* bin = window.binContainer().openBin(binId);
        QVERIFY(bin);

        // Sequence of Layers: asked once, a clip a layer and a sequence named after the file.
        int asked = 0;
        MainWindow::PsdLayersChoice choice = MainWindow::PsdLayersChoice::SequenceOfLayers;
        window.setPsdLayersChoiceHook([&](const QStringList& files) {
            ++asked;
            [&] { QCOMPARE(files.size(), 1); }();
            return choice;
        });
        QStringList ids = window.importFiles(binId, {psd});
        QCOMPARE(asked, 1);
        QCOMPARE(ids.size(), 5);
        const model::BinItem* background = bin->bin().item(ids.at(0));
        QCOMPARE(background->name, QStringLiteral("Background"));
        QCOMPARE(background->kind, model::BinItemKind::MasterClip);
        QCOMPARE(background->fields.value(QStringLiteral("Source File")), QStringLiteral("Collage.psd"));
        QStringList names;
        for (int i = 1; i < 4; ++i) {
            const model::BinItem* key = bin->bin().item(ids.at(i));
            names << key->name;
            QVERIFY(model::isMatteKeyClip(*key)); // the hidden layer too (p.220)
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Photo"), QStringLiteral("Hidden"), QStringLiteral("Titre é")}));
        // Opacity as the Foreground Level: 128 of 255.
        const auto titled = model::matteKeyClipSegment(*bin->bin().item(ids.at(3)));
        QVERIFY(titled && titled->effect);
        QCOMPARE(qRound(titled->effect->parameters.value(QStringLiteral("Foreground")).toObject().value(QStringLiteral("Level")).toDouble()),
                 50);
        const model::BinItem* seq = bin->bin().item(ids.at(4));
        QCOMPARE(seq->kind, model::BinItemKind::Sequence);
        QCOMPARE(seq->name, QStringLiteral("Collage"));
        const model::Sequence s = model::Sequence::fromItem(*seq);
        for (int t = 1; t <= 4; ++t) QVERIFY(s.track(QStringLiteral("V%1").arg(t)));
        QVERIFY(!s.track(QStringLiteral("V5")));
        QCOMPARE(s.track(QStringLiteral("V1"))->segments.first().clipId, background->id);
        const model::Segment& top = s.track(QStringLiteral("V4"))->segments.first();
        QVERIFY(top.effect && model::isMatteKeyClipEffect(*top.effect));
        QCOMPARE(s.duration(), std::int64_t(25));

        // Select Layers: only those chosen, still in a sequence.
        choice = MainWindow::PsdLayersChoice::SelectLayers;
        window.setPsdSelectLayersHook([&](const QString&, const QStringList& layers) -> std::optional<QList<int>> {
            [&] { QCOMPARE(layers.size(), 4); }();
            return QList<int>{1};
        });
        ids = window.importFiles(binId, {psd});
        QCOMPARE(ids.size(), 2);
        QCOMPARE(bin->bin().item(ids.at(0))->name, QStringLiteral("Photo"));
        QCOMPARE(model::Sequence::fromItem(*bin->bin().item(ids.at(1))).trackNames().filter(QStringLiteral("V")).size(), 1);
        // Cancelling the Select Layers dialog, or the message box, imports nothing of the file.
        window.setPsdSelectLayersHook([](const QString&, const QStringList&) { return std::optional<QList<int>>(); });
        QVERIFY(window.importFiles(binId, {psd}).isEmpty());
        choice = MainWindow::PsdLayersChoice::Cancel;
        QVERIFY(window.importFiles(binId, {psd}).isEmpty());

        // Flattened Image: the file's merged picture as any graphic (through FFmpeg's Photoshop decoder).
        choice = MainWindow::PsdLayersChoice::FlattenedImage;
        if (media::probe(psd)) {
            ids = window.importFiles(binId, {psd});
            QCOMPARE(ids.size(), 1);
            QCOMPARE(bin->bin().item(ids.first())->name, QStringLiteral("Collage"));
        }

        // More than 24 layers: all in the bin, 24 in the sequence (p.221).
        media::PsdFile many;
        many.size = QSize(16, 16);
        for (int i = 0; i < 26; ++i) many.layers << layer(QStringLiteral("L%1").arg(i), QRect(i % 8, 0, 4, 4), QColor(i * 9, 0, 0), true);
        const QString big = dir.filePath(QStringLiteral("Many.psd"));
        QVERIFY(write(big, media::psdData(many)));
        choice = MainWindow::PsdLayersChoice::SequenceOfLayers;
        ids = window.importFiles(binId, {big});
        QCOMPARE(ids.size(), 27);
        const model::Sequence manySeq = model::Sequence::fromItem(*bin->bin().item(ids.last()));
        QVERIFY(manySeq.track(QStringLiteral("V24")));
        QVERIFY(!manySeq.track(QStringLiteral("V25")));
        media::setExtraMediaDrives({});
    }
};

QTEST_MAIN(TestPsdImport)
#include "tst_psd_import.moc"

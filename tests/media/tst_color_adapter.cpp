// Colour encoding and colour adapters (Editing Guide p.536-541, p.544; 8.5c, docs/spec/color-adapters.md): the encoding
// kept with a clip and its column, Auto's transformations, segments' adapters through JSON, Refresh Sequence > Color
// Adapters and Remove Color Adapters, and the built-in transformations' values.
#include "lumenis/media/color_transform.h"
#include "lumenis/media/lut_file.h"
#include "lumenis/model/color_adapter.h"
#include "lumenis/model/sequence.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::model;

namespace {

bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }

bool write(const QString& path, const QByteArray& text) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(text) == text.size();
}

} // namespace

class TestColorAdapter : public QObject {
    Q_OBJECT

private slots:
    // The encoding in a clip's Source Settings, the Color Transformations column and JSON (p.536, p.538).
    void encodingOfClips() {
        BinItem clip;
        clip.id = QStringLiteral("c1");
        QVERIFY(!colorEncodingOf(clip) && !colorAdapterOf(clip));
        ColorEncoding e;
        e.sourceSpace = QStringLiteral("Arri Log-C");
        e.transformations = autoTransformations(e.sourceSpace);
        QCOMPARE(e.transformations, QStringList{QStringLiteral("Arri ALEXA Log-C SUP 3.x to ITU 709")});
        e.transformations << QStringLiteral("Levels scaling (full range to video levels)");
        setColorEncoding(clip, e);
        QVERIFY(colorEncodingOf(clip) == e);
        QCOMPARE(cellText(clip, QLatin1String(kColorTransformationsHeading)),
                 QStringLiteral("Arri ALEXA Log-C SUP 3.x to ITU 709, Levels scaling (full range to video levels)"));
        QVERIFY(colorAdapterOf(clip) == e);
        const auto back = binItemFromJson(binItemToJson(clip));
        QVERIFY(back && colorEncodingOf(*back) == e);
        // "Bypass all color transformations": no adapter.
        e.bypass = true;
        setColorEncoding(clip, e);
        QVERIFY(!colorAdapterOf(clip));
        QVERIFY(cellText(clip, QLatin1String(kColorTransformationsHeading)).startsWith(QStringLiteral("Bypassed: ")));
        setColorEncoding(clip, std::nullopt);
        QVERIFY(!colorEncodingOf(clip));
        QVERIFY(cellText(clip, QLatin1String(kColorTransformationsHeading)).isEmpty());
        // Auto: nothing to do from Unknown or Rec. 709 ("the application will leave the colors as they are").
        QVERIFY(autoTransformations(QStringLiteral("Unknown")).isEmpty());
        QVERIFY(autoTransformations(QStringLiteral("Rec.709")).isEmpty());
        QCOMPARE(autoTransformations(QStringLiteral("sRGB")).size(), 2);
        // Every transformation Auto picks is one we make.
        for (const QString& space : sourceColorSpaces()) {
            for (const QString& t : autoTransformations(space)) QVERIFY2(media::hasColorTransformation(t), qPrintable(t));
        }
    }

    // Segments take the adapter, keep it through JSON, refresh to the clip's and lose it to Remove Color Adapters.
    void refreshAndRemove() {
        BinItem clip;
        clip.id = QStringLiteral("c1");
        ColorEncoding e;
        e.transformations = {QStringLiteral("Gamma 2.6 to Linear")};
        setColorEncoding(clip, e);
        Sequence seq = Sequence::withDefaultTracks();
        Segment s;
        s.clipId = clip.id;
        s.binId = QStringLiteral("b");
        s.sourceTrack = QStringLiteral("V1");
        s.length = 10;
        seq.track(QStringLiteral("V1"))->segments = {s, s};
        const auto resolve = [&](const QString&, const QString& id) -> std::optional<BinItem> {
            if (id == clip.id) return clip;
            return std::nullopt;
        };
        QCOMPARE(refreshColorAdapters(seq, resolve), 2);
        QVERIFY(seq.track(QStringLiteral("V1"))->segments[0].color == e);
        QCOMPARE(refreshColorAdapters(seq, resolve), 0);
        BinItem item;
        item.kind = BinItemKind::Sequence;
        seq.storeIn(item);
        QVERIFY(Sequence::fromItem(item).track(QStringLiteral("V1"))->segments[1].color == e);
        QCOMPARE(removeColorAdapters(seq), 2);
        QVERIFY(!seq.track(QStringLiteral("V1"))->segments[0].color);
        // The clip keeps its encoding ("It does not affect adapters on the master clips").
        QVERIFY(colorEncodingOf(clip));
    }

    // The CDL transformation of a clip's ASC_SOP and ASC_SAT columns (p.540, p.543-544).
    void cdl() {
        const QString name = media::cdlTransformationName(QStringLiteral("(1.2 1.0 1.0)(0.1 0.0 0.0)(1.0 2.0 1.0)"),
                                                          QStringLiteral("0.5"));
        QCOMPARE(name, QStringLiteral("CDL ASC_SOP=(1.2 1 1)(0.1 0 0)(1 2 1) ASC_SAT=0.5"));
        QVERIFY(media::isCdlTransformation(name));
        QVERIFY(media::cdlTransformationName(QStringLiteral("(1 1)"), {}).isEmpty());
        // Without saturation: slope, offset and power per channel.
        const QString sop = media::cdlTransformationName(QStringLiteral("(1.2 1.0 1.0)(0.1 0.0 0.0)(1.0 2.0 1.0)"), {});
        QVERIFY(sop.endsWith(QStringLiteral("ASC_SAT=1")));
        const auto c = media::transformColor({0.5, 0.5, 0.5}, {sop});
        QVERIFY(near(c[0], 0.7) && near(c[1], 0.25) && near(c[2], 0.5));
        // Saturation 0: Rec. 709 luma.
        const QString grey = media::cdlTransformationName(QStringLiteral("(1 1 1)(0 0 0)(1 1 1)"), QStringLiteral("0"));
        const auto g = media::transformColor({1.0, 0.0, 0.0}, {grey});
        QVERIFY(near(g[0], 0.2126) && near(g[1], 0.2126) && near(g[2], 0.2126));
    }

    // External LUT files (p.541-542): the formats we read, their tables, installing and the "External" names.
    void lutFiles() {
        QTemporaryDir dir;
        // IRIDAS 1D: an inverting ramp.
        const QString oneD = dir.filePath(QStringLiteral("Invert.cube"));
        QVERIFY(write(oneD, "TITLE \"Invert\"\nLUT_1D_SIZE 2\n1 1 1\n0 0 0\n"));
        auto lut = media::readLutFile(oneD);
        QVERIFY(lut && lut->title == QLatin1String("Invert") && lut->table1D.size() == 2);
        QVERIFY(near(lut->apply({0.25, 0.5, 1.0})[0], 0.75) && near(lut->apply({0.25, 0.5, 1.0})[2], 0.0));
        // IRIDAS 3D: a cube of 2 that swaps red and green (red changes fastest).
        QByteArray cube = "# swap\nLUT_3D_SIZE 2\n";
        for (int b = 0; b < 2; ++b) {
            for (int g = 0; g < 2; ++g) {
                for (int r = 0; r < 2; ++r) cube += QByteArray::number(g) + ' ' + QByteArray::number(r) + ' ' + QByteArray::number(b) + '\n';
            }
        }
        const QString threeD = dir.filePath(QStringLiteral("Swap.cube"));
        QVERIFY(write(threeD, cube));
        lut = media::readLutFile(threeD);
        QVERIFY(lut && lut->size3D == 2);
        auto s = lut->apply({0.2, 0.7, 0.4});
        QVERIFY(near(s[0], 0.7) && near(s[1], 0.2) && near(s[2], 0.4));
        // Kodak KDM .3dl: the same swap, 12-bit output, blue changing fastest.
        QByteArray kdm = "# IDENTIFICATION: 3DMODEL-3DLUT\n0 1023\n";
        for (int r = 0; r < 2; ++r) {
            for (int g = 0; g < 2; ++g) {
                for (int b = 0; b < 2; ++b) {
                    kdm += QByteArray::number(g * 4095) + ' ' + QByteArray::number(r * 4095) + ' ' + QByteArray::number(b * 4095) + '\n';
                }
            }
        }
        const QString kodak = dir.filePath(QStringLiteral("Kodak.3dl"));
        QVERIFY(write(kodak, kdm));
        QCOMPARE(media::lutProduct(QStringLiteral("# IDENTIFICATION: 3DMODEL-3DLUT")), QStringLiteral("Kodak KDM"));
        lut = media::readLutFile(kodak);
        QVERIFY(lut);
        s = lut->apply({0.2, 0.7, 0.4});
        QVERIFY(near(s[0], 0.7) && near(s[1], 0.2) && near(s[2], 0.4));
        // Autodesk: three channels of two 10-bit entries, the green one inverted.
        const QString autodesk = dir.filePath(QStringLiteral("Discreet.lut"));
        QVERIFY(write(autodesk, "LUT: 3 2\n0\n1023\n1023\n0\n0\n1023\n"));
        lut = media::readLutFile(autodesk);
        QVERIFY(lut);
        s = lut->apply({0.25, 0.25, 0.25});
        QVERIFY(near(s[0], 0.25) && near(s[1], 0.75) && near(s[2], 0.25));
        // Products we recognise but do not read, and broken files, say why.
        QString why;
        const QString nucoda = dir.filePath(QStringLiteral("Nucoda.lut"));
        QVERIFY(write(nucoda, "NUCODA_3D_CUBE 2\n"));
        QVERIFY(!media::readLutFile(nucoda, &why));
        QVERIFY(why.contains(QStringLiteral("Nucoda")));
        const QString broken = dir.filePath(QStringLiteral("Broken.cube"));
        QVERIFY(write(broken, "LUT_3D_SIZE 2\n0 0 0\n"));
        QVERIFY(!media::readLutFile(broken, &why));
        QVERIFY(why.contains(QStringLiteral("2 lines")) || why.contains(QStringLiteral("8")));
        // Installed: copied into the project's LUTs folder and listed as External.
        const QString project = dir.filePath(QStringLiteral("Film"));
        const QString folder = media::projectLutFolder(project);
        QCOMPARE(media::installLut(oneD, folder, &why), QStringLiteral("External Invert"));
        QVERIFY(QFile::exists(QDir(folder).filePath(QStringLiteral("Invert.cube"))));
        QVERIFY(media::colorTransformationNames().contains(QStringLiteral("External Invert")));
        QVERIFY(media::installLut(nucoda, folder, &why).isEmpty());
        QImage white(2, 2, QImage::Format_RGB32);
        white.fill(Qt::white);
        QCOMPARE(qRed(media::applyColorTransformations(white, {QStringLiteral("External Invert")}).pixel(0, 0)), 0);
        // A folder registered as a whole: the readable ones, and why the others are not.
        QVERIFY(QFile::copy(threeD, QDir(folder).filePath(QStringLiteral("Swap.cube"))));
        QVERIFY(QFile::copy(broken, QDir(folder).filePath(QStringLiteral("Broken.cube"))));
        QStringList failures;
        const QStringList names = media::registerLutFolders({folder, QString()}, &failures);
        QCOMPARE(names, (QStringList{QStringLiteral("External Invert"), QStringLiteral("External Swap")}));
        QCOMPARE(failures.size(), 1);
        QVERIFY(failures.first().startsWith(QStringLiteral("Broken.cube: ")));
    }

    // The built-in transformations (p.539-540).
    void transformations() {
        const QStringList names = media::colorTransformationNames();
        QCOMPARE(names.first(), QStringLiteral("Levels scaling (full range to video levels)"));
        QVERIFY(names.contains(QStringLiteral("Canon C-Log to REC709")));
        auto one = [](double v, const QString& name) { return media::transformColor({v, v, v}, {name})[0]; };
        // Levels: 0-255 to 16-235 and back.
        QVERIFY(near(one(0, QStringLiteral("Levels scaling (full range to video levels)")), 16.0 / 255));
        QVERIFY(near(one(1, QStringLiteral("Levels scaling (full range to video levels)")), 235.0 / 255));
        QVERIFY(near(one(16.0 / 255, QStringLiteral("Levels scaling (video levels to full range)")), 0));
        QVERIFY(near(one(235.0 / 255, QStringLiteral("Levels scaling (video levels to full range)")), 1));
        QVERIFY(near(one(0.5, QStringLiteral("Gamma 2.6 to Linear")), std::pow(0.5, 2.6)));
        // Linear 18% grey is about 0.41 in 709, then on video levels.
        QVERIFY(near(one(0.18, QStringLiteral("Linear full range to REC709")), (16 + 219 * 0.409) / 255, 3e-3));
        QVERIFY(near(one(0, QStringLiteral("Linear full range to REC709")), 16.0 / 255));
        QVERIFY(near(one(0.5, QStringLiteral("SRGB (IEC 61966-2.1) to linear")), 0.214, 1e-3));
        // Cineon: black 95 to 0, white 685 to 1, above white clamped.
        QVERIFY(near(one(95.0 / 1023, QStringLiteral("Printing Density (Cineon) to linear")), 0));
        QVERIFY(near(one(685.0 / 1023, QStringLiteral("Printing Density (Cineon) to linear")), 1));
        QVERIFY(near(one(1.0, QStringLiteral("Printing Density (Cineon) to linear")), 1));
        // Log-C 18% grey (0.391) to 709's grey; the matrix keeps grey grey.
        const auto grey = media::transformColor({0.391007, 0.391007, 0.391007}, {QStringLiteral("Arri ALEXA Log-C SUP 3.x to ITU 709")});
        QVERIFY(near(grey[0], 0.409, 5e-3) && near(grey[0], grey[1]) && near(grey[1], grey[2]));
        // Applied top to bottom: full to video and back is the identity.
        const QStringList both{QStringLiteral("Levels scaling (full range to video levels)"),
                               QStringLiteral("Levels scaling (video levels to full range)")};
        QVERIFY(near(media::transformColor({0.3, 0.6, 0.9}, both)[1], 0.6));
        // On an image, and a registered transformation.
        QImage image(4, 2, QImage::Format_RGB32);
        image.fill(Qt::white);
        QImage out = media::applyColorTransformations(image, {QStringLiteral("Levels scaling (full range to video levels)")});
        QCOMPARE(qRed(out.pixel(1, 1)), 235);
        media::ColorStep invert;
        invert.curve = [](double v) { return 1.0 - v; };
        media::registerColorTransformation(QStringLiteral("External invert"), {invert});
        QVERIFY(media::colorTransformationNames().contains(QStringLiteral("External invert")));
        out = media::applyColorTransformations(image, {QStringLiteral("External invert"), QStringLiteral("Unknown thing")});
        QCOMPARE(qGreen(out.pixel(0, 0)), 0);
        out = media::applyColorTransformations(image, {QStringLiteral("Arri ALEXA Log-C SUP 3.x to ITU 709")});
        QCOMPARE(qRed(out.pixel(0, 0)), 255); // white is clipped to 709's white
        QCOMPARE(media::applyColorTransformations(image, {}), image);
    }
};

QTEST_GUILESS_MAIN(TestColorAdapter)
#include "tst_color_adapter.moc"

// Colour encoding and colour adapters (Editing Guide p.536-541, p.544; 8.5c, docs/spec/color-adapters.md): the encoding
// kept with a clip and its column, Auto's transformations, segments' adapters through JSON, Refresh Sequence > Color
// Adapters and Remove Color Adapters, and the built-in transformations' values.
#include "lumenis/media/color_transform.h"
#include "lumenis/model/color_adapter.h"
#include "lumenis/model/sequence.h"

#include <QImage>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::model;

namespace {

bool near(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) <= tolerance; }

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

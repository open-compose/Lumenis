// Stereoscopic clips and the project's Stereoscopic option (Editing Guide p.65-66, p.213, p.243; 8.6d1,
// docs/spec/stereoscopic.md): making a Stereo Master Clip from two master clips and its refusals, the S3D Leading Eye,
// the eye each mode shows, and the frame-compatible pictures of Side by Side and Over / Under; the S3D Views and Overlay
// and the S3D Settings' parallax (8.6d2; p.1162-1163, p.1190-1191, p.1216, p.1226-1227).
#include "lumenis/media/stereo.h"
#include "lumenis/model/stereo_clip.h"
#include "lumenis/model/stereo_view.h"

#include <QImage>
#include <QJsonObject>
#include <QTest>

#include <cmath>

using namespace lumenis;
using namespace lumenis::model;

namespace {

BinItem master(const QString& name, const QString& file, std::int64_t frames) {
    BinItem item;
    item.id = newItemId();
    item.kind = BinItemKind::MasterClip;
    item.name = name;
    item.mobId = MobId::generate();
    item.editRate = Rational(25);
    item.durationFrames = frames;
    item.link = QJsonObject{{QStringLiteral("path"), file}};
    return item;
}

QImage filled(QColor c) {
    QImage image(80, 40, QImage::Format_RGB32);
    image.fill(c);
    return image;
}

} // namespace

class TestStereo : public QObject {
    Q_OBJECT

private slots:
    void makeStereoClip() {
        const BinItem left = master(QStringLiteral("Shot L"), QStringLiteral("/m/left.mov"), 100);
        const BinItem right = master(QStringLiteral("Shot R"), QStringLiteral("/m/right.mov"), 80);
        QString error;
        const auto stereo = model::makeStereoClip(left, right, &error);
        QVERIFY2(stereo, qPrintable(error));
        QCOMPARE(stereo->name, QStringLiteral("Shot L S3D"));
        QVERIFY(stereo->id != left.id);
        QVERIFY(!(stereo->mobId == left.mobId));
        QCOMPARE(stereo->durationFrames, std::int64_t(80));
        QCOMPARE(stereo->link, left.link);
        QCOMPARE(stereo->fields.value(QLatin1String(kLeadingEyeHeading)), QStringLiteral("Left"));
        QVERIFY(isStereoClip(*stereo));
        const auto eyes = stereoClipOf(*stereo);
        QVERIFY(eyes);
        QCOMPARE(eyes->rightLink, right.link);
        QCOMPARE(eyes->leftName, QStringLiteral("Shot L"));
        QCOMPARE(eyes->rightName, QStringLiteral("Shot R"));
        // Kept through JSON.
        const auto again = StereoClip::fromJson(eyes->toJson());
        QVERIFY(again && again->rightLink == right.link && again->leadingEye == Eye::Left);
    }

    void refusals() {
        const BinItem left = master(QStringLiteral("A"), QStringLiteral("/m/a.mov"), 10);
        BinItem right = master(QStringLiteral("B"), QStringLiteral("/m/b.mov"), 10);
        QString error;
        QVERIFY(!model::makeStereoClip(left, left, &error));
        BinItem noMedia = right;
        noMedia.link = {};
        QVERIFY(!model::makeStereoClip(left, noMedia, &error));
        BinItem sequence = right;
        sequence.kind = BinItemKind::Sequence;
        QVERIFY(!model::makeStereoClip(left, sequence, &error));
        BinItem otherRate = right;
        otherRate.editRate = Rational(24);
        QVERIFY(!model::makeStereoClip(left, otherRate, &error));
        QVERIFY(error.contains(QStringLiteral("frame rates")));
        const auto stereo = model::makeStereoClip(left, right);
        QVERIFY(stereo);
        QVERIFY(!model::makeStereoClip(*stereo, right, &error));
        // A 2D clip has no eyes and no leading eye to set.
        BinItem flat = left;
        QVERIFY(!setLeadingEye(flat, Eye::Right));
    }

    void leadingEyeAndModes() {
        const BinItem left = master(QStringLiteral("A"), QStringLiteral("/m/a.mov"), 10);
        const BinItem right = master(QStringLiteral("B"), QStringLiteral("/m/b.mov"), 10);
        BinItem stereo = *model::makeStereoClip(left, right);
        QCOMPARE(shownEye(StereoMode::LeadingEye, stereo), Eye::Left);
        QVERIFY(setLeadingEye(stereo, Eye::Right));
        QCOMPARE(stereo.link, right.link);
        QCOMPARE(stereo.fields.value(QLatin1String(kLeadingEyeHeading)), QStringLiteral("Right"));
        QCOMPARE(shownEye(StereoMode::LeadingEye, stereo), Eye::Right);
        QCOMPARE(shownEye(StereoMode::Off, stereo), Eye::Right);
        QCOMPARE(shownEye(StereoMode::Full, stereo), Eye::Right);
        QCOMPARE(shownEye(StereoMode::LeftEyeOnly, stereo), Eye::Left);
        QCOMPARE(shownEye(StereoMode::RightEyeOnly, stereo), Eye::Right);
        QCOMPARE(eyeClip(stereo, Eye::Left).link, left.link);
        QCOMPARE(eyeClip(stereo, Eye::Right).link, right.link);
        // A 2D clip is the same for both eyes (p.66).
        QCOMPARE(eyeClip(left, Eye::Right).link, left.link);
        // The modes by their text in stereoscopicModes().
        QCOMPARE(stereoModeOf(QStringLiteral("Off")), StereoMode::Off);
        QCOMPARE(stereoModeOf(QStringLiteral("Leading Eye")), StereoMode::LeadingEye);
        QCOMPARE(stereoModeOf(QStringLiteral("Left Eye Only")), StereoMode::LeftEyeOnly);
        QCOMPARE(stereoModeOf(QStringLiteral("Right Eye Only")), StereoMode::RightEyeOnly);
        QCOMPARE(stereoModeOf(QStringLiteral("Side by Side")), StereoMode::SideBySide);
        QCOMPARE(stereoModeOf(QStringLiteral("Over / Under")), StereoMode::OverUnder);
        QCOMPARE(stereoModeOf(QStringLiteral("Full")), StereoMode::Full);
        QVERIFY(isFrameCompatible(StereoMode::SideBySide));
        QVERIFY(isFrameCompatible(StereoMode::OverUnder));
        QVERIFY(!isFrameCompatible(StereoMode::Full));
    }

    void frameCompatiblePictures() {
        const QImage red = filled(Qt::red);
        const QImage blue = filled(Qt::blue);
        const QImage sbs = media::frameCompatible(red, blue, true);
        QCOMPARE(sbs.size(), QSize(80, 40));
        QCOMPARE(QColor(sbs.pixel(10, 20)), QColor(Qt::red));
        QCOMPARE(QColor(sbs.pixel(70, 20)), QColor(Qt::blue));
        const QImage ou = media::frameCompatible(red, blue, false, QSize(80, 40));
        QCOMPARE(QColor(ou.pixel(40, 5)), QColor(Qt::red));
        QCOMPARE(QColor(ou.pixel(40, 35)), QColor(Qt::blue));
        // A missing eye leaves its half black.
        const QImage half = media::frameCompatible(red, QImage(), true);
        QCOMPARE(QColor(half.pixel(70, 20)), QColor(Qt::black));
        QVERIFY(media::frameCompatible(QImage(), QImage(), true).isNull());
    }

    void viewValues() {
        using K = S3DView::Kind;
        QCOMPARE(s3dViewOf(QStringLiteral("mix")).kind, K::Mix);
        QCOMPARE(s3dViewOf(QStringLiteral("difference")).kind, K::Difference);
        QCOMPARE(s3dViewOf(QStringLiteral("fullFrame")).kind, K::FullFrame);
        QCOMPARE(s3dViewOf(QStringLiteral("followProject")).kind, K::FollowProject);
        QCOMPARE(s3dViewOf(QStringLiteral("followSR")).kind, K::FollowSourceRecord);
        QCOMPARE(s3dViewOf(QStringLiteral("monoRight")).mono, S3DView::MonoEye::Right);
        QCOMPARE(s3dViewOf(QStringLiteral("monoLeading")).mono, S3DView::MonoEye::Leading);
        QCOMPARE(s3dViewOf(QStringLiteral("anaglyphAmberBlue")).colors, S3DView::Colors::AmberBlue);
        QCOMPARE(s3dViewOf(QStringLiteral("bwGreenMagenta")).kind, K::BwAnaglyph);
        QCOMPARE(s3dViewOf(QStringLiteral("bwGreenMagenta")).colors, S3DView::Colors::GreenMagenta);
        QCOMPARE(s3dViewOf(QStringLiteral("checker50")).block, 50);
        QCOMPARE(s3dViewOf(QStringLiteral("compare25")).percent, 25);
        QCOMPARE(s3dViewOf(QStringLiteral("nonsense")).kind, K::FrameCompatible);
        QCOMPARE(s3dOverlayOf(QStringLiteral("nearFar")), S3DOverlay::NearFar);
        QCOMPARE(s3dOverlayOf(QStringLiteral("off")), S3DOverlay::Off);
    }

    void viewPictures() {
        const QImage red = filled(Qt::red);
        const QImage blue = filled(Qt::blue);
        auto at = [](const QImage& i, int x, int y) { return QColor(i.pixel(x, y)); };
        // Mix: half of each.
        const QImage mix = media::stereoView(red, blue, s3dViewOf(QStringLiteral("mix")), true);
        QCOMPARE(at(mix, 5, 5), QColor(127, 0, 127));
        // Difference: gray where the eyes agree, lighter or darker where they differ.
        QCOMPARE(at(media::stereoView(red, red, s3dViewOf(QStringLiteral("difference")), true), 5, 5), QColor(128, 128, 128));
        QVERIFY(at(media::stereoView(red, blue, s3dViewOf(QStringLiteral("difference")), true), 5, 5).red() > 128);
        // Anaglyph Red-Cyan: the left eye's red, the right eye's green and blue.
        const QImage white = filled(Qt::white);
        const QImage black = filled(Qt::black);
        QCOMPARE(at(media::stereoView(white, black, s3dViewOf(QStringLiteral("anaglyphRedCyan")), true), 5, 5), QColor(255, 0, 0));
        QCOMPARE(at(media::stereoView(white, black, s3dViewOf(QStringLiteral("anaglyphGreenMagenta")), true), 5, 5), QColor(0, 255, 0));
        QCOMPARE(at(media::stereoView(white, black, s3dViewOf(QStringLiteral("anaglyphAmberBlue")), true), 5, 5), QColor(255, 255, 0));
        // B/W: each eye's brightness; a red left eye gives a gray-red, not pure red.
        const QColor bw = at(media::stereoView(red, black, s3dViewOf(QStringLiteral("bwRedCyan")), true), 5, 5);
        QVERIFY(bw.red() > 50 && bw.red() < 120 && bw.green() == 0);
        // Checkerboard 10 x 10: the left eye, then the right.
        const QImage checker = media::stereoView(red, blue, s3dViewOf(QStringLiteral("checker10")), true);
        QCOMPARE(at(checker, 2, 2), QColor(Qt::red));
        QCOMPARE(at(checker, 12, 2), QColor(Qt::blue));
        QCOMPARE(at(checker, 12, 12), QColor(Qt::red));
        // Compare 50%: the left eye top left, the right eye bottom right.
        const QImage compare = media::stereoView(red, blue, s3dViewOf(QStringLiteral("compare50")), true);
        QCOMPARE(at(compare, 2, 2), QColor(Qt::red));
        QCOMPARE(at(compare, 77, 37), QColor(Qt::blue));
        QCOMPARE(at(media::stereoView(red, blue, s3dViewOf(QStringLiteral("compare25")), true), 40, 20), QColor(Qt::blue));
        QCOMPARE(at(media::stereoView(red, blue, s3dViewOf(QStringLiteral("compare75")), true), 40, 20), QColor(Qt::red));
        // Full Frame: both eyes at full size, twice as wide.
        const QImage full = media::stereoView(red, blue, s3dViewOf(QStringLiteral("fullFrame")), true);
        QCOMPARE(full.size(), QSize(160, 40));
        QCOMPARE(at(full, 100, 5), QColor(Qt::blue));
        // Mono: one eye, the leading one for Leading eye.
        QCOMPARE(at(media::stereoView(red, blue, s3dViewOf(QStringLiteral("monoRight")), true), 5, 5), QColor(Qt::blue));
        QCOMPARE(at(media::stereoView(red, blue, s3dViewOf(QStringLiteral("monoLeading")), true, Eye::Right), 5, 5), QColor(Qt::blue));
        // Frame Compatible: Over / Under.
        const QImage ou = media::stereoView(red, blue, s3dViewOf(QStringLiteral("frameCompatible")), false);
        QCOMPARE(at(ou, 40, 35), QColor(Qt::blue));
    }

    void parallaxAndGuides() {
        S3DSettings s; // 10 m away, a 10 m screen, 1920 pixels, 6.35 cm, -2 m and +10 m
        // Near: 0.0635 x -2 / 8 m on a 10 m screen of 1920 pixels.
        QVERIFY(std::abs(parallaxNear(s) - (-3.048)) < 0.001);
        QVERIFY(std::abs(parallaxFar(s) - 6.096) < 0.001);
        // At the screen plane none; Manual overrides.
        QCOMPARE(parallaxPixels(s, 0.0), 0.0);
        s.manualFar = true;
        s.parallaxFarValue = 40.0;
        QCOMPARE(parallaxFar(s), 40.0);
        // Imperial: inches and feet.
        S3DSettings ft;
        ft.imperial = true;
        ft.eyeSeparation = 2.5;
        ft.viewingDistance = 30.0;
        ft.screenWidth = 30.0;
        ft.depthFar = 30.0;
        QVERIFY(std::abs(parallaxFar(ft) - (2.5 / 12.0 * 0.5 / 30.0 * 1920)) < 0.001);
        // The guides: green for Near, blue for Far, either side of the centre as far apart as the parallax.
        QImage image = filled(Qt::black);
        media::drawParallaxGuides(image, S3DOverlay::NearFar, -20.0, 40.0, 80);
        QCOMPARE(QColor(image.pixel(30, 20)), QColor(0, 255, 0));
        QCOMPARE(QColor(image.pixel(50, 20)), QColor(0, 255, 0));
        QCOMPARE(QColor(image.pixel(20, 20)), QColor(0, 0, 255));
        QCOMPARE(QColor(image.pixel(60, 20)), QColor(0, 0, 255));
        QCOMPARE(QColor(image.pixel(40, 20)), QColor(Qt::black));
        QImage off = filled(Qt::black);
        media::drawParallaxGuides(off, S3DOverlay::Off, -20.0, 40.0, 80);
        QCOMPARE(QColor(off.pixel(30, 20)), QColor(Qt::black));
    }
};

QTEST_GUILESS_MAIN(TestStereo)
#include "tst_stereo.moc"

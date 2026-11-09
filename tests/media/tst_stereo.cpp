// Stereoscopic clips and the project's Stereoscopic option (Editing Guide p.65-66, p.213, p.243; 8.6d1,
// docs/spec/stereoscopic.md): making a Stereo Master Clip from two master clips and its refusals, the S3D Leading Eye,
// the eye each mode shows, and the frame-compatible pictures of Side by Side and Over / Under.
#include "lumenis/media/stereo.h"
#include "lumenis/model/stereo_clip.h"

#include <QImage>
#include <QJsonObject>
#include <QTest>

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
};

QTEST_GUILESS_MAIN(TestStereo)
#include "tst_stereo.moc"

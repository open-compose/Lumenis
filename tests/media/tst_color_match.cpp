// The Color Match control's corrections (lumenis/media/color_match.h; FX Guide p.624-631): after Match Color the input
// colour comes out as the output colour, in the components the Match Type names.
#include "lumenis/media/color_correction.h"
#include "lumenis/media/color_match.h"

#include <QTest>

#include <cmath>

using namespace lumenis;
using media::ColorMatch;
using media::MatchGroup;
using model::ColorCorrection;

namespace {

std::array<double, 3> through(const ColorCorrection& c, const QColor& color) {
    return media::colorCorrectedPixel(c, {static_cast<double>(color.redF()), static_cast<double>(color.greenF()), static_cast<double>(color.blueF())});
}

double distance(const std::array<double, 3>& a, const QColor& b) {
    return std::max({std::abs(a[0] - static_cast<double>(b.redF())), std::abs(a[1] - static_cast<double>(b.greenF())),
                     std::abs(a[2] - static_cast<double>(b.blueF()))});
}

double lumaOf(const std::array<double, 3>& p) {
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

double hueOf(const std::array<double, 3>& p) {
    const double y = lumaOf(p);
    return std::atan2((p[0] - y) / 1.5748, (p[2] - y) / 1.8556);
}

const QColor kIn = QColor::fromRgbF(0.40f, 0.42f, 0.58f);  // a cool grey
const QColor kOut = QColor::fromRgbF(0.62f, 0.47f, 0.36f); // a warm skin tone

ColorCorrection matched(MatchGroup group, const QString& type, bool natural = false) {
    ColorCorrection c;
    ColorMatch m;
    m.group = group;
    m.type = type;
    m.natural = natural;
    m.vector = QStringLiteral("blue");
    [&] { QVERIFY(media::matchColor(c, kIn, kOut, m)); }();
    return c;
}

} // namespace

class TestColorMatch : public QObject {
    Q_OBJECT

private slots:
    void menus() {
        // The Match Type menus (p.626-629).
        QCOMPARE(media::matchTypes(MatchGroup::HslControls).size(), 7);
        QCOMPARE(media::matchTypes(MatchGroup::HslHueOffsets), (QStringList{"master", "highlights", "midtones", "shadows"}));
        QCOMPARE(media::matchTypes(MatchGroup::Channels).size(), 8);
        QCOMPARE(media::matchTypes(MatchGroup::Levels), (QStringList{"black", "gray", "white"}));
        QCOMPARE(media::matchTypes(MatchGroup::Secondary), (QStringList{"hue", "saturation", "hs"}));
        QCOMPARE(media::matchTypeText(QStringLiteral("hsl")), QStringLiteral("H+S+L"));
        // "match types appear in the Color Match control with the extension (Nat)" (p.627).
        QCOMPARE(media::matchTypeText(QStringLiteral("rgb"), true), QStringLiteral("R+G+B (Nat)"));
        QVERIFY(media::hasNaturalMatch(MatchGroup::Curves) && media::hasNaturalMatch(MatchGroup::Secondary));
        QVERIFY(!media::hasNaturalMatch(MatchGroup::Channels));
    }

    void wholeColor() {
        // Matching every component turns the input into the output.
        for (const auto& [group, type] : {std::pair{MatchGroup::HslControls, QStringLiteral("hsl")}, std::pair{MatchGroup::Channels, QStringLiteral("rgb")},
                                          std::pair{MatchGroup::Curves, QStringLiteral("rgb")}}) {
            const ColorCorrection c = matched(group, type);
            QVERIFY2(distance(through(c, kIn), kOut) < 0.03, qPrintable(QStringLiteral("%1 %2").arg(static_cast<int>(group)).arg(distance(through(c, kIn), kOut))));
        }
        // Hue Offsets moves the chroma; the brightness stays.
        const ColorCorrection offsets = matched(MatchGroup::HslHueOffsets, QStringLiteral("master"));
        const auto p = through(offsets, kIn);
        QVERIFY(std::abs(hueOf(p) - hueOf({0.62, 0.47, 0.36})) < 0.1);
        QVERIFY(std::abs(lumaOf(p) - lumaOf({0.40, 0.42, 0.58})) < 0.02);
    }

    void partialTypes() {
        // "Hue: Media Composer matches based on only the hue" (p.627): the luminance stays.
        const ColorCorrection hue = matched(MatchGroup::HslControls, QStringLiteral("hue"));
        QVERIFY(std::abs(hueOf(through(hue, kIn)) - hueOf({0.62, 0.47, 0.36})) < 0.05);
        QVERIFY(std::abs(lumaOf(through(hue, kIn)) - lumaOf({0.40, 0.42, 0.58})) < 0.02);
        // Red only (Channels): green and blue stay.
        const ColorCorrection red = matched(MatchGroup::Channels, QStringLiteral("red"));
        const auto r = through(red, kIn);
        QVERIFY(std::abs(r[0] - 0.62) < 0.02 && std::abs(r[2] - 0.58) < 0.02);
        // Levels: "adjusts the white point of the image to the brightness value of the color in the output" (p.628).
        const ColorCorrection white = matched(MatchGroup::Levels, QStringLiteral("white"));
        QCOMPARE(white.number(QStringLiteral("levels/master/inWhite")), std::round(16.0 + 219.0 * lumaOf({0.40, 0.42, 0.58})));
        QCOMPARE(white.number(QStringLiteral("levels/master/outWhite")), std::round(16.0 + 219.0 * lumaOf({0.62, 0.47, 0.36})));
        // Secondary: the selected vector's output hue moves.
        const ColorCorrection vector = matched(MatchGroup::Secondary, QStringLiteral("hue"));
        QVERIFY(vector.values.contains(model::vectorPath(QStringLiteral("blue"), QStringLiteral("output"), QStringLiteral("hue"))));
    }

    void naturalMatch() {
        // "adopts the new hue value, preserves the original luminance value" (p.629).
        const ColorCorrection c = matched(MatchGroup::Curves, QStringLiteral("rgb"), true);
        const auto p = through(c, kIn);
        QVERIFY2(std::abs(lumaOf(p) - lumaOf({0.40, 0.42, 0.58})) < 0.02, qPrintable(QString::number(lumaOf(p))));
        QVERIFY(std::abs(hueOf(p) - hueOf({0.62, 0.47, 0.36})) < 0.1);
        // Without colours, or a type of another group, nothing happens.
        ColorCorrection none;
        ColorMatch m;
        m.group = MatchGroup::Levels;
        m.type = QStringLiteral("rgb");
        QVERIFY(!media::matchColor(none, kIn, kOut, m));
        m.type = QStringLiteral("white");
        QVERIFY(!media::matchColor(none, QColor(), kOut, m));
    }
};

QTEST_MAIN(TestColorMatch)
#include "tst_color_match.moc"

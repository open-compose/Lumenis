#pragma once

// Colour corrections (ROADMAP 6.10; FX Guide ch.19-20; docs/spec/color-correction.md): what the Color Correction tool's
// controls hold, for the Color Correction effect and (6.10d) the relationship corrections. 6.10a1 has the HSL group:
// the Controls tab for the Master, Highlights, Midtones and Shadows ranges, the Hue Offsets tab's ChromaWheels and the
// Luma Ranges curves (p.631-644).
//
// Ours: a correction is a set of controls named by their place in the tool, "hsl/controls/master/hue", each with a
// value only once adjusted (unset controls have their defaults), and the Enable buttons that are turned off, named by
// the same paths ("hsl", "hsl/controls", "hsl/controls/master", "hsl/controls/master/hue"). A control counts when its
// own button and every button above it are on (p.603-605). The Hue Offsets tab's Gain, Gamma and Setup are the
// Controls tab's Master Gain, Gamma and Setup, as "Adjustments to the Gain, Gamma, and Setup controls in the Hue
// Offsets subtab ... enable both the Hue Offsets and the Controls subtabs" (p.603).

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

namespace lumenis::model {

struct Effect;

// The luminance ranges, "highlights, midtones, and shadows" (p.631, p.635).
enum class LumaRange { Shadows, Midtones, Highlights };

struct ColorCorrection {
    QJsonObject values;    // control path -> value (a number, or an array of [in, out] points for a Luma Ranges curve)
    QStringList disabled;  // the Enable buttons that are off

    bool isEmpty() const { return values.isEmpty() && disabled.isEmpty(); }
    QJsonObject toJson() const;
    static ColorCorrection fromJson(const QJsonObject& o);
    bool operator==(const ColorCorrection&) const = default;

    // A control's value, its default while unset.
    double number(const QString& path) const;
    // Whether the control at `path` counts: its button and every button above it on, and it is set.
    bool applies(const QString& path) const;
    // Adjusting a control: its value, and its button and those above it on ("By enabling an individual slider in the HSL
    // group, you enable not only the button for that slider but also the buttons for all the associated tabs", p.604).
    void set(const QString& path, const QJsonValue& value);
    // Clicking an Enable button.
    void setEnabled(const QString& path, bool on);
    bool enabled(const QString& path) const;
    // Alt+click: "All controls linked to that button return to their default values" (p.604), those below it too.
    void reset(const QString& path);
};

// Every control's default and range (p.633-634, p.638): Hue -180 to 180 (0), Saturation 0 to 200 (100), Brightness and
// Contrast -100 to 100 (0), Gain 0 to 200 (100), Gamma 0.1 to 10 (1), Setup -255 to 255 (0), Clip Low 16 and Clip High
// 235, the wheels' Hue -180 to 180 and Amount 0 to 100 (0). Nothing for an unknown path.
std::optional<double> colorControlDefault(const QString& path);
std::optional<std::pair<double, double>> colorControlRange(const QString& path);
// The control paths of the HSL group, in the tool's order.
QStringList hslControlPaths();

// A Luma Ranges curve: [in 0-255, out 0-100] points ("The graph plots brightness values from 0 to 255 on the horizontal
// axis and percentage values on the vertical axis", p.641), the correction's own or the default.
QList<QPointF> lumaRangeCurve(const ColorCorrection& c, LumaRange range);
// The default curves: thirds of the range that blend so that "brightness level 175 is defined as 50% highlight, 50%
// midtone, and 0% shadow. Brightness level 215 is defined as 95% highlight, 5% midtone" (p.642), shadows the mirror
// image (ours).
QList<QPointF> defaultLumaRangeCurve(LumaRange range);
// How much of each range a brightness (0-255) is: the curves' values made to add up to one (ours).
std::array<double, 3> lumaRangeWeights(const ColorCorrection& c, double brightness);
QString lumaRangeKey(LumaRange range); // "shadows", "midtones", "highlights"

// The Color Correction effect keeps its correction under this category (ours).
inline const QString kColorCorrectionCategory = QStringLiteral("Color Correction");
bool isColorCorrectionEffect(const Effect& effect);
ColorCorrection colorCorrectionOf(const Effect& effect);
void setColorCorrection(Effect& effect, const ColorCorrection& correction);

} // namespace lumenis::model

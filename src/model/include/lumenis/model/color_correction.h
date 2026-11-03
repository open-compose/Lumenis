#pragma once

// Colour corrections (ROADMAP 6.10; FX Guide ch.19-20; docs/spec/color-correction.md): what the Color Correction tool's
// controls hold, for the Color Correction effect and (6.10d) the relationship corrections. 6.10a1 has the HSL group:
// the Controls tab for the Master, Highlights, Midtones and Shadows ranges, the Hue Offsets tab's ChromaWheels and the
// Luma Ranges curves (p.631-644). 6.10a2 adds the Channels, Levels and Curves groups (p.644-667).
//
// Ours: a correction is a set of controls named by their place in the tool, "hsl/controls/master/hue", each with a
// value only once adjusted (unset controls have their defaults), and the Enable buttons that are turned off, named by
// the same paths ("hsl", "hsl/controls", "hsl/controls/master", "hsl/controls/master/hue"). A control counts when its
// own button and every button above it are on (p.603-605). The Hue Offsets tab's Gain, Gamma and Setup are the
// Controls tab's Master Gain, Gamma and Setup, as "Adjustments to the Gain, Gamma, and Setup controls in the Hue
// Offsets subtab ... enable both the Hue Offsets and the Controls subtabs" (p.603); the Curves tab's Master Saturation,
// Gain, Gamma and Setup are the same controls ("When you adjust any of these sliders, you enable the equivalent slider
// in the HSL group and the tab hierarchy containing it", p.659).
//
// Paths of the other groups (ours): "channels/red/2/amount", "channels/red/2/type" and "channels/red/2/invert" for the
// Channel Blending pane's four components of each channel; "levels/master/inGray" for the Levels tabs' points;
// "curves/red" ([in, out] points) and "curves/masterGain" for the Curves tab.

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
    // A control that holds a name (the Channels tab's Component Type), its default while unset.
    QString text(const QString& path) const;
    // Whether any control at or under `path` counts.
    bool adjusted(const QString& path) const;
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
// Also the Channels tab's amounts, -200 to 200 (p.646; 100 for the first, which is the channel itself, 0 for the
// others), the Levels points 0 to 255 (16, 128 and 235, p.649).
std::optional<std::pair<double, double>> colorControlRange(const QString& path);
// The default of a control that holds a name: the Channels tab's components are the channel itself, the fourth Offset
// (p.645's illustration).
QString colorControlTextDefault(const QString& path);
// The control paths of the HSL group, in the tool's order.
QStringList hslControlPaths();

// The color correction groups, in the tool's order: "HSL", "Channels", "Levels", "Curves", "Sec" (p.599); applied in
// that order, one after another ("Media Composer applies adjustments that you make in each group cumulatively", p.602;
// the order is ours).
QStringList colorCorrectionGroups(); // "hsl", "channels", "levels", "curves", "secondary"

// The Channels group (p.644-648): the three output channels, each a blend of four components.
QStringList colorChannels();                // "red", "green", "blue"
QStringList channelComponentTypes();        // "red", "green", "blue", "luma", "cr", "cb", "offset" (p.647)
inline constexpr int kChannelComponents = 4; // components 1-4 of a channel
QString channelComponentPath(const QString& channel, int component, const QString& control); // amount, type, invert

// The Levels group (p.648-659): its subdividing tabs and points. Composite and Luma have only black and white points
// (p.657).
QStringList levelsTabs();                      // "composite", "luma", "red", "green", "blue", "master"
QStringList levelsPoints(const QString& tab);  // "inBlack", "inGray", "inWhite", "outBlack", "outGray", "outWhite"
// A Levels tab's input to output mapping on the 8-bit scale: values at or below the input black point become the output
// black point, at or above the input white point the output white point, and in between a curve through the gray
// points, "steeper near the black point and somewhat shallower near the white point" when the gray point is raised
// (p.656). Ours: a power curve between the black and white points through the gray point.
double levelsValue(const ColorCorrection& c, const QString& tab, double value);

// The Curves group (p.659-667): the ChromaCurve graphs' points, [in 0-255, out 0-255], "up to sixteen control points"
// and "at least two" (p.662); by default the 45 degree line from (0, 0) to (255, 255).
inline constexpr int kMaxCurvePoints = 16;
QStringList curveGraphs(); // "red", "green", "blue", "master"
QList<QPointF> colorCurve(const ColorCorrection& c, const QString& graph);
// Stores a graph's points sorted, kept within 0-255 and to at most sixteen; fewer than two resets the graph.
void setColorCurve(ColorCorrection& c, const QString& graph, QList<QPointF> points);
// "Media Composer calculates a true curve based on the values of the control points" (p.656). Ours: a monotone cubic
// through the points (Fritsch-Carlson), so a curve between two points never overshoots them and close points anchor
// the part of the curve beyond them (p.663); flat past the end points.
double curveValue(const QList<QPointF>& points, double x);

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

#pragma once

// Colour corrections (ROADMAP 6.10; FX Guide ch.19-20; docs/spec/color-correction.md): what the Color Correction tool's
// controls hold, for the Color Correction effect and (6.10d) the relationship corrections. 6.10a1 has the HSL group:
// the Controls tab for the Master, Highlights, Midtones and Shadows ranges, the Hue Offsets tab's ChromaWheels and the
// Luma Ranges curves (p.631-644). 6.10a2 adds the Channels, Levels and Curves groups (p.644-667), 6.10a3 the Secondary
// group (p.668-683).
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
// Secondary: "secondary/vectors/red/input/hue" and so on (below), "secondary/isolate", "secondary/invertLast" and
// "secondary/selected" (the selected vector's name).
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
#include <cstdint>
#include <optional>

namespace lumenis::model {

struct Effect;

// The path a control is kept under: the Hue Offsets tab's and the Curves tab's Master Gain, Gamma, Setup (and
// Saturation) are the Controls tab's Master ones (p.603, p.659); any other path is itself.
QString canonicalControlPath(const QString& path);

// The luminance ranges, "highlights, midtones, and shadows" (p.631, p.635).
enum class LumaRange { Shadows, Midtones, Highlights };

struct ColorCorrection {
    QJsonObject values;    // control path -> value (a number, or an array of [in, out] points for a Luma Ranges curve)
    QStringList disabled;  // the Enable buttons that are off
    // Keyframed controls (FX Guide p.683-686; lumenis/model/correction_keyframes.h, 6.10d3a): control path ->
    // ParameterKeyframes::toJson(), Elastic over the segment unless made Fixed. A keyframed control's value at a time
    // comes from its keyframes (correctionAt); its entry in `values` is not used then.
    QJsonObject keyframes;

    bool isEmpty() const { return values.isEmpty() && disabled.isEmpty() && keyframes.isEmpty(); }
    QJsonObject toJson() const;
    static ColorCorrection fromJson(const QJsonObject& o);
    bool operator==(const ColorCorrection&) const = default;

    // A control's value, its default while unset.
    double number(const QString& path) const;
    // A control that holds a name (the Channels tab's Component Type), its default while unset.
    QString text(const QString& path) const;
    // Whether any control at or under `path` counts.
    bool adjusted(const QString& path) const;
    // Whether any control at or under `path` is set, whatever its buttons (an Enable button is lit when it is on and
    // something under it is set; the Color Correction tool, 6.10b2).
    bool hasValues(const QString& path) const;
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
// A Levels tab's points, read once (levelsPointsOf), and the same mapping on them: for code that maps many values with
// one correction (media::CorrectionPlan, a whole picture), where reading the points by path for each would be slow.
struct LevelsPoints {
    double inBlack = 0, inWhite = 0, outBlack = 0, outWhite = 0, inGray = 0, outGray = 0;
    bool gray = false; // the tab has gray points (not Luma or Composite)
};
LevelsPoints levelsPointsOf(const ColorCorrection& c, const QString& tab);
double levelsValue(const LevelsPoints& points, double value);

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
// The three ranges' curves (Shadows, Midtones, Highlights), and the weights from them: lumaRangeWeights() in two halves,
// for many pixels with one correction.
std::array<QList<QPointF>, 3> lumaRangeCurves(const ColorCorrection& c);
std::array<double, 3> lumaRangeWeights(const std::array<QList<QPointF>, 3>& curves, double brightness);
QString lumaRangeKey(LumaRange range); // "shadows", "midtones", "highlights"

// The Secondary group (p.668-683): twelve vectors, "The first six are standard vectors that represent the following
// major colors: Green, Yellow, Red, Magenta, Blue, Cyan. The remaining six are custom vectors" (p.670), in the Selected
// Vectors panel's order, which is the order they apply in (p.683). Hue is in degrees on the wheel of p.635 (0 red, -180
// and 180 cyan, p.675), saturation 0-100 from the centre to the edge.
QStringList secondaryVectors(); // "green", "yellow", "red", "magenta", "blue", "cyan", "1" ... "6"
bool isStandardVector(const QString& vector);
// "secondary/vectors/<vector>/input/<control>": hue, saturation, hueWidth, satMin (standard) or satWidth (custom),
// softness; "secondary/vectors/<vector>/output/<control>": hue, saturation, luminance. A vector's own Enable button is
// "secondary/vectors/<vector>".
QString vectorPath(const QString& vector, const QString& side, const QString& control);
// The vector's input region and its output, each value the correction's or the default. The output's hue and saturation
// default to the input's, so a vector does nothing until its output is changed.
struct SecondaryVector {
    bool standard = true;
    double hue = 0, saturation = 0, hueWidth = 30, satMin = 0, satWidth = 40, softness = 30;
    double outHue = 0, outSaturation = 0, outLuminance = 0;
};
SecondaryVector secondaryVector(const ColorCorrection& c, const QString& vector);
// The vectors that count (any control adjusted and its buttons on), in the panel's order.
QStringList enabledVectors(const ColorCorrection& c);
// How much a colour (hue, saturation) belongs to a vector, 0 to 1. Standard vectors are wedges of hueWidth degrees
// from satMin outwards, custom vectors ellipses of satWidth across and hueWidth degrees along (scaling with saturation,
// p.670). Softness blends beyond the edge (positive) or within it (negative), -255 to 255 (p.676; ours: 255 is the
// vector's own size, see docs/spec/color-correction.md).
double vectorMembership(const SecondaryVector& v, double hue, double saturation);
// Adjusting an input slider; with Lock Vectors on, the output's hue and saturation move by as much (p.676).
void setVectorInput(ColorCorrection& c, const QString& vector, const QString& control, double value, bool lockVectors);
// The syringe (p.678): the vector closest to the sampled (hue, saturation) colours is selected, enabled and adjusted to
// hold them all, its output set to its input, and Isolate selected. Returns the vector, empty with no samples.
QString syringeVector(ColorCorrection& c, const QList<QPointF>& samples);
// Add to Selection and Subtract from Selection on the selected vector (p.679): the vector's shape grows to hold the
// colour or shrinks to leave it out.
void addToVector(ColorCorrection& c, const QString& vector, QPointF sample);
void subtractFromVector(ColorCorrection& c, const QString& vector, QPointF sample);

// Color Correction mode's corrections on a segment (p.596-597): "Media Composer makes a single adjustment using the
// merged values from the Source and Program sides". Ours: the Source side's controls with the Program side's set
// controls in their place, and the buttons each side turned off (but those above a control the Program side set).
struct Segment;
struct Track;
ColorCorrection mergedCorrection(const ColorCorrection& source, const ColorCorrection& program);
ColorCorrection segmentCorrection(const Segment& segment);
// Which side Color Correction mode works on: the Source or the Program correction (p.596-597).
enum class CorrectionSide { Source, Program };
ColorCorrection& correctionOf(Segment& segment, CorrectionSide side);

// "Options for Relationship Color Corrections" (p.597; lumenis/model/correction_relations.h, 6.10d1): which segments a
// correction goes to, in the Correction Type menu's order. The first five are Source corrections, the last two Program
// corrections.
enum class CorrectionRelationship { SourceTape, SourceClipName, MasterClip, SubClip, SourceSegment, ProgramTrack, ProgramSegment };
CorrectionSide sideOf(CorrectionRelationship r);
// Whether it corrects only the current segment ("A dotted line indicates a correction that applies only to that
// segment", p.616).
bool isSegmentOnly(CorrectionRelationship r);
// The menu's text ("Src Tape", "Prog Segment", ...), and the keys bins keep ("sourceTape", "sourceClipName",
// "masterClip", "subClip", "sourceSegment", "programTrack", "programSegment"; nothing for any other key).
QString relationshipText(CorrectionRelationship r);
QString relationshipKey(CorrectionRelationship r);
std::optional<CorrectionRelationship> relationshipFromKey(const QString& key);
// The segment relationship of a side: Src Segment or Prog Segment.
CorrectionRelationship segmentRelationship(CorrectionSide side);
// The relationship a segment's correction on `side` was made with.
CorrectionRelationship relationshipOf(const Segment& segment, CorrectionSide side);

// The shots of Color Correction mode's monitors on a track (p.590-591): the start of the clip `step` shots from the
// one at `frame` (0 itself, -1 "the segment immediately before", 2 "Second Next"); filler is not a shot (ours).
// Nothing past either end of the track.
std::optional<std::int64_t> correctionShotStart(const Track& track, std::int64_t frame, int step);
// "Go to Previous Uncorrected Shot or Go to Next Uncorrected Shot": the start of the nearest clip before (direction
// -1) or after (1) the one at `frame` that "does not already have a color correction applied" (p.592).
std::optional<std::int64_t> uncorrectedShotStart(const Track& track, std::int64_t frame, int direction);

// The Correction setting's Units tab (FX Guide p.606; Editing Guide p.1164-1165): the scale the Color Correction tool
// shows a value on. Values are kept on the 8-bit scale (0 to 255, reference black 16 and white 235); "10 Bit: Measures
// the adjustment on a scale from 0 to 1024 ... the 10-bit RGB values for reference black and reference white are 288
// and 726" (p.606), so 2v + 256. Ours: Percent is the share of the 8-bit scale, IRE puts reference black at 0 and white
// at 100, mVolts at 0 and 700. "The 10-bit and 8-bit options ... do not change the degree of precision for the
// correction" (p.606): only the numbers shown change.
enum class CorrectionUnit { TenBit, EightBit, Percent, Ire, Millivolts };
// The values each Units menu is for (ours: the Levels and Curves tabs' points by the tab's kind, the HSL group's Setup
// and Clip levels and the Luma Ranges graph's brightness as Luma); Hue (Degree) and Saturation/Gain (Percent) have one
// unit each and are shown as kept.
enum class UnitKind { Rgb, Luma, Composite };
struct CorrectionUnits {
    CorrectionUnit rgb = CorrectionUnit::EightBit;
    CorrectionUnit luma = CorrectionUnit::EightBit;
    CorrectionUnit composite = CorrectionUnit::EightBit;
    CorrectionUnit of(UnitKind kind) const { return kind == UnitKind::Rgb ? rgb : kind == UnitKind::Luma ? luma : composite; }
    bool operator==(const CorrectionUnits&) const = default;
};
// An 8-bit value (or, with `difference`, a change of one, such as Setup) on `unit`'s scale, and back.
double toCorrectionUnit(double value, CorrectionUnit unit, bool difference = false);
double fromCorrectionUnit(double shown, CorrectionUnit unit, bool difference = false);
// The Correction setting's keys: "10bit", "8bit", "percent", "ire", "mvolts"; 8 Bit for anything else.
CorrectionUnit correctionUnitFromKey(const QString& key);
// The kind and difference of the value at a control path, for the tool's boxes: levels points by their tab, Curves
// points as RGB, Setup (a difference) and the Clip levels as Luma; nothing for values shown as kept.
std::optional<std::pair<UnitKind, bool>> controlUnitKind(const QString& path);

// The Color Correction effect keeps its correction under this category (ours).
inline const QString kColorCorrectionCategory = QStringLiteral("Color Correction");
bool isColorCorrectionEffect(const Effect& effect);
ColorCorrection colorCorrectionOf(const Effect& effect);
void setColorCorrection(Effect& effect, const ColorCorrection& correction);

} // namespace lumenis::model

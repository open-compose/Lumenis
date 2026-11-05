#pragma once

// Safe colour limits (FX Guide ch.23, p.721-730; Editing Guide p.1216-1217; docs/spec/color-correction.md, 6.10e4): the
// Safe Colors project setting's three limit types, "for the composite signal range, for the luminance range, and for
// the RGB gamut" (p.721), each a low and a high value in its units, and the Actions: "The top button controls both the
// Composite and the Luminance limit types. The bottom button controls the RGB Gamut limit type", Ignore (the default)
// or Warn (p.722). Defaults as the guide's picture of the default settings (p.723): Composite 0.00 to 120.00 IRE,
// Luminance 10.00 to 108.45 IRE, RGB Gamut 16.00 to 235.00 8 Bit.
//
// Levels are measured on 8-bit video levels, reference black 16 and reference white 235 ("the 8-bit RGB values for
// reference black and reference white are 16 and 235 respectively", p.722). Ours: IRE and percent put 0 at 16 and 100
// at 235 (no setup, as the Color Info tool's IRE), millivolts 0 at 16 and 700 at 235; luma and the composite signal
// are Rec. 601's ("all limiting by the Safe Color Limiter effect is performed in the 601 color space", p.728), the
// composite signal's extremes being luma plus and minus the chroma's amplitude (U = 0.492 (B - Y), V = 0.877 (R - Y)).

#include <QJsonObject>
#include <QString>

#include <array>

namespace lumenis::model {

// The Units buttons (p.722): "8 Bit", "%", "IRE" and "mVolts"; Composite only IRE and mVolts.
enum class SafeUnit { Bit8, Percent, Ire, Millivolts };
QString safeUnitKey(SafeUnit unit);       // "bit8", "percent", "ire", "mvolts" (the setting's values)
SafeUnit safeUnitFrom(const QString& key); // unknown: Bit8
// An 8-bit video level in a unit, and back.
double toSafeUnit(double level8, SafeUnit unit);
double fromSafeUnit(double value, SafeUnit unit);

struct SafeRange {
    double low = 0.0;
    double high = 0.0;
    SafeUnit unit = SafeUnit::Bit8;
    // The limits as 8-bit video levels.
    double low8() const { return fromSafeUnit(low, unit); }
    double high8() const { return fromSafeUnit(high, unit); }
    bool operator==(const SafeRange&) const = default;
};

struct SafeColorLimits {
    SafeRange composite{0.0, 120.0, SafeUnit::Ire};
    SafeRange luma{10.0, 108.45, SafeUnit::Ire};
    SafeRange rgb{16.0, 235.0, SafeUnit::Bit8};
    bool warnCompositeLuma = false; // the top Actions button
    bool warnRgb = false;           // the bottom one
    // "If Warn is selected in one or more of the Actions menus, the icon on the Safe Color Settings button appears
    // orange" (p.722).
    bool warns() const { return warnCompositeLuma || warnRgb; }
    // The Safe Colors setting's values (keys compositeLow ... rgbAction) and back; missing keys keep the defaults.
    static SafeColorLimits fromValues(const QJsonObject& values);
    QJsonObject toValues() const;
    bool operator==(const SafeColorLimits&) const = default;
};

// Which limits a colour, or a picture, goes past, low and high.
struct SafeViolations {
    bool compositeLow = false, compositeHigh = false;
    bool lumaLow = false, lumaHigh = false;
    std::array<bool, 3> rgbLow{false, false, false};
    std::array<bool, 3> rgbHigh{false, false, false};
    bool any() const;
    // Joins another pixel's.
    void add(const SafeViolations& other);
    bool operator==(const SafeViolations&) const = default;
};

// A colour's levels (8-bit R, G, B as the pictures hold them): its Rec. 601 luma, and its composite signal's lowest and
// highest excursions, as 8-bit video levels.
struct SafeLevels {
    double luma = 0.0;
    double compositeLow = 0.0;
    double compositeHigh = 0.0;
};
SafeLevels safeLevels(int r, int g, int b);
// The limits a colour exceeds, every limit type counted whatever its Action.
SafeViolations safeViolations(int r, int g, int b, const SafeColorLimits& limits);

} // namespace lumenis::model

#pragma once

// The Safe Color Limiter effect (FX Guide p.471, p.545-546, p.727-730; docs/spec/color-correction.md, 6.10e4c): an Image
// effect on single-layer segments that "modifies color and luminance values in video material to bring the material
// within the safe color limits you define". Its parameters, under "Safe Color Limiter Parameters", "parallel the options
// available in the Safe Color settings" (safe_colors.h): Composite L/H with Composite units, Luma L/H with Luminance
// units and an Enable button for both (stored as "Composite/Luma Levels Parameters: Enable"), RGB Gamut L/H with RGB
// Gamut units and their Enable button ("RGB Levels Parameters: Enable"), 422 Safe, and the Source Monitor Analysis
// menu. "When you first apply a Safe Color Limiter effect, its parameters default to the current limits in the Safe
// Color settings" (p.728): seedSafeColorLimiter writes them.
//
// The limiting itself is ours (the guide gives no formula), on 8-bit video levels as safe_colors.h measures them and in
// Rec. 601 ("all limiting by the Safe Color Limiter effect is performed in the 601 color space", p.728): luma moved into
// its limits keeping the chroma, chroma reduced (towards grey, keeping luma) until the composite signal's extremes are
// within theirs, then R, G and B clamped to the gamut. 422 Safe ("every pixel in every frame is guaranteed to be within
// your safe color ranges", p.545) repeats the three steps until the pixel is within every enabled limit; off, they run
// once, so "a small number of transient pixel values might exceed your safe color ranges".

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/safe_colors.h"
#include "lumenis/model/sequence.h"

#include <array>

namespace lumenis::model {

inline const QString kSafeColorLimiterCategory = QStringLiteral("Safe Color Limiter Parameters");

bool isSafeColorLimiter(const Effect& effect);

// The units menus' entries (p.545-546): "8 Bit", "%", "IRE", "mVolts".
QString safeUnitMenuText(SafeUnit unit);
SafeUnit safeUnitFromMenu(const QString& text); // unknown: 8 Bit

// What the effect limits at a time: its limits, which limit types are enabled, and 422 Safe.
struct SafeLimiting {
    SafeColorLimits limits;
    bool compositeLuma = true; // the Composite/Luma Levels Enable button
    bool rgb = true;           // the RGB Levels Enable button
    bool exact = false;        // 422 Safe
    // Whether the Source monitor shows the analysis display ("Highlight Out of Range Colors", the default).
    bool analysis = true;
};
// The effect's limiting at `t` (keyframed sliders through numberAt); parameters not set read as the json's defaults,
// which are the Safe Colors setting's own (SafeColorLimits{}).
SafeLimiting safeColorLimiting(const Effect& effect, double t, const EffectSpan& span);

// Writes `limits` into the effect's parameters (the Safe Colors setting's, when the effect is applied).
void seedSafeColorLimiter(Effect& effect, const SafeColorLimits& limits);

// A colour (8-bit video levels R, G, B, 16 black and 235 white) brought within the enabled limits.
std::array<double, 3> limitSafeLevels(const std::array<double, 3>& levels, const SafeLimiting& limiting);

} // namespace lumenis::model

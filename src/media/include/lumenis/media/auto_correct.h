#pragma once

// The Color Correction tool's automatic corrections (FX Guide p.618-622; docs/spec/color-correction.md, 6.10c2): the
// Auto Balance, Auto Black, Auto Contrast and Auto White buttons of the HSL group, the Curves tab's Auto Balance and
// Auto Contrast, and the Remove Color Cast buttons. "Media Composer analyzes the currently selected frame in the
// segment and adjusts the appropriate color correction controls" (p.621): each one measures a frame through the
// correction it is about to change and sets that correction's controls, which can be adjusted by hand afterwards.
// The AutoCorrect options of the Color Correction effect (6.10e) use them too.

#include "lumenis/model/color_correction.h"

#include <QColor>
#include <QImage>

namespace lumenis::media {

enum class AutoCorrection {
    // The HSL group's (the Hue Offsets tab, p.621; Black, Contrast and White also in Controls > Master, p.620).
    HslBalance,  // "Adjusts all three ChromaWheel controls"
    HslBlack,    // "Adjusts the Setup slider control"
    HslContrast, // "Adjusts the Gain and Setup slider controls"
    HslWhite,    // "Adjusts the Gain slider control"
    // The Curves tab's (p.621).
    CurvesBalance,  // "Makes adjustments to the Red, Green, and Blue curves to balance the colors"
    CurvesContrast, // "Makes an adjustment to the Master curve to maximize the tonal range"
};

// Measures `frame` (uncorrected, full-range R'G'B' standing for video levels) through `correction` and changes the
// controls `kind` adjusts. False when there is nothing to measure or nothing to change. Ours: the darkest and brightest
// half percent of the pixels are left out of the tonal range, and the balances make the pixels of each luminance range
// (HSL) or each channel's range (Curves) average to neutral. The HSL Black, Contrast and White buttons set the Master
// range's Gain and Setup through the Hue Offsets tab (`hueOffsets`, which turns that tab's Enable button on too, p.603)
// or the Controls tab's Master tab.
bool autoCorrect(model::ColorCorrection& correction, const QImage& frame, AutoCorrection kind, bool hueOffsets = true);

// The Remove Color Cast buttons (p.621-622): below the Highlights, Midtones and Shadows ChromaWheels, which adjust only
// that wheel, and in the Curves tab, which "works by adding a point to each of the Red, Green, and Blue curves that
// makes the area you identify with the eyedropper color neutral" (up to the 16 point limit). `sampled` is the colour
// the eyedropper picked from the corrected picture. False when the colour is outside the wheel's range or no point can
// be added.
enum class CastRemoval { Shadows, Midtones, Highlights, Curves };
bool removeColorCast(model::ColorCorrection& correction, const QColor& sampled, CastRemoval where);

} // namespace lumenis::media

#pragma once

// Colour corrections' pictures (ROADMAP 6.10; FX Guide ch.20; docs/spec/color-correction.md): a correction
// (lumenis/model/color_correction.h) applied to a picture. 6.10a1: the HSL group's Controls (per luminance range),
// Hue Offsets ChromaWheels and Luma Ranges. 6.10a2: the Channels, Levels and Curves groups, after the HSL group on R'G'B'.
// 6.10a3: the Secondary group's vectors, last, on Y'CbCr.
//
// Ours: pixels are taken to video levels in Y'CbCr (Rec. 709, Y' 16-235) where the guide's numbers are 8-bit, worked on
// there and brought back. Each range's adjustments are made in turn, blended by the share of that range the pixel's
// brightness has (model::lumaRangeWeights); the Master range applies to every pixel.

#include "lumenis/media/compositor.h"
#include "lumenis/model/color_correction.h"

#include <memory>

namespace lumenis::media {

// A correction read once into numbers, for many pixels (ROADMAP 10.1b; docs/spec/performance.md): the controls are
// kept by path in the correction, and reading them by path for each pixel made a 1080p frame take over a minute.
// pixel() gives exactly what colorCorrectedPixel() gives. Copies share the numbers; the plan does not refer to the
// correction it was made from.
class CorrectionPlan {
public:
    explicit CorrectionPlan(const model::ColorCorrection& correction);
    ~CorrectionPlan();
    CorrectionPlan(const CorrectionPlan&);
    CorrectionPlan& operator=(const CorrectionPlan&);
    // Whether the correction leaves every pixel as it is (no values).
    bool isEmpty() const;
    // One pixel (straight RGB, 0 to 1) through the correction.
    std::array<double, 3> pixel(const std::array<double, 3>& rgb) const;

    struct Data;

private:
    std::shared_ptr<const Data> d_;
};

// The picture through the correction, rows shared out among the processor's threads.
Picture colorCorrectedPicture(const model::ColorCorrection& correction, const Picture& picture);
// One pixel (straight RGB, 0 to 1) through the correction; for tests and the Color Match control (6.10c). For many
// pixels make a CorrectionPlan once.
std::array<double, 3> colorCorrectedPixel(const model::ColorCorrection& correction, const std::array<double, 3>& rgb);
// A colour's place on the Secondary group's wheels: (hue in degrees, 0 red, green +120; saturation 0-100, 100 at a
// chroma of half the full range, ours). For the syringe and eyedroppers (model::syringeVector).
QPointF wheelHueSaturation(const std::array<double, 3>& rgb);
// Back: the colour at a place on the wheels with luma `y` (0 to 1), clipped to the RGB cube; the Selected Vectors
// panel's swatches (FX Guide p.674).
std::array<double, 3> wheelColor(double hue, double saturation, double y = 0.5);

} // namespace lumenis::media

#pragma once

// Colour corrections' pictures (ROADMAP 6.10; FX Guide ch.20; docs/spec/color-correction.md): a correction
// (lumenis/model/color_correction.h) applied to a picture. 6.10a1: the HSL group's Controls (per luminance range),
// Hue Offsets ChromaWheels and Luma Ranges.
//
// Ours: pixels are taken to video levels in Y'CbCr (Rec. 709, Y' 16-235) where the guide's numbers are 8-bit, worked on
// there and brought back. Each range's adjustments are made in turn, blended by the share of that range the pixel's
// brightness has (model::lumaRangeWeights); the Master range applies to every pixel.

#include "lumenis/media/compositor.h"
#include "lumenis/model/color_correction.h"

namespace lumenis::media {

Picture colorCorrectedPicture(const model::ColorCorrection& correction, const Picture& picture);
// One pixel (straight RGB, 0 to 1) through the correction; for tests and the Color Match control (6.10c).
std::array<double, 3> colorCorrectedPixel(const model::ColorCorrection& correction, const std::array<double, 3>& rgb);

} // namespace lumenis::media

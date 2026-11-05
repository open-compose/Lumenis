#pragma once

// The Key effects' matte arithmetic (key_effects.cpp; docs/spec/keys.md), in a translation unit of its own with no Qt
// or compositor headers. Why: MSVC 14.44 failed with an internal compiler error (C1001) in key_effects.cpp's RGB Keyer
// again and again, each time a widely included header changed (CI runs 219, 250, 317, 318, 319; BUILDING.md), even with
// the optimizer off there. The loops it crashed on now live here, written plainly (no lambdas), so key_effects.cpp
// only reads the parameters and calls them.

#include <cstddef>
#include <vector>

namespace lumenis::media::detail {

// A matte blurred by a box of `radius` pixels, then kept as it is, or taken at its smaller (Erode) or larger (Dilate)
// value against the original. `matte` holds width x height values, row by row.
enum class MatteBlur { Blur, Erode, Dilate };
void blurMatte(std::vector<double>& matte, int width, int height, int radius, MatteBlur mode);

// The RGB Keyer's matte (FX Guide p.474, p.550-551) for `count` premultiplied RGBA float pixels: each pixel's distance
// in RGB from the key colour keyed under `threshold` with `soft` blended (1 everywhere when `threshold` is 0), and
// `spill` of the key colour's own channel (`spillChannel` 0 red, 1 green, 2 blue) taken down towards the others' level,
// in place. `matte` is resized to `count`.
void rgbKeyMatte(float* pixels, std::size_t count, double keyRed, double keyGreen, double keyBlue, double threshold, double soft,
                 double spill, int spillChannel, std::vector<double>& matte);

// The RGB Keyer's matte finished: inverted when `reverse` ("Inverts the key"), blurred by `blur` pixels, eroded by
// `erode` pixels when positive, then scaled by `level` (p.550-551).
void finishMatte(std::vector<double>& matte, int width, int height, bool reverse, int blur, int erode, double level);

} // namespace lumenis::media::detail

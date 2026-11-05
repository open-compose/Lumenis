// The Key effects' matte arithmetic, kept apart from key_effects.cpp for MSVC's sake. See key_mattes.h.

#include "key_mattes.h"

#include <algorithm>
#include <cmath>

namespace lumenis::media::detail {

namespace {

// 0 below `from`, 1 above `to`, linear between (a step when they meet).
double ramp(double v, double from, double to) {
    if (to <= from) return v < from ? 0.0 : 1.0;
    return std::clamp((v - from) / (to - from), 0.0, 1.0);
}

// The index of position `i` (clamped to the line) along line `line`, across (horizontal) or down.
std::size_t matteIndex(bool horizontal, int line, int i, int n, int width) {
    const int c = std::clamp(i, 0, n - 1);
    return static_cast<std::size_t>(horizontal ? line * width + c : c * width + line);
}

} // namespace

void blurMatte(std::vector<double>& matte, int width, int height, int radius, MatteBlur mode) {
    if (radius <= 0 || width <= 0 || height <= 0) return;
    const std::vector<double> original = matte;
    std::vector<double> tmp(matte.size());
    for (int pass = 0; pass < 2; ++pass) {
        const bool horizontal = pass == 0;
        const int n = horizontal ? width : height;
        const int lines = horizontal ? height : width;
        for (int l = 0; l < lines; ++l) {
            double sum = 0;
            for (int i = -radius; i <= radius; ++i) sum += matte[matteIndex(horizontal, l, i, n, width)];
            for (int i = 0; i < n; ++i) {
                tmp[matteIndex(horizontal, l, i, n, width)] = sum / (2.0 * radius + 1.0);
                sum += matte[matteIndex(horizontal, l, i + radius + 1, n, width)] - matte[matteIndex(horizontal, l, i - radius, n, width)];
            }
        }
        matte.swap(tmp);
    }
    if (mode == MatteBlur::Erode) {
        for (std::size_t i = 0; i < matte.size(); ++i) matte[i] = std::min(matte[i], original[i]);
    } else if (mode == MatteBlur::Dilate) {
        for (std::size_t i = 0; i < matte.size(); ++i) matte[i] = std::max(matte[i], original[i]);
    }
}

void rgbKeyMatte(float* pixels, std::size_t count, double keyRed, double keyGreen, double keyBlue, double threshold, double soft,
                 double spill, int spillChannel, std::vector<double>& matte) {
    matte.assign(count, 1.0);
    for (std::size_t i = 0; i < count; ++i) {
        float* px = pixels + i * 4;
        const double alpha = static_cast<double>(px[3]);
        double r = alpha > 0 ? static_cast<double>(px[0]) / alpha : 0.0;
        double g = alpha > 0 ? static_cast<double>(px[1]) / alpha : 0.0;
        double b = alpha > 0 ? static_cast<double>(px[2]) / alpha : 0.0;
        const double dr = r - keyRed;
        const double dg = g - keyGreen;
        const double db = b - keyBlue;
        const double d = std::sqrt((dr * dr + dg * dg + db * db) / 3.0);
        if (spill > 0) {
            double& strongest = spillChannel == 0 ? r : spillChannel == 1 ? g : b;
            const double first = spillChannel == 0 ? g : r;
            const double second = spillChannel == 2 ? g : b;
            const double others = first > second ? first : second;
            if (strongest > others) {
                strongest -= (strongest - others) * spill;
                px[0] = static_cast<float>(r * alpha);
                px[1] = static_cast<float>(g * alpha);
                px[2] = static_cast<float>(b * alpha);
            }
        }
        matte[i] = threshold <= 0.0 ? 1.0 : ramp(d, threshold, threshold + soft);
    }
}

} // namespace lumenis::media::detail

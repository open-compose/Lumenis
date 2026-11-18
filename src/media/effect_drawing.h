#pragma once

#include "lumenis/media/compositor.h"
#include "lumenis/model/effect_keyframes.h"

// Drawing shared by the 2D effects (wipes.cpp, image_effects.cpp; docs/spec/wipes.md,
// docs/spec/image-effects.md): sampling a picture at a point, the distance to a box, and the Border
// band (FX Guide p.493) drawn outside a picture's edge. Internal to lumenis_media.
namespace lumenis::media::detail {

// Signed distance from (x, y) to the box [x0, x1] x [y0, y1]: negative inside.
double boxDistance(double x, double y, double x0, double y0, double x1, double y1);

// The bilinear sample of `p` at (x, y), pixel centres at whole numbers + 0.5; transparent outside it.
// Edge pixels are held out to the picture's edge, so a whole picture is opaque to its border.
// Inline, and without std::floor (a library call without SSE4.1, the x86-64 baseline): the 3D Warp and the wipes call
// it for every pixel, and it was over a quarter of a 3D Warp frame (10.1b). The point is never left of -0.5 here, so a
// truncation stepped down where it overshot is the floor.
inline void sample(const Picture& p, double x, double y, float out[4]) {
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    const int w = p.width();
    const int h = p.height();
    if (p.isNull() || x < 0 || y < 0 || x > w || y > h) return;
    const double fx = x - 0.5;
    const double fy = y - 0.5;
    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    if (static_cast<double>(x0) > fx) --x0;
    if (static_cast<double>(y0) > fy) --y0;
    const float ax = static_cast<float>(fx - x0);
    const float ay = static_cast<float>(fy - y0);
    const int xa = x0 < 0 ? 0 : (x0 > w - 1 ? w - 1 : x0);
    const int xb = x0 + 1 < 0 ? 0 : (x0 + 1 > w - 1 ? w - 1 : x0 + 1);
    const int ya = y0 < 0 ? 0 : (y0 > h - 1 ? h - 1 : y0);
    const int yb = y0 + 1 < 0 ? 0 : (y0 + 1 > h - 1 ? h - 1 : y0 + 1);
    // The corners in the order the loops took them before (x first, then y), so the sums are the same.
    const float* s00 = p.pixel(xa, ya);
    const float* s10 = p.pixel(xb, ya);
    const float* s01 = p.pixel(xa, yb);
    const float* s11 = p.pixel(xb, yb);
    const float w00 = (1.0f - ax) * (1.0f - ay), w10 = ax * (1.0f - ay), w01 = (1.0f - ax) * ay, w11 = ax * ay;
    for (int c = 0; c < 4; ++c) {
        float v = 0.0f;
        v += s00[c] * w00;
        v += s10[c] * w10;
        v += s01[c] * w01;
        v += s11[c] * w11;
        out[c] = v;
    }
}

// An effect's Border parameters at a time, in pixels of a picture `width` wide. Ours: Width 100 and
// Soft 255 are each a sixteenth of the picture's width; the colour is white until set; the band runs
// from Color at the inner edge to Blend Color at its outer edge unless Fixed Border is on ("Turns off
// the Blending option").
struct Border {
    double width = 0.0; // the band, in pixels
    double edge = 1.0;  // the softness of edges, at least a pixel so they are not jagged
    model::Rgb colour{1.0, 1.0, 1.0};
    model::Rgb blend{1.0, 1.0, 1.0};

    static Border of(const model::Effect& effect, double time, const model::EffectSpan& span, int width);
    // Draws into the premultiplied pixel `o` the band and then `in` (the foreground picture's sample)
    // for a pixel `distance` pixels outside the foreground's region (negative inside), with the
    // foreground's opacity `level`.
    void draw(float* o, double distance, const float in[4], float level = 1.0f) const;
};

// The Color Effect (color_effects.cpp).
// `layer` by value: a caller done with its picture moves it in, and the effect works in it (10.1b).
Picture colorEffectPicture(const model::Effect& effect, Picture layer, double time, const model::EffectSpan& span);
// The Safe Color Limiter (color_effects.cpp; model/safe_color_limiter.h): every pixel's colour, read as the video levels
// it stands for (0 as 16, 1 as 235), brought within the effect's enabled limits.
Picture safeColorLimiterPicture(const model::Effect& effect, Picture layer, double time, const model::EffectSpan& span);
// The Blur Effect and Mosaic Effect over the whole picture (color_effects.cpp); intraframe.cpp keeps them to their
// objects.
Picture detailEffectPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
// A near-Gaussian blur (three box blurs) of radius rx pixels across and ry down.
void blurPicture(Picture& picture, int rx, int ry);

// The Key effects (key_effects.cpp): Luma Key and Chroma Key, the RGB Keyer and SpectraMatte, each
// keying the segment's picture so what is below shows through.
Picture lumaChromaKeyPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
Picture rgbKeyerPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
Picture spectraMattePicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);

// The Illusion FX segment effects that colour, texture or draw (illusion_fx.cpp): whether `effect`
// is one, its values until set, and its picture.
bool isIllusionSegmentEffect(const model::Effect& effect);
QJsonValue illusionDefault(const model::Effect& effect, const QString& category, const QString& name);
Picture illusionSegmentPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
// The Illusion FX transitions' values until set (illusion_transitions.cpp).
QJsonValue illusionTransitionDefault(const model::Effect& effect, const QString& category, const QString& name);

} // namespace lumenis::media::detail

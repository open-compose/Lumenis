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
void sample(const Picture& p, double x, double y, float out[4]);

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
Picture colorEffectPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
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

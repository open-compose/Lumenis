#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/effect.h"
#include "lumenis/model/sequence.h"

#include <QString>

#include <cstdint>
#include <optional>

// Matte Key clips (9.2b; docs/spec/graphics-import.md; Editing Guide p.218, p.1196): what importing a graphic or a movie
// with an alpha channel makes when the Import setting's Alpha Channel option is not Ignore ("If the graphic uses a
// transparent background or an alpha channel, Media Composer creates a matte key", p.218). "Importing an image with
// alpha channel creates a matte key effect as a single frame" (p.1196).
//
// Ours, as a Title Effect clip is made (title_clip.h; the Media Tool lists "Precompute Clips - Titles and Matte Keys"
// together, p.346): a bin item of kind Title whose composition is one track, V1, holding one segment: filler with the
// Matte Key effect (FX Guide p.474) and a nest of two tracks, V1 the fill precompute and V2 the matte precompute. The
// precomputes are RenderedEffect master clips, one frame for a still; the matte holds the alpha as grey, white opaque.
// The compositor keys the fill over what lies below by the matte (matteKeyClipLayer), with the Matte Key effect's
// Foreground parameters.
namespace lumenis::model {

// The effect's own parameter category (ours): {"Fill": id, "Matte": id, "Bin": the precomputes' bin, "Frames": their
// length, "Linked Alpha": for a linked file, how its alpha is read}.
inline const QString kMatteKeyClipParameters = QStringLiteral("Matte Key Clip");

struct MatteKeyMedia {
    QString binId;
    QString fill;
    QString matte;
    std::int64_t frames = 0; // the precomputes' frames: 1 for a still, which every frame of the clip shows
    // A linked file with alpha (9.2e2; p.318, p.1204): fill and matte are one linked clip whose frames carry the alpha,
    // keyed as it plays; its Alpha Channel option, "invert" or "noInvert" (media::alphaHandlingOf). Empty for precomputes.
    QString linkedAlpha;
    bool isLinked() const { return !linkedAlpha.isEmpty(); }
    bool isEmpty() const { return fill.isEmpty() || matte.isEmpty(); }
};

// A Matte Key clip `name` of `length` frames at `rate` playing `media`. `level` is the Foreground's Level, 0-100
// (p.220: "Importing converts Opacity to Foreground level in the Matte Key effect").
BinItem matteKeyClip(const QString& name, const Rational& rate, std::int64_t length, const MatteKeyMedia& media,
                     double level = 100.0);
// Whether an effect is a Matte Key clip's (has its precomputes), and the precomputes it plays.
bool isMatteKeyClipEffect(const Effect& effect);
MatteKeyMedia matteKeyMediaOf(const Effect& effect);
// Whether a bin item is a Matte Key clip.
bool isMatteKeyClip(const BinItem& item);
// Its effect segment; nothing for other items.
std::optional<Segment> matteKeyClipSegment(const BinItem& item);

} // namespace lumenis::model

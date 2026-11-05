#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/correction_shapes.h"
#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/intraframe.h"
#include "lumenis/model/sequence.h"

#include <QColor>
#include <QImage>
#include <QJsonValue>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The compositor (ROADMAP Phase 6.1; docs/spec/compositor.md): a sequence's picture at one frame, made
// from its video tracks bottom to top. Each track gives the picture of the segment at that frame,
// through the segment's effect, or across a cut the blend its transition effect makes of the outgoing
// and incoming segments (FX Guide p.33). Pictures are worked on in floating point with premultiplied
// alpha; filler is transparent, so lower tracks show through it, and black shows where no track has a
// picture. The monitors, Full Screen Playback and output take sequence frames from here; the effects
// themselves come step by step (Phase 6.1 the dissolve family and Superimpose, 6.3a the 2D transitions), and an effect not built
// yet leaves the picture as it is (a transition: a cut at the cut point).
namespace lumenis::media {

// Effects processing (FX Guide p.151; the Render settings' Effects Processing, 2026.8): after each
// effect the picture is rounded to 8 or 16 bits a channel, or kept in 32-bit floating point.
enum class EffectPrecision { Bits8, Bits16, Bits32 };

// The Render setting's Image Interpolation (Editing Guide p.1214): "filtering quality vs. speed" when a
// picture is resized to the frame. Ours: Advanced (Polyphase) and Standard (Bilinear) both filter
// (Qt's smooth scaling, which averages when shrinking); Draft (Nearest Neighbor) takes the nearest pixel.
enum class ImageInterpolation { Advanced, Standard, Draft };

// A picture being composited: width x height pixels of red, green, blue and alpha, 0 to 1,
// premultiplied by alpha, row by row.
class Picture {
public:
    Picture() = default;
    Picture(int width, int height); // transparent
    static Picture solid(int width, int height, const QColor& color);
    // `image` fitted inside width x height, keeping its shape and centred; the rest transparent.
    // `interpolation` says how it is resized.
    static Picture fromImage(const QImage& image, int width, int height,
                             ImageInterpolation interpolation = ImageInterpolation::Advanced);

    int width() const { return width_; }
    int height() const { return height_; }
    bool isNull() const { return width_ <= 0 || height_ <= 0; }
    float* pixel(int x, int y) { return &data_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4]; }
    const float* pixel(int x, int y) const {
        return &data_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4];
    }
    std::vector<float>& data() { return data_; }
    const std::vector<float>& data() const { return data_; }

    // Each channel rounded to the precision's steps.
    void quantize(EffectPrecision precision);
    // Over black, as an opaque image.
    QImage toImage() const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<float> data_;
};

// The blends the effects are made of. All take pictures of one size.
// `a` towards `b` by t (0: a, 1: b).
Picture mix(const Picture& a, const Picture& b, double t);
// `top` over `bottom`.
Picture over(const Picture& top, const Picture& bottom);
// `picture` with its opacity multiplied by `level` (0 to 1).
Picture faded(const Picture& picture, double level);
// The film blend of Film Dissolve and Film Fade (FX Guide p.462): ours, the mix done in density
// (log10 of the light), which, as film does, darkens the middle of a dissolve between two bright
// pictures.
Picture filmMix(const Picture& a, const Picture& b, double t);

// The 2D transitions (ROADMAP 6.3a; docs/spec/wipes.md, wipes.cpp): the Box Wipe, Conceal, Edge Wipe,
// L-Conceal, Matrix Wipe, Push, Sawtooth Wipe, Shape Wipe, Spin and Squeeze categories (FX Guide
// p.459-490). True when `effect` is one of them.
bool isWipe(const model::Effect& effect);
// `outgoing` to `incoming` by the wipe `effect` at `progress` (0: all outgoing, 1: all incoming), with
// its Border and Foreground parameters (p.493, p.495) at `time` of `span`. Pictures of one size.
Picture wipePicture(const model::Effect& effect, Picture outgoing, Picture incoming, double progress, double time,
                    const model::EffectSpan& span);

// The Illusion FX transitions (ROADMAP 6.3d4; docs/spec/illusion-fx.md, illusion_transitions.cpp):
// Iris, Melt, Pagecurl, Particle Blast, Particle Orbit, Particle Wind, Random Blend, Rollup and Shear
// (FX Guide p.466-467). True when `effect` is one of them.
bool isIllusionTransition(const model::Effect& effect);
// `outgoing` to `incoming` by the Illusion FX transition `effect` at `progress` (0 to 1; its Amount
// until that is set) and `time` of `span`. Pictures of one size.
Picture illusionTransitionPicture(const model::Effect& effect, Picture outgoing, Picture incoming, double progress, double time,
                                  const model::EffectSpan& span);

// The segment effects that change a segment's own picture by geometry (ROADMAP 6.3b1;
// docs/spec/image-effects.md, image_effects.cpp): Picture-in-Picture, Flip, Flop, Flip-Flop, Field Swap,
// Resize, Mask, the Film masks and Blowup; (6.3b2) the Color Effect, Blur Effect and Mosaic Effect; and
// (6.3c, docs/spec/keys.md) Luma Key, Chroma Key, RGB Keyer and SpectraMatte; and (6.3d2,
// docs/spec/illusion-fx.md) the Illusion FX segment effects that colour, texture or draw. True when
// `effect` is one of them.
bool isImageEffect(const model::Effect& effect);
// `layer` through such an effect at `time` of `span`: the picture moved, cropped or masked, transparent
// where Picture-in-Picture leaves the tracks below to show and the Background Color where the others
// leave no picture. Picture-in-Picture's Foreground Level is applied.
Picture imageEffectPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
// The 3D effects (ROADMAP 6.7a; docs/spec/3d-effects.md, warp3d.cpp): the 3D Warp effect (Blend, FX Guide
// p.457) and the Xpress 3D effects (p.489), and effects promoted to 3D (model::promoteTo3D). True when `effect`
// is one of them.
bool is3DEffect(const model::Effect& effect);
// `foreground` placed in 3D by such an effect at `time` of `span`, with its shadow, over `background` (the
// tracks below, or the outgoing video of a transition). Pictures of one size.
Picture warp3dPicture(const model::Effect& effect, const Picture& foreground, const Picture& background, double time,
                      const model::EffectSpan& span);
// The foreground alone placed by a 3D effect at `time` of `span`, on a transparent picture (no shadow, trail
// or background): what Stamp keeps.
Picture warp3dLayer(const model::Effect& effect, const Picture& foreground, double time, const model::EffectSpan& span);
// Stamp (FX Guide p.298-299, p.584-585; docs/spec/3d-effects.md): the effect times (0 to 1) of a 3D effect's
// Stamp Image and Clear presses, as the Effect Editor stores them under the Stamp category ("Stamps", "Clears").
QList<double> stampTimes(const model::Effect& effect);
QList<double> clearTimes(const model::Effect& effect);
// Pan and Scan (ROADMAP 6.7b1; FX Guide p.230-238, p.480, p.564; docs/spec/pan-scan.md, pan_scan.cpp): a Reformat
// effect on its segment and every track below it. panScanFrames() gives, in a frame of `size`, where the source
// picture lies (its Source ratio), the wire frame (the Target's ratio, Scaling and Position) and where the output
// picture goes (the Target picture); panScanPicture() fills the Target picture with the wire frame's contents.
bool isPanAndScan(const model::Effect& effect);
struct PanScanFrames {
    QRectF source;
    QRectF wire;
    QRectF target;
};
std::optional<PanScanFrames> panScanFrames(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size);
Picture panScanPicture(const model::Effect& effect, const Picture& picture, double time, const model::EffectSpan& span);
// Avid Pan & Zoom (ROADMAP 6.7b2; FX Guide p.239-247, p.468, p.528-531; docs/spec/pan-zoom.md, pan_zoom.cpp): a
// still image ("Image" in its parameters: the file's path) seen through a field of view. panZoomView() gives, in
// a frame of `size`, where the whole image is fitted (Zoom Factor 1), the field of view in that picture, the
// zoom and the Display and Show Safe Action settings; nothing without a readable image. panZoomPicture() draws
// the effect over its segment (the placeholder, or the Background Video); `preview` in the Effect Preview, where
// Display Source shows the whole image. freePanZoomCache() forgets a cached image (all with an empty path), as
// the Cache menu's Free Current Cache and Free All Caches do (p.247).
bool isPanAndZoom(const model::Effect& effect);
QString panZoomImagePath(const model::Effect& effect);
QSize panZoomImageSize(const QString& path);
struct PanZoomView {
    QSize imageSize;
    QRectF fitted; // the whole image at Zoom Factor 1, centred
    QRectF field;  // the field of view, in the same picture
    double zoom = 1.0;
    bool sourceDisplay = false;
    bool safeAction = false;
};
std::optional<PanZoomView> panZoomView(const model::Effect& effect, double time, const model::EffectSpan& span, const QSizeF& size);
Picture panZoomPicture(const model::Effect& effect, const Picture& segment, double time, const model::EffectSpan& span, bool preview);
void freePanZoomCache(const QString& path = {});
// Where the field of view is at `time` (ROADMAP 6.7b2b; p.242-245, p.530): Position X, Y and the Zoom Factor
// between their keyframes as Velocity In and Out at each keyframe (Linear, Constant, Ease In, Ease Out, Hold;
// stored as keyframes of those menus, or once for the effect) and the Path (Linear or Spline) have them move.
struct PanZoomMotion {
    double x = 0.0;
    double y = 0.0;
    double zoom = 1.0;
};
PanZoomMotion panZoomMotion(const model::Effect& effect, double time, const model::EffectSpan& span);
// The Reformat effects that work "on all tracks below the track to which you apply it" (FX Guide
// p.229): 16:9 Letterbox, 14:9 Letterbox and 4:3 Sidebar.
bool isReformat(const model::Effect& effect);
// `picture`, everything at and below the effect's track, reformatted.
Picture reformatPicture(const model::Effect& effect, const Picture& picture, double time, const model::EffectSpan& span);
// The value a parameter has until it is set, where the effect gives it one of its own (ours, said in
// docs/spec/image-effects.md): Scaling 100, or 50 for Picture-in-Picture; 14:9 Letterbox's Position H
// 50; the Color Effect's gains 100; Blur's and Mosaic's Opac. 100, Hor and Vert 10; the keys' colours,
// Gain, Soft, Level and Opacity (docs/spec/keys.md). Undefined otherwise. The Effect Editor shows the same values.
QJsonValue parameterDefault(const model::Effect& effect, const QString& category, const QString& name);

// What one track shows at a frame: a segment, or a transition between the outgoing and incoming
// segments at a cut. Frames of the segments are counted from their clips' starts.
struct TrackFrame {
    const model::Segment* segment = nullptr; // the segment, or the incoming one in a transition
    std::int64_t frame = 0;
    std::int64_t offset = 0; // frames into the segment (into the transition, in a transition)
    // Frames into the segment and into the outgoing one, for Timewarps' format conversions.
    std::int64_t segmentOffset = 0;
    std::int64_t outgoingOffset = 0;
    const model::Segment* outgoing = nullptr; // only in a transition
    std::int64_t outgoingFrame = 0;
    // How much of the next source frame is mixed in (segmentBlend), for the segment and the outgoing one.
    double blend = 0.0;
    double outgoingBlend = 0.0;
    const model::Transition* transition = nullptr;
    // How far through the transition: (frames into it + 1) / (its length + 1), so that neither end
    // shows only one side (ours).
    double progress = 0.0;
};
// The frame of its clip a segment shows `offset` frames into it: sourceIn + offset, or with a Motion
// Effect (model::motionSpeed) sourceIn + offset × speed, toward sourceIn (FX Guide p.168).
std::int64_t segmentClipFrame(const model::Segment& segment, std::int64_t offset);
// For a Timewarp of a blending Type (model::blendsFrames), the fraction of the way from that frame to
// the next; 0 otherwise.
double segmentBlend(const model::Segment& segment, std::int64_t offset);
// What `track` of `sequence` shows at `frame`; nothing past its end.
std::optional<TrackFrame> trackFrame(const model::Sequence& sequence, const QString& track, std::int64_t frame);

// A clip's picture for a segment: the frame of the segment's clip, at about `size`. A null image when
// the clip has no picture (no media was ever linked: nothing is drawn); nothing at all when its media is
// offline. FrameSource must not keep the segment.
using FrameSource = std::function<std::optional<QImage>(const model::Segment& segment, std::int64_t clipFrame, QSize size)>;

// What Timecode Burn-In (burn_in.cpp) reads besides the sequence: the sequence's bin item (its name,
// start timecode, edit rate and columns), its marks in the monitor, and a way to find a segment's
// source clip (its start timecode, name and columns). All may be left empty; the displays that need
// them then show nothing.
struct BurnInInfo {
    model::BinItem sequence;
    std::optional<std::int64_t> markIn;
    std::optional<std::int64_t> markOut;
    std::function<std::optional<model::BinItem>(const model::Segment&)> clip;
};

// A rendered effect's picture (FX Guide p.57; docs/spec/rendering.md): the effect's frame `frame` from
// its precompute clips (model::renderedClipAt), at about `size`; nothing when that media is offline (the
// effect then plays unrendered).
using RenderSource = std::function<std::optional<QImage>(const model::EffectRender& render, std::int64_t frame, QSize size)>;

struct Composition {
    const model::Sequence* sequence = nullptr;
    // The video tracks to show, bottom to top (the Video Track Monitor shows its track and those
    // below it, Editing Guide p.627).
    QStringList tracks;
    QSize size;
    EffectPrecision precision = EffectPrecision::Bits16;
    FrameSource source;
    BurnInInfo burnIn;
    // Where rendered effects' pictures come from; without it every effect is computed. A track whose
    // effect has a render in date at the frame (model::isRenderedAt) shows it in place of itself and
    // every track below it.
    RenderSource rendered;
    // Whether effects are computed. Without them (Composer > Render On-the-Fly off, FX Guide p.56: "shows
    // the monitored frame as if no effects are applied to it") segment effects are left out and
    // transitions show their cut; renders in date still play, being media.
    bool effects = true;
    // Whether segments' Color Correction mode corrections (model::segmentCorrection) are applied, after a
    // Timewarp and before every other effect (FX Guide p.597). Off for Dual Split's uncorrected half (p.592).
    bool corrections = true;
    // What Color Correction effects with shapes show (FX Guide p.698; the shape panel's Show Alpha, Show Result and
    // layer buttons in Color Correction mode); Result everywhere else.
    model::CorrectionShapeView shapeView = model::CorrectionShapeView::Result;
    // How source frames and renders are resized to `size` (the Render setting).
    ImageInterpolation interpolation = ImageInterpolation::Advanced;
    // The project's scan, the formats a Timewarp has until its Source and Output are set (model::sourceFormat),
    // and whether a segment's clip has its first field on the even lines (the media's field order; true
    // without one). Timewarps whose formats are not both Progressive are made field by field
    // (media::convertedFrame).
    bool interlaced = false;
    std::function<bool(const model::Segment&)> topFieldFirst;
    // The frame is for Effect mode's Effect Preview monitor, not playing (FX Guide p.48-50): "In Effect mode, the
    // Effect Preview monitor displays your original media and the Pan and Scan wire frame" (p.233), so Pan and
    // Scan effects pass the picture through unchanged; Avid Pan & Zoom's Display Source shows its whole image
    // (p.241).
    bool effectPreview = false;
    // The project's raster. A classic title whose media was made at another shows "Wrong Format" (FX Guide p.376);
    // empty: not checked.
    QSize raster;
};

// Timecode Burn-In (ROADMAP 6.3d1; docs/spec/burn-in.md, burn_in.cpp). Its parameters live under
// "Timecode Burn-In Effect Parameters" as {"Display 1".."Display 3", "Notes"} objects (ours).
bool isBurnIn(const model::Effect& effect);
// The text Display `display` (1 to 3) shows, or the Notes' text (display 0), at sequence frame
// `frame` for the effect on `track`; empty when it is off or has nothing to show.
QString burnInText(const model::Effect& effect, int display, const Composition& composition, const QString& track,
                   std::int64_t frame);
// A display's (1 to 3; 0 the Notes) setting, or the value it has until set (ours,
// docs/spec/burn-in.md); the Effect Editor shows the same.
QJsonValue burnInSetting(const model::Effect& effect, int display, const QString& name);
// The displays drawn over `layer` at `frame`.
Picture burnInPicture(const model::Effect& effect, const Picture& layer, const Composition& composition,
                      const QString& track, std::int64_t frame);

// The Intraframe effects (ROADMAP 6.9; FX Guide ch.15; docs/spec/intraframe.md, intraframe.cpp): the Paint Effect, Blur
// Effect, Mosaic Effect, Scratch Removal and AniMatte with their objects (model/intraframe.h).
// The outline of control points (fractions of the frame) in pixels of a frame `frame` large: lines between straight
// joins, cubic Béziers through the direction handles otherwise.
QPainterPath intraframePath(const QList<model::ControlPoint>& points, bool closed, const QSizeF& frame);
// What of each pixel an object covers at `time` (0 to 1, row by row, `picture`'s size): its outline filled (or a Brush
// stroke drawn with its head, as much of it as Path shows), limited by Magic Mask's colour in `picture`, feathered
// (Hor, Vert, Bias), times its opacity; nothing while it is not visible.
std::vector<float> intraframeMask(const model::Effect& effect, const model::IntraframeObject& object, const Picture& picture, double time,
                                  const model::EffectSpan& span);
// Shape-based colour correction (FX Guide p.697-699; lumenis/model/correction_shapes.h, 6.10e3): `base` (the effect's
// correction at the time) on `layer`; with shapes, `shapes` (the shape layer's correction) on that picture (Serial Mode)
// or on `layer`, mixed in where the shapes cover (each softened by its Softness, the union inverted with Invert Alpha).
// `view` is what Color Correction mode's monitors show (model::CorrectionShapeView): Result as above; Base the base
// correction alone; Shapes the shaped picture inside the shapes over `layer` uncorrected; Alpha the coverage as grey.
Picture colorCorrectedShapesPicture(const model::Effect& effect, const Picture& layer, const model::ColorCorrection& base,
                                    const model::ColorCorrection& shapes, double time, const model::EffectSpan& span,
                                    model::CorrectionShapeView view = model::CorrectionShapeView::Result);
// The shapes' coverage at `time` (0 to 1, `width` x `height`, row by row), softened and inverted as the effect says:
// what the Show Alpha display shows (p.698).
std::vector<float> correctionShapesMask(const model::Effect& effect, int width, int height, double time, const model::EffectSpan& span);
// The effect on its segment's picture `layer` over `below` (the tracks below, or a transition's outgoing picture with
// `layer` the incoming one): Paint's objects painted onto `layer` bottom layer first in their modes, Blur and Mosaic
// within their objects, AniMatte keying `layer` in or out of its mattes over `below`.
//
// Scratch Removal (6.9d, p.419-429) takes its replacement material from `frames`, the segment's other frames: without
// them (a transition, a test) only from this frame.
struct IntraframeFrames {
    std::int64_t offset = 0; // the frame shown, into the segment
    std::int64_t length = 1; // the segment's frames
    std::function<Picture(std::int64_t offset)> pictureAt; // the segment's own picture at a frame of it
    bool interlaced = false; // two-field media: Frame Processing applies
    bool topFieldFirst = true;
};
Picture intraframePicture(const model::Effect& effect, const Picture& layer, const Picture& below, double time, const model::EffectSpan& span,
                          const IntraframeFrames* frames = nullptr);
// Scratch Removal's replacement material for the frame shown (the picture whose pixels, Hor and Vert away, fill each
// object): with Frame Processing (or single-field media) the whole frame the Frame/Field slider and Offset Fast Menu
// name, "from Field 1 of the clean frame to Field 1 of the flawed frame and from Field 2 ... to Field 2"; without it
// each field's lines from the field they name (p.419-421, p.427-428, p.546-547). `category` holds those parameters.
Picture scratchRemovalSource(const model::Effect& effect, const QString& category, const Picture& layer, const IntraframeFrames* frames,
                             double time, const model::EffectSpan& span);
// Export Matte PICT (ROADMAP 6.9c2; FX Guide p.418): an AniMatte effect's matte at `time` as "a high-contrast matte with
// transparent (black) and opaque (white) components" whose alpha channel is the same; `layer` (the foreground) gives
// its size and the colours Magic Mask picks from.
QImage aniMatteImage(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);

// The sequence's picture at `frame`. Sets `missing` when a segment's media is offline.
Picture composeFrame(const Composition& composition, std::int64_t frame, bool* missing = nullptr);
// The same as an image. When one track shows one plain segment (no effect, no transition), the clip's
// frame as the source gives it, without compositing.
QImage composeImage(const Composition& composition, std::int64_t frame, bool* missing = nullptr);

} // namespace lumenis::media

#pragma once

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QColor>
#include <QImage>
#include <QJsonValue>
#include <QSize>
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

// A picture being composited: width x height pixels of red, green, blue and alpha, 0 to 1,
// premultiplied by alpha, row by row.
class Picture {
public:
    Picture() = default;
    Picture(int width, int height); // transparent
    static Picture solid(int width, int height, const QColor& color);
    // `image` fitted inside width x height, keeping its shape and centred; the rest transparent.
    static Picture fromImage(const QImage& image, int width, int height);

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

// The segment effects that change a segment's own picture by geometry (ROADMAP 6.3b1;
// docs/spec/image-effects.md, image_effects.cpp): Picture-in-Picture, Flip, Flop, Flip-Flop, Field Swap,
// Resize, Mask, the Film masks and Blowup. True when `effect` is one of them.
bool isImageEffect(const model::Effect& effect);
// `layer` through such an effect at `time` of `span`: the picture moved, cropped or masked, transparent
// where Picture-in-Picture leaves the tracks below to show and the Background Color where the others
// leave no picture. Picture-in-Picture's Foreground Level is applied.
Picture imageEffectPicture(const model::Effect& effect, const Picture& layer, double time, const model::EffectSpan& span);
// The Reformat effects that work "on all tracks below the track to which you apply it" (FX Guide
// p.229): 16:9 Letterbox, 14:9 Letterbox and 4:3 Sidebar.
bool isReformat(const model::Effect& effect);
// `picture`, everything at and below the effect's track, reformatted.
Picture reformatPicture(const model::Effect& effect, const Picture& picture, double time, const model::EffectSpan& span);
// The value a parameter has until it is set, where the effect gives it one of its own (ours, said in
// docs/spec/image-effects.md): Scaling 100, or 50 for Picture-in-Picture; 14:9 Letterbox's Position H
// 50. Undefined otherwise. The Effect Editor shows the same values.
QJsonValue parameterDefault(const model::Effect& effect, const QString& category, const QString& name);

// What one track shows at a frame: a segment, or a transition between the outgoing and incoming
// segments at a cut. Frames of the segments are counted from their clips' starts.
struct TrackFrame {
    const model::Segment* segment = nullptr; // the segment, or the incoming one in a transition
    std::int64_t frame = 0;
    std::int64_t offset = 0; // frames into the segment (into the transition, in a transition)
    const model::Segment* outgoing = nullptr; // only in a transition
    std::int64_t outgoingFrame = 0;
    const model::Transition* transition = nullptr;
    // How far through the transition: (frames into it + 1) / (its length + 1), so that neither end
    // shows only one side (ours).
    double progress = 0.0;
};
// What `track` of `sequence` shows at `frame`; nothing past its end.
std::optional<TrackFrame> trackFrame(const model::Sequence& sequence, const QString& track, std::int64_t frame);

// A clip's picture for a segment: the frame of the segment's clip, at about `size`. A null image when
// the clip has no picture (no media was ever linked: nothing is drawn); nothing at all when its media is
// offline. FrameSource must not keep the segment.
using FrameSource = std::function<std::optional<QImage>(const model::Segment& segment, std::int64_t clipFrame, QSize size)>;

struct Composition {
    const model::Sequence* sequence = nullptr;
    // The video tracks to show, bottom to top (the Video Track Monitor shows its track and those
    // below it, Editing Guide p.627).
    QStringList tracks;
    QSize size;
    EffectPrecision precision = EffectPrecision::Bits16;
    FrameSource source;
};

// The sequence's picture at `frame`. Sets `missing` when a segment's media is offline.
Picture composeFrame(const Composition& composition, std::int64_t frame, bool* missing = nullptr);
// The same as an image. When one track shows one plain segment (no effect, no transition), the clip's
// frame as the source gives it, without compositing.
QImage composeImage(const Composition& composition, std::int64_t frame, bool* missing = nullptr);

} // namespace lumenis::media

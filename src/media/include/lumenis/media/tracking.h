#pragma once

// The tracking engines (ROADMAP 6.7c; FX Guide p.248-249, p.262-265; docs/spec/tracking.md): following a target
// area through frames to make a tracker's data points, and the change tracking data makes to an effect as the
// compositor draws it. The data itself (trackers, regions, offset tracking, SteadyGlide, Smoothing) is the
// model's (lumenis/model/tracking.h). Our own code: normalized cross-correlation and our optical flow
// (optical_flow.h); nothing from Avid or a library.

#include "lumenis/media/compositor.h"
#include "lumenis/model/tracking.h"

#include <QImage>
#include <QRectF>

#include <functional>
#include <optional>

namespace lumenis::media {

enum class TrackingEngine { Correlation, Fluid, FluidStabilizer };
// The engine a Tracking Engine menu entry names (p.263); Correlation for anything else.
TrackingEngine trackingEngine(const QString& name);

// Supplies the picture tracked at a frame (the tracks below the effect, or its own segment, p.262); a null image
// when there is none. All the frames of one run have one size.
using TrackedFrame = std::function<QImage(std::int64_t frame)>;

// Follows the target area (fractions of the frame, at frame `from`) frame by frame through `to` (either way), each
// time looking within a search area of `search`'s size around where it was last found:
// * Correlation Tracker: "usually produces accurate tracking data when the target area is distinctive"; ours, the
//   target's pattern at `from` matched over the search area by normalized cross-correlation, to a tenth of a
//   pixel;
// * FluidTracker: "uses the same motion analysis methods that FluidMotion Timewarp uses"; ours, the optical flow
//   at the point from frame to frame;
// * FluidStabilizer: "divides the image into a grid, automatically selects a point of interest in each grid area
//   ... then uses further calculations to combine the motion of these points, de-emphasizing any values that are
//   not consistent with the general pattern of motion" (p.263-264); ours, a 4 x 4 grid's flow, its median.
// Returns the point (the target's centre) at each frame tracked, `from` first. `stop` is asked before each frame
// ("press the space bar", p.265); `progress` is told each point as it is found.
QList<model::TrackPoint> trackTarget(TrackingEngine engine, const TrackedFrame& frames, std::int64_t from, std::int64_t to,
                                     const QRectF& target, const QRectF& search, const std::function<bool()>& stop = {},
                                     const std::function<void(const model::TrackPoint&)>& progress = {});

// The effect as the compositor draws it at `time` of `span` in a frame of `aspect` (width over height), with its
// tracking data's Position, Scaling and Rotation changes folded into those parameters' values at `time` (their
// keyframes replaced): Position in pixels of a 1920-wide frame (Y down for 2D effects, up for 3D ones), Scaling
// multiplied, Rotation's Z added. The effect itself when no tracking applies.
model::Effect trackedEffect(const model::Effect& effect, double time, const model::EffectSpan& span, double aspect);

// The Tracking (Corner Tracking) category's map at `time` (fractions of the frame), when it is on and has data.
std::optional<std::array<double, 9>> trackedTransform(const model::Effect& effect, double time, const model::EffectSpan& span, double aspect);
// `picture` moved by such a map (each output pixel fetched from where the map's inverse puts it).
Picture transformed(const Picture& picture, const std::array<double, 9>& map);

} // namespace lumenis::media

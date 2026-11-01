#pragma once

// Motion tracking's data (ROADMAP 6.7c; FX Guide ch.10, p.248-275; docs/spec/tracking.md): the trackers of an
// effect, their regions and tracking data points, how offset tracking joins the regions, SteadyGlide and
// Smoothing, the reference frame, which trackers drive which effect parameters, and what the data does to the
// effect at a frame. The tracking engines that make the data are media's (media/tracking.h); the Tracking
// window and the Effect Preview's boxes and points are the UI's.
//
// Everything is kept with the effect, under its parameters' "Trackers" object (ours), so it goes where the
// effect goes (templates, copies, undo). Places are fractions of the frame (0 to 1 across and down); frames are
// counted from the effect's first frame.

#include "lumenis/model/effect_keyframes.h"
#include "lumenis/model/sequence.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <optional>

namespace lumenis::model {

// "You can add one or more new tracker regions to the tracker timeline and track a new target area for each"
// (p.265): from its first frame to the next region's, with the target area (the inner box) and the search area
// (the outer box) it starts with (p.260).
// One tracking data point: where the tracked pattern is at a frame.
struct TrackPoint {
    std::int64_t frame = 0;
    double x = 0.0;
    double y = 0.0;
    bool operator==(const TrackPoint&) const = default;
};

// A region's points are its own: tracking a region "steps back one frame from the region boundary before it
// starts tracking" (p.265), so its first point may be on the frame before it, where the previous region has one
// too; offset tracking joins the two there.
struct TrackerRegion {
    std::int64_t start = 0;
    QRectF target;
    QRectF search;
    QList<TrackPoint> points; // in frame order, at most one per frame
    bool operator==(const TrackerRegion&) const = default;
};

// SteadyGlide and Smoothing "are mutually exclusive processing operations" (p.270).
enum class TrackProcess { None, SteadyGlide, Smoothing };

struct Tracker {
    QString name;               // "Point A" ... (p.259)
    QString color;              // "#rrggbb", the Tracker Color box (p.255)
    bool enabled = true;        // the Tracking window's Enable Tracker button (p.260)
    bool offset = true;         // "Offset tracking is enabled by default" (p.266)
    TrackProcess process = TrackProcess::None;
    int smoothing = 50;         // "a value between 1 and 100" (p.270); ours 50 until set
    std::int64_t reference = 0; // "By default ... the first frame of a tracked segment" (p.269)
    QList<TrackerRegion> regions; // at least one, the first from frame 0, in order of their starts

    // The region a frame is in (its index), or -1 before any.
    int regionAt(std::int64_t frame) const;
    // The tracked point at a frame: its region's, if it has one there.
    std::optional<QPointF> pointAt(std::int64_t frame) const;
    // Puts (or replaces) the point of a frame in region `region` (the frame's own region when -1).
    void setPoint(std::int64_t frame, const QPointF& at, int region = -1);
    // Whether any region has data.
    bool hasData() const;
    bool operator==(const Tracker&) const = default;
};

// The tracking engines (p.263-264).
inline constexpr const char* kTrackingEngines[] = {"Correlation Tracker", "FluidTracker", "FluidStabilizer"};

// An effect's tracking: its trackers ("You can use up to eight trackers for each effect", p.259), the Tracking
// window's Tracking Engine and Track Background/Foreground (p.262-263).
struct TrackingSetup {
    QList<Tracker> trackers;
    QString engine = QStringLiteral("Correlation Tracker");
    bool foreground = false;
    const Tracker* tracker(const QString& name) const;
    Tracker* tracker(const QString& name);
    bool operator==(const TrackingSetup&) const = default;
};
inline constexpr int kMaxTrackers = 8;

TrackingSetup trackingOf(const Effect& effect);
void setTracking(Effect& effect, const TrackingSetup& setup);

// The name a new tracker gets: "the next letter of the alphabet that is not already in use, up to the letter D.
// If all of the letters A through D are in use ... 'Untitled Tracker'" (p.259).
QString nextTrackerName(const TrackingSetup& setup);
// A new tracker by that name, with a first region whose boxes are centred in the frame (ours: a target a tenth
// of the frame across, a search area twice as large) and its own colour. Nothing when there are eight.
std::optional<Tracker> newTracker(const TrackingSetup& setup);

// The parameters tracking data can drive (p.250-251): Position, Scaling, Rotation (3D effects), and the Tracking
// category (Corner Tracking in 3D effects) with up to four trackers.
enum class TrackedParameter { Position, Scaling, Rotation, Tracking };
QString trackedParameterKey(TrackedParameter parameter);

// A parameter's tracker selectors (T1, T2, ...): the tracker each is mapped to and whether it is enabled; the
// whole parameter's enable; Position's Axis menu ("X and Y", "X Tracking Only", "Y Tracking Only", p.275); and
// Stabilize Tracking (3D, p.577-578; used by 6.7d).
struct TrackerMapping {
    bool on = false;
    QStringList selectors;
    QList<bool> selectorOn;
    QString axis = QStringLiteral("X and Y");
    bool stabilize = false;
    bool operator==(const TrackerMapping&) const = default;
};
TrackerMapping trackerMapping(const Effect& effect, TrackedParameter parameter);
void setTrackerMapping(Effect& effect, TrackedParameter parameter, const TrackerMapping& mapping);
// The number of tracker selectors a parameter has: one for Position, two for Scaling and Rotation, four for
// Tracking (p.257-259).
int selectorCount(TrackedParameter parameter);
// Enables tracking for a parameter as the Effect Editor's enable button does (p.257-258): trackers that exist are
// mapped to its selectors and missing ones are made (Point A, Point B ...); the other group of parameters is
// turned off ("When you enable the Tracking parameter, tracking is automatically disabled in the Position,
// Scaling, and Rotation parameters", p.251). `selector` (Tracking only) enables one selector; -1 all it needs.
// Returns false when nothing changes.
bool enableTracking(Effect& effect, TrackedParameter parameter, int selector = -1);
bool disableTracking(Effect& effect, TrackedParameter parameter);

// Renames a tracker (the Tracker Name text box, p.255) and the selectors mapped to it; false when the name is
// empty, taken or unknown.
bool renameTracker(Effect& effect, const QString& from, const QString& to);
// "Closes the tracker and permanently deletes any information within it" (p.255): the tracker, and its name
// from the selectors it was mapped to.
bool deleteTracker(Effect& effect, const QString& name);

// A tracker's path at a frame: its points joined across regions by offset tracking ("Media Composer calculates
// the offset from the first set of tracking points to the second", p.265), then smoothed (Smoothing) or with its
// jitter taken out (SteadyGlide) by its value; between points, and beyond the first and last, the nearest data
// (linearly between two). Nothing without points.
std::optional<QPointF> trackerPath(const Tracker& tracker, std::int64_t frame);
// The path as the Effect Preview shows it after processing, one point per frame with data.
QList<TrackPoint> processedPoints(const Tracker& tracker);
// "The reference frame ... is the frame at which the tracking data makes no change to the appearance of the
// effect" (p.269): the path's movement from there.
std::optional<QPointF> trackerMovement(const Tracker& tracker, std::int64_t frame);

// SteadyGlide "processes tracking data to reduce abrupt, jittery movements without affecting underlying motion"
// (p.269): the jitter it takes out at a frame (the path before processing less after); zero for other trackers.
// Stabilizing with SteadyGlide (6.7d) cancels only this.
std::optional<QPointF> trackerJitter(const Tracker& tracker, std::int64_t frame);

// What the tracking data does to an effect at a frame (`aspect` is the frame's width over its height):
// Position's movement (fractions of the frame, after the Axis menu), Scaling's and Rotation's change from two
// trackers' distance and angle, and the Tracking category's transform of the picture (fractions of the frame to
// fractions of the frame; a translation, similarity, affine or perspective map for one to four trackers).
struct TrackedChange {
    QPointF position;
    double scale = 1.0;
    double rotation = 0.0; // degrees, clockwise as seen
    std::optional<std::array<double, 9>> transform; // row-major 3x3, maps (x, y, 1)
    bool any() const { return !position.isNull() || scale != 1.0 || rotation != 0.0 || transform.has_value(); }
};
TrackedChange trackedChange(const Effect& effect, std::int64_t frame, double aspect);

// The effect's frame at `time` of `span`, as tracking data counts frames.
std::int64_t trackingFrame(double time, const EffectSpan& span);

} // namespace lumenis::model

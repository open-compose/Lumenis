#pragma once

#include "lumenis/model/timewarp.h"

#include <QImage>

#include <cstdint>
#include <functional>
#include <optional>

// Fields of interlaced pictures and the format conversions of Timewarp effects and Motion Adapters (FX Guide
// p.181-186, p.203-205; Editing Guide p.472-473; ROADMAP 6.5d3a; docs/spec/motion-effects.md). Pictures are
// whole frames as the media gives them: an interlaced frame holds two fields woven line by line, the first in
// time on the even lines when `topFirst`, else on the odd lines. Our own code throughout.
namespace lumenis::media {

// How the lines a field lacks are made (FX Guide p.185-186): "Duplicated Field ... Displays a single field"
// (each line twice), "Interpolated Field ... combining scan line pairs from the first field", "VTR-Style ...
// shifting selected video fields of the original media by a full scan line" (ours: each pair of lines shows
// the field's line of the pair, so the second field sits where the first does instead of a line lower).
enum class FieldFill { Duplicate, Interpolate, ShiftLine };
// One field (0 the first in time, 1 the second) of `frame` as a whole frame.
QImage fieldPicture(const QImage& frame, int field, bool topFirst, FieldFill fill);
// A frame woven from two pictures: the first field's lines from `first`, the second's from `second`.
QImage weaveFields(const QImage& first, const QImage& second, bool topFirst);
// Field `field` of `frame` made whole by adaptive deinterlacing ("analyzes adjacent frames as well as the
// current frame", p.181): where the picture is still across the frames around it the other field's lines are
// kept, where it moves they are interpolated. `previous` and `next` may be null (the clip's ends).
QImage adaptiveDeinterlace(const QImage& previous, const QImage& frame, const QImage& next, int field, bool topFirst);
// `a` mixed with `b`: t = 0 is a, 1 is b. Images of other sizes are scaled to a's.
QImage blendImages(const QImage& a, const QImage& b, double t);

// 2:3 pulldown: four film frames A, B, C and D in five video frames, AA BB BC CD DD (Editing Guide p.473: "The
// telecine process inserts pulldown frames when it transfers film footage shot at 24 fps to 30 fps video").
// `phase` (0-4) is the frame of that cycle the clip's first frame is: 0 for an A frame. The film frame field
// `field` (counted from the clip's first frame's first field) belongs to, counted from the first film frame
// the clip shows; and the first field of film frame `film`.
std::int64_t pulldownFilmFrame(std::int64_t field, int phase);
std::int64_t pulldownFirstField(std::int64_t film, int phase);
// The film frame a video frame shows whole, or nothing for the two split frames of each cycle (BC and CD).
std::optional<std::int64_t> pulldownWholeFrame(std::int64_t frame, int phase);

// Getting the clip's frame n; a null image past its ends or offline.
using FrameGetter = std::function<QImage(std::int64_t frame)>;
// The phase of 2:3 pulldown in the clip's first frames ("automatically set the pulldown pattern", p.205): the
// one whose split frames show the most difference between their fields and whose whole frames the least. 0
// when nothing tells them apart (no motion).
int detectPulldownPhase(const FrameGetter& frameAt, std::int64_t frames, bool topFirst);
// How many times each picture is repeated in a clip of repeated progressive frames ("automatically set the
// repeated frame count", p.205): the commonest run of identical frames in its first frames, 1 for none.
int detectRepeatCount(const FrameGetter& frameAt, std::int64_t frames);

// The Source and Output formats of a Timewarp effect (p.183, p.205) or a Motion Adapter.
enum class SourceFormat { Interlaced, Progressive, FilmPulldown, ProgressiveRepeated };
enum class OutputFormat { Interlaced, Progressive, FilmPulldown };
// How the in-between pictures are made, from the Type (model::TimewarpType, p.185-186).
enum class FieldRendering { DuplicatedField, BothFields, InterpolatedField, VtrStyle, BlendedInterpolated, BlendedVtr, FluidMotionDraft, FluidMotion };

// The rendering of a Timewarp or Motion Adapter Type.
FieldRendering fieldRendering(model::TimewarpType type);

struct FieldConversion {
    SourceFormat source = SourceFormat::Progressive;
    OutputFormat output = OutputFormat::Progressive;
    FieldRendering rendering = FieldRendering::BothFields;
    bool adaptive = false; // Adaptive Deinterlace Source (p.181)
    bool topFirst = true;
    int phase = 0;  // FilmPulldown sources
    int repeat = 2; // ProgressiveRepeated sources: each picture's frames
};

// One output frame. `positionAt(t)` is the source position (clip frames, with a fraction) shown at output time
// t, in output frames from the effect's first frame (a fraction for the second field and for film frames).
// Output Interlaced weaves the pictures at t and t + 1/2; Film with pulldown takes 2:3 cadence fields of film
// frames sampled every 5/4 output frames; Progressive shows the picture at t. A source picture comes from
// the source format and rendering: an interlaced source's fields at their own times, a film source's film
// frames, a repeated source's pictures; whole frames for Both Fields, one field filled for Duplicated Field,
// fields at field times for Interpolated Field and VTR-Style, and two of those mixed (the blended Types) or
// followed along their motion (FluidMotion, media::interpolateFrames) by the fraction between them. Null when
// the frame cannot be had.
QImage convertedFrame(const FrameGetter& frameAt, const std::function<double(double)>& positionAt, double outputFrame,
                      const FieldConversion& conversion);

} // namespace lumenis::media

#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"
#include "lumenis/model/timewarp.h"

#include <QList>
#include <QString>

#include <functional>
#include <optional>

// Mixed rate clips and Motion Adapters (Editing Guide p.468-478; ROADMAP 6.5d2; docs/spec/motion-effects.md).
// "Clips that do not match the frame rate or field motion type of the project are known as mixed rate
// clips ... Mixed rate clips always play at the project's frame rate, both in Source or pop-up monitors and
// in sequences. Audio remains synchronized with video" (p.469).
//
// Ours: wherever a mixed rate clip is played or edited, it is seen as its adapted copy (adaptedClip): the
// same clip at the project's rate, its duration, timecode and markers counted in the project's frames, and
// the adapter kept in its composition. Media is read by time (media::Link::fileFrame maps a clip frame to
// the file frame showing at its time), so the copy plays its own pictures and sound at the project's rate
// with nothing else to do. Segments edited from it count in those frames too and carry the adapter
// (Segment::adapter); clipForSegment() gives the copy back for playing them. A clip linked to media at
// another rate is already at the project's rate (media::linkClip) and needs no copy of its frames, but is a
// mixed rate clip all the same. So is a clip whose Field Motion does not match the project (an interlaced clip
// in a progressive project, 2:3 pulldown, repeated frames; 6.5d3a): its adapter makes the project's frames
// from the clip's fields, film frames or pictures (media::FrameCache::frame, media::convertedFrame).
namespace lumenis::model {

// The Field Motion bin column (p.472-473): "whether the clip is interlaced or progressive, or whether it
// contains 2:3 pulldown or repeated (strobe) frames".
enum class FieldMotionAttribute { Interlaced, Film23, Progressive, ProgressiveStrobe };
inline constexpr char kFieldMotionHeading[] = "Field Motion";
QString fieldMotionText(FieldMotionAttribute f); // "Interlaced", "2:3 Film", "Progressive", "Progressive Strobe"
std::optional<FieldMotionAttribute> fieldMotionFromText(const QString& text);
// "When you create a clip or subclip, its Field Motion attribute is set to either Interlaced or Progressive,
// depending on the project type": the clip's Field Motion cell, or that default when it has none (ours: the
// format in its Format column, else the project's scan).
FieldMotionAttribute clipFieldMotion(const BinItem& clip, bool interlacedProject);
// The menu's choices: "2:3 Film: Only available for clips or subclips created in 30 fps projects" (ours:
// clips at a 30 fps rate, 29.97 or 30).
QList<FieldMotionAttribute> fieldMotionChoices(const BinItem& clip);
// Whether a clip has a Field Motion attribute: master clips and subclips with a picture.
bool hasFieldMotion(const BinItem& clip);

// The rate of the clip's own frames: its media's for a clip linked at another rate (a linked clip runs at
// the project's rate, media::linkClip), else its edit rate. An adapted copy gives its adapter's. Stills
// and image sequences take the clip's rate.
Rational clipFrameRate(const BinItem& clip);
// A master clip or subclip with a picture whose own frames are at another rate than `projectRate`.
bool isMixedRate(const BinItem& clip, const Rational& projectRate);

// The Type a new adapter renders with: "When this option [Use frame blending in Motion Adapters] is
// selected, the Blended Interpolated mode is used ... When it is not selected, the Interpolated Field mode
// is used for interlaced clips, and Both Fields mode is used for progressive clips" (p.1215-1216).
TimewarpType defaultAdapterType(FieldMotionAttribute source, bool blending);
// The adapter a mixed rate clip gets in a project of `projectRate` (nothing for other clips): the clip's
// own rate, its Field Motion and the default Type.
std::optional<MotionAdapter> adapterFor(const BinItem& clip, const Rational& projectRate, bool interlacedProject, bool blending);
TimewarpType adapterType(const MotionAdapter& adapter);
// Whether a segment's adapter changes how its clip plays, and so is shown (the green dot, the Motion Effect
// Editor): a clip at another rate, or one whose Field Motion is not Progressive (6.5d3a). Audio segments'
// adapters of clips at the project's rate only keep their units.
bool adapterShown(const MotionAdapter& adapter);
// Whether the adapter's Type makes in-between frames from the two frames of the clip around a project
// frame's time (Blended Interpolated, Blended VTR, the FluidMotion types).
bool adapterBlends(const MotionAdapter& adapter);

// The clip as a mixed rate clip is seen at the project's rate (above): `clip` itself when it is not one.
// Its id, link and everything else stay; editRate becomes the adapter's sequenceRate, and the duration,
// Start and End, markers and a subclip's offset in its master are counted in it. An adapted copy is
// adapted again from its own clip. Never stored in a bin (unadaptedClip).
BinItem adaptedClip(const BinItem& clip, const MotionAdapter& adapter);
// The adapter an adapted copy carries; nothing for any other item.
std::optional<MotionAdapter> adapterOf(const BinItem& item);
bool isAdapted(const BinItem& item);
// The clip to play a segment from: a group clip's angle (groupAngleClip, group_clip.h), its adapted copy when the
// segment has an adapter, else `clip`.
BinItem clipForSegment(const BinItem& clip, const Segment& segment);
// The clip an adapted copy came from, with the changes made to the copy (its markers, name, columns) in
// its own frames: `original` (the clip in the bin) gives back the duration and timecode the conversion
// rounded, when it is given. Other items come back as they are.
BinItem unadaptedClip(const BinItem& item, const std::optional<BinItem>& original = std::nullopt);
// A frame of the project's rate (an adapted copy's) as the clip's own frame showing at its time, and back
// (the first project frame that shows it).
std::int64_t clipFrameAt(std::int64_t projectFrame, const Rational& clipRate, const Rational& projectRate);
std::int64_t projectFrameAt(std::int64_t clipFrame, const Rational& clipRate, const Rational& projectRate);

// "Mixed rate clips also display with their original frame rate appended to the clip name ... playtime
// (59.94 fps)" (p.470).
QString mixedRateName(const QString& name, const Rational& clipRate);

// Promote (p.472): "The motion adapter is promoted to a Timewarp effect and all standard Timewarp effect
// parameters are available". The Timewarp plays the clip in real time (100%, ours: the segment counts the
// project's frames, so the adapter's own speed is 100%) with the adapter's Type; the adapter stays under it,
// so removing the Timewarp leaves the adapter ("Media Composer re-applies a Motion Adapter effect", p.469).
// False when the segment has no adapter or already has an effect.
bool promoteAdapter(Segment& segment);
// The adapter's Speed in the Motion Effect Editor (p.472: "a Speed percentage value that adjusts the clip's
// speed to the project's rate"): 100 for ours, which keep the clip in real time.
double adapterSpeed(const MotionAdapter& adapter);

// Refresh Sequence > Motion Adapters / Timewarps (p.478): "Refreshes the sequence so that media with
// Motion Adapters or Timewarps use the current Field Motion attribute value from their source clip". Each
// adapter takes its clip's (found through `resolve`) Field Motion, and the default Type for it when it had
// the default Type of the old one (ours: a Type chosen in the Motion Effect Editor stays). Returns how many
// adapters changed.
int refreshMotionAdapters(Sequence& sequence, const std::function<std::optional<BinItem>(const QString& bin, const QString& clip)>& resolve,
                          bool interlacedProject, bool blending);

} // namespace lumenis::model

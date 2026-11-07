#pragma once

#include "lumenis/model/bin.h"
#include "lumenis/model/sequence.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

// Group clips for MultiCamera editing (Editing Guide ch.32, p.1123-1135; docs/spec/multicamera.md):
// "Grouping creates a separate group clip out of a single set of master clips" (p.1123), synced by
// their IN points, OUT points, source timecode or an Auxiliary TC column (p.1124). A group is a bin
// item of kind Group whose composition["group"] holds each angle: a copy of its clip and where the
// clip lies in the group. Group frame g shows frame g + offset of an angle's clip.
//
// Segments edited from a group keep its id as their clip and the angle they show
// (Segment::groupAngle); clipForSegment() (motion_adapter.h) plays them as that angle's clip through
// groupAngleClip(). MainWindow makes groups (Clip > Group Clips) and the Composer shows them in the
// Source monitor full size or split (p.1126-1129).
namespace lumenis::model {

// What the clips are synced by (the Group Clips dialog, p.1124). FilmTc, Ink and AuxInk are listed for the dialog
// only: groupClips() refuses them (film projects are not built yet; docs/spec/multicamera.md). Waveform ("Media
// Composer performs an analysis of the waveforms of the selected clips") takes each clip's sync point from
// GroupOptions::waveform.
enum class GroupSync { FilmTc, Inpoints, Outpoints, SourceTimecode, AuxTimecode, Ink, AuxInk, Waveform };

// One camera angle: the clip it shows and where.
struct GroupAngle {
    QString binId;  // the bin the clip was in when grouped
    QString clipId; // its BinItem::id
    // A copy of the clip as it was grouped, played for the angle (groupAngleClip).
    BinItem clip;
    // Group frame g is frame g + offset of the clip.
    std::int64_t offset = 0;
};

struct GroupClip {
    QList<GroupAngle> angles;
    // The angle shown in the Source monitor and edited into sequences ("Whenever you switch camera
    // angles, you also switch the frame representing the group in the bin", p.1131).
    int selected = 0;
};

// The group a Group item holds; nothing for other items.
std::optional<GroupClip> groupOf(const BinItem& item);
bool isGroupClip(const BinItem& item);
// Stores the group in the item.
void setGroup(BinItem& item, const GroupClip& group);

// Where each clip's sync point is, as a frame of the clip, for the IN or OUT point sync; nothing when
// the clip has none (the Source monitor's marks, else the bin's Mark IN / Mark OUT columns).
using GroupMarks = std::function<std::optional<std::int64_t>(const BinItem& clip, bool in)>;

// The GroupSync::Waveform sync point of a clip (GroupOptions::waveform): the frame of the clip whose sound is the
// first clip's frame 0, worked out by MainWindow from the clips' audio (lumenis/media/waveform_sync.h).
using GroupWaveformPoint = std::function<std::optional<std::int64_t>(const BinItem& clip)>;

struct GroupOptions {
    GroupSync sync = GroupSync::Inpoints;
    int auxTc = 1; // Auxiliary TC1-5 for AuxTimecode
    GroupMarks marks;
    GroupWaveformPoint waveform; // for GroupSync::Waveform
    QDateTime when;
};

// Whether a bin item can be an angle: a master clip, subclip or audio clip.
bool canBeGroupAngle(const BinItem& item);

// Clip > Group Clips (p.1124): a group of `clips` (each with the bin it is in), in that order, synced
// as `options` says. The group runs from the first frame of the earliest clip to the last of the
// latest once synced (ours: "from the IN point to the OUT point of the longest clip", p.1123, read as
// covering every clip). Its name is the first clip's followed by ".Grp.n", n counting groups of that
// name in `bin` (p.1124). Nothing, with the reason in *error, when fewer than two clips can be
// grouped, their edit rates differ, or a clip lacks its sync point or timecode.
std::optional<BinItem> groupClips(const Bin& bin, const QList<std::pair<QString, BinItem>>& clips, const GroupOptions& options,
                                  QString* error = nullptr);
// "<name>.Grp.n" with the first n not yet used in the bin.
QString groupName(const Bin& bin, const QString& name);

// The tracks a group offers (sourceTracksOf): V1 when an angle has picture, and as many audio tracks
// as the angle with the most (ours: the guide's Group menu patches any angle's tracks, 8.1b).
QStringList groupTracks(const GroupClip& group);

// The angle's clip as the group plays it: its frames moved by the offset so that frame g of the
// returned clip is group frame g, as long as the group. Frames where the angle has no material are
// black and silent (groupAngleRange). The clip's own id is kept. An index out of range gives the
// selected angle; a group without angles gives the item itself.
// `offsets`, when it has the angle, gives its offset in place of the group's (a segment's Segment::groupOffsets).
BinItem groupAngleClip(const BinItem& group, int angle, const QList<std::int64_t>& offsets = {});
// The frames [first, second) of an angle clip made by groupAngleClip() that have material; nothing for
// other clips.
std::optional<std::pair<std::int64_t, std::int64_t>> groupAngleRange(const BinItem& clip);

// A segment's clip, by its bin and id; nothing when it cannot be found.
using GroupClipLookup = std::function<std::optional<BinItem>(const QString& binId, const QString& clipId)>;

// Edit Group (p.1125): a sequence of the group's clips where the group has them, each angle with picture on a video
// track of its own (V1 for the first such angle) and "One representative audio track from each clip" (its A1) on an
// audio track of its own, in the angles' order. Tagged as editing the group (Sequence::editedGroup).
Sequence groupEditSequence(const QString& binId, const BinItem& group);
// Update Group and Create Group (p.1126): the clips of a sequence's tracks as a group's angles, in the order their
// first segments appear on the video tracks (V1 first) and then on the audio tracks, each placed where its first
// segment is. "Currently, you must have one audio track from a clip to include any audio from that clip in the
// group" (ours: an angle is any clip the sequence shows; sound comes with it). Segments of group clips are left out.
// The group lasts as long as the sequence, never less than `atLeast` ("You cannot make the group clip shorter when
// editing the group clip"). Nothing, with the reason, when fewer than two clips are found or they are at another rate
// than the sequence.
std::optional<std::pair<GroupClip, std::int64_t>> groupFromSequence(const Sequence& sequence, const Rational& rate,
                                                                   const GroupClipLookup& lookup, std::int64_t atLeast = 0,
                                                                   QString* error = nullptr);
// A group item made from or updated with `group` and `duration`: its tracks, Start and End columns, and the angles.
void storeGroup(BinItem& item, const GroupClip& group, std::int64_t duration);
// Clip > Refresh Sequence > Group Clips (p.478: "Refreshes the sequence to include updates applied to group clips";
// p.1126: "will not change selected shots, only the relationship between the selected shot and the other clips in the
// group"): each group segment takes its group's current offsets, its source frames moved so its shown angle shows the
// same material. Returns how many segments changed.
int refreshGroupEdits(Sequence& sequence, const GroupClipLookup& lookup);

// The angle `step` places on from `angle` among `count`, wrapping round (Previous In Group and Next In
// Group "cycle through all the clips", p.1131).
int stepAngle(int angle, int step, int count);

} // namespace lumenis::model

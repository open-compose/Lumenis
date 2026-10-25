#pragma once

#include "lumenis/model/sequence.h"

#include <optional>

// Volume and pan automation (Editing Guide p.714-723; docs/spec/automation.md): the keyframes a
// segment carries (Segment::volumeKeys and panKeys), what they make the segment play, and the
// editing the Timeline (src/ui/timeline.cpp) and the Audio Mixer's Auto mode (audio_tools.cpp) do
// with them. Playback and Audio Mixdown read the same values through media::audioPieces
// (src/media/playback.cpp).
//
// Keyframes count in the source clip's frames (AudioKey), so they move with the material; a
// KeyRef names one by where it is in the sequence, its record frame, which may have a fraction
// (sub-frame keyframes, p.720-721).
namespace lumenis::model {

enum class AutomationParam { Volume, Pan };

const QList<AudioKey>& keysOf(const Segment& segment, AutomationParam param);
QList<AudioKey>& keysOf(Segment& segment, AutomationParam param);

// A -∞ keyframe counts as this many dB in the ramps to and from it, so a fade
// to silence is heard as one (ours: the guide does not say how such a ramp
// goes); the keyframe itself is silent.
inline constexpr double kRampFloorDb = -96.0;

// The keyframes' value at source frame `frame`: a straight line from one keyframe to the next
// (p.714: "a linear ramp"; for volume in dB), the first one's value before it and the last one's
// after it. Nothing without keyframes.
std::optional<double> automationAt(const QList<AudioKey>& keys, double frame);

// The pan a segment's centred clip pan plays at on track `trackNumber`: odd tracks to the left,
// even to the right, unless the Audio setting centres mono tracks (p.627, p.1147).
int sidePan(int trackNumber, bool alternatingPan);

// What plays at source frame `frame`:
// - the volume in dB: the clip gain plus the volume automation ("additive to the system clip
//   volume values", p.701); kGainOff (silence) when either is -∞;
// - the pan: the pan automation, else the clip pan, else (a centred clip pan) `side`
//   (ours: pan automation takes the place of the clip pan rather than adding to it).
double volumeAt(const Segment& segment, double frame);
double panAt(const Segment& segment, double frame, int side);
// The automation's own value at `frame`, as the Timeline and the Auto mode faders show it: volume
// automation (0 dB without keyframes) or the pan panAt() gives.
double automationValue(const Segment& segment, AutomationParam param, double frame, int side);

// A keyframe, by its track and its place in the sequence: the record frame it falls on, with any
// fraction.
struct KeyRef {
    QString track;
    double frame = 0;
    bool operator==(const KeyRef&) const = default;
};

// The keyframes of the audio `tracks` (empty: every audio track) at record frames [from, to), in
// track order then time. A keyframe outside its segment's material (hidden by a trim, p.723) is
// listed only with `hidden`.
QList<KeyRef> keyframesIn(const Sequence& sequence, const QStringList& tracks, double from, double to,
                          AutomationParam param, bool hidden = false);

// Add Keyframe (p.716): a keyframe at record frame `frame` on each of `tracks` that has material
// there, holding the value that plays there, so nothing is heard to change until it is moved.
// Filler, locked tracks and tracks with a keyframe there already are left alone. Returns how
// many were added. `alternatingPan` as for sidePan().
int addKeyframes(Sequence& sequence, const QStringList& tracks, double frame, AutomationParam param,
                 bool alternatingPan = true);
// Sets the keyframes named to `value` (clamped: clampGain for volume, ±kPanMax for pan). Returns
// how many were found.
int setKeyframes(Sequence& sequence, const QList<KeyRef>& keys, AutomationParam param, double value);
// Moves the keyframes named `frames` later in time and `by` up in value (dragging, p.716;
// nudging, p.723). A keyframe stays on its segment's material; one moved onto another keyframe
// takes its place. Returns how many were found.
int moveKeyframes(Sequence& sequence, const QList<KeyRef>& keys, AutomationParam param, double frames, double by);
int deleteKeyframes(Sequence& sequence, const QList<KeyRef>& keys, AutomationParam param);

// The Audio Mixer's Fast menu in Auto mode (p.719-720), on the audio `tracks` between record
// frames [from, to). Locked tracks are left alone; each returns how many keyframes it changed.
// Remove volume automation / Remove Pan on Track: deletes the keyframes there.
int removeAutomation(Sequence& sequence, const QStringList& tracks, double from, double to, AutomationParam param);
// Adjust Volume/Pan on Track: adds `by` (dB, or pan) to the keyframes there; a segment there
// without keyframes gets one at its first frame with its value plus `by`, which sets the whole
// segment.
int adjustAutomation(Sequence& sequence, const QStringList& tracks, double from, double to, AutomationParam param,
                     double by, bool alternatingPan = true);
// Filter volume automation / Filter Pan on Track: removes the keyframes there that lie on a
// straight line between their neighbours ("points on a linear ramp", p.719), then, until about
// half are gone, every other one that is not a peak. With `peaksOnly` (Alt-click) keeps only the first, the last and the peaks (local minima
// and maxima).
int filterAutomation(Sequence& sequence, const QStringList& tracks, double from, double to, AutomationParam param,
                     bool peaksOnly);
// Remove Hidden ... on Track (p.720, p.723): removes the keyframes outside the material of the
// segments there. A keyframe is first added at the edge of the material, with the value playing
// there, when hidden ones shaped it, so what plays does not change (ours).
int removeHiddenKeyframes(Sequence& sequence, const QStringList& tracks, double from, double to,
                          AutomationParam param);

// Copied keyframes (p.721-722), a list per track: AudioKey::frame is the distance in record
// frames from where the copy started.
struct KeyClipboard {
    struct Track {
        QList<AudioKey> volume;
        QList<AudioKey> pan;
    };
    QList<Track> tracks;
    bool isEmpty() const;
};
// The keyframes named, grouped by track in the order the tracks are first named, measured from
// the earliest one.
KeyClipboard copyKeyframes(const Sequence& sequence, const QList<KeyRef>& volume, const QList<KeyRef>& pan);
// Every visible keyframe of `tracks` between [from, to), measured from `from`; a track without
// any still takes its place.
KeyClipboard copyKeyframes(const Sequence& sequence, const QStringList& tracks, double from, double to);
// Pastes the clipboard's tracks in order onto `tracks`, starting at record frame `at`: each
// keyframe lands on the segment there (a keyframe there is replaced; filler takes none). `volume`
// and `pan` choose which keyframes (the Paste Audio Keyframes dialog, p.722). Returns how many
// were pasted.
int pasteKeyframes(Sequence& sequence, const QStringList& tracks, double at, const KeyClipboard& clipboard, bool volume,
                   bool pan);

} // namespace lumenis::model

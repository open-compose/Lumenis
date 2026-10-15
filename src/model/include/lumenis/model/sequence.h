#pragma once

#include "lumenis/model/bin.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>

namespace lumenis::model {

// Track types of a sequence (guide p.459, p.624-626). The timecode track
// TC1 has no segments; a sequence just shows it or not.
enum class TrackKind { Video, Audio, Data };
// Audio track formats: Timeline > New > Audio Track (p.625).
enum class AudioFormat { Mono, Stereo, Surround51, Surround71 };

// "V1", "A2", "D1".
QString trackName(TrackKind kind, int number);
struct TrackRef {
    TrackKind kind = TrackKind::Video;
    int number = 1;
};
std::optional<TrackRef> parseTrackName(const QString& name);
// A Tracks column ("V1 A1-2", "V1-2 A1 A2 TC1") as track names; timecode
// tracks are left out.
QStringList parseTrackList(const QString& text);

// Up to 99 video and 99 audio tracks and one data track (p.459, p.634;
// p.624 says 64).
inline constexpr int kMaxTracks = 99;

// An effect in a sequence (FX guide ch. 2): its category and name in the
// Effect Palette (p.37) and the parameter values changed in the Effect
// Editor (p.83), by parameter category and name.
struct Effect {
    QString category; // "Blend"
    QString name;     // "Dissolve"
    QJsonObject parameters;

    QJsonObject toJson() const;
    static Effect fromJson(const QJsonObject& o);
    bool operator==(const Effect&) const = default;
};

// Where a transition effect lies relative to its cut (Transition Effect
// Alignment, FX guide p.500).
enum class TransitionAlignment { EndingAtCut, CenteredOnCut, StartingAtCut, CustomStart };

// A transition effect at a cut (FX guide p.44, p.500): `duration` frames,
// `start` of them before the cut (Custom Start; the other alignments work
// it out).
struct Transition {
    Effect effect;
    std::int64_t duration = 0;
    TransitionAlignment alignment = TransitionAlignment::CenteredOnCut;
    std::int64_t start = 0;

    // Frames before and after the cut.
    std::int64_t before() const;
    std::int64_t after() const { return duration - before(); }
    bool operator==(const Transition&) const = default;
};

// One piece of a track: material from a source clip, or filler (black,
// silence) where `clipId` is empty.
struct Segment {
    QString clipId;
    QString binId;
    QString name;        // the clip's name when it was edited in
    QString sourceTrack; // the clip's track this came from, e.g. "A2"
    std::int64_t sourceIn = 0; // first frame used, counted from the clip's start
    std::int64_t length = 0;
    // Filler that starts with an Add Edit (p.640); filler is otherwise
    // joined to the filler before it.
    bool addEdit = false;
    // A segment effect (FX guide p.44); filler can have one too.
    std::optional<Effect> effect;
    // The transition effect at the cut where this segment starts.
    std::optional<Transition> transition;

    bool hasEffects() const { return effect || transition; }
    bool isFiller() const { return clipId.isEmpty(); }
    static Segment filler(std::int64_t length);
    bool operator==(const Segment&) const = default;
};

struct Track {
    TrackKind kind = TrackKind::Video;
    int number = 1;
    AudioFormat format = AudioFormat::Mono;
    QList<Segment> segments; // back to back from frame 0
    bool locked = false;     // no edits (p.633)
    bool syncLocked = false; // kept in sync with other sync-locked tracks (p.493)

    QString name() const { return trackName(kind, number); }
    std::int64_t length() const;
    // The segment covering `frame` and its first frame, or -1.
    int segmentAt(std::int64_t frame, std::int64_t* start = nullptr) const;
    // Where segments start and the track ends.
    QList<std::int64_t> editPoints() const;
    bool operator==(const Track&) const = default;
};

// A sequence's tracks and segments (guide ch. 15 and 19). Stored in the
// sequence's bin item.
class Sequence {
public:
    // A new sequence's tracks: V1, A1, A2 and TC1 (p.459).
    static Sequence withDefaultTracks();
    // The item's composition, or the default tracks if it has none.
    static Sequence fromItem(const BinItem& item);
    // Writes the composition and the duration into a sequence item.
    void storeIn(BinItem& item) const;

    QJsonObject toJson() const;
    static Sequence fromJson(const QJsonObject& o);

    const QList<Track>& tracks() const { return tracks_; }
    const Track* track(const QString& name) const;
    Track* track(const QString& name);
    // Video, then audio, then data, each by number.
    QStringList trackNames() const;
    bool hasTimecodeTrack() const { return timecode_; }
    void setTimecodeTrack(bool on) { timecode_ = on; }

    // Timeline > New > Video Track, Audio Track, Data Track (p.625): the next
    // free number, or nothing past the limit (one data track).
    std::optional<QString> addTrack(TrackKind kind, AudioFormat format = AudioFormat::Mono);
    // Adds the named track if it is missing.
    Track* ensureTrack(const QString& name);

    std::int64_t duration() const;
    // Edit points on the named tracks (all tracks if empty), in order,
    // including the start and the ends of the tracks.
    QList<std::int64_t> editPoints(const QStringList& tracks = {}) const;

    // Material from a track: `length` frames from `start`, filler where the
    // track has nothing.
    QList<Segment> material(const QString& track, std::int64_t start, std::int64_t length) const;

    // Edits on one track (p.466-467, p.478-479). Frames past the end of the
    // track are first filled with filler. Removing returns what was removed.
    void splice(const QString& track, std::int64_t at, const QList<Segment>& material);
    void overwrite(const QString& track, std::int64_t at, const QList<Segment>& material);
    QList<Segment> lift(const QString& track, std::int64_t start, std::int64_t length);
    QList<Segment> extract(const QString& track, std::int64_t start, std::int64_t length);

    // Deletes a track (p.636).
    bool removeTrack(const QString& name);

    // Add Edit (p.639-640): an edit point at `frame` on the track. Returns
    // false if one is already there or the frame is outside the track.
    bool addEdit(const QString& track, std::int64_t frame);
    // Timeline > Remove Match Frame Edits (p.640): joins segments that
    // continue each other within [start, start + length). Returns how many.
    int removeMatchFrameEdits(const QStringList& tracks, std::int64_t start, std::int64_t length);
    // Whether the edit between two neighbouring segments is a match frame:
    // the second continues the first (p.639).
    static bool isMatchFrame(const Segment& before, const Segment& after);

    // A part of the sequence as a sequence of its own: the Clipboard
    // (p.479-480). Tracks keep their names; empty tracks are left out.
    Sequence excerpt(const QStringList& tracks, std::int64_t start, std::int64_t length) const;

    // Merges filler and drops empty segments after a track was changed
    // through track().
    void normalize(const QString& track);

    bool operator==(const Sequence&) const = default;

private:
    // Splits the segment at `frame` so a segment starts there; pads the
    // track with filler to reach it. Returns that segment's index.
    static int cut(Track& track, std::int64_t frame);
    static void tidy(Track& track);
    void sortTracks();

    QList<Track> tracks_;
    bool timecode_ = true;
};

// The tracks a monitor's clip offers on the source side of the Track
// Selector (p.626): a sequence's own tracks, a clip's Tracks column, or V1,
// A1 and A2 for clips without one.
QStringList sourceTracksOf(const BinItem& item);

// The Track Selector panel's selections and patches (p.625-630).
class TrackPatch {
public:
    // The source clip changed: tracks it still has keep their selection and
    // patches; new tracks start selected.
    void setSourceTracks(const QStringList& tracks);
    const QStringList& sourceTracks() const { return source_; }

    // Tracks are selected until deselected.
    bool isSourceSelected(const QString& track) const;
    void setSourceSelected(const QString& track, bool on);
    bool isRecordSelected(const QString& track) const { return !recordOff_.contains(track); }
    void setRecordSelected(const QString& track, bool on);
    // The sequence's selected tracks, in its order.
    QStringList selectedRecordTracks(const Sequence& sequence) const;

    // Patching: audio only to audio, video only to video, never data
    // (p.629). Returns false if not allowed.
    static bool canPatch(const QString& source, const QString& record);
    bool patch(const QString& source, const QString& record);
    // Timeline > Restore Default Patch (p.630).
    void restoreDefault() { patch_.clear(); }
    QString recordFor(const QString& source) const;
    // The source track patched to a record track, or empty.
    QString sourceFor(const QString& record) const;

    // Pairs edited: a source track selected, patched to a record track that
    // is selected or does not exist yet (p.626).
    QList<std::pair<QString, QString>> editPairs(const Sequence& sequence) const;

    bool operator==(const TrackPatch&) const = default;

private:
    QStringList source_;
    QSet<QString> sourceOff_;
    QSet<QString> recordOff_;
    QHash<QString, QString> patch_; // source -> record
};

// The marks and position of one monitor, for working out an edit.
struct EditMarks {
    std::optional<std::int64_t> in;
    std::optional<std::int64_t> out;
    std::int64_t position = 0;
    std::int64_t duration = 0;
};

// A three-point edit worked out from the marks (p.466-468, phantom marks
// p.501): source material from `sourceStart`, placed at `recordStart`.
struct EditPlan {
    std::int64_t sourceStart = 0;
    std::int64_t recordStart = 0;
    std::int64_t length = 0;
    QString error; // why the edit cannot be made
    bool ok() const { return error.isEmpty(); }
};
EditPlan planEdit(const EditMarks& source, const EditMarks& record);

enum class EditKind { SpliceIn, Overwrite };

// Edits the marked source material into the sequence on every edited
// track pair. Returns the record tracks changed.
QStringList applyEdit(Sequence& sequence, EditKind kind, const EditPlan& plan, const QString& binId,
                      const BinItem& source, const TrackPatch& patch);

// Lift or Extract the frames [start, start + length) on the given tracks.
// Locked tracks are left alone. Extract also extracts from the other
// sync-locked tracks when a sync-locked track is among them (p.479).
// Returns the record tracks changed.
QStringList removeMaterial(Sequence& sequence, bool extract, std::int64_t start, std::int64_t length,
                           const QStringList& tracks);

// Replace Edit (p.467-468): the segment under the record position on the
// first edited track (or the record IN to OUT) filled with source
// material lined up on the two positions.
EditPlan planReplace(const Sequence& sequence, const QString& track, const EditMarks& source,
                     const EditMarks& record);

// Pastes a Clipboard sequence onto the tracks it came from (ks2009), at
// `at`. Returns the tracks changed; locked tracks are left alone.
QStringList paste(Sequence& sequence, const Sequence& clip, std::int64_t at, EditKind kind);

// A segment, by its track and first frame.
struct SegmentRef {
    QString track;
    std::int64_t start = 0;
    bool operator==(const SegmentRef&) const = default;
};
// The segment a reference names, and its length; nullopt if it is gone.
std::optional<Segment> segmentFor(const Sequence& sequence, const SegmentRef& ref);
// Segment editing (p.610-623): moves selected segments `frames` later and
// `tracks` tracks up (negative: down), Lift/Overwrite or Extract/Splice-in,
// or copies them (Alt-drag, p.620). Tracks past the last are added
// (p.635). On failure nothing changes and `error` says why. Returns the
// moved segments' new references.
QList<SegmentRef> moveSegments(Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t frames, int tracks,
                               EditKind kind, bool copy, QString* error = nullptr);
// Deletes segments: Lift leaves filler, Extract closes the gaps (p.621).
bool deleteSegments(Sequence& sequence, const QList<SegmentRef>& segments, bool extract);
// The selected segments as a Clipboard sequence, from the first one's start
// (p.622), and that start.
Sequence copySegments(const Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t* start = nullptr);

// Match Frame (p.432-434): the segment under `position` on the first of
// `tracks` in V1, A1, A2 order that has material there, and the frame of
// its clip.
struct MatchedFrame {
    Segment segment;
    std::int64_t frame = 0; // counted from the clip's start
};
std::optional<MatchedFrame> matchFrame(const Sequence& sequence, const QStringList& tracks, std::int64_t position);

// ---- Trimming (guide ch. 20, p.647-665)

// Which side of an edit point a trim roller is on: the outgoing segment
// (A-side), the incoming one (B-side), or both (a dual-roller trim, p.655).
enum class TrimSide { A, B, Both };
// Single-roller trims: Ripple moves the rest of the track (p.654);
// Overwrite keeps the track's length, adding filler or overwriting (p.653).
enum class TrimKind { Ripple, Overwrite };

// A roller at the edit point `frame` of `track`.
struct TrimRoller {
    QString track;
    std::int64_t frame = 0;
    TrimSide side = TrimSide::Both;
    bool operator==(const TrimRoller&) const = default;
};

// How long a segment's clip is, so trims stop at the end of its media;
// nullopt when unknown (no limit). Filler has no limit.
using SourceLength = std::function<std::optional<std::int64_t>(const Segment&)>;

// The frames all the rollers can move together, in unison, [min, max]:
// no segment shorter than one frame (p.647), no trim past a clip's media,
// and with sync locks no sync-locked track losing anything but filler
// (p.662).
struct TrimRange {
    std::int64_t min = 0;
    std::int64_t max = 0;
};
TrimRange trimRange(const Sequence& sequence, const QList<TrimRoller>& rollers, TrimKind kind,
                    const SourceLength& sourceLength = {});
// Trims by `frames` (positive: later), clamped to trimRange. Ripple trims
// keep the other sync-locked tracks in sync with filler when a trimmed
// track is sync-locked (p.662). Locked tracks are not trimmed. Returns the
// rollers at their new places; `applied` is the frames actually trimmed.
QList<TrimRoller> trim(Sequence& sequence, const QList<TrimRoller>& rollers, TrimKind kind, std::int64_t frames,
                       const SourceLength& sourceLength = {}, std::int64_t* applied = nullptr);

// The edit points of a track that rollers can sit on: between two
// segments, and the end of the track.
QList<std::int64_t> trimPoints(const Sequence& sequence, const QString& track);

// Slip: the segments show other frames of their clips; positive shows
// later material (Slip Left, p.665). Slide: the segments move along the
// track, trimming their neighbours (p.663). Both are clamped; they return
// the frames applied.
std::int64_t slipSegments(Sequence& sequence, const QList<SegmentRef>& segments, std::int64_t frames,
                          const SourceLength& sourceLength = {});
std::int64_t slideSegments(Sequence& sequence, QList<SegmentRef>& segments, std::int64_t frames,
                           const SourceLength& sourceLength = {});

// Extend (p.661): on each track, the edit nearest the mark is moved to it
// with a dual-roller trim: the next edit back to an IN, or the previous edit
// forward to just after an OUT. Returns the tracks changed.
QStringList extendEdits(Sequence& sequence, const QStringList& tracks, std::int64_t mark, bool toIn,
                        const SourceLength& sourceLength = {});

// ---- Effects (FX guide ch. 2-3)

// Where an effect from the Effect Palette can go (FX guide p.456-490).
// Segment effects go on video segments, transitions on the cuts of
// video and audio tracks; never on data tracks (p.44).
enum class EffectTarget { Segment, Transition };

// Puts a segment effect on the segment at `frame`, replacing any there.
bool applySegmentEffect(Sequence& sequence, const QString& track, std::int64_t frame, const Effect& effect);

// How much material a transition at the cut `frame` may use: frames of
// the outgoing clip past its end (Media A) and of the incoming clip before
// its start (Media B), within the two segments (p.48). nullopt: there is
// no cut there.
struct TransitionRoom {
    std::int64_t before = 0; // the most frames before the cut
    std::int64_t after = 0;  // the most frames after the cut
};
std::optional<TransitionRoom> transitionRoom(const Sequence& sequence, const QString& track, std::int64_t frame,
                                             const SourceLength& sourceLength = {});
// Whether the transition fits the room; if not, `which` names the side that
// is short ("Media A", "Media B"; the Insufficient Source dialog, p.48).
bool transitionFits(const Transition& transition, const TransitionRoom& room, QString* which = nullptr);
// Size To Fit (p.48): the transition shortened to the room, keeping its
// alignment where it can.
Transition sizeToFit(const Transition& transition, const TransitionRoom& room);
// Puts a transition at the cut `frame`. Returns false if there is no cut
// there or it does not fit.
bool applyTransition(Sequence& sequence, const QString& track, std::int64_t frame, const Transition& transition,
                     const SourceLength& sourceLength = {});
// Remove Effect (p.47): the transition at the cut `frame`, or else the
// segment effect of the segment there. Returns what was removed: "", or
// the effect's name.
QString removeEffect(Sequence& sequence, const QString& track, std::int64_t frame, EffectTarget target);

// An effect in the sequence and where it is.
struct EffectRef {
    QString track;
    std::int64_t frame = 0; // the cut, or the segment's first frame
    EffectTarget target = EffectTarget::Segment;
    bool operator==(const EffectRef&) const = default;
};
// The effect the Effect Editor shows at `position` (p.83-84): on the first
// of `tracks` (selected tracks, top down) with a transition spanning the
// position or a segment effect under it.
std::optional<EffectRef> effectAt(const Sequence& sequence, const QStringList& tracks, std::int64_t position);
// The effect an EffectRef names.
const Effect* effectFor(const Sequence& sequence, const EffectRef& ref);
Effect* effectFor(Sequence& sequence, const EffectRef& ref);
const Transition* transitionFor(const Sequence& sequence, const EffectRef& ref);
Transition* transitionFor(Sequence& sequence, const EffectRef& ref);

// One step of Edit > Undo/Redo (p.466): a sequence item before and after.
struct EditStep {
    QString description; // "Splice-in", "Overwrite", ...
    QString binId;
    BinItem before;
    BinItem after;
};

// Undo and redo, 100 levels (p.466).
class EditHistory {
public:
    static constexpr int kLevels = 100;

    void push(EditStep step);
    bool canUndo() const { return next_ > 0; }
    bool canRedo() const { return next_ < steps_.size(); }
    // The step undone or redone.
    const EditStep* undo();
    const EditStep* redo();
    void clear();
    // Edit > Undo/Redo List: the most recent first, and how many of them
    // can be undone (the rest can be redone).
    QStringList descriptions() const;
    int undoCount() const { return static_cast<int>(next_); }

private:
    QList<EditStep> steps_;
    qsizetype next_ = 0; // steps before this are done
};

} // namespace lumenis::model

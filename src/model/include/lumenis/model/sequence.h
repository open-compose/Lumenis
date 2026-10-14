#pragma once

#include "lumenis/model/bin.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>
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

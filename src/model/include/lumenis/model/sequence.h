#pragma once

#include "lumenis/model/bin.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

namespace lumenis::model {

// Track types of a sequence (guide p.459, p.624-626). The timecode track
// TC1 has no segments; a sequence just shows it or not.
enum class TrackKind { Video, Audio, Data };
// Audio track formats: Timeline > New > Audio Track (p.625).
enum class AudioFormat { Mono, Stereo, Surround51, Surround71 };

// "V1", "A2", "D1".
// No range check: trackName(Video, 0) gives "V0".
QString trackName(TrackKind kind, int number);
// A parsed track name: its kind and number (1 and up).
struct TrackRef {
    TrackKind kind = TrackKind::Video;
    int number = 1;
};
// "V1", "a2", " D1 " (case and surrounding spaces do not matter), numbers 1 to 99; nullopt for
// anything else, including "TC1".
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
    // Parameter category -> {parameter name -> value}; empty when every parameter has its default.
    QJsonObject parameters;

    // {category, name, parameters?}; fromJson() takes missing members as empty.
    QJsonObject toJson() const;
    static Effect fromJson(const QJsonObject& o);
    bool operator==(const Effect&) const = default;
};

// A speed change on a segment: the imports' speed changes (an EDL's M2 line, an OTIO
// LinearTimeWarp or FreezeFrame) and Fit to Fill's Variable Speed effect. An effect of category
// "EDL" named "Motion Effect", with the speed as a multiple of normal (0 for a freeze frame,
// negative for reverse), which the EDL export writes back as M2. The segment's length is its
// length in the sequence; it uses about length × speed frames of its clip from sourceIn.
Effect motionEffect(double speed);
// The speed of a motionEffect(), or nothing for another effect.
std::optional<double> motionSpeed(const Effect& e);

// Where a transition effect lies relative to its cut (Transition Effect
// Alignment, FX guide p.500).
enum class TransitionAlignment { EndingAtCut, CenteredOnCut, StartingAtCut, CustomStart };

// A transition effect at a cut (FX guide p.44, p.500): `duration` frames,
// `start` of them before the cut (Custom Start; the other alignments work
// it out).
struct Transition {
    Effect effect;
    // Length in frames, and how it lies on the cut; `start` is used only with CustomStart.
    std::int64_t duration = 0;
    TransitionAlignment alignment = TransitionAlignment::CenteredOnCut;
    std::int64_t start = 0;

    // Frames before and after the cut.
    // before() is clamped to 0..duration for CustomStart.
    std::int64_t before() const;
    std::int64_t after() const { return duration - before(); }
    bool operator==(const Transition&) const = default;
};

// One volume or pan automation keyframe (p.714-723; docs/spec/automation.md):
// where it is, in frames of the source clip from its start (a fraction is a
// sub-frame keyframe, p.720-721), and its value: dB added to the clip gain
// (kGainOff for -∞), or a pan from -kPanMax to kPanMax. Keyframes count in
// the clip's frames so they move with its material: a trim shows or hides
// them (p.723, "hidden keyframes"), and both halves of an Add Edit keep
// them.
struct AudioKey {
    double frame = 0;
    double value = 0;
    bool operator==(const AudioKey&) const = default;
};

// An Audio EQ effect (Editing Guide p.736-745; lumenis/model/audio_eq.h): three bands, each
// from -20 to +15 dB (p.739). The low shelf turns over at 50, 80, 120 or 240 Hz, the high shelf
// at 6, 8, 12 or 15 kHz; the parametric midrange is 1/4 octave or 2 octaves wide (p.738), centred
// between 50 Hz and 15 kHz (the EQ Range slider, picture on p.737). `in`: the In button (p.737);
// an EQ that is not In does not play. The defaults are the picture's: 50 Hz, 2 octaves, 15 kHz,
// centred at 500 Hz.
struct AudioEq {
    double low = 0;
    double mid = 0;
    double high = 0;
    int lowTurnover = 50;
    int highTurnover = 15000;
    double midFrequency = 500;
    bool wideMid = true; // 2 octaves; false: 1/4 octave
    bool in = true;
    // A predefined template's (p.743-744): its name, and the midrange's width in octaves when it is
    // neither 2 nor 1/4 (0: wideMid). A template keeps turnovers and widths the tool's own controls
    // do not offer, and the tool does not change them ("you cannot change the parameters of a
    // predefined EQ template", p.744).
    QString preset;
    double midOctaves = 0;
    // The midrange's width in octaves.
    double octaves() const { return midOctaves > 0 ? midOctaves : wideMid ? 2.0 : 0.25; }
    bool operator==(const AudioEq&) const = default;
};

// An AudioSuite plug-in effect on an audio segment (Editing Guide p.796-800;
// docs/spec/audiosuite.md): the process's id (media::audioSuitePlugins()), its name when it was
// applied (so one that is not installed can still be named), and its parameter values. "You need to
// render all AudioSuite plug-ins before you can play back the effect" (p.800): `rendered` is the
// processed sound's media file (managed media) with its stream and channel, and `renderedKey` what it
// was made from (model::audioSuiteKey); a segment whose key no longer matches plays unprocessed until
// it is rendered again.
struct AudioSuiteEffect {
    QString effect;
    QString name;
    QMap<QString, double> params;
    QString rendered;
    int renderedStream = 0;
    int renderedChannel = 0;
    QString renderedKey;
    bool operator==(const AudioSuiteEffect&) const = default;
};

// A surround pan of one channel of a mono or stereo audio segment in a 5.1 or 7.1 sequence (Editing
// Guide p.704-710; lumenis/model/surround_pan.h; docs/spec/surround.md): the Advanced Panner's
// controls. The Pan Location cursor runs along a trajectory from the Front Position (left/right,
// -100 to 100, at the front edge) to the Rear Position (at the rear edge); the F/R Position (-100
// rear to 100 front) places it on that line. Center percentage (0-100) sends the front of the pan to
// a discrete centre channel or, lower, to a phantom centre between L and R; Side percentage (7.1)
// does the same for the side surrounds; LFE (0-100) is the share sent to the LFE channel. The
// defaults put the cursor at the front centre.
struct SurroundPan {
    int front = 0;
    int rear = 0;
    int frontRear = 100;
    int center = 100;
    int side = 100;
    int lfe = 0;
    // The cursor's left/right position on the trajectory at frontRear.
    double x() const;
    // A pan with the cursor at (x, y) in the X/Y grid: front and rear both at x; the percentages
    // are kept from `from`.
    static SurroundPan at(double x, double y, const SurroundPan& from);
    static SurroundPan at(double x, double y);
    bool operator==(const SurroundPan&) const = default;
};

// One piece of a track: material from a source clip, or filler (black,
// silence) where `clipId` is empty.
struct Segment {
    // The source clip's BinItem::id (empty for filler) and the bin it was in when edited in (may be
    // empty, e.g. for material from AAF).
    QString clipId;
    QString binId;
    // The source clip's MobID (AAF's SourceClip SourceID). Null: the one
    // derived from clipId, as for clips without a MobID of their own
    // (derivedMobId in lumenis/model/bin.h).
    MobId sourceMob;
    QString name;        // the clip's name when it was edited in
    QString sourceTrack; // the clip's track this came from, e.g. "A2"
    std::int64_t sourceIn = 0; // first frame used, counted from the clip's start
    // Frames this segment lasts in the sequence (> 0 once a track is tidied).
    std::int64_t length = 0;
    // Filler that starts with an Add Edit (p.640); filler is otherwise
    // joined to the filler before it.
    bool addEdit = false;
    // A segment effect (FX guide p.44); filler can have one too.
    std::optional<Effect> effect;
    // The transition effect at the cut where this segment starts.
    std::optional<Transition> transition;
    // Clip Volume and Pan (p.702): the segment's clip gain in dB and its
    // pan, -100 (left) to 100 (right).
    double gain = 0.0;
    int pan = 0;
    // Whether the segment has a pan of its own (ownPan()). Without one it plays at its track's
    // default pan (the Audio setting's Default Pan, p.1147), and follows that setting when it
    // changes (Remove/Commit Default Clip Pan on Track, p.711). A pan other than 0 always counts
    // as one, so a centred pan of its own needs this flag.
    bool hasPan = false;
    bool ownPan() const { return hasPan || pan != 0; }
    // The pan it plays at on a track whose default pan is `side` (model::sidePan).
    int panOr(int side) const { return ownPan() ? pan : side; }
    // Volume and pan automation (lumenis/model/automation.h), each sorted by
    // frame, at most one keyframe a frame position. Empty: none.
    QList<AudioKey> volumeKeys;
    QList<AudioKey> panKeys;
    // The segment's Audio EQ effect (p.736-745), on audio segments only.
    std::optional<AudioEq> eq;
    // The segment's AudioSuite plug-in effect (p.797-800), on audio segments only.
    std::optional<AudioSuiteEffect> audioSuite;
    // Surround pans (p.704-710), one for each channel of a mono or stereo segment; empty for the
    // default (lumenis/model/surround_pan.h: surroundPansOf). Used in 5.1 and 7.1 sequences only.
    QList<SurroundPan> surround;
    // Set Local Clip Color (p.597-598): "#rrggbb", empty for none. Shown when the Clip Color dialog's
    // Timeline Local is on (lumenis/model/clip_coloring.h).
    QString localColor;

    bool hasEffects() const { return effect || transition; }
    bool hasAutomation() const { return !volumeKeys.isEmpty() || !panKeys.isEmpty(); }
    bool isFiller() const { return clipId.isEmpty(); }
    // The MobID the segment refers to: sourceMob, else the one derived from clipId; null for filler.
    MobId sourceMobId() const;
    // A filler segment of `length` frames with nothing else set.
    static Segment filler(std::int64_t length);
    bool operator==(const Segment&) const = default;
};

// An Audio Track Effect insert (Editing Guide p.790-795; docs/spec/track-effects.md): the effect's id
// (media::audioEffects()), its name when it was inserted, so an effect that is not installed can
// still be named ("Unavailable Effect", p.790), its parameter values by parameter id, and whether it
// is bypassed (p.794).
struct TrackInsert {
    QString effect;
    QString name;
    QMap<QString, double> params;
    bool bypass = false;
    bool operator==(const TrackInsert&) const = default;
};
// "Up to five Audio Track Effect plug-in inserts on each audio track", a through e (p.790-791).
inline constexpr int kTrackInserts = 5;

// One track of a sequence: its segments laid end to end. Segments are kept tidy: no empty
// segments, and neighbouring plain filler merged (see Sequence::normalize()).
struct Track {
    TrackKind kind = TrackKind::Video;
    int number = 1;
    // Audio tracks only; video and data tracks keep Mono.
    AudioFormat format = AudioFormat::Mono;
    QList<Segment> segments; // back to back from frame 0
    bool locked = false;     // no edits (p.633)
    bool syncLocked = false; // kept in sync with other sync-locked tracks (p.493)
    // Audio tracks: the Audio Track Effect inserts a-e, processed left to right (p.794).
    std::array<std::optional<TrackInsert>, kTrackInserts> inserts{};
    bool hasInserts() const;
    // "This track is LFE only" (p.710): a mono audio track heard only on the LFE channel of a surround
    // mix, without a panner.
    bool lfeOnly = false;

    QString name() const { return trackName(kind, number); }
    // The sum of the segment lengths: the frame after the track's last one.
    std::int64_t length() const;
    // The segment covering `frame` and its first frame, or -1.
    int segmentAt(std::int64_t frame, std::int64_t* start = nullptr) const;
    // Where segments start and the track ends.
    // Always starts with 0 and ends with length().
    QList<std::int64_t> editPoints() const;
    bool operator==(const Track&) const = default;
};

// A sequence's tracks and segments (guide ch. 15 and 19). Stored in the
// sequence's bin item.
// A value type: copy it, edit it, and storeIn() the item to keep the result. Frames count from
// the sequence's first frame (0), not from its start timecode. Track names are as trackName()
// gives them. Functions given an unknown track name do nothing (or return empty / false).
class Sequence {
public:
    // A new sequence's tracks: V1, A1, A2 and TC1 (p.459).
    static Sequence withDefaultTracks();
    // The item's composition, or the default tracks if it has none.
    static Sequence fromItem(const BinItem& item);
    // Writes the composition and the duration into a sequence item.
    void storeIn(BinItem& item) const;

    // The composition JSON: {tracks: [{name, segments: [...], locked?, syncLocked?, format?,
    // lfeOnly?, inserts?: [{slot (0-4), effect, name, params: {id: value}, bypass?}]}],
    // timecode, mix? ("5.1" or "7.1": mixFormat())}. A segment is {length, gain?, pan?,
    // volumeKeys?, panKeys?, eq?, effect?, transition?} (keys as [[frame, value], ...]) plus
    // addEdit? for filler or {clip, mob?, bin?, name, track, in, localColor?, surround?: [{front,
    // rear, fr, center?, side?, lfe?}]} for material.
    // fromJson() skips unreadable and duplicate tracks, tidies each track and sorts the tracks.
    QJsonObject toJson() const;
    static Sequence fromJson(const QJsonObject& o);

    const QList<Track>& tracks() const { return tracks_; }
    // The named track, or null. The pointer is valid until tracks are added or removed.
    const Track* track(const QString& name) const;
    Track* track(const QString& name);
    // Video, then audio, then data, each by number.
    QStringList trackNames() const;
    // Whether TC1 is shown; it has no segments.
    bool hasTimecodeTrack() const { return timecode_; }
    void setTimecodeTrack(bool on) { timecode_ = on; }
    // The Audio Mixer's Sequence Mix Format (p.686): the format the sequence is mixed in, Stereo
    // (the default), 5.1 or 7.1; it decides which speakers its tracks can be panned to and what an
    // Audio Mixdown's surround formats hold (lumenis/media/surround.h). Mono is taken as Stereo.
    AudioFormat mixFormat() const { return mixFormat_; }
    void setMixFormat(AudioFormat format);

    // Timeline > New > Video Track, Audio Track, Data Track (p.625): the next
    // free number, or nothing past the limit (one data track).
    std::optional<QString> addTrack(TrackKind kind, AudioFormat format = AudioFormat::Mono);
    // Adds the named track if it is missing; a new audio track gets `format`.
    Track* ensureTrack(const QString& name, AudioFormat format = AudioFormat::Mono);
    // Puts a whole track in, replacing any of the same name (Split Track to Mono rebuilds tracks so).
    void restoreTrack(const Track& track);

    // The length of the longest track.
    std::int64_t duration() const;
    // Edit points on the named tracks (all tracks if empty), in order,
    // including the start and the ends of the tracks.
    QList<std::int64_t> editPoints(const QStringList& tracks = {}) const;

    // Material from a track: `length` frames from `start`, filler where the
    // track has nothing.
    QList<Segment> material(const QString& track, std::int64_t start, std::int64_t length) const;

    // Edits on one track (p.466-467, p.478-479). Frames past the end of the
    // track are first filled with filler. Removing returns what was removed.
    // splice() inserts the material at `at`, pushing the rest of the track later. overwrite() replaces
    // the frames from `at` for the material's length, so the track only grows if the material goes
    // past its end. lift() replaces [start, start + length) with filler; extract() removes it and
    // closes the gap. Both clip the range to the track.
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
    // An empty `tracks` list means every track. Only edits strictly inside the range are joined.
    int removeMatchFrameEdits(const QStringList& tracks, std::int64_t start, std::int64_t length);
    // Whether the edit between two neighbouring segments is a match frame:
    // the second continues the first (p.639).
    static bool isMatchFrame(const Segment& before, const Segment& after);

    // A part of the sequence as a sequence of its own: the Clipboard
    // (p.479-480). Tracks keep their names; empty tracks are left out.
    Sequence excerpt(const QStringList& tracks, std::int64_t start, std::int64_t length) const;

    // Merges filler and drops empty segments after a track was changed
    // through track().
    // The same tidying every edit function does at its end.
    void normalize(const QString& track);

    bool operator==(const Sequence&) const = default;

private:
    // Splits the segment at `frame` so a segment starts there; pads the
    // track with filler to reach it. Returns that segment's index.
    // The part after the cut loses the Add Edit flag and the transition, which stay with the first
    // part. For a frame at or past the end, returns the number of segments (where to append).
    static int cut(Track& track, std::int64_t frame);
    // Drops segments of length <= 0 and merges filler into the filler before it unless the later
    // one has an Add Edit or effects, or the earlier one has a segment effect.
    static void tidy(Track& track);
    // Video, then audio, then data, each by number.
    void sortTracks();

    // Always sorted by sortTracks(), and without duplicate names.
    QList<Track> tracks_;
    bool timecode_ = true;
    AudioFormat mixFormat_ = AudioFormat::Stereo;
};

// The tracks a monitor's clip offers on the source side of the Track
// Selector (p.626): a sequence's own tracks, a clip's Tracks column (its audio
// channels grouped by its Track Formats, lumenis/model/multichannel.h), or V1,
// A1 and A2 for clips without one.
QStringList sourceTracksOf(const BinItem& item);

// The Track Selector panel's selections and patches (p.625-630).
// The Timeline (src/ui/timeline.*) keeps one for the Track Selector; applyEdit() reads it.
class TrackPatch {
public:
    // The source clip changed: tracks it still has keep their selection and
    // patches; new tracks start selected.
    void setSourceTracks(const QStringList& tracks);
    // The source side's tracks, in the order given to setSourceTracks().
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
    // patch() also needs `source` to be one of sourceTracks(). A source track already patched to
    // `record` takes the moved track's old record track (or its own default) in exchange. Patching a
    // track to its own name restores its default.
    static bool canPatch(const QString& source, const QString& record);
    bool patch(const QString& source, const QString& record);
    // Timeline > Restore Default Patch (p.630).
    void restoreDefault() { patch_.clear(); }
    // The record track a source track goes to: its patch, or by default the track of the same name.
    QString recordFor(const QString& source) const;
    // The source track patched to a record track, or empty.
    QString sourceFor(const QString& record) const;

    // Pairs edited: a source track selected, patched to a record track that
    // is selected or does not exist yet (p.626).
    QList<std::pair<QString, QString>> editPairs(const Sequence& sequence) const;

    bool operator==(const TrackPatch&) const = default;

private:
    // The source tracks; sourceOff_ and recordOff_ hold the deselected tracks (all are selected by
    // default).
    QStringList source_;
    QSet<QString> sourceOff_;
    QSet<QString> recordOff_;
    QHash<QString, QString> patch_; // source -> record
};

// The marks and position of one monitor, for working out an edit.
// Frames from the clip's or sequence's first frame; in and out are inclusive marks, nullopt when
// not set; duration is the clip's length in frames (for a sequence, its duration()).
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
// The marks are used in this order: source IN and OUT; record IN and OUT (a lone source OUT
// backtimes); a source OUT alone; a record OUT alone; else from the source IN (or position) to
// the end of the clip. When the source marks give the length, a record OUT without a record IN
// backtimes: the edit ends on it. On failure `error`
// holds the message to show and the other fields are not to be used.
EditPlan planEdit(const EditMarks& source, const EditMarks& record);

// Fit to Fill (FX guide p.170): the source IN to OUT overwritten into the record IN to OUT, with
// a Variable Speed motion effect that makes it fit; `speed` gets the source length over the
// record length. Needs all four marks (ours: the guide describes only marked durations on both
// sides).
EditPlan planFitToFill(const EditMarks& source, const EditMarks& record, double* speed);

// Splice-in pushes later material along; Overwrite replaces it.
enum class EditKind { SpliceIn, Overwrite };

// Edits the marked source material into the sequence on every edited
// track pair. Returns the record tracks changed. A pair whose record track
// exists with another audio format than the source track's is left out
// (multichannel tracks patch only to tracks of their format, p.629; see
// editFormatMismatch); missing record tracks are added in the source's format.
// A source sequence is edited in as its segments (taken from its patched track); a clip becomes
// one segment per track pair. Missing record tracks are added; locked ones are skipped. A splice
// into a sync-locked track splices filler into the other sync-locked tracks (p.493).
QStringList applyEdit(Sequence& sequence, EditKind kind, const EditPlan& plan, const QString& binId,
                      const BinItem& source, const TrackPatch& patch);
// The first edited pair whose record track exists with another format than its source track
// ("you can only patch multichannel audio tracks to multichannel audio tracks, or mono tracks to
// mono tracks", p.629), as "A1 is stereo but A2 is mono"; empty when every pair matches.
QString editFormatMismatch(const Sequence& sequence, const BinItem& source, const TrackPatch& patch);

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
// Each of the clip's tracks is pasted for the clip's whole duration, so shorter tracks are padded
// with filler.
QStringList paste(Sequence& sequence, const Sequence& clip, std::int64_t at, EditKind kind);

// A segment, by its track and first frame.
// A reference stays valid only while the segment starts at that frame; segmentFor() checks it.
struct SegmentRef {
    QString track;
    std::int64_t start = 0;
    bool operator==(const SegmentRef&) const = default;
};
// The segment a reference names, and its length; nullopt if it is gone.
std::optional<Segment> segmentFor(const Sequence& sequence, const SegmentRef& ref);
// Change Source Track (p.595; 2026.8 guide p.507, What's New 2026.8 p.7): the
// audio segment `ref` names takes its sound from `sourceTrack`, another
// audio track of the same clip. Its place, length, clip gain, pan and
// effects stay; the clip itself is not changed. False if `ref` names no
// audio segment, or `sourceTrack` is not an audio track.
bool changeSourceTrack(Sequence& sequence, const SegmentRef& ref, const QString& sourceTrack);
// Change Source Track > More: a subclip's segment takes `sourceTrack` of the
// master clip `master` (in `masterBin`) instead, the subclip starting
// `offset` frames into it (its Clip source, BinItem::sources). The segment
// then refers to the master clip, at the same frames.
bool changeSourceTrack(Sequence& sequence, const SegmentRef& ref, const BinItem& master, const QString& masterBin,
                       std::int64_t offset, const QString& sourceTrack);
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
// An empty `tracks` list means every track. Filler never matches.
std::optional<MatchedFrame> matchFrame(const Sequence& sequence, const QStringList& tracks, std::int64_t position);

// Audio Mixdown's targets (p.732): the audio tracks, in order, that are not in `exclude` and hold
// nothing but filler in frames [start, end) (a track that ends before `start` counts).
QStringList emptyAudioTracks(const Sequence& sequence, std::int64_t start, std::int64_t end, const QStringList& exclude = {});

// Reverse Match Frame (p.433-434): the sequence frames, in order, that show frame `frame` of clip
// `clipId` from one of `sourceTracks` (empty: any) on one of `tracks` (empty: every track).
QList<std::int64_t> reverseMatchFrames(const Sequence& sequence, const QStringList& tracks, const QString& clipId,
                                       const QStringList& sourceTracks, std::int64_t frame);

// ---- Trimming (guide ch. 20, p.647-665)

// Which side of an edit point a trim roller is on: the outgoing segment
// (A-side), the incoming one (B-side), or both (a dual-roller trim, p.655).
enum class TrimSide { A, B, Both };
// Single-roller trims: Ripple moves the rest of the track (p.654);
// Overwrite keeps the track's length, adding filler or overwriting (p.653).
enum class TrimKind { Ripple, Overwrite };

// A roller at the edit point `frame` of `track`.
// `frame` is the edit point: the frame after the A-side's last frame, which is the B-side's
// first.
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
// Frames, relative to where the rollers are now; {0, 0} when they cannot move.
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
// Every segment end, in order (0 is not included).
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
// Video tracks only; false on any other track, a locked track or past the end of the track.
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
// is short ("Media A" (outgoing), "Media B" (incoming), or "Media A and
// Media B" when both are; the Insufficient Source dialog, FX guide p.48).
// It is empty when the transition fits or only its length is zero.
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
// For a transition, `frame` is the first frame of the segment that carries it (its B-side).
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
// Null when the reference no longer names an effect. Pointers are valid until the track changes.
const Effect* effectFor(const Sequence& sequence, const EffectRef& ref);
Effect* effectFor(Sequence& sequence, const EffectRef& ref);
const Transition* transitionFor(const Sequence& sequence, const EffectRef& ref);
Transition* transitionFor(Sequence& sequence, const EffectRef& ref);

// ---- Audio (ch. 21)

// Clip gain: the fader goes to +12 dB, typed values to +36 dB (p.669-670);
// the bottom of the fader is -∞, stored as kGainOff.
inline constexpr double kGainMax = 36.0;
inline constexpr double kGainFaderMax = 12.0;
inline constexpr double kGainOff = -1000.0;
inline constexpr int kPanMax = 100; // ours: the guide gives no range
// Rounds to 0.1 dB and caps at kGainMax; anything at or below kGainOff + 1 becomes kGainOff.
double clampGain(double db);
// Clip Volume and Pan mode (p.702, p.711): sets the gain and/or pan of the
// whole segments of a track that overlap [start, start + length); never
// part of a segment. Filler and locked tracks are left alone. Returns how
// many segments were set.
// A pan of kDefaultPan removes the segments' own pans (Remove Pan on Track), so they play at the
// track's default again.
int setClipAudio(Sequence& sequence, const QString& track, std::int64_t start, std::int64_t length,
                 std::optional<double> gain, std::optional<int> pan);
inline constexpr int kDefaultPan = std::numeric_limits<int>::min();
// Adjust Pan/Vols on Track (p.711): adds `byDb` to the clip gain and `byPan` to the pan (from the
// default `side` for segments without one) of the same segments. Returns how many changed.
int adjustClipAudio(Sequence& sequence, const QString& track, std::int64_t start, std::int64_t length, double byDb,
                    int byPan, int side);
// Remove Default Clip Pan on Track (`commit` false): segments whose own pan equals the default
// `side` lose it, so they follow the default from now on. Commit Default Clip Pan on Track:
// segments without a pan of their own take `side` as theirs, so they keep it when the default
// changes (p.711). Returns how many changed.
int setDefaultClipPan(Sequence& sequence, const QString& track, std::int64_t start, std::int64_t length, int side,
                      bool commit);

// One step of Edit > Undo/Redo (p.466): a sequence item before and after.
struct EditStep {
    // The menu text of the edit; binId is the bin holding the sequence item.
    QString description; // "Splice-in", "Overwrite", ...
    QString binId;
    BinItem before;
    BinItem after;
};

// Undo and redo, 100 levels (p.466).
class EditHistory {
public:
    static constexpr int kLevels = 100;

    // Drops any steps that were undone, adds the step and forgets the oldest past kLevels.
    void push(EditStep step);
    bool canUndo() const { return next_ > 0; }
    bool canRedo() const { return next_ < steps_.size(); }
    // The step undone or redone.
    // Pointers are valid until the next push() or clear(); null when there is nothing to do. The
    // caller puts the step's `before` (undo) or `after` (redo) item back into its bin.
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

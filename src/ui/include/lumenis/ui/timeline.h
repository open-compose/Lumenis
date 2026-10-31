#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/media/waveform.h"
#include "lumenis/model/automation.h"
#include "lumenis/model/clip_coloring.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_palette.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QLabel;
class QMimeData;
class QPainter;
class QLineEdit;
class QMenu;
class QScrollBar;
class QSlider;
class QToolButton;

// The Timeline window and its tracks area. Timeline holds the editing state (track patch, segment
// selection, Trim mode, the Clipboard, zoom) and carries out the Timeline's commands by changing
// the Record monitor's sequence through the model (src/model/include/lumenis/model/sequence.h,
// marker.h) and storing it back in its bin; TimelineView draws the tracks and turns mouse input
// into calls on Timeline. MainWindow (main_window.cpp) creates the Timeline and wires its stores;
// the Composer (composer.h) owns the monitors whose loaded items the Timeline shows and changes.
namespace lumenis::ui {

class ClipColorDialog;
class SequenceMap;
class SequenceMapWindow;
class Timeline;

// The tracks area of the Timeline: the Track Selector panel on the left
// (p.625-630) and, to its right, the timecode ruler, the tracks with their
// segments, the IN to OUT highlight and the blue position indicator
// (p.607-609).
class TimelineView : public QWidget {
    Q_OBJECT

public:
    // The view reads all of its state from `timeline`, which must outlive it.
    explicit TimelineView(Timeline& timeline, QWidget* parent = nullptr);

    // The rows top to bottom: D1, video tracks from the highest, TC1, audio
    // tracks from A1 (picture on p.607).
    struct Row {
        QString track; // "V1", "TC1", ...
        int top = 0;
        int height = 0;
        bool exists = true; // false: only a source track is patched here
    };
    // The rows in display order, with `top` already offset by the vertical scroll (so it can be
    // negative or below the widget). Empty with nothing shown.
    QList<Row> rows() const;
    // The ruler plus every row, in pixels, ignoring the scroll.
    int contentHeight() const;
    // The height of the timecode ruler at the top, in pixels.
    int rulerHeight() const;
    // The width of the Track Selector panel, or 0 when the Fast menu's Track Panel is off.
    int headerWidth() const;
    // Frame at an x position in the tracks area, and back.
    // Frames count from the start of the shown sequence (0 = its first frame); x is in widget
    // pixels, so the header is included. frameAt() rounds down, and gives 0 if the scale is not
    // positive.
    std::int64_t frameAt(int x) const;
    double xFor(std::int64_t frame) const;
    // The Track Selector's buttons for a row, in widget pixels: the source track button, the
    // record track button, the Sync Lock button and the Track Monitor button (p.625-630).
    QRect sourceButton(const Row& row) const;
    QRect recordButton(const Row& row) const;
    QRect syncButton(const Row& row) const;
    QRect monitorButton(const Row& row) const;
    // The Track Control panel's buttons for an audio row (p.602), right of the Track Selector:
    // Waveform, Clip Volume/Pan, Inactive and Solo on the top line, the inserts a-e
    // (InsertA + slot) and Mute below. Empty while the panel is hidden, for other rows, and
    // for the lower line on a row too short for it ("If you reduce the size of the Timeline
    // tracks, you might not see the Track Control panel tools", p.602).
    enum Control { Waveform, AudioData, Inactive, Solo, Mute, InsertA };
    QRect controlButton(const Row& row, int control) const;
    // The Track Control Panel button above the track buttons (p.602).
    QRect controlPanelButton() const;
    // Heads and Heads Tails views (p.638-639): one box per clip along a single row, with its head
    // frame (and tail frame). headsBox() is clip `index` of `count`'s box; headsAt() the clip under a
    // point, or with `boundary` the place between clips nearest it (0 to the clip count); -1 for none.
    QRect headsBox(int index, int count) const;
    int headsAt(const QPoint& pos, bool boundary) const;
    // The row under a point, or nothing (also over the ruler).
    std::optional<Row> rowAt(const QPoint& pos) const;
    // The segment under a point (filler too, but not at the ends of a
    // track, p.611).
    std::optional<model::SegmentRef> segmentAt(const QPoint& pos) const;
    // With both segment tools on, the upper half of a segment is
    // Lift/Overwrite and the lower half Extract/Splice-in (p.610).
    model::EditKind segmentModeAt(const QPoint& pos) const;
    // The transition under a point, as a roller: on the edit a dual roller,
    // just left of it the A-side, just right the B-side (p.650).
    std::optional<model::TrimRoller> transitionAt(const QPoint& pos) const;
    // The trim kind a click there picks with the trim tools (p.650): the
    // upper half Overwrite, the lower half Ripple when both are on.
    model::TrimKind trimKindAt(const QPoint& pos) const;
    // The automation graph of an audio row (Audio Data, p.715): where a value is drawn, and the
    // value at a height. Volume follows the Audio Mixer fader's scale (+12 dB at the top, -∞ at
    // the bottom); pan goes from L100 at the top to R100 at the bottom (ours).
    double yForValue(const Row& row, model::AutomationParam param, double value) const;
    double valueForY(const Row& row, model::AutomationParam param, double y) const;
    // The keyframe of the graph shown under a point (within a few pixels), if any.
    std::optional<model::KeyRef> keyframeAt(const QPoint& pos) const;

    // Vertical scrolling (p.608).
    // The offset is in pixels from the top of the rows; it is clamped to be at least 0.
    void setTopOffset(int offset);
    int topOffset() const { return top_; }

    // Zoom In from the Fast menu: the next drag picks the area (p.608).
    void beginZoomSelection() { zoomSelecting_ = true; }
    bool isZoomSelecting() const { return zoomSelecting_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    // Expands or closes a nest (Timeline::doubleClickTrack).
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // Effects dragged from the Effect Palette (FX guide p.44).
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;

private:
    void paintEffects(QPainter& p, const Row& r, const model::Track& t) const;
    // Audio Data over an audio row's segments (timeline_automation.cpp).
    void paintAudioData(QPainter& p, const Row& r, const model::Track& t) const;
    void paintControls(QPainter& p, const Row& r, const model::Track& t) const;
    void paintHeads(QPainter& p);
    // A segment's waveform (p.677-680) in `box`: a band per channel of the track, in Display Channel
    // Order, with a line between them.
    void paintWaveform(QPainter& p, const QRectF& box, const model::Track& t, const model::Segment& s,
                       std::int64_t at) const;
    // The frame, with its fraction, at a widget x.
    double exactFrameAt(double x) const;
    // `click`: the press that starts a scrub, which Position Bar Snap snaps (p.1220).
    void moveIndicator(const QPoint& pos, Qt::KeyboardModifiers modifiers, bool click = false);
    // Use Fast Scrub (p.1219, p.397): while the position indicator is dragged, markers, the IN to OUT
    // marks, clip frames and waveforms are not drawn.
    bool fastScrubbing() const;
    // Whether a click or lasso may select the segment: filler only with Select Filler with Segment
    // Tools (p.616, p.1220).
    bool selectable(const model::SegmentRef& ref) const;
    // A segment drag: frames and tracks moved so far.
    std::int64_t dragFrames(Qt::KeyboardModifiers modifiers) const;
    int dragTracks() const;
    void selectLasso(const QRect& area);

    Timeline& timeline_;
    int top_ = 0;
    bool scrubbing_ = false;
    bool zoomSelecting_ = false;
    std::optional<int> zoomFrom_;
    int zoomTo_ = 0;
    QString patchFrom_; // a source track being dragged to a record track
    QPoint patchPos_;
    std::optional<QPoint> dragFrom_; // a segment drag
    QPoint dragPos_;
    bool dragging_ = false;
    model::EditKind dragKind_ = model::EditKind::Overwrite;
    std::optional<QPoint> lassoFrom_;
    QPoint lassoTo_;
    std::optional<QPoint> trimFrom_; // a roller or slip drag
    QPoint trimPos_;
    std::optional<QPoint> keyFrom_; // a keyframe drag (p.716)
    QPoint keyPos_;
    model::KeyRef keyGrab_;
    double keyGrabValue_ = 0;
    // An insert button pressed (p.795): its track and slot, and where. Released on another insert
    // button it moves the insert there, or with Alt copies it; released in place it is a click.
    struct InsertPress {
        QString track;
        int slot = 0;
        QPoint from;
    };
    std::optional<InsertPress> insertFrom_;
    // A bin clip being dragged in (p.624): its first frame and length, for the white outline.
    struct BinDrop {
        std::int64_t frame = 0;
        std::int64_t length = 0;
    };
    std::optional<BinDrop> binDrop_;
    // The frame a bin clip dragged to x goes to: with Segment Drag Snap, the nearest edit point or the
    // position indicator within a few pixels (p.1220).
    std::int64_t binDropFrame(int x) const;
    // A clip being Alt+dragged in a Heads view (p.639), and where the mouse is.
    std::optional<int> headsFrom_;
    QPoint headsPos_;
    // The insert button under a point: its track and slot.
    std::optional<std::pair<QString, int>> insertAt(const QPoint& pos) const;
};

// The Timeline window (ch. 19): the Record monitor's sequence, or with
// Toggle Source/Record in Timeline the Source monitor's clip, and the
// editing that puts clips into sequences (ch. 15).
class Timeline : public QWidget {
    Q_OBJECT

public:
    // View Type (Fast menu, p.592, p.638-639): Heads shows each clip's head frame, Heads Tails its
    // head and tail frames, in place of the tracks.
    enum class ViewType { Standard, Heads, HeadsTails };
    // Sync Breaks (p.491-492): sync-break numbers and match-frame signs on every track, video tracks
    // only, audio tracks only, or none.
    enum class SyncBreaks { All, Video, Audio, None };
    // Render Ranges (FX Guide p.150): no red lines; on the unrendered part of partly rendered effects
    // ("the default option"); or also on every effect that is not rendered at all.
    enum class RenderRanges { None, PartialOnly, All };
    // Stores a changed or new sequence in its bin.
    using SequenceStore = std::function<bool(const QString& binId, const model::BinItem& item, bool isNew)>;
    // Makes a new, empty sequence for the first edit (p.463): its bin and
    // the item, not yet stored.
    using SequenceMaker = std::function<std::optional<std::pair<QString, model::BinItem>>()>;

    // The Timeline follows `composer`, which must outlive it: it redraws when the Composer changes,
    // takes the Composer's Timeline commands and installs the Composer's event and stop resolvers.
    explicit Timeline(Composer& composer, QWidget* parent = nullptr);

    // The callbacks MainWindow supplies: where sequences are stored, how the first edit makes
    // one, and which command a key press means. Without a store, changes reach only the Composer.
    void setSequenceStore(SequenceStore store) { store_ = std::move(store); }
    void setSequenceMaker(SequenceMaker maker) { maker_ = std::move(maker); }
    void setKeyResolver(Composer::KeyResolver resolver) { keys_ = std::move(resolver); }

    // Behaviour set in the settings (guide ch.33).
    struct Options {
        // Timeline settings (p.1218-1220).
        bool toolbar = true;           // Show Toolbar
        bool markedRegion = true;      // Show Marked Region
        enum class Movement { Page, Scroll, None };
        Movement movement = Movement::Page; // Movement During Play
        bool onlyOneSegmentTool = false;
        model::EditKind defaultSegmentTool = model::EditKind::Overwrite;
        // Composer settings: Edit tab (p.1160).
        bool activateRecordAfterEdit = false;
        // Composer settings, Edit tab (p.1159-1160; docs/spec/composer-edit.md).
        bool syncPointEditing = false;    // Sync Point Editing (Overwrites), also Composer > Sync Point Overwrite
        bool singleMarkEditing = true;    // Single Mark Editing (p.468)
        bool phantomMarks = false;        // Phantom Marks (p.501)
        bool autoCreateTracks = true;     // Auto-create New Tracks
        bool autoEnableSourceTracks = true; // Auto-enable Source Tracks
        bool copySourceMarkers = true;    // Copy Source Markers
        // Composer settings: Move tab (p.1160).
        bool stopAtHead = true;
        bool stopAtTail = false;
        bool stopAtMarkers = false;
        bool ignoreTrackSelectors = false;
        // Trim settings (p.1223; docs/spec/trim-settings.md).
        bool trimAutoFocus = false;
        int trimPreroll = 2;      // Play Loop tab: seconds before the transition (ours: seconds)
        int trimPostroll = 2;     // seconds after it
        int trimIntermission = 0; // seconds between passes (ours: a pause)
        bool jklTrim = true;      // J-K-L Trim (p.658)
        // Features tab: Small Trim display never, after Go to Previous/Next Event, or always (p.649).
        enum class SmallTrim { Never, GoTo, Always };
        SmallTrim smallTrim = SmallTrim::Never;
        bool dualImagePlay = false; // Dual Image Play (p.659)
        bool syncRollers = false;   // Sync Rollers for Sync Locked Tracks (p.656)
        // Timeline settings, Edit tab (p.1220): Start Filler Duration (seconds, ours) and Find Flash
        // Frames Shorter Than (frames).
        int startFillerSeconds = 2;
        int flashFrames = 10;
        // Timeline settings: Show Marked Waveforms, waveforms only between the marks (p.1218).
        bool markedWaveforms = false;
        // Audio settings: Display Channel Order of 5.1 and 7.1 tracks' waveforms (p.1148): SMPTE, or
        // Pro Tools.
        bool proToolsChannelOrder = false;
        // Timeline settings: Dupe Detection Handles, frames added at both ends of each clip (p.1220).
        double dupeHandles = 0;
        // Timeline settings, Display tab (p.1218-1219; docs/spec/timeline-settings.md).
        enum class TextColor { White, Black, Both };
        TextColor textColor = TextColor::White; // Timeline Text Color
        bool wireframeDragging = false;         // Wireframe Dragging: outlines only while dragging
        // Display During Segment Drag (p.617-618, p.1219).
        enum class DragDisplay { PositionBar, FourFrame, None };
        DragDisplay segmentDragDisplay = DragDisplay::PositionBar;
        bool fastScrub = false;                 // Use Fast Scrub: no marks or markers while scrubbing
        // Timeline settings, Edit tab (p.1220; docs/spec/timeline-settings.md).
        bool autoPatching = false;         // Auto-Patching: patches follow the enabled tracks
        bool autoMonitoring = true;        // Auto-Monitoring: "selected by default"
        bool segmentDragSyncLocks = false; // Segment Drag Sync Locks (p.618)
        bool positionBarSnap = false;      // Position Bar Snap (p.397)
        bool segmentDragSnap = false;      // Segment Drag Snap: bin clips dragged in snap (p.1220)
        // Display tab: Double-Click to Show Nesting, "lets you double-click segments in the Timeline to display
        // the nested effects" (p.1219); in Effect mode a double-click always does (FX Guide p.220).
        bool doubleClickNesting = true;
        bool editFromBin = false;          // Bin settings: Enable edit from bin (p.1153, p.624)
        // Interface settings, Timeline and Viewers tab (p.1201-1202): Use Timeline Background Color
        // (invalid: the default), and Use Blending Based on Skin, which mixes it half and half with
        // the default grey.
        QColor background;
        bool blendBackground = true;
        bool defaultSyncLocks = false;     // Default Sync Locks On
        bool selectFiller = true;          // Select Filler with Segment Tools (p.616); ours: on
        bool openEffectEditor = false;     // Applying Effects opens Effect Editor
        bool tcDisablesSmartTools = false; // Clicking the TC Track or ruler Disables Smart Tools (p.722)
        bool lassoSegmentMode = false;     // Lassoing transitions enters: Segment Mode (else Trim Mode)
        // Timeline settings: Sequence Map (p.1219). Ours: the defaults.
        bool mapOverlay = true;    // Open Sequence Map as overlay on Timeline
        bool mapClickHides = true; // Clicking Elsewhere Hides the Sequence Map
        bool mapWaveforms = false; // Display waveforms
        bool mapEffects = true;    // Display effects
    };
    // Applies new settings at once: shows or hides the top toolbar and, with Only One Segment
    // Tool, turns Extract/Splice-in off if both segment tools were on.
    void setOptions(const Options& options);
    const Options& options() const { return options_; }

    Composer& composer() const { return composer_; }
    // Toggle Source/Record in Timeline (p.607).
    void setShowSource(bool on);
    bool showsSource() const { return showSource_; }
    MonitorId shownMonitor() const { return showSource_ ? MonitorId::Source : MonitorId::Record; }
    // What the Timeline shows: a sequence, or a clip as one segment per
    // track. Empty with nothing loaded.
    const model::Sequence& shown() const { return shown_; }
    // Whether the shown monitor has anything loaded.
    bool hasShown() const;
    // The timecode format for the shown item's edit rate.
    const TimecodeFormat& format() const { return format_; }
    // The shown item's starting timecode as a frame count; frame 0 of shown() is at this timecode.
    std::int64_t startFrame() const;

    // The Track Selector state: which source tracks there are, which are patched to which record
    // tracks, and which are selected.
    model::TrackPatch& patch() { return patch_; }
    // The playback performance indicators (FX Guide p.136-137) shown in TC1 after playing the Record
    // monitor, while the Timeline setting Highlight Suggested Render Areas After Playback is on; the next
    // play or change of sequence clears them.
    void setPlaybackIndicators(const QList<Composer::PlaybackRange>& ranges);
    // ExpertRender (FX Guide p.142-146): the effects it chose, highlighted; while `modifying` (Modify
    // Selection), Shift+click on an effect adds it or takes it out (toggleExpertTarget) and a click in
    // TC1 cancels (cancelExpertSelection, which emits expertSelectionCancelled()).
    void setExpertSelection(const QList<model::RenderTarget>& targets, bool modifying);
    const QList<model::RenderTarget>& expertSelection() const { return expert_; }
    bool isModifyingExpertSelection() const { return modifyingExpert_; }
    void cancelExpertSelection();
    bool toggleExpertTarget(const QString& track, std::int64_t frame);
    const QList<Composer::PlaybackRange>& playbackIndicators() const { return indicators_; }
    const model::TrackPatch& patch() const { return patch_; }
    // The undo history of the record sequence's changes.
    model::EditHistory& history() { return history_; }

    // Commands the Timeline carries out (editing, tracks, undo).
    // Returns false for a command it does not handle; true otherwise, also when the command did
    // nothing (see lastMessage()), except for a few commands that report false on failure.
    bool perform(const QString& commandId);
    // Changes the Record monitor's sequence as one undo step named `description` (Audio Mixdown):
    // `change` edits a copy and returns false to leave it as it was. False without a sequence.
    bool changeRecordSequence(const QString& description, const std::function<bool(model::Sequence&)>& change);

    // Nests (FX Guide p.219-227; docs/spec/nesting.md). Step In: "Media Composer replaces the full sequence
    // in the Timeline with just the tracks that make up the nested effect" (p.220) — the segment with an
    // effect under the position on the highest selected video track (the highest video track with one
    // when none is selected), which gets a nest of its own picture when it has none (model::ensureNest).
    // The nest is shown as a sequence of its own in the Record monitor; every change to it is stored back
    // into its segment, and up through the levels to the sequence in its bin. Step Out goes back a level,
    // the position on the same frame. False when there is nothing to step into or out of.
    bool stepIn();
    bool stepOut();
    // How many levels in the Timeline is (0 at the sequence itself): "N2 indicates that you are two levels
    // in from the outermost track level" (p.220), shown in the Timecode display.
    int nestLevel() const { return static_cast<int>(nests_.size()); }
    // Expanding nested effects in the Timeline (FX Guide p.220-221; docs/spec/nesting.md): "The nested tracks
    // appear in the Timeline above the track you double-clicked. Editing and patching features are available
    // for changing the content of the track's effect." expandNest() opens the segment effect under `frame` on
    // `track` (one without a nest gets one, an undo step "Expand Nest", as Step In does): the Record monitor
    // shows the expanded sequence (model::expandNest) as an item of its own, and every change to it (edits,
    // undo) goes back into the sequence in its bin (model::collapseExpansion). One nest is expanded at a time
    // (ours): expanding another closes the first. closeNest() closes it, the position kept; it closes by
    // itself when another sequence is loaded or its segment is removed. False when there is nothing there.
    bool expandNest(const QString& track, std::int64_t frame);
    bool closeNest();
    bool isNestExpanded() const { return expanded_.has_value(); }
    // The expanded segment's track ("V2"), and whether a row is one of its nested tracks ("You cannot monitor
    // individual tracks when you expand nested tracks", p.220: they are never monitored).
    QString expandedTrack() const { return expanded_ ? expanded_->expansion.track : QString(); }
    bool isNestedRow(const QString& track) const { return expanded_ && expanded_->expansion.isNested(track); }
    // The expanded segment's frames [first, end); the nested rows hold the nest's material there.
    std::optional<std::pair<std::int64_t, std::int64_t>> expandedFrames() const;
    // With a nest expanded, the sequence as it goes to its bin (model::collapseExpansion of what is shown);
    // nothing otherwise.
    std::optional<model::Sequence> collapsedSequence() const;
    // A double-click in the tracks (p.220): on the expanded segment's track, closes it; on a segment
    // effect, expands it in Effect mode, or in Source/Record mode with a segment tool on and Double-Click
    // to Show Nesting (Options::doubleClickNesting). False when it did neither.
    bool doubleClickTrack(const QString& track, std::int64_t frame);
    // Collapse (p.226-227): the selected video tracks (adjacent) between the marks, or the segments selected
    // in segment mode, into a Submaster effect (model::collapseTracks).
    bool collapse();
    // Whether perform() takes this command ID (from commands.json).
    static bool handles(const QString& commandId);
    // Why the last command did nothing, for the status line.
    QString lastMessage() const { return message_; }

    // Patching from the Track Selector (p.629).
    // Returns false, with a message, if the patch is not allowed (audio only to audio, video
    // only to video, never data; model::TrackPatch::canPatch; and an audio track only to an
    // existing record track of its format, p.629).
    bool patchTracks(const QString& source, const QString& record);
    // Split Track to Mono (p.734-735) on `track`, or with an empty name Split All Tracks to Mono
    // (Timeline > Split All Tracks, Clip > Split All Tracks, Clip > Audio > Split All Tracks, p.736):
    // model::splitToMono on the record sequence, an undo step, after asking for a copy of the
    // sequence as it was (sequenceCopyRequested). False with a message for a mono track or a
    // sequence without multichannel tracks.
    bool splitTracksToMono(const QString& track);
    // Select or deselect a source or record track button.
    void toggleSourceTrack(const QString& track);
    void toggleRecordTrack(const QString& track);
    // Track Monitor buttons (p.627-628): which tracks are heard and seen.
    bool isMonitored(const QString& track) const { return !unmonitored_.contains(track) && !isNestedRow(track); }
    void toggleMonitor(const QString& track);

    // Track locks (p.633) and sync locks (p.493).
    // These change the record sequence without an undo step; they do nothing (with a message)
    // if the Record monitor has no sequence.
    void setTracksLocked(const QStringList& tracks, bool locked);
    void toggleSyncLock(const QString& track);
    void toggleSyncLockAll();

    // Segment mode (p.610-623). The two segment tools, each on or off; the
    // tool used when neither or both apply (Timeline settings, p.623; ours
    // is Lift/Overwrite, as for a lasso with no tool, p.612).
    bool isSegmentTool(model::EditKind kind) const;
    void setSegmentTool(model::EditKind kind, bool on);
    // A segment drag of the selection by `frames` is under way (or, with nullopt, has ended): shows it
    // in the Composer as Display During Segment Drag says (p.617-618, p.1219).
    void showSegmentDrag(std::optional<std::int64_t> frames);

    // Bin editing into the Timeline (p.623-624; docs/spec/bin-editing.md), with the Bin setting
    // Enable edit from bin. The edit a bin drag makes: the one segment tool that is on, otherwise
    // (neither or both) the Default Segment Tool.
    model::EditKind binEditKind() const;
    // The tracks area's background: the Interface setting's colour, blended or not, or the default.
    QColor backgroundColor() const;
    // The clip a bin drag carries (binItemsMime): one master clip, subclip, audio clip, group or
    // sequence, resolved through the Composer; nothing for several items or other kinds.
    std::optional<std::pair<QString, model::BinItem>> droppedClip(const QMimeData* mime) const;
    // Edits `item` (from bin `binId`) into the record sequence: it is loaded into the Source monitor,
    // which keeps the marks it had there ("Otherwise, the entire clip is edited", p.624: without marks
    // the whole clip), then spliced in or overwritten at `recordAt` (a drop), or without one at the
    // Record monitor's marks or position (the V and B keys). False, with lastMessage(), if no edit
    // was made.
    bool editClip(model::EditKind kind, const QString& binId, const model::BinItem& item,
                  std::optional<std::int64_t> recordAt = std::nullopt);
    // Turns every edit tool of the Smart Tool off: both segment tools and both trim tools (p.1220).
    void disableSmartTools();
    // The kind of edit the segment tools make: the one tool that is on, else Lift/Overwrite.
    model::EditKind segmentKind() const;
    // The selected segments of the record sequence; empty when the source is shown.
    const QList<model::SegmentRef>& selection() const { return selection_; }
    void setSelection(const QList<model::SegmentRef>& segments);
    // Click and Shift-click (p.611).
    void clickSegment(const model::SegmentRef& segment, bool toggle);
    // Moves (or with `copy` copies) the selected segments by frames and by tracks of the same kind
    // (positive to higher-numbered tracks), as one undo step; the moved segments stay selected
    // (p.620).
    bool moveSelection(std::int64_t frames, int tracks, model::EditKind kind, bool copy);
    // The Delete key: selected segments (p.621), or else the selected
    // tracks after asking (p.636).
    bool deleteSelection();
    // Removes the named tracks from the record sequence without asking, as one undo step.
    bool deleteTracks(const QStringList& tracks);
    // The right-click Select menu (p.613-614).
    // Clips with Same Source Clip Color collects the source (bin) colours of the selected clips and
    // selects every clip with one of them; Clips With No Source Clip Color, Offline Clips and Clips
    // With Same Local Clip Color (the selected clips' local colours) select as they say (p.613-614).
    enum class SelectWhere { Left, Right, Reverse, SameSourceColor, NoSourceColor, Offline, SameLocalColor };
    // Selects on the selected record tracks; turns on Lift/Overwrite if no segment tool is on.
    void selectSegments(SelectWhere where);

    // Clip colours (p.594-598; docs/spec/timeline-colors.md). The Clip Color dialog's choices, kept in
    // the Timeline setting by MainWindow; a change repaints and emits clipColorOptionsChanged().
    const model::ClipColorOptions& clipColorOptions() const { return clipColors_; }
    void setClipColorOptions(const model::ClipColorOptions& options);
    // Fast menu > Clip Color...: the dialog; OK applies it. Tests answer it through the hook, which
    // returns whether OK was clicked.
    using ClipColorAsk = std::function<bool(ClipColorDialog&)>;
    void setClipColorAsk(ClipColorAsk ask) { clipColorAsk_ = std::move(ask); }
    void openClipColorDialog();
    // Set Local Clip Color (p.598): the selected segments take `hex` ("#rrggbb"; empty for None), as
    // one undo step. False with a message when no segment is selected.
    bool setLocalClipColor(const QString& hex);
    // Timeline Clip Notes (p.483-486). setClipNote() gives `segments` of the record sequence `note`
    // (empty deletes it), as one undo step; false if none of them is material or nothing changed.
    bool setClipNote(const QList<model::SegmentRef>& segments, const QString& note);
    // Right-click > Add Timeline Clip Note: "The Timeline Clip Note dialog box opens. Type your notes
    // in the text box, and click OK" (p.483). On the selected segments, else on `segment`. The dialog
    // starts with the first one's note; tests answer it through setNoteAsk (nothing: Cancel).
    bool addClipNote(const std::optional<model::SegmentRef>& segment = {});
    using NoteAsk = std::function<std::optional<QString>(const QString& current)>;
    void setNoteAsk(NoteAsk ask) { noteAsk_ = std::move(ask); }

    // Timeline > Add Filler at Start (p.462): Start Filler Duration's filler before everything, an
    // undo step. Refused for an empty sequence or a locked track.
    bool addFillerAtStart();
    // Timeline > Find Black Hole (false) and Find Flash Frame (true) (p.644-645): moves the Record
    // position to the next one after it on the selected tracks; false, with a message, if there is
    // none or no track is selected.
    bool findGap(bool flash);
    // Rename Track (p.635): "The Comments window opens. Type a new name for the track. Click OK"; its
    // Remove button gives the default name back. Kept with the sequence, without an undo step (ours,
    // as for locks). setTrackName() does it without the window; an empty name removes it.
    void renameTrack(const QString& track);
    void setTrackName(const QString& track, const QString& name);
    // Tests answer the Comments window through this: the name, "" for Remove, nothing for Cancel.
    using RenameAsk = std::function<std::optional<QString>(const QString& current)>;
    void setRenameAsk(RenameAsk ask) { renameAsk_ = std::move(ask); }
    // Create Sequence Based on Selection (p.613-614): emits sequenceFromSelectionRequested().
    bool createSequenceFromSelection();
    // The facts about a segment's clip the colours depend on (model::ClipFacts); nothing when the clip
    // cannot be found. Whether media is online is checked at most every two seconds per clip.
    std::optional<model::ClipFacts> clipFacts(const model::Segment& segment) const;
    // The colour a track's segments are drawn in: its kind's, or its Track Color (p.597); an
    // inactive track's darker (ours).
    static QColor trackColor(const model::Track& track);
    // The colour a segment is drawn in; empty for its track's own.
    QString segmentColor(const model::Segment& segment) const;
    // The project's picture height, for SD/HD (SD up to 576 lines).
    void setProjectHeight(int height);
    // Show Source/Record colors in Timeline (Interface settings, p.383): selected source track
    // buttons green, record ones blue.
    void setSourceRecordColors(bool on);
    // Show Mismatched Sample Rates as Different Color (Audio Project settings, p.1149): with `show`,
    // the waveforms of clips whose Audio SR is not `projectRate` are drawn in
    // kMismatchedWaveformColor (p.680).
    void setMismatchedRates(bool show, int projectRate);
    bool showsMismatchedRates() const { return showMismatchedRates_; }
    int projectSampleRate() const { return projectSampleRate_; }
    // The waveforms the Timeline draws (lumenis/media/waveform.h); MainWindow points it at the project's
    // WaveformCache folder (p.677).
    media::WaveformCache& waveforms() const { return *waveforms_; }
    void setWaveformFolder(const QString& folder);
    // Audio Data > Waveform and the Waveform Options (p.677-679), as the Fast menu sets them.
    void setWaveform(bool on);
    void setWaveformLevels(bool output, bool fullHeight);
    // Enlarges (+1) or reduces (-1) the waveform plot without its track (p.678); false at the limit.
    bool zoomWaveform(int steps);
    // The order a track of `format` shows its channels' waveforms in (Display Channel Order, p.1148,
    // with the orders of p.689): the indices of its channels (0 for its first), top to bottom.
    QList<int> waveformChannels(model::AudioFormat format) const;
    // The plot of the sound at the project's rate, and of other rates when Show Mismatched Sample
    // Rates as Different Color is on (p.680).
    static constexpr QRgb kWaveformColor = 0xff000000;
    static constexpr QRgb kMismatchedWaveformColor = 0xffffffff;

    // What an audio track of the shown sequence shows (p.677-680): the Audio Data menu's choice for
    // every track, or, while Allow Per Track Settings is on, also the track's own (model::Track's
    // showWaveform, showClipVolume, showVolume, showPan). A track's own Volume or Pan takes the
    // place of the menu's.
    bool showsWaveform(const model::Track& track) const;
    bool showsClipVolume(const model::Track& track) const;
    std::optional<model::AutomationParam> automationShown(const model::Track& track) const;
    // Audio Data > Allow Per Track Settings (p.677-679).
    void setPerTrackSettings(bool on);
    // The Track Control panel's Waveform button (p.602, p.678): turns `track`'s waveform on or off,
    // or with `all` (Alt+click) every audio track's, and turns Allow Per Track Settings on (ours: so
    // the click shows something). Kept with the sequence, without an undo step.
    void setTrackWaveform(const QString& track, bool on, bool all = false);
    // The Clip Volume/Pan button's menu (p.679-680): Clip Volume, and Volume or Pan, on `track` or
    // with `all` on every audio track, as setTrackWaveform does.
    void setTrackAudioData(const QString& track, bool clipVolume, std::optional<model::AutomationParam> automation,
                           bool all = false);
    // The Clip Volume/Pan button's menu for `track` (the caller owns it): Clip Volume, Volume, Pan.
    QMenu* trackAudioDataMenu(const QString& track, bool all, QWidget* parent);
    // The Track Control panel's Inactive button (p.602): the track plays without its plug-ins or
    // automation (media::audioPieces, media::trackInserts). Audio tracks only.
    void setTrackInactive(const QString& track, bool inactive);
    // The Fast menu's Track Control Panel and the button above the track buttons (p.602).
    void setTrackControlPanel(bool on);
    // Solo and Mute are the Audio Mixer's (p.698-699): MainWindow gives the Timeline a way to read
    // them (`soloed` false: muted), and the panel's buttons emit soloMuteRequested().
    void setSoloMuteState(std::function<bool(const QString& track, bool soloed)> state);
    bool isSoloMuted(const QString& track, bool soloed) const;

    // Enlarge Track (+1) and Reduce Track (-1), Ctrl+L and Ctrl+K in the Timeline (p.594, p.601):
    // the selected record tracks' heights, kept with the sequence. False with no track selected or at
    // the limit.
    bool resizeTracks(int steps);
    // A row's height in pixels for a track of the shown sequence, by kind, Audio Data, the Track
    // Control panel and the track's height step.
    int trackHeight(const QString& track) const;
    // Fast menu > Track Color (p.597): the colour of `tracks` ("#rrggbb", empty for the default).
    // Clip colours take its place on their segments (p.595).
    void setTrackColor(const QStringList& tracks, const QString& hex);
    // Fast menu > Segment Selection and Mark IO Selection (p.593): the colour selected segments and
    // the IN to OUT highlight are drawn in; empty for the default.
    void setSelectionColors(const QString& segment, const QString& markIO);
    // Fast menu > Show Markers (p.599): whether markers of a colour (a name from model::markerColors())
    // are drawn. All shows every colour, None none.
    bool showsMarkers(const QString& color) const { return !display_.hiddenMarkers.contains(color); }
    void setMarkersShown(const QString& color, bool shown);
    void setAllMarkersShown(bool shown);
    // Fast menu > Sync Breaks and Dupe Detection (p.491-492, p.641).
    void setSyncBreaks(SyncBreaks which);
    void setDupeDetection(model::TrackKind kind, bool on);
    // Whether sync breaks and match frames show on a track of this kind.
    bool showsSyncBreaks(model::TrackKind kind) const;
    // The Clip Text a segment of `track` shows (p.592-593), in the menu's order, joined with spaces.
    QString clipText(const model::Track& track, const model::Segment& segment) const;

    // View Type > Heads or Heads Tails (p.638-639). "Heads and Tails view is disabled when you are
    // displaying material from the Source monitor" (p.603): showsHeads() is false then.
    void setViewType(ViewType type);
    bool showsHeads() const;
    // The clips those views show: the record sequence cut at every edit point of the selected record
    // tracks ("the system cuts all tracks to the same edit point", p.638), as [start, end) pairs.
    QList<std::pair<std::int64_t, std::int64_t>> headsSpans() const;
    QStringList headsTracks() const;
    // Alt+drag in those views (p.639): clip `from` moves to just before clip `before`
    // (headsSpans().size() for the end) on the selected tracks; an undo step.
    bool moveHeads(int from, int before);

    // Timeline views (p.604-605). MainWindow keeps them as Timeline View User settings and gives the
    // Timeline their names, sorted, and the one in use ("" for Untitled); the View Menu button in the
    // bottom toolbar shows it.
    void setViews(const QStringList& names, const QString& current);
    const QStringList& views() const { return views_; }
    const QString& currentView() const { return currentView_; }
    // The View Menu button's menu (the caller owns it): the views, then Save As.... With `replace`
    // (Alt+click) it lists "<view> Replace" for each instead (p.604).
    QMenu* viewMenu(bool replace, QWidget* parent);
    bool sourceRecordColors() const { return sourceRecordColors_; }

    // Trim mode (ch. 20). Rollers sit on edit points; single-roller trims
    // are Ripple or Overwrite (the trim tools, p.650).
    bool isTrimMode() const { return trimMode_; }
    // Effect mode (FX Guide p.48-50): the Effect Mode button opens the Effect Editor and turns the
    // Record monitor into the Effect Preview monitor (the Effect Editor drives it). It leaves Trim mode;
    // Trim mode, slipping and the Source/Record Mode button leave it (Editing Guide p.457, p.652).
    bool isEffectMode() const { return effectMode_; }
    void setEffectMode(bool on);
    // The rollers on the record sequence, empty when slipping.
    const QList<model::TrimRoller>& rollers() const { return rollers_; }
    model::TrimKind trimKind() const { return trimKind_; }
    // The Trim Mode button: dual rollers at the transition nearest the
    // position on the selected tracks (p.655); `direction` -1 or 1 takes the
    // previous or next one (the A and S keys, p.651).
    bool enterTrimMode(int direction = 0);
    // Leaves Trim mode and slip, remembering the rollers for Alt+Trim Mode (p.651), and undoes
    // the Focus that Trim mode's auto focus turned on.
    void exitTrimMode();
    // Enters Trim mode with exactly these rollers, clearing the segment selection and the trim
    // counters.
    void setRollers(const QList<model::TrimRoller>& rollers, model::TrimKind kind);
    // Click and Shift-click on a transition (p.652).
    void clickTransition(const model::TrimRoller& roller, model::TrimKind kind, bool add);
    // All rollers to one side, or both (the trim counters and Cycle Trim
    // Sides, p.651).
    void setTrimSide(model::TrimSide side);
    // Trim mode's Play Loop (p.656-657, p.659-660; the Play Loop button, `trim.play_loop`): plays
    // the transition of the first roller from Preroll before it to Postroll after it, over and over,
    // pausing for the Intermission between passes; trims made meanwhile show in the next pass. In
    // Source/Record mode it enters Trim mode first, at the last trimmed transition (p.656). Again, or
    // the space bar, stops it. False with no transition.
    bool playTrimLoop();
    bool isTrimLooping() const { return trimLooping_; }
    // Sync Rollers for Sync Locked Tracks (p.656): in a single-roller ripple trim on a sync-locked
    // track, the gray rollers drawn on the sync-locked tracks without one, which the trim moves too.
    QList<model::TrimRoller> syncRollers() const;
    // Edit Review (the Play tab, p.657-658): in Trim mode, leaves it (deselecting the rollers) and plays
    // from Preroll before the trimmed transition; otherwise from Preroll before the last transition at
    // or before the position where every selected track has an edit. False with none.
    bool editReview();
    // Trims by frames: Trim buttons, drags, J-K-L (p.656, p.658).
    // Positive frames trim to the right. Returns false, and beeps, when no frame could be trimmed.
    bool trimBy(std::int64_t frames);
    // The Overwrite Trim and Ripple Trim tools (p.650).
    bool isTrimTool(model::TrimKind kind) const;
    void setTrimTool(model::TrimKind kind, bool on);
    // Slip and slide (p.663-665): the segments selected for it.
    // Enters Trim mode slipping (or with `slide` sliding) the segments; an empty list leaves it.
    void setSlip(const QList<model::SegmentRef>& segments, bool slide);
    const QList<model::SegmentRef>& slipSelection() const { return slip_; }
    bool isSlide() const { return slide_; }
    // How long the clip a segment comes from is, from its bin.
    // The callback returns nothing when the clip is not in an open bin.
    model::SourceLength sourceLength() const;

    // Markers (p.412-424). Add Marker opens the Edit Marker dialog unless
    // it is disabled (the Markers window's Fast menu, p.416).
    // addMarker() marks the active monitor's item. A marker change on the Record monitor's
    // sequence is an undo step; on anything else it is stored without one.
    void setMarkerDialogEnabled(bool on) { markerDialog_ = on; }
    bool addMarker(const QString& color = QStringLiteral("Red"), bool spanned = false);
    // The marker in a monitor, changed or deleted.
    bool changeMarker(MonitorId monitor, const model::Marker& marker);
    bool deleteMarker(MonitorId monitor, const QString& id);

    // The Clipboard (p.479-480): a sequence that is in no bin.
    // Lift, Extract, Copy to Clipboard and Ctrl+C/X fill it; empty until one of them does.
    const std::optional<model::BinItem>& clipboard() const { return clipboard_; }

    // ---- Effects (FX guide ch. 2-3)
    // An effect dropped from the Effect Palette (p.44): a transition effect
    // goes on the cut `cut` if given, else the nearer cut of the segment at
    // `frame`; a segment effect on that segment. Video tracks only.
    bool dropEffect(const QString& track, std::int64_t frame, const EffectInfo& effect,
                    std::optional<std::int64_t> cut = std::nullopt);
    // An effect double-clicked in the Effect Palette (p.44): on the
    // selected segments, or on the transitions with trim rollers.
    bool applyEffectToSelection(const EffectInfo& effect);
    // Remove Effect (p.47): the effect the Effect Editor would show; without one, the Audio EQ
    // effects at the position on the selected audio tracks (p.741).
    bool removeEffectAtPosition();
    // The effect at the Record monitor's position on the selected tracks,
    // top down (p.84).
    std::optional<model::EffectRef> currentEffect() const;
    // Changes an effect of the record sequence, with an undo step.
    // `change` gets the effect and, for a transition, the transition itself (else nullptr).
    // Returns false if there is no such effect or its track is locked.
    bool changeEffect(const model::EffectRef& ref, const QString& description,
                      const std::function<void(model::Effect&, model::Transition*)>& change);
    // Nested effects (FX Guide p.221-222; model::nestedEffects): changes the effect at `level` of the
    // segment effect `ref`, `offset` frames into its segment (level 0 is the segment's own, as
    // changeEffect), with an undo step. False when there is no such effect or the track is locked.
    bool changeNestedEffect(const model::EffectRef& ref, std::int64_t offset, int level, const QString& description,
                            const std::function<void(model::Effect&)>& change);
    // "Drag the effect icon for the effect you want to move to the position you want it to occupy in the
    // nest" (p.222): model::moveNestedEffect, one undo step; false with lastMessage() saying why not.
    bool moveNestedEffect(const model::EffectRef& ref, std::int64_t offset, int from, int to);
    // The Transition parameters (p.63, p.500): the duration and alignment
    // of the transitions at the cuts; a Dissolve where there is none.
    bool setTransitions(const QList<model::TrimRoller>& cuts, std::int64_t duration,
                        model::TransitionAlignment alignment, std::int64_t start = 0);
    // The Audio Mixer in Clip mode (p.702, p.711): the gain and/or pan of
    // the segment under the Record position; with `onTrack` (Set Level on
    // Track and the like) of the segments between the marks: IN to OUT, IN
    // to the end, the start to OUT, or the whole track without marks.
    // A pan of model::kDefaultPan removes the segments' own pans.
    bool setClipAudio(const QString& track, std::optional<double> gain, std::optional<int> pan, bool onTrack,
                      const QString& description);
    // The Clip mode Fast menu's other items (p.711), on `tracks` between the marks as for
    // setClipAudio with `onTrack`, each one undo step; false with a message when nothing changed.
    // Adjust Pan/Vols on Track: adds `byDb` to the clip gains and `byPan` to the pans.
    bool adjustClipAudio(const QStringList& tracks, double byDb, int byPan);
    // Commit Default Clip Pan on Track (`commit`) and Remove Default Clip Pan on Track
    // (model::setDefaultClipPan, with each track's default from the Audio setting).
    bool setDefaultClipPan(const QStringList& tracks, bool commit);
    // Set Live Mix as Automation (p.725-726): on `tracks` between the marks (IN to OUT, IN to the
    // end, the start to OUT, the whole tracks without marks) the volume and pan keyframes give way
    // to each track's Live Mix value (`live`: track -> {dB, pan}), ramped at marks inside segments
    // (model::setAutomationTo). One undo step.
    bool setLiveMixAsAutomation(const QStringList& tracks, const QHash<QString, std::pair<double, int>>& live);
    // Surround panning (p.704-710; lumenis/model/surround_pan.h): channel `channel`'s pan of the
    // segment under the Record position on a mono or stereo track, with `other` the other channel's
    // too (Stereo Link and Mirror), as one undo step ("Surround Pan"). Already so: true without a step.
    bool setSurroundPan(const QString& track, int channel, const model::SurroundPan& pan,
                        const std::optional<model::SurroundPan>& other = std::nullopt);
    // "This track is LFE only" (p.710), one undo step. False for anything but a mono audio track.
    bool setLfeOnly(const QString& track, bool on);
    // Audio EQ (p.736-741; lumenis/model/audio_eq.h): `eq` on the whole segment under the Record
    // position on each of `tracks`, or without one its EQ removed; with `onTrack` (Set EQ - In/Out
    // and the Remove EQs items) on the segments between the marks: IN to OUT, IN to the end, the
    // start to OUT, or the whole track. One undo step. Already so: true without a step; no audio
    // segment there: false.
    bool setEq(const QStringList& tracks, const std::optional<model::AudioEq>& eq, bool onTrack,
               const QString& description);
    // AudioSuite (p.797-800; lumenis/model/audio_suite.h): `effect` on the whole segment under the
    // Record position on each of `tracks`, or with `onTrack` (the Fast menu's items) on the segments
    // between the marks: IN to OUT, IN to the end ("From IN"), the start to OUT, or the whole track
    // ("Global", p.799); without an effect, the effect removed. One undo step. Already so: true
    // without a step; no audio segment there: false.
    bool setAudioSuite(const QStringList& tracks, const std::optional<model::AudioSuiteEffect>& effect, bool onTrack,
                       const QString& description);
    // The record frames [from, to) the Fast menu's items work on for `track`, as for onTrack above.
    std::pair<std::int64_t, std::int64_t> markedRange(const QString& track) const;
    // The AudioSuite effect of the segment under the Record position on `track`, if any.
    std::optional<model::AudioSuiteEffect> audioSuiteAt(const QString& track) const;
    // Render Effect (p.797, p.800): each segment of `tracks` overlapping record frames [from, to)
    // whose AudioSuite effect is not rendered gets the render `render` makes of it, as one undo step.
    // A render that fails stops there with its message (the ones made before it are kept). Returns
    // how many were rendered, or nothing on failure.
    using AudioSuiteRenderer = std::function<std::optional<model::AudioSuiteEffect>(const model::Segment&, QString* error)>;
    std::optional<int> renderAudioSuite(const QStringList& tracks, std::int64_t from, std::int64_t to,
                                        const AudioSuiteRenderer& render);
    // How many segments of `tracks` overlapping [from, to) have an AudioSuite effect to render.
    int audioSuiteToRender(const QStringList& tracks, std::int64_t from, std::int64_t to) const;
    // Audio Punch-In (p.749-751; audio_punch_in.h): one recorded take of `clip` (a master clip of
    // `targets.size()` audio tracks, in bin `binId`) edited into the record sequence. The clip's
    // track k goes to targets[k]: a mono audio track that is not locked, or empty for a new audio
    // track, made with the first punch-in. Each punch-in overwrites its frames with the clip from
    // its source frame, as an undo step of its own ("the first undo removes the most recent
    // punch-in", p.751). Returns the tracks used, or nothing with a message, before any change.
    struct PunchIn {
        std::int64_t at = 0;     // record frame
        std::int64_t length = 0; // frames
        std::int64_t source = 0; // the clip's frame
        bool operator==(const PunchIn&) const = default;
    };
    std::optional<QStringList> punchIn(const QString& binId, const model::BinItem& clip, const QStringList& targets,
                                       const QList<PunchIn>& punches);
    // ---- Audio Track Effects (p.790-795; docs/spec/track-effects.md)
    // The insert in slot `slot` (0-4: a-e) of an audio track of the record sequence.
    std::optional<model::TrackInsert> trackInsert(const QString& track, int slot) const;
    // Puts `insert` in the slot (replacing what is there), or with none removes it, as an undo step.
    // Refused, with a message, for a track that is not audio, a locked one, or a slot out of range.
    // Already so: true without a step.
    bool setTrackInsert(const QString& track, int slot, const std::optional<model::TrackInsert>& insert,
                        const QString& description);
    // Drag an insert button to another (p.795): moves the insert, replacing what is there; with `copy`
    // (Alt+drag) leaves the original. Mono inserts go to mono tracks only (p.794).
    bool moveTrackInsert(const QString& fromTrack, int fromSlot, const QString& toTrack, int toSlot, bool copy);
    // Fills `menu` with Insert a-e, each with No Insert and the audio effects by category (the
    // track button's Audio Track Effects menu, p.791, and the Audio Mixer's insert button).
    void fillTrackEffectsMenu(QMenu* menu, const QString& track);
    // A new insert of an effect with its default values.
    static model::TrackInsert newInsert(const QString& effectId);
    // An audio effect dropped on an audio track (p.792): the Select Insert dialog picks the slot; it
    // goes there, replacing what is there. Without a hook a dialog asks.
    bool dropTrackEffect(const QString& track, const QString& effectId);
    // The EQ a drag carries (p.740): the Audio EQ tool's effect icon (eqTemplateMime), or one EQ
    // template dragged from a bin (binItemsMime, resolved through the Composer); nothing otherwise.
    std::optional<model::AudioEq> droppedEq(const QMimeData* mime) const;
    // "Drag the effect icon in the Audio EQ tool to another audio clip in the Timeline" (p.740): `eq`
    // on the whole segment at record frame `frame` of audio track `track`, one undo step; already so:
    // true without a step. False for filler, a locked track or anything but an audio track.
    bool dropEq(const QString& track, std::int64_t frame, const model::AudioEq& eq);
    // ---- Effect templates (FX Guide p.52-55; docs/spec/effect-templates.md)
    // The video effect template a drag carries: the Effect Editor's effect icon or the Motion Effect
    // Editor's Timewarp icon (effectTemplateMime), or one template item from a bin or the Effect Palette
    // (binItemsMime, resolved through the Composer); nothing otherwise.
    std::optional<model::EffectTemplate> droppedTemplate(const QMimeData* mime) const;
    // "apply an effect template with all its parameters directly to segments or transitions as you would
    // any other effect" (p.53): model::applyTemplate at record frame `frame` of `track` (with Alt held,
    // nested inside the segment's effect); a transition template on the cut `cut`, else the segment's
    // nearer cut, asking about insufficient source as palette effects do. One undo step.
    bool dropTemplate(const QString& track, std::int64_t frame, const model::EffectTemplate& t,
                      std::optional<std::int64_t> cut = std::nullopt);
    // A template double-clicked in the Effect Palette: on the selected segments (one with its source media
    // at the position on the selected video tracks' top one), or a transition template on the trim
    // rollers' cuts or the selected segments' head cuts.
    bool applyTemplateToSelection(const model::EffectTemplate& t);
    using SelectInsertAsk = std::function<std::optional<int>(const QString& track)>;
    void setSelectInsertAsk(SelectInsertAsk ask) { selectInsertAsk_ = std::move(ask); }
    // The Fade Effect button (FX Guide p.46-47; docs/spec/effect-keyframes.md): the segment effects
    // selected in Segment mode, or else the one at the position on the selected tracks, get their
    // Foreground Level keyframed to fade up over `up` frames and down over `down` (model::fadeEffect),
    // one undo step. How many effects changed; 0 with a message when there is none.
    int fadeEffects(std::int64_t up, std::int64_t down);
    // The Fade Effect dialog box: the frames to fade up and down; nothing when cancelled.
    using FadeAsk = std::function<std::optional<std::pair<std::int64_t, std::int64_t>>()>;
    void setFadeAsk(FadeAsk ask) { fadeAsk_ = std::move(ask); }
    // Audio Loop Play (p.712): the frames to loop in the record sequence. IN to OUT when both are
    // marked; with neither, the shortest segment under the position on the selected audio tracks
    // (any audio track when none of those has one); with only an IN, from it to the end of that
    // segment, and with only an OUT, from the segment's start to it. Inclusive.
    std::optional<std::pair<std::int64_t, std::int64_t>> audioLoopRange() const;
    // The Audio Loop Play button: loops audioLoopRange() in the Record monitor, or stops a loop.
    // False, with a message, when there is nothing to loop.
    bool toggleAudioLoopPlay();
    // The Audio Loop Play button of the Audio Mixer, Audio EQ and AudioSuite tools ("appears in
    // several of the audio effect tools", p.712): checkable, checked while a loop plays.
    QToolButton* makeAudioLoopPlayButton(QWidget* parent);
    // The segment under the Record position on an audio track, if any.
    std::optional<model::Segment> audioSegmentAt(const QString& track) const;

    // ---- Volume and pan automation (p.714-723; lumenis/model/automation.h; timeline_automation.cpp)
    // What Add Keyframe and the keyframe edits act on: the graph Audio Data shows (Display), else
    // Volume.
    model::AutomationParam automationParam() const;
    // Audio Data (Fast menu, p.677-680): the clip gain's line and/or the Volume or Pan graph (not
    // both, p.715) on the audio tracks. Changing the graph clears the keyframe selection.
    void setAudioData(bool clipVolume, std::optional<model::AutomationParam> automation);
    // The Audio setting's Default Pan for mono tracks (p.1147): odd tracks left and even right,
    // unless Centered. New pan keyframes take the pan that plays, so they need it.
    void setAlternatingPan(bool on) { alternatingPan_ = on; }
    bool alternatingPan() const { return alternatingPan_; }
    // Add Keyframe (the ' key or the Tool palette's button, p.716): a keyframe at the Record
    // position on each selected audio track with material there, as one undo step. Shows the
    // Volume graph when no graph is shown.
    bool addKeyframe();
    // Ctrl+Shift+click on a track (p.721): a keyframe there at `frame`, which with Alt too may
    // fall between frames (p.721).
    bool addKeyframeAt(const QString& track, double frame);
    // The selected keyframes, of automationParam() (pink, p.721).
    const QList<model::KeyRef>& keySelection() const { return keySelection_; }
    void setKeySelection(const QList<model::KeyRef>& keys);
    // Click and Shift-click on a keyframe (p.721-722).
    void clickKeyframe(const model::KeyRef& key, bool toggle);
    // The keyframes a drag of `grabbed` moves (p.716, p.723): the selection if it holds it and
    // more; else
    // those in the marked region (IN to OUT) on the selected tracks if it is there; else it and
    // those at the same place on the other selected audio tracks.
    QList<model::KeyRef> keyframesMovedWith(const model::KeyRef& grabbed) const;
    // Moves those keyframes `frames` later (Alt-drag) and `by` up (dB or pan), as one undo step;
    // with `snap` (Ctrl, p.716) `by` is changed so that `grabbed` lands on the nearest line of the
    // graph's scale. The moved keyframes become the selection.
    bool dragKeyframes(const model::KeyRef& grabbed, double frames, double by, bool snap);
    // The Delete key over a keyframe (p.717): it, and those at the same place on the other
    // selected audio tracks.
    bool deleteKeyframeAt(const model::KeyRef& key);
    // Ctrl+Shift+arrows (p.723): the selected keyframes by whole frames and dB (pan: by 1).
    bool nudgeKeyframes(double frames, double by);
    // The Audio Mixer's Fast menu in Auto mode (p.719-720), on the selected audio tracks between
    // the marks: IN to OUT, IN to the end, the start to OUT, or the whole tracks without marks.
    // `param` empty: volume and pan. `by` for Adjust; `peaksOnly` for Filter with Alt.
    enum class AutomationEdit { Filter, Adjust, Remove, RemoveHidden };
    bool editAutomation(AutomationEdit edit, std::optional<model::AutomationParam> param, double by = 0,
                        bool peaksOnly = false);
    // A track's automation value at the Record position, as the Auto mode faders show it; nothing
    // without material there.
    std::optional<double> automationAt(const QString& track, model::AutomationParam param) const;
    // An Auto mode fader or pan set to `value` (p.716): the keyframe at the position; else every
    // keyframe in the marked region when the position is in it; else the keyframes either side of
    // the position ("between keyframes"), or the only one; else a first keyframe at the position.
    // They move by the same amount, so the value at the position becomes `value`. One undo step.
    bool setAutomationValue(const QString& track, model::AutomationParam param, double value);
    // Recording automation (p.717-719): between begin and end, writeAutomation() puts a keyframe at
    // the Record position (frame-accurate, p.721) without an undo step each; ending keeps it all
    // as one undo step, or with `cancel` puts the sequence back.
    bool beginAutomationRecording();
    bool isRecordingAutomation() const { return recordBefore_.has_value(); }
    void writeAutomation(const QString& track, model::AutomationParam param, double value);
    void endAutomationRecording(bool cancel);
    // Keyframes copied (p.721-722) by Ctrl+C with keyframes selected, or with a graph shown the
    // keyframes of the marked region (else the selected segments) on the selected audio tracks.
    const model::KeyClipboard& keyClipboard() const { return keyClipboard_; }
    // Pastes them at the IN (else the position) onto the selected audio tracks, as one undo step.
    bool pasteKeyframes(bool volume, bool pan);
    // Edit > Paste Audio Keyframes (p.722) asks which with this: (gain, pan), or nothing to
    // cancel. Without one, the Timeline's own dialog asks.
    using PasteKeysAsk = std::function<std::optional<std::pair<bool, bool>>()>;
    void setPasteKeysAsk(PasteKeysAsk ask) { pasteKeysAsk_ = std::move(ask); }
    // Go to Next/Previous Event in the Audio Mixer's Auto mode (p.720): the record frame of the
    // next (direction 1) or previous (-1) keyframe on the selected audio tracks, if any.
    std::optional<std::int64_t> nextKeyframe(int direction) const;
    // The Insufficient Source dialog (p.48): `which` is "Media A" or
    // "Media B"; true sizes the effect to fit, false cancels.
    using InsufficientSource = std::function<bool(const QString& which)>;
    // Without one, the Timeline asks with its own message box (tst_effects sets one).
    void setInsufficientSource(InsufficientSource ask) { insufficient_ = std::move(ask); }
    // Transition effects default to one second (Editing Guide p.1296).
    std::int64_t defaultTransitionLength() const;

    // Zoom (p.607-608). The scale is in pixels per frame.
    double scale() const;
    double fitScale() const;
    // A scale at or below fitScale() shows the whole sequence. `anchor` (default the position) is
    // the frame that stays where it is on screen.
    void setScale(double pixelsPerFrame, std::optional<std::int64_t> anchor = std::nullopt);
    // The frame at the left edge of the tracks area; always 0 when the whole sequence fits.
    double firstFrame() const { return first_; }
    // Scrolls horizontally; ignored when the whole sequence fits.
    void setFirstFrame(double frame);
    void showEntireSequence();
    // The Focus button: 90 pixels per second around the position, and back
    // (p.608).
    void toggleFocus();
    bool isFocused() const { return focusBack_.has_value(); }
    // Scrolls across (as setFirstFrame) and down (as TimelineView::setTopOffset) at once, and moves
    // the scroll bars with it.
    void scrollTo(double firstFrame, int topOffset);
    // Timeline > Show Sequence Map (p.606; sequence_map.h). It opens as an overlay in the tracks area
    // or in its own window, as the Timeline settings say (p.1219); the command shows it, or hides it if
    // it is showing ("the user must manually hide the Sequence Map using menu pick or mapped key").
    void showSequenceMap(bool on);
    bool sequenceMapShown() const { return mapShown_; }
    // Moves it into its own window or back into the Timeline (its right-click menu, p.606), and shows it.
    void setSequenceMapInWindow(bool inWindow);
    bool sequenceMapInWindow() const { return mapInWindow_; }
    // The map, made the first time it is shown; nullptr before.
    SequenceMap* sequenceMap() const { return map_; }
    // The map's window, while it is in one.
    SequenceMapWindow* sequenceMapWindow() const { return mapWindow_; }
    // Shows the frames from..to across the tracks area, keeping the old view for zoomBack().
    void zoomTo(std::int64_t from, std::int64_t to);
    void zoomBack();
    static constexpr int kFocusPixelsPerSecond = 90;

    // Fast menu display options (p.592-593).
    // What the Timeline draws; changed from the Fast menu, kept per Timeline (not a setting).
    struct Display;
    // The clip text's font (Set Font, p.81-82; Display::clipFontFamily and clipFontSize).
    QFont clipFont() const;
    struct Display {
        bool trackPanel = true;
        bool clipNames = true;
        bool clipDurations = false;
        bool clipTracks = false;
        bool timecodeTrack = true; // Show Track > TC1 (p.598)
        bool effectIcons = true;   // Effect Icons, Dissolve Icons (FX guide p.40)
        bool dissolveIcons = true;
        bool clipFrames = false;   // Clip Frames: each segment's start frame (p.592)
        // Audio Data (p.677-680): the clip gain's line, and the Volume or Pan automation graph.
        // On every audio track; a track's own settings add to them (showsWaveform() ...). Ours:
        // audio rows are taller while one is shown.
        bool clipVolume = false;
        std::optional<model::AutomationParam> automation;
        // Audio Data > Waveform (p.677-678) and the Fast menu's Waveform Options (p.679): Source
        // Levels (false) or Output Levels, and Full Height. waveformZoom is the plot's size without the
        // track's, in steps of 2x (Ctrl+Alt+L and Ctrl+Alt+K, p.678), from -3 to 4.
        bool waveform = false;
        bool outputLevels = false;
        bool fullHeight = false;
        int waveformZoom = 0;
        // Audio Data > Allow Per Track Settings (p.677-679) and the Track Control panel (p.601-602).
        bool perTrack = false;
        bool trackControlPanel = false;
        // Show Markers (p.599): the marker colours not drawn.
        QSet<QString> hiddenMarkers;
        // Segment Selection and Mark IO Selection (p.593): "#rrggbb", empty for the default colours.
        QString segmentSelectionColor;
        QString markIOColor;
        ViewType viewType = ViewType::Standard;
        SyncBreaks syncBreaks = SyncBreaks::All;
        RenderRanges renderRanges = RenderRanges::PartialOnly;
        // Dupe Detection on video, audio and data tracks (p.641).
        bool dupeVideo = false;
        bool dupeAudio = false;
        bool dupeData = false;
        // The rest of Clip Text (p.592): the source clip's full duration, the transition's duration,
        // the tape or source file name, the media file names and the clip's resolution.
        bool clipFullDurations = false;
        bool clipTransDuration = false;
        bool sourceNames = false;
        bool mediaFullNames = false;
        bool clipResolution = false;
        bool clipNotes = false; // Timeline Clip Notes (p.483)
        // Show Adapters (p.599): "Temporal (motion) adapters appear with a T". Spatial (FrameFlex) and
        // Color adapters come with FrameFlex and color management.
        bool motionAdapters = false;
        // Windows > Set Font (p.81-82): the clip text's font, "saves as a Timeline View setting".
        // Empty family and 0 size: the Timeline's own.
        QString clipFontFamily;
        int clipFontSize = 0;
        bool operator==(const Display&) const = default;
    };
    // The Fast menu's settings as a Timeline view keeps them (p.604), and back. Values missing from
    // `o` keep the defaults.
    static QJsonObject displayToJson(const Display& d);
    static Display displayFromJson(const QJsonObject& o);
    // Sets every display option at once, as a Timeline view or Default Setup does.
    void setDisplay(const Display& d);
    const Display& display() const { return display_; }

    TimelineView* view() const { return view_; }
    // A toolbar button by object name ("focus", "smart.lift_overwrite", ...), or nullptr.
    QToolButton* toolButton(const QString& name) const;
    QSlider* scaleBar() const { return scaleBar_; }
    QScrollBar* scrollBar() const { return scroll_; }
    // The Fast menu, rebuilt each time it opens. The Timeline owns it.
    QMenu* fastMenu();
    // The right-click menu, over a track's button or elsewhere (p.613,
    // p.633-636); over an audio segment, `segment`, it starts with Change
    // Source Track (p.595; 2026.8 guide p.507). The caller owns it.
    QMenu* contextMenu(const QString& track = {}, const std::optional<model::SegmentRef>& segment = {});
    // The master timecode display in the top toolbar.
    QLabel* timecodeDisplay() const { return timecode_; }
    // Edit > Undo/Redo List (p.466): the most recent first.
    // The menu deletes itself when closed.
    QMenu* undoRedoMenu();

signals:
    // ExpertRender's highlighting was cancelled (a click in TC1, p.145).
    void expertSelectionCancelled();
    // The shown item, the selection, the patch or Trim mode changed.
    void changed();
    // Effect mode was entered or left.
    void effectModeChanged(bool on);
    // An effect was put on segments or transitions (Applying Effects opens Effect Editor, p.1220).
    void effectApplied();
    // The Track Control panel's Solo (`solo`) or Mute button for `track` was clicked (p.602).
    void soloMuteRequested(const QString& track, bool solo);
    // The View Menu: a view was chosen, or the current display is to be saved as view `name` (Save
    // As, or Replace when `replace`) (p.604).
    void viewRequested(const QString& name);
    void viewSaveRequested(const QString& name, bool replace);
    // Why a command did nothing, as lastMessage() gives it.
    void message(const QString& text);
    // The Clip Color dialog's choices changed (setClipColorOptions()).
    void clipColorOptionsChanged(const model::ClipColorOptions& options);
    // Video Quality Menu > Video Display Settings (p.405).
    void videoDisplaySettingsRequested();
    // A command that is not the Timeline's (New > Sequence).
    void commandRequested(const QString& commandId);
    // The track button's Audio Track Effect Tool item (p.793, p.795): the tool, on that track.
    void trackEffectToolRequested(const QString& track);
    // A Track Control panel insert button was clicked (p.792, p.795): the Audio Track Effect tool
    // on that track and insert (0 for a).
    void trackInsertRequested(const QString& track, int slot);
    // The record sequence took an undo step, or one was undone or redone.
    void sequenceEdited();
    // Before Split Track to Mono changes a sequence: a copy of it as it was belongs in its bin
    // ("[sequence_name].Copy.[number]", p.735); MainWindow makes it.
    void sequenceCopyRequested(const QString& binId, const model::BinItem& sequence);
    // Create Sequence Based on Selection (p.613-614): the new sequence, to go into a bin MainWindow
    // chooses ("A Select a bin dialog box opens (if you have more than one bin open)"); `binId` is the
    // record sequence's.
    void sequenceFromSelectionRequested(const QString& binId, const model::BinItem& sequence);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    friend class TimelineView; // its resizeEvent() calls syncScrollBars()

    void sync();
    void syncScrollBars();
    // contextMenu()'s Change Source Track submenu for the audio segment `ref`.
    void addChangeSourceTrack(QMenu* menu, const model::SegmentRef& ref);
    // `fitToFill`: Fit to Fill, an overwrite with a Variable Speed effect (FX guide p.170).
    bool edit(model::EditKind kind, bool fitToFill = false); // true: the edit was made
    void remove(bool extract);
    void replaceEdit();
    void addEdit(bool fillerOnly);
    void topOrTail(bool top);
    void matchFrame();
    void reverseMatchFrame();
    void copyToClipboard();
    void cutOrCopySelection(bool cut);
    void pasteClipboard();
    void moveClips(int tracks);
    void removeMatchFrameEdits();
    // The record sequence, for changing it; nullptr (and a message) if the
    // Record monitor has none.
    const model::LoadedClip* recordSequence();
    // Fills the Clipboard; with Alt also loads it into the Source monitor
    // (p.480).
    void setClipboard(const model::Sequence& material, const QString& from);
    void loadClipboard(const QString& name);
    void storeLocks(const model::Sequence& sequence);
    void syncTrimDisplay();
    void slip(std::int64_t frames);
    void extend();
    // Changes the item a monitor shows, through the bin; for the Record
    // monitor's sequence with an undo step.
    bool changeItem(MonitorId monitor, const QString& description, const std::function<bool(model::BinItem&)>& change);
    void composerPerformed(const QString& id);
    void addTrack(model::TrackKind kind, model::AudioFormat format = model::AudioFormat::Mono);
    // Stores a changed record sequence, with an undo step.
    void commit(const QString& description, const model::BinItem& before, model::BinItem after);
    void restore(const QString& binId, const model::BinItem& item);
    bool fail(const QString& text);
    // Puts transitions on cuts of `seq`, asking about insufficient source.
    bool addTransitions(model::Sequence& seq, const QList<model::TrimRoller>& cuts, const EffectInfo& effect);
    // The same with the transition given (an effect template's duration and alignment).
    bool addTransitions(model::Sequence& seq, const QList<model::TrimRoller>& cuts, const model::Transition& transition);
    bool commitSequence(const QString& description, const model::Sequence& seq);
    // The selected record audio tracks of `seq`, in order.
    QStringList selectedAudioTracks(const model::Sequence& seq) const;
    // The marked range of the Record monitor as the Fast menu commands use it (p.719).
    std::pair<double, double> markedRange(const model::Sequence& seq) const;
    // Ctrl+C's keyframe half: true if it copied keyframes and the segments should not be copied.
    bool copyKeyframes();
    // Edit > Paste Audio Keyframes: asks which (pasteKeysAsk_ or a dialog), then pastes.
    bool pasteAudioKeyframes();
    int sliderFor(double scale) const;
    double scaleFor(int slider) const;

    SelectInsertAsk selectInsertAsk_;
    FadeAsk fadeAsk_;
    Composer& composer_;
    TimelineView* view_ = nullptr;
    QWidget* toolbar_ = nullptr;
    QWidget* topbar_ = nullptr;
    QLabel* timecode_ = nullptr;
    QSlider* scaleBar_ = nullptr;
    QScrollBar* scroll_ = nullptr;
    QScrollBar* vscroll_ = nullptr;
    QLineEdit* search_ = nullptr;
    QMenu* fastMenu_ = nullptr;
    model::Sequence shown_;
    TimecodeFormat format_;
    QString shownId_;
    model::TrackPatch patch_;
    // The levels stepped into, outermost first: the item one level out (the sequence, or the nest it is
    // in), its bin, and where the nested segment is in it.
    struct NestLevel {
        QString binId;
        model::BinItem parent;
        QString track;
        std::int64_t start = 0;  // the segment's first frame in the parent
        std::int64_t nestIn = 0; // the nest frame of that first frame
        QString itemId;          // the nest's item in the Record monitor
    };
    QList<NestLevel> nests_;
    // The expanded nest (expandNest()): the sequence as stored, its bin, the expanded item's id in the Record
    // monitor and how it was expanded.
    struct Expanded {
        QString binId;
        model::BinItem parent;
        QString itemId;
        model::NestExpansion expansion;
    };
    std::optional<Expanded> expanded_;
    // The last expansion of each expanded item, by its id, for an undo or redo that comes after it was closed.
    QHash<QString, model::NestExpansion> pastExpansions_;
    // An undo or redo of a step made in a nest stepped into ("<parent>/nest:<track>:<start>") or an expanded
    // nest ("<parent>/expand:<track>:<start>") the Timeline has since left: the item goes back into its
    // parent, and that through restore() on to its bin. Nothing for another id.
    bool storeDetached(const QString& binId, const model::BinItem& item);
    // An item as it is now: a bin's, or one inside a nest or expanded nest of one (ids as above).
    std::optional<model::BinItem> currentItem(const QString& binId, const QString& id) const;
    // The expanded item changed: the sequence takes it and goes to its bin; the expanded item is made again.
    void storeExpanded(const model::BinItem& item);
    // A nest's item changed: its segment in the level out takes it, and so on out to the bin.
    void storeNest(const model::BinItem& item);
    model::EditHistory history_;
    QSet<QString> unmonitored_;
    std::function<bool(const QString&, bool)> soloMuteState_;
    QStringList views_;
    QString currentView_;
    NoteAsk noteAsk_;
    RenameAsk renameAsk_;
    // Changes `tracks` of the record sequence and keeps them without an undo step (per track
    // settings, p.677). The tracks of the Audio Data menu for a click on `track`'s Track Control
    // button: it alone, or with `all` (Alt+click) every audio track.
    void changeTracks(const QStringList& tracks, const std::function<void(model::Track&)>& change);
    QStringList audioTracksFor(const QString& track, bool all) const;
    // The graph keyframe editing works on when the Audio Data menu shows none: the one shown on the
    // track of the keyframe clicked last (per track settings).
    model::AutomationParam editParam_ = model::AutomationParam::Volume;
    bool showSource_ = false;
    double scale_ = 0;  // 0: the whole sequence fits
    double first_ = 0;  // first frame shown
    std::optional<std::pair<double, double>> focusBack_;
    bool trimFocused_ = false; // Trim mode turned Focus on
    bool trimLooping_ = false; // the Play Loop of Trim mode is playing
    // J-K-L Trim: J, L or slow play started in Trim mode; when play stops the transition moves to
    // the frame shown (p.658-659).
    bool jklTrimming_ = false;
    bool goToTrim_ = false; // Trim mode came from Go to Previous/Next Event ("Go to Transition", p.1223)
    std::int64_t playLoopCut() const;
    Options options_;
    QList<std::pair<double, double>> zoomStack_;
    Display display_;
    QList<Composer::PlaybackRange> indicators_;
    QList<model::RenderTarget> expert_;
    bool modifyingExpert_ = false;
    // The Sequence Map (showSequenceMap): where it is and whether it shows.
    SequenceMap* map_ = nullptr;
    SequenceMapWindow* mapWindow_ = nullptr;
    bool mapShown_ = false;
    bool mapInWindow_ = false;
    // Puts the overlay at the bottom right of the tracks area (ours).
    void placeSequenceMap();
    // Composer > Load Filler (p.462): a two-minute clip of filler in the Source monitor, on the record
    // sequence's tracks.
    void loadFiller();
    // Composer > Sync Point Overwrite (p.496): turns Sync Point editing on or off; while on the Overwrite
    // buttons carry an orange mark.
    void setSyncPointEditing(bool on);
    // The phantom marks of a monitor: the marks an edit would use that are not set (p.501).
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> phantomMarks(MonitorId monitor) const;
    QString lastSourceId_; // Auto-enable Source Tracks: the clip the source tracks were set for
    // Auto-Patching (Timeline settings, p.1220), and a patch with Auto-Monitoring (p.629).
    void autoPatch();
    bool movePatch(const QString& source, const QString& record);
    bool liftTool_ = false;
    bool spliceTool_ = false;
    QList<model::SegmentRef> selection_;
    bool trimMode_ = false;
    bool effectMode_ = false;
    QList<model::TrimRoller> rollers_;
    QList<model::TrimRoller> lastRollers_;
    model::TrimKind trimKind_ = model::TrimKind::Ripple;
    bool overwriteTrimTool_ = false;
    bool rippleTrimTool_ = false;
    QList<model::SegmentRef> slip_;
    bool slide_ = false;
    std::int64_t trimA_ = 0; // frames trimmed on each side since entering
    std::int64_t trimB_ = 0;
    bool markerDialog_ = true;
    std::optional<model::BinItem> clipboard_;
    // Automation (timeline_automation.cpp).
    bool alternatingPan_ = true;
    QList<model::KeyRef> keySelection_;
    model::KeyClipboard keyClipboard_;
    bool keysCopiedLast_ = false;          // the last Ctrl+C copied individual keyframes
    std::optional<model::BinItem> recordBefore_; // the sequence when recording began
    PasteKeysAsk pasteKeysAsk_;
    int clipboardCount_ = 0;
    // Where the last Reverse Match Frame cued the sequence: the next one goes on from there.
    std::optional<std::int64_t> reverseMatchAt_;
    QString message_;
    bool syncing_ = false;
    SequenceStore store_;
    SequenceMaker maker_;
    Composer::KeyResolver keys_;
    InsufficientSource insufficient_;
    // Clip colours.
    model::ClipColorOptions clipColors_ = model::ClipColorOptions::defaults();
    ClipColorAsk clipColorAsk_;
    int projectHeight_ = 1080;
    bool sourceRecordColors_ = false;
    bool showMismatchedRates_ = false;
    std::unique_ptr<media::WaveformCache> waveforms_;
    int projectSampleRate_ = 48000;
    // Whether each clip's media was online when last looked at ("bin|clip"), and when.
    mutable QHash<QString, std::pair<bool, qint64>> online_;
    mutable QElapsedTimer onlineClock_;
};

} // namespace lumenis::ui

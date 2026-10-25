#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/effect_palette.h"

#include <QWidget>

#include <functional>
#include <optional>

class QLabel;
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
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // Effects dragged from the Effect Palette (FX guide p.44).
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void paintEffects(QPainter& p, const Row& r, const model::Track& t) const;
    void moveIndicator(const QPoint& pos, Qt::KeyboardModifiers modifiers);
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
};

// The Timeline window (ch. 19): the Record monitor's sequence, or with
// Toggle Source/Record in Timeline the Source monitor's clip, and the
// editing that puts clips into sequences (ch. 15).
class Timeline : public QWidget {
    Q_OBJECT

public:
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
        // Composer settings: Move tab (p.1160).
        bool stopAtHead = true;
        bool stopAtTail = false;
        bool stopAtMarkers = false;
        bool ignoreTrackSelectors = false;
        // Trim settings (p.1223).
        bool trimAutoFocus = false;
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
    // Whether perform() takes this command ID (from commands.json).
    static bool handles(const QString& commandId);
    // Why the last command did nothing, for the status line.
    QString lastMessage() const { return message_; }

    // Patching from the Track Selector (p.629).
    // Returns false, with a message, if the patch is not allowed (audio only to audio, video
    // only to video, never data; model::TrackPatch::canPatch).
    bool patchTracks(const QString& source, const QString& record);
    // Select or deselect a source or record track button.
    void toggleSourceTrack(const QString& track);
    void toggleRecordTrack(const QString& track);
    // Track Monitor buttons (p.627-628): which tracks are heard and seen.
    bool isMonitored(const QString& track) const { return !unmonitored_.contains(track); }
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
    enum class SelectWhere { Left, Right, Reverse };
    // Selects on the selected record tracks; turns on Lift/Overwrite if no segment tool is on.
    void selectSegments(SelectWhere where);

    // Trim mode (ch. 20). Rollers sit on edit points; single-roller trims
    // are Ripple or Overwrite (the trim tools, p.650).
    bool isTrimMode() const { return trimMode_; }
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
    // Trims by frames: Trim buttons, drags, J-K-L later (p.656).
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
    // Remove Effect (p.47): the effect the Effect Editor would show.
    bool removeEffectAtPosition();
    // The effect at the Record monitor's position on the selected tracks,
    // top down (p.84).
    std::optional<model::EffectRef> currentEffect() const;
    // Changes an effect of the record sequence, with an undo step.
    // `change` gets the effect and, for a transition, the transition itself (else nullptr).
    // Returns false if there is no such effect or its track is locked.
    bool changeEffect(const model::EffectRef& ref, const QString& description,
                      const std::function<void(model::Effect&, model::Transition*)>& change);
    // The Transition parameters (p.63, p.500): the duration and alignment
    // of the transitions at the cuts; a Dissolve where there is none.
    bool setTransitions(const QList<model::TrimRoller>& cuts, std::int64_t duration,
                        model::TransitionAlignment alignment, std::int64_t start = 0);
    // The Audio Mixer in Clip mode (p.702, p.711): the gain and/or pan of
    // the segment under the Record position; with `onTrack` (Set Level on
    // Track and the like) of the segments between the marks: IN to OUT, IN
    // to the end, the start to OUT, or the whole track without marks.
    bool setClipAudio(const QString& track, std::optional<double> gain, std::optional<int> pan, bool onTrack,
                      const QString& description);
    // The segment under the Record position on an audio track, if any.
    std::optional<model::Segment> audioSegmentAt(const QString& track) const;
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
    // Shows the frames from..to across the tracks area, keeping the old view for zoomBack().
    void zoomTo(std::int64_t from, std::int64_t to);
    void zoomBack();
    static constexpr int kFocusPixelsPerSecond = 90;

    // Fast menu display options (p.592-593).
    // What the Timeline draws; changed from the Fast menu, kept per Timeline (not a setting).
    struct Display {
        bool trackPanel = true;
        bool clipNames = true;
        bool clipDurations = false;
        bool clipTracks = false;
        bool timecodeTrack = true; // Show Track > TC1 (p.598)
        bool effectIcons = true;   // Effect Icons, Dissolve Icons (FX guide p.40)
        bool dissolveIcons = true;
        bool clipFrames = false;   // Clip Frames: each segment's start frame (p.592)
    };
    const Display& display() const { return display_; }

    TimelineView* view() const { return view_; }
    // A toolbar button by object name ("focus", "smart.lift_overwrite", ...), or nullptr.
    QToolButton* toolButton(const QString& name) const;
    QSlider* scaleBar() const { return scaleBar_; }
    QScrollBar* scrollBar() const { return scroll_; }
    // The Fast menu, rebuilt each time it opens. The Timeline owns it.
    QMenu* fastMenu();
    // The right-click menu, over a track's button or elsewhere (p.613,
    // p.633-636). The caller owns it.
    QMenu* contextMenu(const QString& track = {});
    // The master timecode display in the top toolbar.
    QLabel* timecodeDisplay() const { return timecode_; }
    // Edit > Undo/Redo List (p.466): the most recent first.
    // The menu deletes itself when closed.
    QMenu* undoRedoMenu();

signals:
    // The shown item, the selection, the patch or Trim mode changed.
    void changed();
    // Why a command did nothing, as lastMessage() gives it.
    void message(const QString& text);
    // Video Quality Menu > Video Display Settings (p.405).
    void videoDisplaySettingsRequested();
    // A command that is not the Timeline's (New > Sequence).
    void commandRequested(const QString& commandId);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    friend class TimelineView; // its resizeEvent() calls syncScrollBars()

    void sync();
    void syncScrollBars();
    // `fitToFill`: Fit to Fill, an overwrite with a Variable Speed effect (FX guide p.170).
    void edit(model::EditKind kind, bool fitToFill = false);
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
    bool commitSequence(const QString& description, const model::Sequence& seq);
    int sliderFor(double scale) const;
    double scaleFor(int slider) const;

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
    model::EditHistory history_;
    QSet<QString> unmonitored_;
    bool showSource_ = false;
    double scale_ = 0;  // 0: the whole sequence fits
    double first_ = 0;  // first frame shown
    std::optional<std::pair<double, double>> focusBack_;
    bool trimFocused_ = false; // Trim mode turned Focus on
    Options options_;
    QList<std::pair<double, double>> zoomStack_;
    Display display_;
    bool liftTool_ = false;
    bool spliceTool_ = false;
    QList<model::SegmentRef> selection_;
    bool trimMode_ = false;
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
    int clipboardCount_ = 0;
    // Where the last Reverse Match Frame cued the sequence: the next one goes on from there.
    std::optional<std::int64_t> reverseMatchAt_;
    QString message_;
    bool syncing_ = false;
    SequenceStore store_;
    SequenceMaker maker_;
    Composer::KeyResolver keys_;
    InsufficientSource insufficient_;
};

} // namespace lumenis::ui

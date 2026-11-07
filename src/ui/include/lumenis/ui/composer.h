#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/media/ofx_host.h"
#include "lumenis/model/closed_captions.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/monitor.h"
#include "lumenis/model/sequence.h"

#include <QColor>
#include <QRectF>
#include <QElapsedTimer>
#include <QImage>
#include <QKeySequence>
#include <QWidget>

#include <functional>
#include <optional>

class QJsonObject;
class QLabel;
class QMenu;
class QTimer;
class QToolButton;

class QPainter;

namespace lumenis::ui {

class Composer;
class CorrectionMonitors;
class MappableButton;

// The monitors of the Composer window (guide p.378-381).
enum class MonitorId { Source, Record };

// The bar under a monitor: the length of the clip, the marks, and the
// position indicator, which is dragged to move (p.396-397).
class PositionBar : public QWidget {
    Q_OBJECT

public:
    // Draws and edits `monitor`'s state in `composer`, which must outlive the bar.
    PositionBar(Composer& composer, MonitorId monitor, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    // Frame at an x position, and back.
    // x is in widget pixels; frames run from 0 at the left edge to duration - 1 at the right. frameAt
    // clamps x to the bar; both return 0 while the monitor holds less than two frames.
    std::int64_t frameAt(int x) const;
    int xFor(std::int64_t frame) const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    std::optional<model::Marker> markerAt(int x) const;
    // The frames the bar spans: the clip, or in the Effect Preview monitor only the effect.
    std::pair<std::int64_t, std::int64_t> span() const;

    Composer& composer_;
    MonitorId monitor_;
};

// A monitor's picture: black until there is media (Phase 2), in the
// project's aspect ratio, with the IN and OUT sawtooth marks (p.405-406).
// Clips are dropped on it from bins (p.394).
class MonitorPicture : public QWidget {
    Q_OBJECT

public:
    // Shows `monitor` of `composer`, which must outlive the picture.
    MonitorPicture(Composer& composer, MonitorId monitor, QWidget* parent = nullptr);
    // A picture of Small Trim display (p.648): always the Record monitor's sequence at that side of the
    // transition (Composer::trimFrame).
    void setTrimSide(model::TrimSide side) { trimSide_ = side; }

    // The area of the widget the picture fills: the largest rectangle of the project's aspect ratio,
    // centred (letterboxed or pillarboxed), in widget coordinates.
    QRect pictureRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    // Ctrl+Alt+drag moves an enlarged Effect Preview picture (FX Guide p.82); dragging a handle of the
    // Effect Preview's overlay manipulates the effect (Composer::effectHandleDragged).
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    // Double-clicking a tracking data point selects it and goes to its frame (FX Guide p.271).
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // What this picture shows now: in Big Trim display both monitors show the sequence's outgoing
    // (Source) and incoming (Record) frames, "The new outgoing frame displays in the Source monitor as
    // you trim" (p.655).
    std::optional<model::TrimSide> shownTrimSide() const;

    Composer& composer_;
    MonitorId monitor_;
    std::optional<model::TrimSide> trimSide_;
    QPointF panFrom_; // the last point of a Ctrl+Alt drag
    // The Effect Preview overlay: a point's place in the widget and back (fractions of pictureRect()), the
    // handle under a widget point, and the drag under way (its handle and where it began, as a fraction).
    QPointF overlayPoint(const QPointF& fraction) const;
    QPointF overlayFraction(const QPointF& point) const;
    QString overlayHandleAt(const QPointF& point) const;
    void paintOverlay(QPainter& p) const;
    // A closed caption over the picture (p.983): CEA-608's grid of 15 rows of 32 columns over the middle 80% of the
    // picture (ours), each row's text on a black box.
    void paintCaption(QPainter& p, const QRect& pic, const model::CaptionScreen& caption) const;
    QString handleDrag_;
    QPointF handleFrom_;
    bool overlayDrag_ = false; // a press the Composer's OverlayInput took
    // A lasso over the Effect Preview's tracking data points: where it began and is now (widget points).
    std::optional<QPointF> lassoFrom_;
    QPointF lassoTo_;
};

// The Composer window (p.379-387): the Source and Record monitors, each with
// its Clip Name menu and tracking display above, the position bar and two
// rows of buttons below, and the center duration display between them.
class Composer : public QWidget {
    Q_OBJECT

public:
    // A bin item, from the Bin Container: its bin and the item itself.
    using ItemResolver = std::function<std::optional<model::BinItem>(const QString& binId, const QString& itemId)>;
    // The command a key runs (the Keyboard settings), or empty.
    using KeyResolver = std::function<QString(const QKeySequence&)>;

    explicit Composer(QWidget* parent = nullptr);
    ~Composer() override;

    // Set by MainWindow; without one, resolve() finds nothing and loadFromBin() loads nothing.
    void setItemResolver(ItemResolver resolver) { resolver_ = std::move(resolver); }
    std::optional<model::BinItem> resolve(const QString& binId, const QString& itemId) const {
        return resolver_ ? resolver_(binId, itemId) : std::nullopt;
    }
    // A segment's clip as the segment plays it: a mixed rate clip at the sequence's rate
    // (model::clipForSegment). Never to be stored back in its bin.
    std::optional<model::BinItem> resolveForSegment(const model::Segment& segment) const;
    // A clip by its MobID, from the open bins: its bin and the item. The Timeline's Change Source
    // Track > More finds a subclip's master clip with it. Set by MainWindow.
    using MobResolver = std::function<std::optional<std::pair<QString, model::BinItem>>(const MobId& mob)>;
    void setMobResolver(MobResolver resolver) { mobs_ = std::move(resolver); }
    std::optional<std::pair<QString, model::BinItem>> resolveMob(const MobId& mob) const {
        return mobs_ ? mobs_(mob) : std::nullopt;
    }
    // Keys pressed in the Composer are run through it (keyPressEvent).
    void setKeyResolver(KeyResolver resolver) { keys_ = std::move(resolver); }
    // The picture of what a monitor holds at `frame`, scaled to fit `size`:
    // the clip's frame, or for a sequence the frame of its top video track
    // there. `offline` is set when the media cannot be read (p.347).
    using PictureResolver = std::function<QImage(MonitorId, std::int64_t frame, QSize size, bool* offline)>;
    void setPictureResolver(PictureResolver resolver) { pictures_ = std::move(resolver); }
    // A picture shown in a monitor in place of its own, loaded or not, while the override gives one (a null image:
    // the monitor's own): the Safe Color Limiter's analysis display in the Source monitor (FX Guide p.729; 6.10e4c2).
    using MonitorOverride = std::function<QImage(MonitorId, QSize size)>;
    void setMonitorOverride(MonitorOverride override) { override_ = std::move(override); }
    // The override's picture for a monitor; null when there is none.
    QImage overridePicture(MonitorId monitor, QSize size) const { return override_ ? override_(monitor, size) : QImage(); }
    // Phantom marks (Composer settings, p.501): the IN and OUT an edit would use where none is set,
    // drawn in blue in the position bars. The Timeline works them out.
    using PhantomResolver =
        std::function<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>(MonitorId)>;
    void setPhantomResolver(PhantomResolver resolver) { phantoms_ = std::move(resolver); }
    // Closed captions (7.4b; Editing Guide p.983): "Right-click the monitor where you want to preview the Closed
    // Captions, and select Show Captions." MainWindow gives the caption on a monitor's frame (the D1 data of a
    // sequence or a caption clip's own); shownCaption() is the one drawn at the position while Show Captions is on.
    using CaptionResolver = std::function<std::optional<model::CaptionScreen>(MonitorId, std::int64_t)>;
    void setCaptionResolver(CaptionResolver resolver) { captions_ = std::move(resolver); }
    bool showsCaptions(MonitorId monitor) const;
    void setShowCaptions(MonitorId monitor, bool on);
    std::optional<model::CaptionScreen> shownCaption(MonitorId monitor) const;
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> phantomMarks(MonitorId monitor) const {
        return phantoms_ ? phantoms_(monitor) : std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{};
    }
    // MultiCamera displays (8.1a; Editing Guide p.1126-1129): a group clip in the Source monitor shows its selected
    // angle full size (Full-Monitor display, split 1) or the angles of bank groupBank() in Quad Split (4) or Nine Split
    // (9) Source view; MainWindow's picture resolver draws them. The Quad Split and Nine Split buttons "switch the Source
    // monitor between Full-Monitor display and" their view; Swap Cam Bank steps to the next bank, back to the first
    // after the last.
    int groupSplit() const { return groupSplit_; }
    void setGroupSplit(int split);
    int groupBank() const { return groupBank_; }
    void swapCamBank();
    // Repaints the monitors and position bars, after a setting they show changed.
    void refreshDisplay() { refresh(); }
    // Puts a clip that is in no bin (loaded from the Source Browser) into
    // one, returning the bin's id; empty if it cannot.
    using Committer = std::function<QString(const model::BinItem&)>;
    void setCommitter(Committer committer) { committer_ = std::move(committer); }
    // Commits the Source monitor's clip if it is in no bin (p.316). False if
    // it could not be.
    bool commitSource();
    // Removes the clips that are in no bin from both monitors (p.315).
    void clearUncommitted();

    // The PictureResolver's image; a null image when there is no resolver or the monitor is empty.
    // `*offline` is reset to false first.
    QImage picture(MonitorId monitor, QSize size, bool* offline = nullptr) const;
    // The same at another frame of the monitor's item.
    QImage pictureAt(MonitorId monitor, std::int64_t frame, QSize size, bool* offline = nullptr) const;
    // The eyedropper (FX Guide p.89, p.397): the colour of a monitor's picture at a fraction of it, and of the picture
    // under a point on the screen (either monitor); nothing outside a picture or when the monitor is empty.
    std::optional<QColor> colorAt(MonitorId monitor, const QPointF& fraction) const;
    std::optional<QColor> colorAtScreen(const QPoint& global) const;
    // Edit points of what a monitor shows (the Timeline knows a sequence's;
    // a clip has its start and end), for Go to Previous/Next Event and
    // Mark Clip.
    using EventResolver = std::function<QList<std::int64_t>(MonitorId)>;
    void setEventResolver(EventResolver resolver) { events_ = std::move(resolver); }

    // Frame numbers in the monitor's own time, in ascending order.
    QList<std::int64_t> events(MonitorId monitor) const;
    // Where Go to Previous/Next Event stop (Composer settings, Move tab,
    // p.1160); the events when nothing else decides.
    void setStopResolver(EventResolver resolver) { stops_ = std::move(resolver); }
    QList<std::int64_t> stops(MonitorId monitor) const;
    // What Mark Clip marks when something other than the segment under the
    // position decides it (segments selected in the Timeline, p.622).
    using MarkResolver = std::function<std::optional<std::pair<std::int64_t, std::int64_t>>(MonitorId)>;
    void setMarkResolver(MarkResolver resolver) { marks_ = std::move(resolver); }
    // The project's rate and scan, and the Render setting's Use frame blending in Motion Adapters
    // (p.1215): mixed rate clips load as their adapted copies at that rate (model::adaptedClip, Editing
    // Guide p.469: "Media Composer applies Motion Adapter effects when they load into a Source or pop-up
    // monitor"). Without a rate clips load as they are.
    void setAdapterOptions(const Rational& projectRate, bool interlaced, bool blending);
    // The project's edit rate as setAdapterOptions() gave it; 0/1 until then.
    Rational projectRate() const { return projectRate_; }
    // The item as the monitors show it: a mixed rate clip's adapted copy, else the item.
    model::BinItem monitorItem(const model::BinItem& item) const;
    // HD projects are 16:9; SD 4:3 or 16:9 (p.379).
    void setAspectRatio(const Rational& aspect);
    Rational aspectRatio() const { return aspect_; }

    // The loaded clips, position and marks of a monitor. Changing the state directly does not refresh
    // the widgets; the Composer's own methods do.
    model::MonitorState& state(MonitorId monitor);
    const model::MonitorState& state(MonitorId monitor) const;

    // Loading (p.393-394). By default clips go to the Source monitor and
    // sequences to the Record monitor.
    void load(MonitorId monitor, const QString& binId, const model::BinItem& item);
    void loadDefault(const QString& binId, const model::BinItem& item);

    // Loads each item through the ItemResolver in turn; the last one stays shown.
    void loadFromBin(MonitorId monitor, const QString& binId, const QStringList& itemIds);
    void clear(); // the project closed
    // A loaded item changed in its bin (an edit, undo): both monitors show
    // the new version.
    void updateItem(const model::BinItem& item);

    // The monitor commands act on (the one last clicked).
    MonitorId activeMonitor() const { return active_; }
    void setActiveMonitor(MonitorId monitor);

    // Composer > Show Single/Dual Monitor (p.381).
    void setSingleMonitor(bool single);
    bool isSingleMonitor() const { return single_; }

    // Tracking displays (p.384-387), one per monitor here (one information
    // row), and the center duration display (p.380).
    void setTracking(MonitorId monitor, model::TrackingFormat format);
    model::TrackingFormat tracking(MonitorId monitor) const;
    QString trackingText(MonitorId monitor) const;
    // The second row's tracking display (Always Display Two Rows of Data, p.385, p.1158): "you can
    // display two different types of tracking information for the footage in each monitor".
    void setSecondTracking(MonitorId monitor, model::TrackingFormat format);
    model::TrackingFormat secondTracking(MonitorId monitor) const;
    QString secondTrackingText(MonitorId monitor) const;
    // Whether the second row of data shows now: Two Rows, or Flow Data Dynamically with the Composer
    // at least kFlowHeight pixels high (ours: the guide says only that the data shows "in either one
    // or two rows, depending on the size of the Composer window", p.380).
    bool showsSecondRow() const;
    static constexpr int kFlowHeight = 420;
    // The middle Composer button panel (p.1159): 3 or 6 buttons between the monitors' button rows.
    QWidget* middlePanel() const { return middle_; }
    QString centerDuration() const;

    // Effect mode's Effect Preview monitor (FX Guide p.49-50, p.75-78), set by the Effect Editor while
    // the Timeline is in Effect mode and an effect is shown: the Record monitor's position bar spans
    // only the effect and shows its keyframes (the effect track's, p.102-103), two boxes above it show
    // the effect's length and the position in it (p.77-78), and the Effect Preview toolbar takes the
    // place of the Record monitor's buttons (p.76-77).
    struct EffectPreview {
        bool on = false;
        std::int64_t start = 0;  // the Record frame of the effect's first frame
        std::int64_t length = 0; // in frames
        struct Keyframe {
            double frame = 0.0; // frames into the effect
            bool selected = false;
            bool operator==(const Keyframe&) const = default;
        };
        QList<Keyframe> keyframes;
        // Direct manipulation (FX Guide p.92-93, p.292-293; media/effect_handles.h): the wire-frame outline
        // (four corners), the handles and the Outline/Path button's motion path, drawn over the picture.
        // Points are fractions of the picture (0 to 1 across and down; outside it beyond them, as Reduce shows).
        struct Handle {
            QString id;
            QPointF at;
            std::optional<QPointF> from; // a line from here to the handle (the rotation handles)
            bool operator==(const Handle&) const = default;
        };
        QList<QPointF> outline;
        QList<Handle> handles;
        QList<QPointF> path;     // the centre's line over the effect
        QList<QPointF> pathDots; // the centre at each keyframe
        QList<QList<QPointF>> guides; // dashed polygons (Avid Pan & Zoom's Safe Action area)
        // Motion tracking (FX Guide p.260-261, p.267-268): each shown tracker's tracking boxes (the target area
        // inside the search area, with a data point locator in the middle, when `boxes`), its data points as dots
        // and the position's as a cross, in its colour. Dragging inside the boxes moves them (handle
        // "track:<n>:move"), their corners resize them ("track:<n>:target", "track:<n>:search"), n being the
        // tracker's index in this list.
        struct TrackerShow {
            QColor color;
            bool boxes = false;
            QRectF target;
            QRectF search;
            QList<QPointF> points;
            QList<std::int64_t> frames; // each point's frame (of the effect)
            QList<bool> selected;       // each point's selection, "highlighted with a circle" (p.271)
            std::optional<QPointF> current;
            bool operator==(const TrackerShow&) const = default;
        };
        QList<TrackerShow> trackers;
        // Titler+ (FX Guide p.303-313): the selected objects' bounding boxes (solid), highlighted text (filled), the
        // text cursor and the anchor points' crosshairs.
        QList<QList<QPointF>> boxes;
        // Open wire frames: Intraframe Brush strokes and objects being drawn (6.9b, intraframe_tool.h).
        QList<QList<QPointF>> lines;
        QList<QList<QPointF>> highlights;
        std::optional<std::pair<QPointF, QPointF>> caret;
        QList<QPointF> anchors;
        // An OFX plug-in's overlay (FX Guide p.162; media/ofx_host.h, 6.11c2), shown with Outline/Path.
        QList<media::OfxOverlayShape> pluginShapes;
        bool operator==(const EffectPreview&) const = default;
    };
    void setEffectPreview(const EffectPreview& preview);
    // Something that takes the Effect Preview's mouse and keys before the Composer does (the Titler+ tools, FX Guide
    // p.303-319). Points are fractions of the picture, as EffectPreview's. `press` returns true to take the press and
    // the moves and release that follow it; `key` true when it used the key, `wantsKey` true for keys it takes before
    // shortcuts. The input must outlive its use or be cleared with setOverlayInput(nullptr).
    class OverlayInput {
    public:
        virtual ~OverlayInput() = default;
        virtual bool overlayPress(const QPointF& at, Qt::KeyboardModifiers modifiers, bool doubleClick) = 0;
        virtual void overlayMove(const QPointF& at, Qt::KeyboardModifiers modifiers) = 0;
        virtual void overlayRelease(const QPointF& at, Qt::KeyboardModifiers modifiers) = 0;
        virtual bool overlayKey(QKeyEvent* event) = 0;
        virtual bool overlayWantsKey(const QKeyEvent* event) const = 0;
    };
    void setOverlayInput(OverlayInput* input) { overlayInput_ = input; }
    OverlayInput* overlayInput() const { return effect_.on ? overlayInput_ : nullptr; }
    const EffectPreview& effectPreview() const { return effect_; }

    // Color Correction mode (FX Guide p.589-594): the three monitors of correction_monitors.h in place of the Source
    // and Record monitors. The Timeline turns it on and off (Timeline::setCorrectionMode).
    void setCorrectionMode(bool on);
    bool isCorrectionMode() const { return correctionMode_; }
    CorrectionMonitors* correctionMonitors() const { return correctionMonitors_; }
    // While set, the picture resolver leaves segments' Color Correction mode corrections out (media::Composition::
    // corrections): Dual Split's uncorrected part (p.592).
    void setUncorrectedPictures(bool on) { uncorrected_ = on; }
    bool uncorrectedPictures() const { return uncorrected_; }
    // The Effect Preview toolbar, and the effect's length and position boxes (for tests).
    QWidget* effectBar() const { return effectBar_; }
    QToolButton* effectLengthBox() const { return effectLength_; }
    QToolButton* effectPositionBox() const { return effectPosition_; }
    // Reduce and Enlarge (p.76-77): the Effect Preview picture's size in steps of two, 0 being the
    // monitor's size (ours: from a quarter to eight times), and where an enlarged picture was moved to
    // (p.81-82). Leaving the Effect Preview resets both.
    int previewZoom() const { return zoom_; }
    void setPreviewZoom(int steps);
    QPointF previewPan() const { return pan_; }
    void panPreview(const QPointF& by);

    // Trim mode's Big Trim display (p.648-651), set by the Timeline.
    struct TrimDisplay {
        bool on = false;
        std::int64_t aFrames = 0; // frames trimmed on each side
        std::int64_t bFrames = 0;
        bool aActive = false;
        bool bActive = false;
        // Small Trim display (Trim settings, p.648-649, p.1223): the Source monitor stays and the
        // Record monitor gives way to the outgoing and incoming frames side by side.
        bool small = false;
        // The last outgoing and the first incoming frame of the sequence at the transition.
        std::int64_t aFrame = 0;
        std::int64_t bFrame = 0;
        // Dual Image Play (p.659): while play runs, both sides follow it.
        bool dualImage = false;
    };

    // Turns the Big Trim display on or off and updates its counters. While on, the position bars and
    // button rows are hidden and both monitors are shown even in single-monitor mode.
    void setTrimDisplay(const TrimDisplay& display);
    const TrimDisplay& trimDisplay() const { return trim_; }
    // The Record monitor's frame a trim picture of `side` (A or B) shows: the side's frame, or while the
    // Record monitor plays the position (B, or the side last clicked; A shows the frame before it);
    // with Dual Image Play both sides follow play (p.659).
    std::int64_t trimFrame(model::TrimSide side) const;
    // Display During Segment Drag (Timeline settings, p.617-618, p.1219), set by the Timeline while a
    // segment is dragged. Four-frame: the Source monitor shows `frames` 0 and 1 side by side (the frame
    // before the drop point and the segment's first frame), the Record monitor 2 and 3 (the segment's
    // last frame and the frame after where it would end), with the offset counter centred beneath; with
    // only the Record monitor, its outer two. Otherwise (Position Bar) the Record monitor shows frame 0.
    // All frames are the Record monitor's sequence's.
    struct DragDisplay {
        bool on = false;
        bool fourFrame = false;
        std::int64_t frames[4] = {0, 0, 0, 0};
        std::int64_t offset = 0;
    };
    void setDragDisplay(const DragDisplay& display);
    // Use custom viewer background (Interface settings, p.1201): the area around the pictures;
    // invalid for the default.
    void setViewerBackground(const QColor& color);
    QColor viewerBackground() const;
    const DragDisplay& dragDisplay() const { return drag_; }
    QWidget* dragCounter() const;
    // A trim picture was clicked: that side plays in real time (p.659).
    void setTrimPlaySide(model::TrimSide side);
    model::TrimSide trimPlaySide() const { return trimPlaySide_; }
    // The Small Trim display's widget in the Record column (two pictures and their counters).
    QWidget* smallTrim() const { return smallTrim_; }

    // The frame counter under a monitor in Trim mode (Source: A side, Record: B side), and the row of
    // Trim buttons; exposed for tests and the Timeline.
    QToolButton* trimCounter(MonitorId monitor) const;
    QWidget* trimBar() const { return trimBar_; }

    // What the Composer shows (Composer settings, Window and Edit tabs,
    // p.1158-1160).
    struct Display {
        bool infoRow = true;       // Data Display at Top: Off, or one row
        bool twoRows = false;      // Always Display Two Rows of Data (p.1158)
        bool flowData = false;     // Flow Data Dynamically (p.380, p.1158)
        bool tickMarks = false;    // Tick Marks in Position Bars (p.1159)
        bool middlePanel = false;  // Display middle Composer button panel (p.1159)
        int buttonRows = 2;        // Button Display at Bottom: 0, 1 or 2
        bool centerDuration = true;
        bool markers = true;       // Show Composer Markers
        bool spannedMarkers = true;
    };
    void setDisplay(const Display& display);
    const Display& display() const { return display_; }
    // The markers of a monitor's clip that are shown.
    QList<model::Marker> shownMarkers(MonitorId monitor) const;

    // The commands of the buttons under the monitors, slot by slot:
    // {"source": [[row 1], [row 2]], "record": [...]}; "" is Blank. The
    // Command Palette maps others onto them (p.97); they are saved with the
    // Composer settings.
    QJsonObject buttonLayout() const;
    void setButtonLayout(const QJsonObject& layout); // {} for the defaults

    // The middle panel's slots are under "middle" (ours: Lift, Extract and Trim Mode, then Replace,
    // Fit to Fill and Copy to Clipboard; the guide does not name them).
    // The built-in commands of every slot, in the same format.
    static QJsonObject defaultButtonLayout();

    // Show Source/Record colors in Composer (Interface settings, p.383).
    void setSourceRecordColors(bool on);
    bool sourceRecordColors() const { return colors_; }

    // Gang (p.495): the Record monitor is always ganged; a ganged Source
    // monitor moves with it.
    void setGanged(bool ganged);
    bool isGanged() const { return ganged_; }

    // Playback: the position moves in real time. Speeds are multiples of
    // normal, negative for reverse (J-K-L, p.400-401). At normal speed
    // forward the play clock, when there is one, keeps the picture with the
    // sound: it gives the seconds played since play started.
    using PlayClock = std::function<std::optional<double>()>;
    void setPlayClock(PlayClock clock) { playClock_ = std::move(clock); }
    // Video quality for playback (p.403-404): Full Quality, Draft Quality
    // (half the width and lines) and Best Performance (a quarter of each).
    enum class VideoQuality { Full, Draft, Best };
    void setVideoQuality(VideoQuality quality);
    VideoQuality videoQuality() const { return quality_; }
    // The share of the picture's width and height decoded at this quality.
    double qualityScale() const;
    // Video Display settings: High-Quality Scaling for Real-Time Decode
    // (p.1224), for Full Quality only.
    void setHighQualityScaling(bool on);
    bool smoothScaling() const { return smoothScaling_ && quality_ == VideoQuality::Full; }
    // Since play last started from a stop: frames shown, and frames the
    // picture skipped because it fell behind.
    struct PlayStats {
        std::int64_t shown = 0;
        std::int64_t dropped = 0;
    };
    const PlayStats& playStats() const { return stats_; }
    // Where the last playback had trouble (FX Guide p.136): frames it could not show (`dropped`, the red
    // bars) and frames shown late (the yellow ones, ours: a step that came more than half a frame period
    // late without skipping), in the played monitor's frames, joined into ranges. Reported with
    // playbackDifficulties() when play stops.
    struct PlaybackRange {
        std::int64_t start = 0;
        std::int64_t length = 0;
        bool dropped = true;
        bool operator==(const PlaybackRange&) const = default;
    };
    const QList<PlaybackRange>& playbackRanges() const { return playRanges_; }

    // The current speed: 0 when stopped, otherwise a multiple of normal (negative: reverse). In slow
    // play it is 1 or -1 and isSlow() is true.
    bool isPlaying() const { return speed_ != 0; }
    int speed() const { return speed_; }
    // Slow play (p.401): "Press the K and L keys together for slow forward (8 fps for NTSC, 6 fps for
    // PAL, and 6 fps for 24p projects)", K and J for slow backward. Holding K and tapping L or J still
    // steps a frame; holding both starts slow play, and letting go of L or J stops it. There is no sound.
    void playSlow(int direction);
    bool isSlow() const { return slow_; }
    // Dynamic Play Forward and Reverse (p.401-402; the Play tab): play starts at normal speed, and each
    // press in the same direction raises the speed by `acceleration` steps, twelve of which double it
    // (Timeline settings, Dynamic Play Acceleration, p.1219). With `slowStart` (Alt) play starts
    // `slowStartPresses` presses below normal speed ("the number of keystrokes it will take to get to
    // 1x speed"). The other direction starts again at normal speed. Ours: the speed stops rising at 8
    // times normal; sound plays only at whole multiples of normal speed.
    void playDynamic(int direction, bool slowStart);
    void setDynamicPlay(int acceleration, int slowStartPresses);
    // The current speed as a multiple of normal (negative: reverse); 0 when stopped.
    double rate() const;
    // The slow-play rate for a project's nominal frame rate: 8 for 30 (NTSC), 6 for 25 and 24, scaled
    // for their multiples (ours: 16 for 60, 12 for 50, 12 for 48), else a quarter.
    static int slowFps(int nominalFps);
    // What the Play button of the playing monitor shows (ours: the owner's design, docs/spec/ui-inventory.md
    // section 10). Normal: forward at normal speed, the play arrow in green. Forward: faster or slower
    // than normal, the rate in frames per second with ">" after it in a green box. Reverse: always the
    // rate, with "<" before it, in an amber box. `fps` counts the project's nominal frames (60 for 2x in
    // a 30-frame project, as on p.400-401).
    struct PlayDisplay {
        enum class Kind { Stopped, Normal, Forward, Reverse };
        Kind kind = Kind::Stopped;
        int fps = 0;
        bool operator==(const PlayDisplay&) const = default;
    };
    PlayDisplay playDisplay() const;

    // play() plays the active monitor at `speed` (0 stops); stop() stops and clears Play In to Out.
    void play(int speed);
    void stop();
    // Plays `monitor` from `start` to `end` (inclusive) once at normal speed; when it gets there (not when stopped
    // first) the position goes to `returnTo`. Color Correction mode's Edit Review: "When playback is complete, the
    // position indicator returns to its location in the current clip" (FX Guide p.594).
    void playOnce(MonitorId monitor, std::int64_t start, std::int64_t end, std::optional<std::int64_t> returnTo);
    // Audio Loop Play (p.712): plays `monitor` from `start` to `end` (inclusive) at normal speed, over
    // and over, until stop(). Each pass restarts the sound (playStarted()). Playing at another speed
    // ends the loop.
    void playLoop(MonitorId monitor, std::int64_t start, std::int64_t end);
    // Trim mode's Play Loop (p.657, p.659-660): as above, but before each pass `range` (when set) gives
    // the frames to play, so a trim made during a pass is seen in the next one, and after each pass
    // play waits `pauseMs` milliseconds (the Intermission) at the end before going round again.
    struct LoopRange {
        std::int64_t start = 0;
        std::int64_t end = 0;
    };
    using LoopResolver = std::function<std::optional<LoopRange>()>;
    void playLoop(MonitorId monitor, LoopResolver range, int pauseMs);
    bool isLooping() const { return loop_.has_value(); }
    // True between two passes of a loop with an intermission.
    bool isLoopPaused() const;
    // Clip > Loop Selected Clips (p.393): the clips play one after another in the Source monitor, each
    // from its IN to its OUT (`whole`, with Alt: start to end), round and round until stop(). Tab jumps
    // to the next clip and Shift+Tab to the previous one while the loop plays.
    struct LoopClip {
        QString binId;
        model::BinItem item;
    };
    void playClipLoop(const QList<LoopClip>& clips, bool whole);
    // Moves the clip loop `delta` clips on (round), and plays from there.
    void stepClipLoop(int delta);
    bool isClipLooping() const { return !clipLoop_.isEmpty(); }
    // Advances playback by `ms` milliseconds (also used by tests).
    void advance(qint64 ms);
    // J-K-L Trim (Trim settings, p.658, p.1223): when set, holding K and tapping J or L asks it to step
    // `direction` frames (-1 or 1) first; it returns true when it took the step (a trim), so the monitor
    // does not move. Set by the Timeline.
    using KStepHandler = std::function<bool(int direction)>;
    void setKStepHandler(KStepHandler handler) { kStep_ = std::move(handler); }

    // Runs a Composer command by id (the buttons, the menus and the
    // keyboard all come here). Returns false if it is not one.
    bool perform(const QString& commandId);

    // Whether perform() knows the command.
    static bool handles(const QString& commandId);

    // Moves the active monitor, and a ganged one with it.
    void moveTo(MonitorId monitor, std::int64_t frame);

    // The Composer's widgets, for MainWindow and tests. button() is the first button carrying the
    // command, or null; none of these transfer ownership.
    QToolButton* button(MonitorId monitor, const QString& commandId) const;
    QToolButton* clipNameButton(MonitorId monitor) const;
    // Windows > Set Font for the Composer (p.81-82): "the font and point size of the sequence or source
    // clip name text; saves as a Composer setting". An empty family and size 0 restore the default.
    void setNameFont(const QString& family, int pointSize);
    QFont nameFont() const;
    QToolButton* trackingButton(MonitorId monitor) const;
    PositionBar* positionBar(MonitorId monitor) const;
    MonitorPicture* picture(MonitorId monitor) const;

    // clipNameMenu() rebuilds and returns the monitor's Clip Name menu (owned by the Composer).
    // contextMenu() makes a new right-click menu that deletes itself when closed.
    QMenu* clipNameMenu(MonitorId monitor);
    QMenu* contextMenu();
    // The menu of a monitor's picture: contextMenu() with Show Captions for that monitor.
    QMenu* contextMenu(MonitorId monitor);

signals:
    // Anything shown changed: what is loaded, positions, marks.
    void changed();
    // A button or key for a command that is not the Composer's own
    // (Splice-in, Overwrite).
    void commandRequested(const QString& commandId);
    // A command the Composer carried out, after it did.
    void performed(const QString& commandId);
    // The Effect Preview: a keyframe indicator in the position bar was clicked (Ctrl: `extend`), at
    // `frame` frames into the effect (p.103); a toolbar button the Composer does not carry out itself
    // (Add Keyframe, Go to Previous/Next Event), with Alt held or not.
    void effectKeyframePressed(double frame, bool extend);
    void effectCommandRequested(const QString& id, bool alt);
    // A handle of the Effect Preview's overlay (EffectPreview::handles; "move" also for a press inside the
    // outline when a handle of that name is shown) dragged from `from` to `to` (fractions of the picture,
    // from where the drag began); `finished` on the release; `shift` with Shift held (Pan and Scan moves its
    // wire frame in all directions, FX Guide p.237).
    void effectHandleDragged(const QString& id, const QPointF& from, const QPointF& to, bool finished, bool shift);
    // Tracking data points (FX Guide p.271): a data point ("trackpoint:<tracker>:<frame>") double-clicked, and a
    // lasso dragged over the Effect Preview (`area` in fractions of the picture; `shift` held).
    void effectPointDoubleClicked(const QString& id);
    void effectLasso(const QRectF& area, bool shift);
    // A trim counter was clicked: that side, or both with Shift (p.651).
    void trimSideRequested(model::TrimSide side);
    // Play started or changed speed, and play stopped.
    void playStarted(MonitorId monitor, int speed);

    // playStopped() fires only when play was running.
    void playStopped();
    // Play stopped on `monitor` with these difficulties (empty when it played smoothly).
    void playbackDifficulties(MonitorId monitor, const QList<Composer::PlaybackRange>& ranges);
    // Audio Loop Play started or ended.
    void loopChanged(bool looping);

    // The Video Quality Menu button's setting changed.
    void videoQualityChanged(VideoQuality quality);
    // Media files dropped on a monitor from the Source Browser (p.316).
    void filesDropped(MonitorId monitor, const QStringList& paths);
    // A Titler+ template was dragged to the Record monitor, to be edited there (FX Guide p.320).
    void titleTemplateDropped(const QString& binId, const QString& itemId);
    // A marker was double-clicked (p.416).
    void markerEditRequested(MonitorId monitor, const QString& markerId);

    // Composer > Show Single/Dual Monitor switched modes.
    void singleMonitorChanged(bool single);
    // A command was mapped onto one of the buttons.
    void buttonsChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    // Keys the OverlayInput takes beat the menus' shortcuts (Ctrl+G groups Titler+ objects there).
    bool event(QEvent* event) override;
    // While a clip loop plays, Tab and Shift+Tab anywhere in the application step it (p.393).
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Column;
    Column& column(MonitorId monitor) const;
    QWidget* buildColumn(MonitorId monitor);
    void setupButton(MonitorId monitor, MappableButton* button, const QString& commandId);
    void refresh();
    void tick();

    model::MonitorState source_;
    model::MonitorState record_;
    model::TrackingFormat sourceTracking_ = model::TrackingFormat::None;
    model::TrackingFormat recordTracking_ = model::TrackingFormat::None;
    MonitorId active_ = MonitorId::Source;
    Rational aspect_{16, 9};
    bool single_ = false;
    bool colors_ = false;
    bool ganged_ = false;
    // K is held down (K+J / K+L step a frame).
    bool kHeld_ = false;
    int speed_ = 0;
    bool slow_ = false; // slow play (playSlow())
    double fraction_ = 0; // Dynamic Play between whole speeds: the multiple of normal (speed_ is +-1)
    int dynamicDirection_ = 0; // Dynamic Play is playing this way
    int dynamicSteps_ = 0;     // twelfths of an octave of speed above normal
    int dynamicAcceleration_ = 3;
    int dynamicSlowStart_ = 3;
    std::optional<std::int64_t> stopAt_; // Play In to Out
    // Loop Selected Clips: the clips, the one playing, and whether each plays whole.
    QList<LoopClip> clipLoop_;
    int clipLoopIndex_ = 0;
    bool clipLoopWhole_ = false;
    Rational projectRate_{0, 1};
    bool interlacedProject_ = false;
    bool adapterBlending_ = true;
    bool startingLoopClip_ = false; // load() inside the loop must not end it
    void startLoopClip(int index);
    void endClipLoop();
    LoopResolver loopRange_;             // Trim mode's Play Loop: the next pass's frames
    int loopPauseMs_ = 0;
    QTimer* loopPause_ = nullptr;        // the Intermission between passes
    KStepHandler kStep_;
    std::optional<std::int64_t> loop_;   // Audio Loop Play: where each pass starts; it ends at stopAt_
    // Play reached its end going forward: the next pass of a loop, or a stop.
    void reachedEnd();
    void nextLoopPass();
    void endLoop();
    double carry_ = 0; // fractional frames between ticks
    QElapsedTimer clock_;
    QTimer* timer_ = nullptr;
    QLabel* center_ = nullptr;
    QWidget* trimBar_ = nullptr;
    QWidget* effectBar_ = nullptr;
    QWidget* monitorArea_ = nullptr; // the Source and Record columns and the middle panel
    CorrectionMonitors* correctionMonitors_ = nullptr;
    bool correctionMode_ = false;
    bool uncorrected_ = false;
    std::optional<std::int64_t> returnTo_; // playOnce()
    QToolButton* effectLength_ = nullptr;
    QToolButton* effectPosition_ = nullptr;
    EffectPreview effect_;
    OverlayInput* overlayInput_ = nullptr;
    int zoom_ = 0;
    QPointF pan_;
    // Shows the Effect Preview's widgets or the normal ones.
    void applyEffectPreview();
    // An Effect Preview toolbar button: the Composer's own (Reduce, Enlarge, steps, Play, Play Loop) or
    // effectCommandRequested().
    void effectButton(const QString& id);
    QWidget* smallTrim_ = nullptr;
    QWidget* middle_ = nullptr;
    DragDisplay drag_;
    QColor viewerBackground_;
    class QLabel* dragCounter_ = nullptr;
    QList<QWidget*> middleRows_;
    QList<QStringList> middleLayout_;
    model::TrackingFormat sourceTracking2_ = model::TrackingFormat::None;
    model::TrackingFormat recordTracking2_ = model::TrackingFormat::None;
    void applyRows();
    QList<QToolButton*> smallCounters_; // A, B
    TrimDisplay trim_;
    model::TrimSide trimPlaySide_ = model::TrimSide::B;
    Display display_;
    // The command of every button slot, row by row (see buttonLayout()).
    QList<QStringList> layout_[2]; // Source, Record
    Column* columns_[2] = {nullptr, nullptr};
    ItemResolver resolver_;
    MobResolver mobs_;
    PictureResolver pictures_;
    MonitorOverride override_;
    PhantomResolver phantoms_;
    CaptionResolver captions_;
    bool showCaptions_[2] = {false, false}; // Source, Record
    int groupSplit_ = 1; // 1, 4 or 9 (setGroupSplit)
    int groupBank_ = 0;
    PlayClock playClock_;
    // The position play last started (or changed speed) from; the play clock counts from here.
    std::int64_t playStart_ = 0;
    VideoQuality quality_ = VideoQuality::Full;
    bool smoothScaling_ = true;
    PlayStats stats_;
    QList<PlaybackRange> playRanges_;
    // Adds frames [start, start + length) of one kind to playRanges_, joined to the last range of the
    // kind when they touch.
    void notePlayback(std::int64_t start, std::int64_t length, bool dropped);
    Committer committer_;
    KeyResolver keys_;
    EventResolver events_;
    EventResolver stops_;
    MarkResolver marks_;
};

} // namespace lumenis::ui

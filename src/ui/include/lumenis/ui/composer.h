#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/monitor.h"
#include "lumenis/model/sequence.h"

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

namespace lumenis::ui {

class Composer;
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

    // The area of the widget the picture fills: the largest rectangle of the project's aspect ratio,
    // centred (letterboxed or pillarboxed), in widget coordinates.
    QRect pictureRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    Composer& composer_;
    MonitorId monitor_;
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
    // A clip by its MobID, from the open bins: its bin and the item. The Timeline's Change Source
    // Track > More finds a subclip's master clip with it. Set by MainWindow.
    using MobResolver = std::function<std::optional<std::pair<QString, model::BinItem>>(const MobId& mob)>;
    void setMobResolver(MobResolver resolver) { mobs_ = std::move(resolver); }
    std::optional<std::pair<QString, model::BinItem>> resolveMob(const MobId& mob) const {
        return mobs_ ? mobs_(mob) : std::nullopt;
    }
    // Keys pressed in the Composer are run through it (keyPressEvent).
    void setKeyResolver(KeyResolver resolver) { keys_ = std::move(resolver); }
    // The picture a monitor shows at its position, scaled to fit `size`:
    // the clip's frame, or for a sequence the frame of its top video track
    // there. `offline` is set when the media cannot be read (p.347).
    using PictureResolver = std::function<QImage(MonitorId, QSize size, bool* offline)>;
    void setPictureResolver(PictureResolver resolver) { pictures_ = std::move(resolver); }
    // Phantom marks (Composer settings, p.501): the IN and OUT an edit would use where none is set,
    // drawn in blue in the position bars. The Timeline works them out.
    using PhantomResolver =
        std::function<std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>(MonitorId)>;
    void setPhantomResolver(PhantomResolver resolver) { phantoms_ = std::move(resolver); }
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> phantomMarks(MonitorId monitor) const {
        return phantoms_ ? phantoms_(monitor) : std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>>{};
    }
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
    QString centerDuration() const;

    // Trim mode's Big Trim display (p.648-651), set by the Timeline.
    struct TrimDisplay {
        bool on = false;
        std::int64_t aFrames = 0; // frames trimmed on each side
        std::int64_t bFrames = 0;
        bool aActive = false;
        bool bActive = false;
    };

    // Turns the Big Trim display on or off and updates its counters. While on, the position bars and
    // button rows are hidden and both monitors are shown even in single-monitor mode.
    void setTrimDisplay(const TrimDisplay& display);
    const TrimDisplay& trimDisplay() const { return trim_; }

    // The frame counter under a monitor in Trim mode (Source: A side, Record: B side), and the row of
    // Trim buttons; exposed for tests and the Timeline.
    QToolButton* trimCounter(MonitorId monitor) const;
    QWidget* trimBar() const { return trimBar_; }

    // What the Composer shows (Composer settings, Window and Edit tabs,
    // p.1158-1160).
    struct Display {
        bool infoRow = true;       // Data Display at Top: Off, or one row
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

    // The current speed: 0 when stopped, otherwise a multiple of normal (negative: reverse). In slow
    // play it is 1 or -1 and isSlow() is true.
    bool isPlaying() const { return speed_ != 0; }
    int speed() const { return speed_; }
    // Slow play (p.401): "Press the K and L keys together for slow forward (8 fps for NTSC, 6 fps for
    // PAL, and 6 fps for 24p projects)", K and J for slow backward. Holding K and tapping L or J still
    // steps a frame; holding both starts slow play, and letting go of L or J stops it. There is no sound.
    void playSlow(int direction);
    bool isSlow() const { return slow_; }
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
    // Audio Loop Play (p.712): plays `monitor` from `start` to `end` (inclusive) at normal speed, over
    // and over, until stop(). Each pass restarts the sound (playStarted()). Playing at another speed
    // ends the loop.
    void playLoop(MonitorId monitor, std::int64_t start, std::int64_t end);
    bool isLooping() const { return loop_.has_value(); }
    // Advances playback by `ms` milliseconds (also used by tests).
    void advance(qint64 ms);

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
    QToolButton* trackingButton(MonitorId monitor) const;
    PositionBar* positionBar(MonitorId monitor) const;
    MonitorPicture* picture(MonitorId monitor) const;

    // clipNameMenu() rebuilds and returns the monitor's Clip Name menu (owned by the Composer).
    // contextMenu() makes a new right-click menu that deletes itself when closed.
    QMenu* clipNameMenu(MonitorId monitor);
    QMenu* contextMenu();

signals:
    // Anything shown changed: what is loaded, positions, marks.
    void changed();
    // A button or key for a command that is not the Composer's own
    // (Splice-in, Overwrite).
    void commandRequested(const QString& commandId);
    // A command the Composer carried out, after it did.
    void performed(const QString& commandId);
    // A trim counter was clicked: that side, or both with Shift (p.651).
    void trimSideRequested(model::TrimSide side);
    // Play started or changed speed, and play stopped.
    void playStarted(MonitorId monitor, int speed);

    // playStopped() fires only when play was running.
    void playStopped();
    // Audio Loop Play started or ended.
    void loopChanged(bool looping);

    // The Video Quality Menu button's setting changed.
    void videoQualityChanged(VideoQuality quality);
    // Media files dropped on a monitor from the Source Browser (p.316).
    void filesDropped(MonitorId monitor, const QStringList& paths);
    // A marker was double-clicked (p.416).
    void markerEditRequested(MonitorId monitor, const QString& markerId);

    // Composer > Show Single/Dual Monitor switched modes.
    void singleMonitorChanged(bool single);
    // A command was mapped onto one of the buttons.
    void buttonsChanged();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

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
    std::optional<std::int64_t> stopAt_; // Play In to Out
    std::optional<std::int64_t> loop_;   // Audio Loop Play: where each pass starts; it ends at stopAt_
    // Play reached its end going forward: the next pass of a loop, or a stop.
    void reachedEnd();
    void endLoop();
    double carry_ = 0; // fractional frames between ticks
    QElapsedTimer clock_;
    QTimer* timer_ = nullptr;
    QLabel* center_ = nullptr;
    QWidget* trimBar_ = nullptr;
    TrimDisplay trim_;
    Display display_;
    // The command of every button slot, row by row (see buttonLayout()).
    QList<QStringList> layout_[2]; // Source, Record
    Column* columns_[2] = {nullptr, nullptr};
    ItemResolver resolver_;
    MobResolver mobs_;
    PictureResolver pictures_;
    PhantomResolver phantoms_;
    PlayClock playClock_;
    // The position play last started (or changed speed) from; the play clock counts from here.
    std::int64_t playStart_ = 0;
    VideoQuality quality_ = VideoQuality::Full;
    bool smoothScaling_ = true;
    PlayStats stats_;
    Committer committer_;
    KeyResolver keys_;
    EventResolver events_;
    EventResolver stops_;
    MarkResolver marks_;
};

} // namespace lumenis::ui

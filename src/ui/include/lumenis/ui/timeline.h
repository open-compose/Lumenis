#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"

#include <QWidget>

#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QMenu;
class QScrollBar;
class QSlider;
class QToolButton;

namespace lumenis::ui {

class Timeline;

// The tracks area of the Timeline: the Track Selector panel on the left
// (p.625-630) and, to its right, the timecode ruler, the tracks with their
// segments, the IN to OUT highlight and the blue position indicator
// (p.607-609).
class TimelineView : public QWidget {
    Q_OBJECT

public:
    explicit TimelineView(Timeline& timeline, QWidget* parent = nullptr);

    // The rows top to bottom: D1, video tracks from the highest, TC1, audio
    // tracks from A1 (picture on p.607).
    struct Row {
        QString track; // "V1", "TC1", ...
        int top = 0;
        int height = 0;
        bool exists = true; // false: only a source track is patched here
    };
    QList<Row> rows() const;
    int contentHeight() const;
    int rulerHeight() const;
    int headerWidth() const;
    // Frame at an x position in the tracks area, and back.
    std::int64_t frameAt(int x) const;
    double xFor(std::int64_t frame) const;
    QRect sourceButton(const Row& row) const;
    QRect recordButton(const Row& row) const;
    QRect syncButton(const Row& row) const;
    QRect monitorButton(const Row& row) const;
    // The row under a point, or nullptr.
    std::optional<Row> rowAt(const QPoint& pos) const;
    // The segment under a point (filler too, but not at the ends of a
    // track, p.611).
    std::optional<model::SegmentRef> segmentAt(const QPoint& pos) const;
    // With both segment tools on, the upper half of a segment is
    // Lift/Overwrite and the lower half Extract/Splice-in (p.610).
    model::EditKind segmentModeAt(const QPoint& pos) const;

    // Vertical scrolling (p.608).
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

private:
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

    explicit Timeline(Composer& composer, QWidget* parent = nullptr);

    void setSequenceStore(SequenceStore store) { store_ = std::move(store); }
    void setSequenceMaker(SequenceMaker maker) { maker_ = std::move(maker); }
    void setKeyResolver(Composer::KeyResolver resolver) { keys_ = std::move(resolver); }

    Composer& composer() const { return composer_; }
    // Toggle Source/Record in Timeline (p.607).
    void setShowSource(bool on);
    bool showsSource() const { return showSource_; }
    MonitorId shownMonitor() const { return showSource_ ? MonitorId::Source : MonitorId::Record; }
    // What the Timeline shows: a sequence, or a clip as one segment per
    // track. Empty with nothing loaded.
    const model::Sequence& shown() const { return shown_; }
    bool hasShown() const;
    const TimecodeFormat& format() const { return format_; }
    std::int64_t startFrame() const;

    model::TrackPatch& patch() { return patch_; }
    const model::TrackPatch& patch() const { return patch_; }
    model::EditHistory& history() { return history_; }

    // Commands the Timeline carries out (editing, tracks, undo).
    bool perform(const QString& commandId);
    static bool handles(const QString& commandId);
    // Why the last command did nothing, for the status line.
    QString lastMessage() const { return message_; }

    // Patching from the Track Selector (p.629).
    bool patchTracks(const QString& source, const QString& record);
    void toggleSourceTrack(const QString& track);
    void toggleRecordTrack(const QString& track);
    // Track Monitor buttons (p.627-628): which tracks are heard and seen.
    bool isMonitored(const QString& track) const { return !unmonitored_.contains(track); }
    void toggleMonitor(const QString& track);

    // Track locks (p.633) and sync locks (p.493).
    void setTracksLocked(const QStringList& tracks, bool locked);
    void toggleSyncLock(const QString& track);
    void toggleSyncLockAll();

    // Segment mode (p.610-623). The two segment tools, each on or off; the
    // tool used when neither or both apply (Timeline settings, p.623; ours
    // is Lift/Overwrite, as for a lasso with no tool, p.612).
    bool isSegmentTool(model::EditKind kind) const;
    void setSegmentTool(model::EditKind kind, bool on);
    model::EditKind segmentKind() const;
    const QList<model::SegmentRef>& selection() const { return selection_; }
    void setSelection(const QList<model::SegmentRef>& segments);
    // Click and Shift-click (p.611).
    void clickSegment(const model::SegmentRef& segment, bool toggle);
    bool moveSelection(std::int64_t frames, int tracks, model::EditKind kind, bool copy);
    // The Delete key: selected segments (p.621), or else the selected
    // tracks after asking (p.636).
    bool deleteSelection();
    bool deleteTracks(const QStringList& tracks);
    // The right-click Select menu (p.613-614).
    enum class SelectWhere { Left, Right, Reverse };
    void selectSegments(SelectWhere where);

    // The Clipboard (p.479-480): a sequence that is in no bin.
    const std::optional<model::BinItem>& clipboard() const { return clipboard_; }

    // Zoom (p.607-608). The scale is in pixels per frame.
    double scale() const;
    double fitScale() const;
    void setScale(double pixelsPerFrame, std::optional<std::int64_t> anchor = std::nullopt);
    double firstFrame() const { return first_; }
    void setFirstFrame(double frame);
    void showEntireSequence();
    // The Focus button: 90 pixels per second around the position, and back
    // (p.608).
    void toggleFocus();
    bool isFocused() const { return focusBack_.has_value(); }
    void zoomTo(std::int64_t from, std::int64_t to);
    void zoomBack();
    static constexpr int kFocusPixelsPerSecond = 90;

    // Fast menu display options (p.592-593).
    struct Display {
        bool trackPanel = true;
        bool clipNames = true;
        bool clipDurations = false;
        bool clipTracks = false;
        bool timecodeTrack = true; // Show Track > TC1 (p.598)
    };
    const Display& display() const { return display_; }

    TimelineView* view() const { return view_; }
    QToolButton* toolButton(const QString& name) const;
    QSlider* scaleBar() const { return scaleBar_; }
    QScrollBar* scrollBar() const { return scroll_; }
    QMenu* fastMenu();
    // The right-click menu, over a track's button or elsewhere (p.613,
    // p.633-636). The caller owns it.
    QMenu* contextMenu(const QString& track = {});
    QLabel* timecodeDisplay() const { return timecode_; }
    // Edit > Undo/Redo List (p.466): the most recent first.
    QMenu* undoRedoMenu();

signals:
    void changed();
    void message(const QString& text);
    // A command that is not the Timeline's (New > Sequence).
    void commandRequested(const QString& commandId);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void sync();
    void syncScrollBars();
    void edit(model::EditKind kind);
    void remove(bool extract);
    void replaceEdit();
    void addEdit(bool fillerOnly);
    void topOrTail(bool top);
    void matchFrame();
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
    void addTrack(model::TrackKind kind, model::AudioFormat format = model::AudioFormat::Mono);
    // Stores a changed record sequence, with an undo step.
    void commit(const QString& description, const model::BinItem& before, model::BinItem after);
    void restore(const QString& binId, const model::BinItem& item);
    bool fail(const QString& text);
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
    QList<std::pair<double, double>> zoomStack_;
    Display display_;
    bool liftTool_ = false;
    bool spliceTool_ = false;
    QList<model::SegmentRef> selection_;
    std::optional<model::BinItem> clipboard_;
    int clipboardCount_ = 0;
    QString message_;
    bool syncing_ = false;
    SequenceStore store_;
    SequenceMaker maker_;
    Composer::KeyResolver keys_;
};

} // namespace lumenis::ui

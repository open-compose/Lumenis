#pragma once

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/composer.h"

#include <QWidget>

#include <functional>
#include <optional>

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
    QRect monitorButton(const Row& row) const;
    // The row under a point, or nullptr.
    std::optional<Row> rowAt(const QPoint& pos) const;

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

private:
    void moveIndicator(const QPoint& pos, Qt::KeyboardModifiers modifiers);

    Timeline& timeline_;
    int top_ = 0;
    bool scrubbing_ = false;
    bool zoomSelecting_ = false;
    std::optional<int> zoomFrom_;
    int zoomTo_ = 0;
    QString patchFrom_; // a source track being dragged to a record track
    QPoint patchPos_;
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
    // Edit > Undo/Redo List (p.466): the most recent first.
    QMenu* undoRedoMenu();

signals:
    void changed();
    void message(const QString& text);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void sync();
    void syncScrollBars();
    void edit(model::EditKind kind);
    void remove(bool extract);
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
    QString message_;
    bool syncing_ = false;
    SequenceStore store_;
    SequenceMaker maker_;
    Composer::KeyResolver keys_;
};

} // namespace lumenis::ui

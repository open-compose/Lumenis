#pragma once

#include <QPointF>
#include <QRectF>
#include <QWidget>

#include <optional>

class QMenu;
class QPainter;

namespace lumenis::model {
struct Segment;
struct Track;
} // namespace lumenis::model

namespace lumenis::ui {

class Timeline;

// The Timeline Sequence Map (Timeline > Show Sequence Map; Editing Guide p.606, Timeline settings
// p.1219; docs/spec/sequence-map.md): the whole of the shown sequence, every track, drawn small, with
// a white box over the part the Timeline shows. Dragging the box scrolls the Timeline across and
// down. The Timeline owns the map and shows it as an overlay in its tracks area or in a window of its
// own (Timeline::showSequenceMap, setSequenceMapInWindow); the right-click menu moves it between the
// two.
class SequenceMap : public QWidget {
    Q_OBJECT

public:
    // `timeline` must outlive the map.
    explicit SequenceMap(Timeline& timeline, QWidget* parent = nullptr);

    // The sequence's area inside the map (the map less a margin), and the white box in it: the frames
    // and rows the Timeline shows. Empty with nothing shown.
    QRectF mapArea() const;
    QRectF viewBox() const;
    // Scrolls the Timeline so the white box's centre is at `centre` (map pixels), as far as it goes.
    void centreViewAt(const QPointF& centre);
    // The right-click menu: Show in Dedicated Window, or Show in Timeline in the window (p.606). The
    // caller owns it.
    QMenu* contextMenu();

    QSize sizeHint() const override { return {320, 140}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    // As an overlay with "Clicking Elsewhere Hides the Sequence Map" (p.1219): a mouse press anywhere
    // outside the map, or its menu, hides it.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // The content of the Timeline's tracks area, in its pixels: the frames across (0 to the
    // sequence's duration) and the rows down (0 to their total height, without the ruler).
    double contentFrames() const;
    double contentHeight() const;
    // The first channel's peaks across an audio segment's box (Display waveforms, p.1219).
    void paintWaveform(QPainter& p, const QRectF& box, const model::Track& t, const model::Segment& s) const;

    Timeline& timeline_;
    std::optional<QPointF> grab_; // the drag's offset from the box's centre
    bool filtering_ = false;
};

// The Sequence Map's own window (Show in Dedicated Window, p.606). Closing it hides the map.
class SequenceMapWindow : public QWidget {
    Q_OBJECT

public:
    explicit SequenceMapWindow(QWidget* parent = nullptr);

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
};

} // namespace lumenis::ui

#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

class QMimeData;
class QSplitter;

// Docking bins inside a Bin Container (guide p.241-243): tab groups, the
// splitters between them and the drop zones shown while a bin is dragged.
// BinContainer (bin_container.h) owns the areas, opens and closes the bins and
// answers the signals here; each tab holds a BinView (bin_view.h). This is our
// own docking, separate from the main window's panels (panels.h).

namespace lumenis::ui {

class BinDockArea;
class BinView;

// Drag data: a bin tab (its Sidebar id), Sidebar entries, and bin items.
// The data is UTF-8 text: binTabMime() holds one node id; binNodesMime() one
// node id a line; binItemsMime() the source bin's node id on the first line,
// then one item id a line.
QString binTabMime();
QString binNodesMime();
QString binItemsMime();
// Media files dragged from the Source Browser (p.316-317), one path a line.
QString mediaFilesMime();

// Where a dragged bin goes (guide p.241): beside a group of bins, which
// subdivides the area, or tabbed into it.
enum class BinDropSide { Tab, Left, Right, Top, Bottom };

// Bin tabs: dragging a tab out of the bar docks or floats the bin. Moving a
// tab along the bar reorders it as a plain QTabBar does; leaving the bar
// vertically by more than a few pixels ends that move and emits dragOut().
class BinTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit BinTabBar(QWidget* parent = nullptr);

signals:
    // The tab at `index` was dragged out of the bar; BinTabGroup turns this
    // into BinDockArea::tabDragged, and BinContainer starts the QDrag.
    void dragOut(int index);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPoint pressPos_;
    int pressIndex_ = -1;
};

// Bins tabbed together (p.258): an asterisk on unsaved bins, a close button
// on each tab, and a tab menu listing them all.
class BinTabGroup : public QTabWidget {
    Q_OBJECT

public:
    // Created only by BinDockArea, as a child of `area`. Its close buttons,
    // tab changes, drags out and right-clicks are reported through the
    // area's signals.
    explicit BinTabGroup(BinDockArea& area);

    BinDockArea& area() const { return area_; }
    // The bins in tab order.
    QList<BinView*> views() const;
    // The bin of the current tab; null if the group is empty.
    BinView* currentView() const;

private:
    BinDockArea& area_;
};

// The right side of a Bin Container: groups of tabbed bins, side by side
// or above one another with splitters between them (p.241-242). The layout
// is a tree: the root is a BinTabGroup or a QSplitter, and each splitter holds
// groups or further splitters. The area does not own the BinDocuments; it only
// arranges their views.
class BinDockArea : public QWidget {
    Q_OBJECT

public:
    // A drop target: a group and the side of it a bin would go to. An invalid
    // zone (no group) means nowhere.
    struct Zone {
        BinTabGroup* group = nullptr;
        BinDropSide side = BinDropSide::Tab;
        QRect rect; // in area coordinates
        bool isValid() const { return group != nullptr; }
    };

    explicit BinDockArea(QWidget* parent = nullptr);

    // Groups in layout order, left to right and top to bottom.
    QList<BinTabGroup*> groups() const;
    // Every bin, group by group in layout order.
    QList<BinView*> views() const;
    // The group holding `view`; null if it is not in this area.
    BinTabGroup* groupOf(const BinView* view) const;
    bool isEmpty() const { return groups().isEmpty(); }
    // The top of the layout tree; null when the area is empty.
    QWidget* root() const { return root_; }

    // The group last used: new bins open there. Falls back to the first group
    // if that one has gone; null when the area is empty.
    BinTabGroup* currentGroup() const;
    void setCurrentGroup(BinTabGroup* group);

    // Puts a bin into the area: tabbed into `target` (by default the current
    // group), or in a new group on one side of it. The area takes `view` as a
    // child widget; the new or target group becomes current.
    void addView(BinView* view, const QString& title, BinTabGroup* target = nullptr,
                 BinDropSide side = BinDropSide::Tab);
    // Takes a bin out without deleting it. Empty groups go, and splitters
    // left with one side are removed.
    void takeView(BinView* view);
    // The text of the bin's tab.
    void setTitle(BinView* view, const QString& title);
    // Brings the bin's tab to the front and makes its group current.
    void showView(BinView* view);

    // Drop zones: every group's edges (green, p.241), or with Alt held only
    // tabbing into a group (p.243). Rectangles are in area coordinates; edge
    // zones are a quarter of the group's width or height, at least 16 pixels.
    QList<Zone> zones(bool tabOnly) const;
    // The zone under `pos` (area coordinates): the nearest edge if it is in
    // one, otherwise tabbing into the group under it; invalid if none.
    Zone zoneAt(const QPoint& pos, bool tabOnly) const;
    // While something is dragged over the area: the zone drawn green. An
    // invalid zone hides the overlay.
    const Zone& hoveredZone() const { return hovered_; }
    void setHoveredZone(const Zone& zone);

signals:
    // A group's current tab changed; `view` may be null if the group emptied.
    void currentViewChanged(lumenis::ui::BinView* view);
    // A tab's close button. The area does not close the bin itself.
    void closeRequested(lumenis::ui::BinView* view);
    // A bin tab dragged out of a bar.
    void tabDragged(lumenis::ui::BinView* view);
    // Something dropped on a zone: a bin tab, or Sidebar entries. `zone` may
    // be invalid when the area is empty.
    void binDropped(const QString& nodeId, const lumenis::ui::BinDockArea::Zone& zone);
    void nodesDropped(const QStringList& nodeIds, const lumenis::ui::BinDockArea::Zone& zone);
    // Right-click on a bar ("Show Sidebar", p.241).
    void tabMenuRequested(const QPoint& globalPos);
    // The last group was removed.
    void emptied();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class BinTabGroup;
    BinTabGroup* newGroup();
    void setRoot(QWidget* root);
    void replace(QWidget* old, QWidget* replacement);
    void removeGroup(BinTabGroup* group);
    static bool accepts(const QMimeData* mime);

    QWidget* root_ = nullptr;
    QWidget* overlay_ = nullptr;
    BinTabGroup* current_ = nullptr;
    Zone hovered_;
};

} // namespace lumenis::ui

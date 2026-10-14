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

namespace lumenis::ui {

class BinDockArea;
class BinView;

// Drag data: a bin tab (its Sidebar id), Sidebar entries, and bin items.
QString binTabMime();
QString binNodesMime();
QString binItemsMime();

// Where a dragged bin goes (guide p.241): beside a group of bins, which
// subdivides the area, or tabbed into it.
enum class BinDropSide { Tab, Left, Right, Top, Bottom };

// Bin tabs: dragging a tab out of the bar docks or floats the bin.
class BinTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit BinTabBar(QWidget* parent = nullptr);

signals:
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
    explicit BinTabGroup(BinDockArea& area);

    BinDockArea& area() const { return area_; }
    QList<BinView*> views() const;
    BinView* currentView() const;

private:
    BinDockArea& area_;
};

// The right side of a Bin Container: groups of tabbed bins, side by side
// or above one another with splitters between them (p.241-242).
class BinDockArea : public QWidget {
    Q_OBJECT

public:
    struct Zone {
        BinTabGroup* group = nullptr;
        BinDropSide side = BinDropSide::Tab;
        QRect rect; // in area coordinates
        bool isValid() const { return group != nullptr; }
    };

    explicit BinDockArea(QWidget* parent = nullptr);

    // Groups in layout order, left to right and top to bottom.
    QList<BinTabGroup*> groups() const;
    QList<BinView*> views() const;
    BinTabGroup* groupOf(const BinView* view) const;
    bool isEmpty() const { return groups().isEmpty(); }
    QWidget* root() const { return root_; }

    // The group last used: new bins open there.
    BinTabGroup* currentGroup() const;
    void setCurrentGroup(BinTabGroup* group);

    // Puts a bin into the area: tabbed into `target` (by default the current
    // group), or in a new group on one side of it.
    void addView(BinView* view, const QString& title, BinTabGroup* target = nullptr,
                 BinDropSide side = BinDropSide::Tab);
    // Takes a bin out without deleting it. Empty groups go, and splitters
    // left with one side are removed.
    void takeView(BinView* view);
    void setTitle(BinView* view, const QString& title);
    void showView(BinView* view);

    // Drop zones: every group's edges (green, p.241), or with Alt held only
    // tabbing into a group (p.243).
    QList<Zone> zones(bool tabOnly) const;
    Zone zoneAt(const QPoint& pos, bool tabOnly) const;
    // While something is dragged over the area.
    const Zone& hoveredZone() const { return hovered_; }
    void setHoveredZone(const Zone& zone);

signals:
    void currentViewChanged(lumenis::ui::BinView* view);
    void closeRequested(lumenis::ui::BinView* view);
    // A bin tab dragged out of a bar.
    void tabDragged(lumenis::ui::BinView* view);
    // Something dropped on a zone: a bin tab, or Sidebar entries.
    void binDropped(const QString& nodeId, const lumenis::ui::BinDockArea::Zone& zone);
    void nodesDropped(const QStringList& nodeIds, const lumenis::ui::BinDockArea::Zone& zone);
    // Right-click on a bar ("Show Sidebar", p.241).
    void tabMenuRequested(const QPoint& globalPos);
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

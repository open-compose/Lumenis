#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QSplitter;
class QStackedWidget;

namespace lumenis::ui {

// The paneled interface of guide ch. 4 (p.74-79). Every window (Bin
// Container, Composer, Timeline, tools, extensions) is a Panel. Panels sit
// in PanelGroups, which show a vertical title bar with one tab per panel.
// Groups are arranged by splitters inside a DockHost: the main window has
// one, and so does every floating window ("You can also panel windows within
// any floating window", p.79). PanelManager owns the arrangement: docking,
// tabbing, floating, closing, dragging with green drop zones, and saving and
// restoring layouts for workspaces.

class PanelGroup;
class PanelManager;
class DockHost;

enum class DockSide { Left, Right, Top, Bottom };

class Panel : public QWidget {
    Q_OBJECT

public:
    // `id` is stable and is what layouts store ("Composer", "Timeline", ...).
    Panel(const QString& id, const QString& title, QWidget* content = nullptr, QWidget* parent = nullptr);

    QString id() const { return objectName(); }
    QString title() const { return title_; }
    void setTitle(const QString& title);
    QWidget* content() const { return content_; }
    bool isBlank() const { return blank_; }

    // The group the panel is in, or null while it is closed.
    PanelGroup* group() const;

signals:
    void titleChanged();

private:
    friend class PanelManager;
    QString title_;
    QWidget* content_ = nullptr;
    bool blank_ = false;
};

// A stack of panels with a vertical title bar on the left (p.74-75).
class PanelGroup : public QWidget {
    Q_OBJECT

public:
    explicit PanelGroup(PanelManager& manager, QWidget* parent = nullptr);

    void insertPanel(int index, Panel* panel);
    void addPanel(Panel* panel) { insertPanel(-1, panel); }
    // Removes the panel and leaves it without a parent (hidden).
    void takePanel(Panel* panel);
    const QList<Panel*>& panels() const { return panels_; }
    int count() const { return static_cast<int>(panels_.size()); }
    Panel* current() const;
    void setCurrent(Panel* panel);

    // The host it is docked in.
    DockHost* host() const;
    QWidget* titleBar() const { return titleBar_; }

    bool isActive() const { return active_; }
    void setActive(bool active);

signals:
    void currentChanged();

private:
    PanelManager& manager_;
    QWidget* titleBar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QList<Panel*> panels_;
    bool active_ = false;
};

// An area that holds a tree of splitters with PanelGroups as leaves.
class DockHost : public QWidget {
    Q_OBJECT

public:
    explicit DockHost(PanelManager& manager, QWidget* parent = nullptr);

    QWidget* root() const { return root_; }
    bool isEmpty() const { return root_ == nullptr; }
    // Groups in layout order: left to right, top to bottom.
    QList<PanelGroup*> groups() const;

    // Places `group` next to `target` (which must be in this host), taking
    // `fraction` of the target's space.
    void insert(PanelGroup* group, PanelGroup* target, DockSide side, double fraction = 0.5);
    // Places `group` along one edge of the whole host.
    void insertAtEdge(PanelGroup* group, DockSide side, double fraction = 0.33);
    // Fills an empty host.
    void fill(PanelGroup* group);
    // Detaches `group` (left without a parent) and removes splitters that are
    // no longer needed.
    void remove(PanelGroup* group);
    // Detaches the whole tree and returns it.
    QWidget* takeRoot();
    void setRoot(QWidget* root);

    QWidget* overlay() const { return overlay_; }
    PanelManager& manager() const { return manager_; }

    static QSplitter* createSplitter(Qt::Orientation orientation);
    // Sets a splitter's sizes as proportions, applied once it has its real
    // size (a layout restored before the window is shown has none yet).
    static void setProportions(QSplitter* splitter, const QList<int>& proportions);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void replace(QWidget* old, QWidget* replacement);

    PanelManager& manager_;
    QWidget* root_ = nullptr;
    QWidget* overlay_ = nullptr;
};

// A floating window: a top-level window with its own DockHost.
class FloatingWindow : public QWidget {
    Q_OBJECT

public:
    FloatingWindow(PanelManager& manager, QWidget* owner);
    DockHost* host() const { return host_; }
    void updateTitle();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    PanelManager& manager_;
    DockHost* host_ = nullptr;
};

// Where a dragged panel would land.
struct DropTarget {
    enum class Kind { None, Split, Edge, Fill, Tab };
    Kind kind = Kind::None;
    DockHost* host = nullptr;
    PanelGroup* group = nullptr; // Split and Tab
    DockSide side = DockSide::Left;
    QRect zone;    // in host coordinates
    QRect preview; // in host coordinates: how the panel will appear
};

class PanelManager : public QObject {
    Q_OBJECT

public:
    // Floating windows belong to `owner` (the main window).
    explicit PanelManager(QWidget* owner);
    ~PanelManager() override;

    DockHost* mainHost() const { return mainHost_; }
    QList<FloatingWindow*> floatingWindows() const;

    // Registers a panel. It starts closed.
    void addPanel(Panel* panel);
    Panel* panel(const QString& id) const;
    QList<Panel*> panels() const;
    bool isOpen(const Panel* panel) const;
    bool isFloating(const Panel* panel) const;

    Panel* activePanel() const { return active_; }
    void activate(Panel* panel);

    // Moves a panel (alone) next to `target`, into `target` as a tab, along
    // a host's edge, or into its own floating window.
    void dock(Panel* panel, PanelGroup* target, DockSide side, double fraction = 0.5);
    void dockAtEdge(Panel* panel, DockHost* host, DockSide side, double fraction = 0.33);
    void tab(Panel* panel, PanelGroup* target);
    FloatingWindow* floatPanel(Panel* panel, const QRect& geometry = {});
    // The same for a whole group.
    void dockGroup(PanelGroup* group, PanelGroup* target, DockSide side, double fraction = 0.5);
    FloatingWindow* floatGroup(PanelGroup* group, const QRect& geometry = {});

    // Windows > Float All Panels / Float Active Panel (p.78-79).
    void floatAll();
    void floatActive();
    // Windows > Add Blank Panel (p.83): next to the active panel.
    Panel* addBlankPanel();
    // The tab's close button, File > Close. A docked panel that was the last
    // in its group leaves a blank panel when blankPanelOnClose() is set; the
    // panel returns to that place when it is opened again (p.83).
    void closePanel(Panel* panel);
    // Tools > name: opens a closed panel (floating, p.94) or brings it forward.
    void showPanel(Panel* panel);

    bool blankPanelOnClose() const { return blankOnClose_; }
    void setBlankPanelOnClose(bool on) { blankOnClose_ = on; }

    // Ctrl+Up/Down (Cmd on macOS) cycles windows, Ctrl+Tab cycles tabs (p.78).
    void cycleWindows(int direction);
    void cycleTabs();

    // Layouts, for workspaces. Panels that a layout does not mention are
    // closed; unknown ids are ignored.
    QJsonObject saveLayout() const;
    void restoreLayout(const QJsonObject& layout);

    // Dragging a title bar (p.75-78). `panel` null drags the whole group.
    void beginDrag(PanelGroup* group, Panel* panel);
    void updateDrag(const QPoint& globalPos, bool tabbing);
    void endDrag(const QPoint& globalPos, bool tabbing);
    void cancelDrag();
    bool isDragging() const { return dragGroup_ != nullptr; }
    DropTarget dropTargetAt(const QPoint& globalPos, bool tabbing) const;

    // Drop zones of one host while dragging, for its overlay.
    QList<DropTarget> dropZones(const DockHost* host, bool tabbing) const;
    const DropTarget& hoveredTarget() const { return hovered_; }
    bool tabbingDrag() const { return dragTabbing_; }

signals:
    void layoutChanged();
    void activePanelChanged(lumenis::ui::Panel* panel);
    void floatingWindowCreated(QWidget* window);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class FloatingWindow;
    friend class PanelGroup;

    PanelGroup* newGroup();
    Panel* newBlankPanel(const QString& id = {});
    FloatingWindow* newFloatingWindow(const QRect& geometry);
    // Detaches `panel` from its group; deletes the group if it empties.
    void detach(Panel* panel);
    // Unregisters and deletes a panel (blank panels).
    void forget(Panel* panel);
    // Takes `panel` (alone) or `group` out of the layout, ready to be placed.
    PanelGroup* takeForMove(PanelGroup* group, Panel* panel);
    void finishMove(PanelGroup* moved);
    void detachGroup(PanelGroup* group);
    void discardGroupIfEmpty(PanelGroup* group);
    void removeEmptyFloatingWindows();
    void floatingWindowClosed(FloatingWindow* window);
    QList<DockHost*> hosts() const;
    QList<PanelGroup*> allGroups() const;
    QRect defaultFloatingGeometry(const QSize& size) const;
    void refreshOverlays();
    void changed();

    QJsonObject nodeToJson(QWidget* node) const;
    QWidget* nodeFromJson(const QJsonObject& node, QList<Panel*>& placed);

    QWidget* owner_;
    DockHost* mainHost_ = nullptr;
    QList<Panel*> panels_; // removed when destroyed
    QList<FloatingWindow*> floating_; // removed when destroyed
    Panel* active_ = nullptr;
    QHash<QString, QString> blankHomes_; // panel id -> blank panel id
    int nextBlank_ = 1;
    bool blankOnClose_ = false;

    QPointer<PanelGroup> dragGroup_;
    QPointer<Panel> dragPanel_;
    bool dragTabbing_ = false;
    DropTarget hovered_;
};

} // namespace lumenis::ui

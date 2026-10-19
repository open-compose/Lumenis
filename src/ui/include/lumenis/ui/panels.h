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

// Which side of a group or host a panel is docked on.
enum class DockSide { Left, Right, Top, Bottom };

// One window of the interface. A Panel wraps its `content` widget (it takes ownership and fills itself with
// it) and is registered with the PanelManager, which moves it between groups and hosts. A closed panel has
// no parent and is hidden; it is deleted with the manager unless it has a parent then.
class Panel : public QWidget {
    Q_OBJECT

public:
    // `id` is stable and is what layouts store ("Composer", "Timeline", ...).
    Panel(const QString& id, const QString& title, QWidget* content = nullptr, QWidget* parent = nullptr);

    // The stable id, stored as the widget's objectName.
    QString id() const { return objectName(); }
    // The text shown on the panel's tab and, for the first group, a floating window's title.
    QString title() const { return title_; }
    // Emits titleChanged() when the title differs.
    void setTitle(const QString& title);
    // The wrapped widget, or null if there is none (blank panels).
    QWidget* content() const { return content_; }
    // True for the placeholders of Windows > Add Blank Panel and of closing (p.83). Their ids start with
    // "Blank"; the manager deletes them when they are closed or a layout leaves them out.
    bool isBlank() const { return blank_; }

    // The group the panel is in, or null while it is closed.
    PanelGroup* group() const;

signals:
    // Fires after setTitle() changes the title; the group's title bar repaints.
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
    // Groups are made by the PanelManager; they are deleted when their last panel leaves.
    explicit PanelGroup(PanelManager& manager, QWidget* parent = nullptr);

    // Adds `panel` as a tab at `index` (-1 or out of range: at the end). The first panel added becomes
    // current. A panel already in the group is ignored.
    void insertPanel(int index, Panel* panel);
    void addPanel(Panel* panel) { insertPanel(-1, panel); }
    // Removes the panel and leaves it without a parent (hidden).
    void takePanel(Panel* panel);
    // The panels in tab order, top to bottom on the title bar.
    const QList<Panel*>& panels() const { return panels_; }
    int count() const { return static_cast<int>(panels_.size()); }
    // The panel whose content is showing, or null when the group is empty.
    Panel* current() const;
    // Shows `panel` (which must be in the group); emits currentChanged() if it changes.
    void setCurrent(Panel* panel);

    // The host it is docked in.
    DockHost* host() const;
    // The vertical title bar on the left: drag box, tab menu and tabs (drawn in panels.cpp).
    QWidget* titleBar() const { return titleBar_; }

    // The group of the active panel; its title bar is drawn lighter. Set by PanelManager::activate().
    bool isActive() const { return active_; }
    void setActive(bool active);

signals:
    // Fires when the current panel changes, also when the current one is taken out.
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
    // Also creates the drop overlay, a child kept above the tree.
    explicit DockHost(PanelManager& manager, QWidget* parent = nullptr);

    // The top of the tree: a PanelGroup, a QSplitter, or null when the host is empty.
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

    // The transparent widget that paints the drop zones while a title bar is dragged.
    QWidget* overlay() const { return overlay_; }
    PanelManager& manager() const { return manager_; }

    // A splitter with the panel chrome's flat handles (kSplitterHandle wide) whose children cannot collapse.
    // The caller owns it until it is placed in a host.
    static QSplitter* createSplitter(Qt::Orientation orientation);
    // Sets a splitter's sizes as proportions, applied once it has its real
    // size (a layout restored before the window is shown has none yet).
    static void setProportions(QSplitter* splitter, const QList<int>& proportions);

protected:
    // Fills the host with kHostBackground (seen while it is empty).
    void paintEvent(QPaintEvent* event) override;
    // The root and the overlay always cover the whole host.
    void resizeEvent(QResizeEvent* event) override;

private:
    // Puts `replacement` where `old` is in the tree (as root, or at old's index in its splitter, keeping the
    // splitter's sizes). `old` is left where Qt puts it; callers re-parent or delete it.
    void replace(QWidget* old, QWidget* replacement);

    PanelManager& manager_;
    QWidget* root_ = nullptr;
    QWidget* overlay_ = nullptr;
};

// A floating window: a top-level window with its own DockHost.
class FloatingWindow : public QWidget {
    Q_OBJECT

public:
    // A Qt::Tool window owned by `owner`, so it stays above the main window. Made by the PanelManager.
    FloatingWindow(PanelManager& manager, QWidget* owner);
    DockHost* host() const { return host_; }
    // Sets the window title to the current panel of its first group.
    void updateTitle();

protected:
    // Closing the window closes its panels (PanelManager::floatingWindowClosed).
    void closeEvent(QCloseEvent* event) override;

private:
    PanelManager& manager_;
    DockHost* host_ = nullptr;
};

// Where a dragged panel would land.
struct DropTarget {
    // None: no zone, the panel floats. Split: beside `group`. Edge: along a whole side of `host`. Fill: into an
    // empty host. Tab: into `group` as tabs (Alt-drag).
    enum class Kind { None, Split, Edge, Fill, Tab };
    Kind kind = Kind::None;
    DockHost* host = nullptr;
    PanelGroup* group = nullptr; // Split and Tab
    // For Split and Edge.
    DockSide side = DockSide::Left;
    QRect zone;    // in host coordinates
    QRect preview; // in host coordinates: how the panel will appear
};

// Owns the arrangement of all panels: the main window's DockHost, the floating windows, which panel is
// active, blank panels, and dragging. Implemented in panel_manager.cpp. It installs an application-wide
// event filter so that clicking anywhere in a panel activates it, Esc cancels a drag and Alt switches a
// drag to tabbing.
class PanelManager : public QObject {
    Q_OBJECT

public:
    // Floating windows belong to `owner` (the main window).
    explicit PanelManager(QWidget* owner);
    ~PanelManager() override;

    // The main window's host. Created without a parent; the main window places it in its layout.
    DockHost* mainHost() const { return mainHost_; }
    // In creation order (the newest last, and on top).
    QList<FloatingWindow*> floatingWindows() const;

    // Registers a panel. It starts closed.
    void addPanel(Panel* panel);
    // The registered panel with this id, or null.
    Panel* panel(const QString& id) const;
    // Every registered panel, open or closed, in registration order.
    QList<Panel*> panels() const;
    // Open means in a group, docked or floating. Null is not open.
    bool isOpen(const Panel* panel) const;
    // In a floating window's host rather than the main window's.
    bool isFloating(const Panel* panel) const;

    // The panel that keyboard commands act on, or null.
    Panel* activePanel() const { return active_; }
    // Makes an open panel current in its group and active; emits activePanelChanged() if it changes. Closed
    // panels and null are ignored.
    void activate(Panel* panel);

    // Moves a panel (alone) next to `target`, into `target` as a tab, along
    // a host's edge, or into its own floating window.
    // `fraction` (0-1) is the share of the target's or host's width or height the panel gets. Each emits
    // layoutChanged() and activates the moved panel.
    void dock(Panel* panel, PanelGroup* target, DockSide side, double fraction = 0.5);
    void dockAtEdge(Panel* panel, DockHost* host, DockSide side, double fraction = 0.33);
    void tab(Panel* panel, PanelGroup* target);
    // `geometry` is in global coordinates; empty puts the window where the panel is now, or centred on the
    // main window. A panel alone in its group floats with the group.
    FloatingWindow* floatPanel(Panel* panel, const QRect& geometry = {});
    // The same for a whole group.
    // A group alone in a floating window that is floated again only moves that window.
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
    // `direction` is +1 or -1 through the groups of every host, in layout order.
    void cycleWindows(int direction);
    void cycleTabs();

    // Layouts, for workspaces. Panels that a layout does not mention are
    // closed; unknown ids are ignored.
    // The JSON is {"version", "main": node, "floating": [{"geometry": [x, y, w, h], "root": node}],
    // "active": id, "blankHomes": {panel id: blank id}}, where a node is {"tabs": [ids], "current": index} or
    // {"split": "h"|"v", "sizes": [...], "children": [nodes]}. restoreLayout() ignores another version.
    QJsonObject saveLayout() const;
    void restoreLayout(const QJsonObject& layout);

    // Dragging a title bar (p.75-78). `panel` null drags the whole group.
    // Called by the title bars. `globalPos` is in screen coordinates; `tabbing` is whether Alt is held.
    // endDrag() places the panel or group at the drop target, or floats it where it was dropped.
    void beginDrag(PanelGroup* group, Panel* panel);
    void updateDrag(const QPoint& globalPos, bool tabbing);
    void endDrag(const QPoint& globalPos, bool tabbing);
    void cancelDrag();
    bool isDragging() const { return dragGroup_ != nullptr; }
    // The zone under `globalPos` in the topmost host there, or Kind::None.
    DropTarget dropTargetAt(const QPoint& globalPos, bool tabbing) const;

    // Drop zones of one host while dragging, for its overlay.
    QList<DropTarget> dropZones(const DockHost* host, bool tabbing) const;
    const DropTarget& hoveredTarget() const { return hovered_; }
    bool tabbingDrag() const { return dragTabbing_; }

signals:
    // After any change to the arrangement: a move, close, open, or restoreLayout().
    void layoutChanged();
    // `panel` is null when no panel is open after restoreLayout().
    void activePanelChanged(lumenis::ui::Panel* panel);
    // For each new floating window, so the main window can give it its shortcuts.
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

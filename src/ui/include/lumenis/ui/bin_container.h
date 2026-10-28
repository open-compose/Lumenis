#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/bin_lock.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/ui/bin_dock.h"

#include <QAbstractItemModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QLineEdit;
class QMenu;
class QJsonArray;
class QJsonValue;
class QSettings;
class QSplitter;
class QTimer;
class QTreeView;

// The Bin Container (guide p.239-299): the project's bin list (the Sidebar),
// the bins open beside it, floating Bin Containers and Favorite Bins.
// BinContainer is the hub: it owns the open BinDocuments, creates their
// BinViews (bin_view.h), arranges them in BinDockAreas (bin_dock.h) and keeps
// the project's bin list, model::ProjectBins (src/model/project_bins.h), in
// step. MainWindow creates one and drives it from the Bin, File and Edit
// menus; the Project panel shows a second BinSidebar on it.

namespace lumenis::ui {

class BinContainer;
class BinDocument;
class BinView;

// Favorite Bins (guide p.293): bins the user reaches from any project. "The
// list of Favorite Bins is stored in the Bin User settings" (p.293), as is
// whether its folder shows at the top or the bottom of the Sidebar
// ("Favorite Bins show at"). MainWindow keeps the list in the active Bin
// setting's "favoriteBins" value: it reads it in applySettings() and writes
// it on BinContainer::favoritesChanged().
class FavoriteBins {
public:
    // Absolute paths of the bin files, process-wide.
    static QStringList& paths();
    static bool& atTop();
    // The paths as a JSON array of strings, and back. fromJson() skips
    // empty and repeated paths, and returns whether the list changed.
    static QJsonArray toJson();
    static bool fromJson(const QJsonValue& value);
    // The list as builds before it was a User setting kept it, in the
    // application's QSettings: as toJson() makes it, null when there is
    // none. forgetLegacy() removes it once MainWindow has adopted it.
    static QJsonValue legacy(const QSettings& settings);
    static void forgetLegacy(QSettings& settings);

    // Sidebar ids: the folder, and one per bin ("favorite:" and the path).
    static QString folderId() { return QStringLiteral("favorites"); }
    static QString idFor(const QString& path) { return QStringLiteral("favorite:") + path; }
    static bool isFavoriteId(const QString& id) { return id.startsWith(QLatin1String("favorite:")); }
    static QString pathOf(const QString& id) { return id.mid(9); }
};

// The Bin Container Sidebar's list (guide p.294-299): bins, folders, the
// Trash, Other Bins and Favorite Bins, or a flat list of bins when filtering
// or in Flat View. Entries can be renamed in place and dragged into
// folders; bin items dropped on a bin move into it (p.243). Rows hold only
// Sidebar ids; names, kinds and sizes are looked up in the BinContainer each
// time. Two columns: Name and Size.
class BinTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    static constexpr int kIdRole = Qt::UserRole + 1;
    static constexpr int kNameColumn = 0;
    static constexpr int kSizeColumn = 1;

    explicit BinTreeModel(BinContainer& container, QObject* parent = nullptr);

    // Re-reads the container's bins and resets the model.
    void rebuild();
    // Flat View (p.297): every bin, no folders.
    void setFlat(bool flat);
    bool isFlat() const { return flat_; }
    // Bins whose names contain `text`, as a flat list; empty restores the tree.
    void setFilter(const QString& text);
    // The Name-column index of a Sidebar id; invalid if it is not listed.
    QModelIndex indexOf(const QString& id) const;
    // The Sidebar id of a row; empty for an invalid index.
    QString idOf(const QModelIndex& index) const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

private:
    struct Row {
        QString id;
        Row* parent = nullptr;
        std::vector<std::unique_ptr<Row>> children;
    };
    void build(const QList<model::BinNode>& nodes, Row* into);
    void addFavorites();
    Row* rowFor(const QModelIndex& index) const;
    int rowIndex(const Row* row) const;

    BinContainer& container_;
    Row root_;
    bool flat_ = false;
    QString filter_;
};

// A Bin Container Sidebar (p.240): the quick filter, the fast menu and the
// list. The main Bin Container, floating Bin Containers and the Project Bin
// Container each have one.
class BinSidebar : public QWidget {
    Q_OBJECT

public:
    BinSidebar(BinContainer& container, QWidget* parent = nullptr);

    QTreeView* tree() const { return tree_; }
    BinTreeModel* model() const { return model_; }
    QLineEdit* filter() const { return filter_; }

    // Rebuilds the list, keeping folders open. The first time, every folder
    // opens.
    void refresh();
    // Sidebar ids of the selected entries.
    QStringList selectedNodes() const;
    void setFlatView(bool flat);
    // Also puts `text` in the filter box.
    void setFilter(const QString& text);
    // Opens a folder; an empty id does nothing.
    void expand(const QString& folderId);
    // Hide Sidebar in the fast menu, when the owner allows it.
    std::function<void()> onHide;

private:
    void showMenu(const QPoint& pos);

    BinContainer& container_;
    QLineEdit* filter_ = nullptr;
    QTreeView* tree_ = nullptr;
    BinTreeModel* model_ = nullptr;
};

// A Bin Container dragged out of the main one (p.241): a window of its own
// whose Sidebar starts closed. It closes when its last bin closes or is
// dragged elsewhere.
class FloatingBinContainer : public QWidget {
    Q_OBJECT

public:
    // A tool window over `owner`. Made only by BinContainer::floatBin(), which
    // keeps it in its list and deletes it when it empties.
    FloatingBinContainer(BinContainer& container, QWidget* owner);

    BinDockArea* area() const { return area_; }
    BinSidebar* sidebar() const { return sidebar_; }
    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    BinContainer& container_;
    QSplitter* split_ = nullptr;
    BinSidebar* sidebar_ = nullptr;
    BinDockArea* area_ = nullptr;
};

// The Bin Container (p.239-243): the Sidebar with the project's bins on the
// left, open bins on the right, tabbed or side by side. Bins and folders are
// named by Sidebar id throughout: a model::BinNode id, or a FavoriteBins id.
// Changes to the bin list are saved at once (saveTree()); open bins are saved
// on close, on Save All, on the auto-save timer and when the project closes.
class BinContainer : public QWidget {
    Q_OBJECT

public:
    explicit BinContainer(QWidget* parent = nullptr);
    // Saves open bins and the bin list, then deletes floating containers.
    ~BinContainer() override;

    // Shows a project's bins and reopens the bins that were open when it was
    // last closed. A null folder closes the project (saving open bins).
    void setProject(const QString& folder, const QString& name, const Rational& editRate);
    bool hasProject() const { return bins_ != nullptr; }
    // The project's bin list; null with no project.
    model::ProjectBins* bins() const { return bins_.get(); }

    // The Bin menu, offered as each bin's fast menu (p.259).
    void setBinMenu(QMenu* menu) { binMenu_ = menu; }
    // File > Input, which bins offer too (p.315).
    void setInputMenu(QMenu* menu) { inputMenu_ = menu; }

    // Open bins, in the order they opened. The container owns them.
    QList<BinDocument*> openDocuments() const;
    // The open bin with Sidebar id `id`; null if it is not open.
    BinDocument* document(const QString& id) const;
    BinDocument* activeDocument() const;
    // The bin last clicked or typed in, in any Bin Container; failing that,
    // the current tab of the first area that has one.
    BinView* activeView() const;
    // The view showing `document`, in any area; null if none.
    BinView* viewFor(const BinDocument* document) const;
    // Opens a bin, tabbed into the main area, and makes it active; an open
    // bin is brought to the front instead. Null if `id` is not a bin, is in
    // the Trash, or its file cannot be read. A bin on shared storage takes
    // its lock unless someone holds it; `takeLock` false opens it without
    // taking the lock (Alt+double-click, p.302).
    BinDocument* openBin(const QString& id, bool takeLock = true);
    // Saves the bin if it has changes, then closes it. Its view and document
    // are deleted later, from the event loop. If the save fails the bin stays
    // open with its changes: returns false, sets `error` and emits
    // saveFailed().
    bool closeBin(BinDocument* document, QString* error = nullptr);
    // Windows > Close All Bins But Active.
    void closeAllBinsButActive();
    // Saves one bin, whether or not it has changes.
    bool saveBin(BinDocument* document, QString* error = nullptr);
    // File > Save All: every open bin with changes, and the bin list.
    bool saveAll(QString* error = nullptr);

    // Arranging bins (p.241-243). `target` null means the area's current
    // group; floating makes a new Bin Container.
    void dockBin(BinDocument* document, BinTabGroup* target, BinDropSide side);
    // `geometry`: the new window's; by default the bin's size at the pointer.
    // A bin already alone in a floating container stays there.
    FloatingBinContainer* floatBin(BinDocument* document, const QRect& geometry = {});
    // The area showing `view`; null if none.
    BinDockArea* areaOf(const BinView* view) const;
    // The main area and those of floating Bin Containers.
    QList<BinDockArea*> areas() const;
    QList<FloatingBinContainer*> floatingContainers() const { return floating_; }
    BinDockArea* binArea() const { return area_; }
    // The main area's current group of tabs.
    BinTabGroup* tabs() const;

    // The Sidebar's entries. An empty `parentFolder` is the top level.
    // newBin() and newFolder() return the new id (empty on failure) and open
    // its name for typing; a new bin also opens.
    QString newBin(const QString& parentFolder = {});
    QString newFolder(const QString& parentFolder = {});
    // Saves the bin first if it is open with changes. Favorite Bins cannot be
    // renamed here.
    bool renameNode(const QString& id, const QString& name, QString* error = nullptr);
    // Moves a bin or folder to the Trash (see model::ProjectBins::remove),
    // closing what is open in it (refused if a bin there cannot be saved); a
    // Favorite Bin only leaves the favourites.
    // Entries already in the Trash, and the Trash itself, are refused.
    bool deleteNode(const QString& id, QString* error = nullptr);
    bool emptyTrash(QString* error = nullptr);
    // File > Open Bin of a bin outside the project: adds it under Other Bins
    // and opens it. Returns its id, empty on failure.
    QString openOtherBin(const QString& path, QString* error = nullptr);
    // Into `parentFolder` at `index` (-1: the end). Moving into the Trash
    // deletes.
    bool moveNode(const QString& id, const QString& parentFolder, int index, QString* error = nullptr);
    // In the Sidebar that has the focus, otherwise the main one.
    QStringList selectedNodes() const;
    bool isOpen(const QString& id) const { return document(id) != nullptr; }
    // A Sidebar entry: a project node, or a Favorite Bin.
    QString nodeName(const QString& id) const;
    QString nodePath(const QString& id) const;

    // Favorite Bins (p.293). addToFavorites() returns false if the bin is
    // already one; it saves the bin first.
    bool addToFavorites(BinDocument* document);
    void removeFromFavorites(const QString& path);

    // Hide Sidebar (Sidebar fast menu), Show Sidebar (right-click), and
    // Bin > Show Sidebar Only (p.243).
    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const;
    void setSidebarOnly(bool only);
    bool isSidebarOnly() const;
    // The main Sidebar's Flat View and filter.
    void setFlatView(bool flat);
    void setBinFilter(const QString& text);
    // Every Sidebar, so that all show changes.
    void refreshTree();
    // Every open bin's Bin View menu, after the saved views changed.
    void refreshViewMenus();
    // Fills `menu` (clearing it first) with the Sidebar's fast menu, or with
    // the right-click menu for entry `id`; an empty id is the list itself.
    // Returns `menu`.
    QMenu* sidebarMenu(QMenu* menu, const QString& id, BinSidebar* sidebar);

    // Bin contents, for the active bin.
    // Timeline > New > Sequence (p.457): into the active bin, or `target`
    // (opened if need be). Returns the new item's id, empty with no bin.
    QString newSequence(const QString& targetBin = {});
    // Edit > Duplicate (p.260). Returns the new ids, which become selected.
    QStringList duplicateSelection();
    // Edit > Delete (p.261). Clips have no media yet, so only the objects go.
    // Locked items stay, and the delete filter may keep more. Returns how many
    // went; with headings selected it deletes columns instead and returns 0.
    int deleteSelection();
    // Edit > Select All in the active bin.
    void selectAll();
    // Dragging items to another bin moves them; with Alt/Option or
    // Ctrl/Command it copies them (p.260-261, isCopyDrop() in bin_dock.h).
    // Locked items are copied even by a move: locking prevents deletion
    // (p.265). A closed bin opens with the items selected (p.243). Returns
    // the ids now in the target.
    QStringList transferItems(const QString& fromBin, const QStringList& ids, const QString& toBin, bool copy);

    BinSidebar* sidebar() const { return sidebar_; }
    QTreeView* tree() const;
    BinTreeModel* treeModel() const;
    QList<BinSidebar*> sidebars() const { return sidebars_; }

    // Bin settings "Auto-Save interval" (p.299), in minutes; 0 or less turns
    // it off. Each tick is a Save All.
    void setAutoSaveMinutes(int minutes);
    // Bin settings (p.1153): where Favorite Bins show, whether bins opened
    // from the sidebar float, and what the quick filter searches.
    void setFavoritesAtTop(bool top);
    void setOpenBinsFloating(bool floating) { openFloating_ = floating; }
    bool opensBinsFloating() const { return openFloating_; }
    void setQuickFilterAllColumns(bool all);
    // A bin the user opens or creates: tabbed or floating as the Bin
    // settings say; Ctrl (Command) inverts that (p.1153). `takeLock` as for
    // openBin().
    BinDocument* openBinAsSet(const QString& id, bool takeLock = true);
    // General settings "Default Starting TC" (p.1192) for new sequences.
    void setDefaultStartTimecode(const QString& tc) { startTimecode_ = tc; }
    const QString& defaultStartTimecode() const { return startTimecode_; }
    // New sequences sync-lock all their tracks (Timeline settings, Default Sync Locks On, p.1220).
    void setDefaultSyncLocks(bool on) { defaultSyncLocks_ = on; }

    // Shared-storage bin locking (p.300-304, docs/spec/bin-locking.md). The
    // user whose locks these are (the User profile, p.1138), on this machine.
    void setLockUser(const QString& user);
    const model::BinLockHolder& lockUser() const { return lockUser_; }
    // The Bin Lock Status button (p.301-303): yellow reads the bin again and
    // turns red, blue takes the lock and turns green, Alt on green releases
    // the lock; otherwise it shows the bin's history (showLockHistory()).
    void lockButtonClicked(BinDocument* document, bool alt);
    // A window listing who saved the bin and when (p.301). Not modal; it is
    // named "binLockHistory".
    QWidget* showLockHistory(BinDocument* document);
    // Lock Project Bin and Protect Project Bin (`kind` Locked or Protected)
    // and Unlock Project Bin (p.302), for Sidebar entries; bins not on shared
    // storage, and bins someone else holds, are refused with `error`.
    bool lockProjectBins(const QStringList& ids, model::BinLockKind kind, QString* error = nullptr);
    bool unlockProjectBins(const QStringList& ids, QString* error = nullptr);
    // Who holds a bin's lock, as last read; nothing when it has none or is
    // not on shared storage.
    std::optional<model::BinLockHolder> lockOf(const QString& id) const;
    // Bins locked by another user are in bold in the Bin Container (p.301).
    bool isLockedByOther(const QString& id) const;
    // Reads the locks and files again: of the open bins (their buttons) and
    // of the Sidebar's bins, and the bin list when someone else changed it.
    // A timer does it every two seconds while the project is shared.
    void pollLocks();

    // Called with each bin read from its file, before it opens: the media
    // database points managed clips at their files (p.358).
    void setBinLoader(std::function<void(model::Bin&)> loader) { binLoader_ = std::move(loader); }
    // Asked before Delete removes items: which of them to remove; nothing
    // cancels (managed clips' media, p.359).
    using DeleteFilter = std::function<std::optional<QStringList>(const BinDocument&, const QStringList& ids)>;
    void setDeleteFilter(DeleteFilter filter) { deleteFilter_ = std::move(filter); }

signals:
    // The active bin changed, a bin closed, or the project changed.
    void activeBinChanged();
    // Something that needs saving, or was saved.
    void dirtyChanged();
    // An item double-clicked in a bin, to load into a monitor (p.394).
    void itemActivated(const QString& binId, const QString& itemId);
    // Media files dropped on a bin from the Source Browser, to link (p.317).
    void filesDropped(const QString& binId, const QStringList& paths);
    // A bin floated into a new Bin Container window.
    void floatingContainerCreated(QWidget* window);
    // A bin could not be saved: on close (it stays open), on auto-save or
    // as the project closed. `bin` is its name, empty when several bins were
    // being saved; `error` says why. MainWindow shows it to the user.
    void saveFailed(const QString& bin, const QString& error);
    // A change to a bin someone else holds was refused (p.304); MainWindow
    // says so in the Console.
    void lockRefused(const QString& binId);
    // The user's Favorite Bins list (FavoriteBins::paths()) or saved bin
    // views (BinViewLibrary) changed here; MainWindow stores them in the
    // User settings.
    void favoritesChanged();
    void binViewsChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class BinSidebar;
    friend class FloatingBinContainer;
    void saveTree();
    void updateTab(BinDocument* document);
    QString folderForNew(const QString& id) const;
    void connectArea(BinDockArea* area);
    void placeView(BinView* view, BinDockArea* area, BinTabGroup* target, BinDropSide side);
    void dragTab(BinView* view);
    void dropNodes(const QStringList& ids, BinDockArea* area, const BinDockArea::Zone& zone);
    void setActiveView(BinView* view);
    void removeEmptyFloating();
    void registerSidebar(BinSidebar* sidebar);
    QString canonicalBin(const QString& id) const;

    // Takes (or, with `take` false, only reads) a shared bin's lock as it
    // opens; releases it as it closes.
    void lockOnOpen(BinDocument* document, bool take);
    void releaseOnClose(BinDocument* document);
    // The project's folder is on shared storage (model::isSharedLocation).
    bool projectShared() const { return projectShared_; }
    // A bin's lock in its Sidebar's terms, read again into locks_.
    void readLock(const QString& id);
    // Rename, move and delete are refused for a bin someone else holds (p.304).
    bool refuseLocked(const QString& id, QString* error) const;

    std::function<void(model::Bin&)> binLoader_;
    model::BinLockHolder lockUser_;
    QTimer* lockTimer_ = nullptr;
    QHash<QString, std::optional<model::BinLockHolder>> locks_; // Sidebar bin id -> its lock
    QHash<const BinDocument*, QDateTime> readAt_;               // when each open shared bin was read
    QDateTime treeReadAt_;                                      // the bin list's file, as last read or written
    bool projectShared_ = false;
    DeleteFilter deleteFilter_;
    std::unique_ptr<model::ProjectBins> bins_;
    Rational editRate_{24000, 1001};
    bool openFloating_ = false;
    QString startTimecode_;
    bool defaultSyncLocks_ = false;
    QList<BinDocument*> documents_;
    QMenu* binMenu_ = nullptr;
    QMenu* inputMenu_ = nullptr;
    QSplitter* split_ = nullptr;
    BinSidebar* sidebar_ = nullptr;
    QList<BinSidebar*> sidebars_;
    BinDockArea* area_ = nullptr;
    QList<FloatingBinContainer*> floating_;
    BinView* active_ = nullptr;
    QTimer* autoSave_ = nullptr;
    bool sized_ = false;
};

} // namespace lumenis::ui

#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/ui/bin_dock.h"

#include <QAbstractItemModel>
#include <QList>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QLineEdit;
class QMenu;
class QSettings;
class QSplitter;
class QTimer;
class QTreeView;

namespace lumenis::ui {

class BinContainer;
class BinDocument;
class BinView;

// Favorite Bins (guide p.293): bins the user reaches from any project. The
// list belongs to the user, not to a project, and is kept in the user's
// settings, as is whether its folder shows at the top or the bottom of the
// Sidebar ("Favorite Bins show at", Bin settings).
class FavoriteBins {
public:
    static QStringList& paths();
    static bool& atTop();
    static void load(QSettings& settings);
    static void save(QSettings& settings);

    // Sidebar ids: the folder, and one per bin.
    static QString folderId() { return QStringLiteral("favorites"); }
    static QString idFor(const QString& path) { return QStringLiteral("favorite:") + path; }
    static bool isFavoriteId(const QString& id) { return id.startsWith(QLatin1String("favorite:")); }
    static QString pathOf(const QString& id) { return id.mid(9); }
};

// The Bin Container Sidebar's list (guide p.294-299): bins, folders, the
// Trash, Other Bins and Favorite Bins, or a flat list of bins when filtering
// or in Flat View. Entries can be renamed in place and dragged into
// folders; bin items dropped on a bin move into it (p.243).
class BinTreeModel : public QAbstractItemModel {
    Q_OBJECT

public:
    static constexpr int kIdRole = Qt::UserRole + 1;
    static constexpr int kNameColumn = 0;
    static constexpr int kSizeColumn = 1;

    explicit BinTreeModel(BinContainer& container, QObject* parent = nullptr);

    void rebuild();
    void setFlat(bool flat);
    bool isFlat() const { return flat_; }
    void setFilter(const QString& text);
    QModelIndex indexOf(const QString& id) const;
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

    // Rebuilds the list, keeping folders open.
    void refresh();
    QStringList selectedNodes() const;
    void setFlatView(bool flat);
    void setFilter(const QString& text);
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
// left, open bins on the right, tabbed or side by side.
class BinContainer : public QWidget {
    Q_OBJECT

public:
    explicit BinContainer(QWidget* parent = nullptr);
    ~BinContainer() override;

    // Shows a project's bins and reopens the bins that were open when it was
    // last closed. A null folder closes the project (saving open bins).
    void setProject(const QString& folder, const QString& name, const Rational& editRate);
    bool hasProject() const { return bins_ != nullptr; }
    model::ProjectBins* bins() const { return bins_.get(); }

    // The Bin menu, offered as each bin's fast menu (p.259).
    void setBinMenu(QMenu* menu) { binMenu_ = menu; }

    // Open bins.
    QList<BinDocument*> openDocuments() const;
    BinDocument* document(const QString& id) const;
    BinDocument* activeDocument() const;
    // The bin last clicked or typed in, in any Bin Container.
    BinView* activeView() const;
    BinView* viewFor(const BinDocument* document) const;
    BinDocument* openBin(const QString& id);
    void closeBin(BinDocument* document);
    // Windows > Close All Bins But Active.
    void closeAllBinsButActive();
    bool saveBin(BinDocument* document, QString* error = nullptr);
    // File > Save All: every open bin with changes, and the bin list.
    bool saveAll(QString* error = nullptr);

    // Arranging bins (p.241-243). `target` null means the area's current
    // group; floating makes a new Bin Container.
    void dockBin(BinDocument* document, BinTabGroup* target, BinDropSide side);
    FloatingBinContainer* floatBin(BinDocument* document, const QRect& geometry = {});
    BinDockArea* areaOf(const BinView* view) const;
    // The main area and those of floating Bin Containers.
    QList<BinDockArea*> areas() const;
    QList<FloatingBinContainer*> floatingContainers() const { return floating_; }
    BinDockArea* binArea() const { return area_; }
    // The main area's current group of tabs.
    BinTabGroup* tabs() const;

    // The Sidebar's entries.
    QString newBin(const QString& parentFolder = {});
    QString newFolder(const QString& parentFolder = {});
    bool renameNode(const QString& id, const QString& name, QString* error = nullptr);
    bool deleteNode(const QString& id, QString* error = nullptr);
    bool emptyTrash(QString* error = nullptr);
    QString openOtherBin(const QString& path, QString* error = nullptr);
    bool moveNode(const QString& id, const QString& parentFolder, int index, QString* error = nullptr);
    QStringList selectedNodes() const;
    bool isOpen(const QString& id) const { return document(id) != nullptr; }
    // A Sidebar entry: a project node, or a Favorite Bin.
    QString nodeName(const QString& id) const;
    QString nodePath(const QString& id) const;

    // Favorite Bins (p.293).
    bool addToFavorites(BinDocument* document);
    void removeFromFavorites(const QString& path);

    // Hide Sidebar (Sidebar fast menu), Show Sidebar (right-click), and
    // Bin > Show Sidebar Only (p.243).
    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const;
    void setSidebarOnly(bool only);
    bool isSidebarOnly() const;
    void setFlatView(bool flat);
    void setBinFilter(const QString& text);
    // Every Sidebar, so that all show changes.
    void refreshTree();
    QMenu* sidebarMenu(QMenu* menu, const QString& id, BinSidebar* sidebar);

    // Bin contents, for the active bin.
    // Timeline > New > Sequence (p.457): into the active bin, or `target`.
    QString newSequence(const QString& targetBin = {});
    // Edit > Duplicate (p.260).
    QStringList duplicateSelection();
    // Edit > Delete (p.261). Clips have no media yet, so only the objects go.
    int deleteSelection();
    void selectAll();
    // Dragging items to another bin moves them; with Alt/Option (or Ctrl on
    // Windows) it copies them (p.260-261). A closed bin opens with the items
    // selected (p.243). Returns the ids now in the target.
    QStringList transferItems(const QString& fromBin, const QStringList& ids, const QString& toBin, bool copy);

    BinSidebar* sidebar() const { return sidebar_; }
    QTreeView* tree() const;
    BinTreeModel* treeModel() const;
    QList<BinSidebar*> sidebars() const { return sidebars_; }

    // Bin settings "Auto-Save interval" (p.299), in minutes; 0 turns it off.
    void setAutoSaveMinutes(int minutes);

signals:
    void activeBinChanged();
    // Something that needs saving, or was saved.
    void dirtyChanged();
    // A bin floated into a new Bin Container window.
    void floatingContainerCreated(QWidget* window);

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

    std::unique_ptr<model::ProjectBins> bins_;
    Rational editRate_{24000, 1001};
    QList<BinDocument*> documents_;
    QMenu* binMenu_ = nullptr;
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

#pragma once

#include "lumenis/core/rational.h"
#include "lumenis/model/project_bins.h"

#include <QAbstractItemModel>
#include <QList>
#include <QWidget>

#include <memory>
#include <vector>

class QLineEdit;
class QMenu;
class QSplitter;
class QTabWidget;
class QTimer;
class QTreeView;

namespace lumenis::ui {

class BinContainer;
class BinDocument;
class BinView;

// The Bin Container Sidebar's list (guide p.294-299): bins, folders, the
// Trash and Other Bins, or a flat list of bins when filtering or in Flat
// View. Entries can be renamed in place and dragged into folders.
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
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;

private:
    struct Row {
        QString id;
        Row* parent = nullptr;
        std::vector<std::unique_ptr<Row>> children;
    };
    void build(const QList<model::BinNode>& nodes, Row* into);
    Row* rowFor(const QModelIndex& index) const;
    int rowIndex(const Row* row) const;

    BinContainer& container_;
    Row root_;
    bool flat_ = false;
    QString filter_;
};

// The Bin Container (p.239-243): the Sidebar with the project's bins on the
// left, open bins as tabs on the right.
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
    BinView* activeView() const;
    BinView* viewFor(const BinDocument* document) const;
    BinDocument* openBin(const QString& id);
    void closeBin(BinDocument* document);
    // Windows > Close All Bins But Active.
    void closeAllBinsButActive();
    bool saveBin(BinDocument* document, QString* error = nullptr);
    // File > Save All: every open bin with changes, and the bin list.
    bool saveAll(QString* error = nullptr);

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

    // Hide Sidebar (Sidebar fast menu), Show Sidebar (right-click), and
    // Bin > Show Sidebar Only (p.243).
    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const;
    void setSidebarOnly(bool only);
    bool isSidebarOnly() const;
    void setFlatView(bool flat);
    void setBinFilter(const QString& text);

    // Bin contents, for the active bin.
    // Timeline > New > Sequence (p.457): into the active bin, or `target`.
    QString newSequence(const QString& targetBin = {});
    // Edit > Duplicate (p.260).
    QStringList duplicateSelection();
    // Edit > Delete (p.261). Clips have no media yet, so only the objects go.
    int deleteSelection();
    void selectAll();

    QTreeView* tree() const { return tree_; }
    QTabWidget* tabs() const { return tabs_; }
    BinTreeModel* treeModel() const { return model_; }

    // Bin settings "Auto-Save interval" (p.299), in minutes; 0 turns it off.
    void setAutoSaveMinutes(int minutes);

signals:
    void activeBinChanged();
    // Something that needs saving, or was saved.
    void dirtyChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void saveTree();
    void refreshTree();
    void updateTab(BinDocument* document);
    void showTreeMenu(const QPoint& pos);
    QMenu* sidebarMenu(QMenu* menu, const QString& id);
    QString folderForNew(const QString& id) const;

    std::unique_ptr<model::ProjectBins> bins_;
    Rational editRate_{24000, 1001};
    QList<BinDocument*> documents_;
    QMenu* binMenu_ = nullptr;
    QSplitter* split_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QLineEdit* binFilter_ = nullptr;
    QTreeView* tree_ = nullptr;
    BinTreeModel* model_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTimer* autoSave_ = nullptr;
    bool sized_ = false;
};

} // namespace lumenis::ui

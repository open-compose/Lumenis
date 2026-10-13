#include "lumenis/ui/bin_container.h"

#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPalette>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace lumenis::ui {

namespace {

const QString kNodeMime = QStringLiteral("application/x-cutthulhu-bin-nodes");
// The Sidebar's share of the Bin Container, from the picture on p.74.
constexpr int kSidebarPercent = 36;
constexpr int kMinimumSidebar = 80;
constexpr int kMinimumBins = 160;

// Moves bins and folders by dragging in the Sidebar (p.242, p.297).
class SidebarTree : public QTreeView {
public:
    using DeleteHandler = std::function<void()>;
    explicit SidebarTree(QWidget* parent) : QTreeView(parent) {}
    DeleteHandler onDelete;

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && state() != EditingState) {
            if (onDelete) onDelete();
            return;
        }
        QTreeView::keyPressEvent(event);
    }
};

} // namespace

// ---------------------------------------------------------------- BinTreeModel

BinTreeModel::BinTreeModel(BinContainer& container, QObject* parent) : QAbstractItemModel(parent), container_(container) {}

void BinTreeModel::build(const QList<model::BinNode>& nodes, Row* into) {
    for (const model::BinNode& n : nodes) {
        auto row = std::make_unique<Row>();
        row->id = n.id;
        row->parent = into;
        if (n.isContainer()) build(n.children, row.get());
        into->children.push_back(std::move(row));
    }
}

void BinTreeModel::rebuild() {
    beginResetModel();
    root_.children.clear();
    if (const model::ProjectBins* bins = container_.bins()) {
        if (flat_ || !filter_.isEmpty()) {
            // A flat list of matching bins (p.297, p.299).
            for (const model::BinNode* b : bins->bins()) {
                if (!filter_.isEmpty() && !b->name.contains(filter_, Qt::CaseInsensitive)) continue;
                auto row = std::make_unique<Row>();
                row->id = b->id;
                row->parent = &root_;
                root_.children.push_back(std::move(row));
            }
        } else {
            build(bins->nodes(), &root_);
        }
    }
    endResetModel();
}

void BinTreeModel::setFlat(bool flat) {
    if (flat_ == flat) return;
    flat_ = flat;
    rebuild();
}

void BinTreeModel::setFilter(const QString& text) {
    if (filter_ == text) return;
    filter_ = text;
    rebuild();
}

BinTreeModel::Row* BinTreeModel::rowFor(const QModelIndex& index) const {
    return index.isValid() ? static_cast<Row*>(index.internalPointer()) : const_cast<Row*>(&root_);
}

int BinTreeModel::rowIndex(const Row* row) const {
    const Row* parent = row->parent;
    for (std::size_t i = 0; i < parent->children.size(); ++i) {
        if (parent->children[i].get() == row) return static_cast<int>(i);
    }
    return -1;
}

QModelIndex BinTreeModel::index(int row, int column, const QModelIndex& parent) const {
    const Row* p = rowFor(parent);
    if (row < 0 || column < 0 || column >= columnCount() || row >= static_cast<int>(p->children.size())) return {};
    return createIndex(row, column, p->children[static_cast<std::size_t>(row)].get());
}

QModelIndex BinTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) return {};
    const Row* r = rowFor(child);
    if (!r->parent || r->parent == &root_) return {};
    return createIndex(rowIndex(r->parent), 0, r->parent);
}

int BinTreeModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() && parent.column() != 0) return 0;
    return static_cast<int>(rowFor(parent)->children.size());
}

int BinTreeModel::columnCount(const QModelIndex&) const {
    return 2;
}

QModelIndex BinTreeModel::indexOf(const QString& id) const {
    std::function<QModelIndex(const Row*, const QModelIndex&)> search = [&](const Row* r, const QModelIndex& parent) {
        for (std::size_t i = 0; i < r->children.size(); ++i) {
            const QModelIndex idx = index(static_cast<int>(i), 0, parent);
            if (r->children[i]->id == id) return idx;
            if (QModelIndex found = search(r->children[i].get(), idx); found.isValid()) return found;
        }
        return QModelIndex();
    };
    return search(&root_, {});
}

QString BinTreeModel::idOf(const QModelIndex& index) const {
    return index.isValid() ? rowFor(index)->id : QString();
}

QVariant BinTreeModel::data(const QModelIndex& index, int role) const {
    const model::ProjectBins* bins = container_.bins();
    if (!index.isValid() || !bins) return {};
    const model::BinNode* n = bins->node(rowFor(index)->id);
    if (!n) return {};
    using Kind = model::BinNode::Kind;
    if (role == kIdRole) return n->id;
    if (index.column() == kSizeColumn) {
        if (role == Qt::TextAlignmentRole) return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (role != Qt::DisplayRole || n->kind != Kind::Bin) return {};
        // Bin sizes in kilobytes, as in the pictures on p.240.
        const qint64 bytes = QFileInfo(bins->binPath(n->id)).size();
        return QStringLiteral("%1K").arg((bytes + 1023) / 1024);
    }
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return n->name;
    case Qt::DecorationRole:
        switch (n->kind) {
        case Kind::Bin: return glyphIcon(GlyphIcon::Bin);
        case Kind::Folder:
        case Kind::OtherBins: return glyphIcon(GlyphIcon::Folder);
        case Kind::Trash: return glyphIcon(GlyphIcon::Trash);
        }
        return {};
    case Qt::FontRole:
        // Bins from other projects, and Other Bins, are in italic (p.294, p.296).
        if (n->external || n->kind == Kind::OtherBins) {
            QFont f;
            f.setItalic(true);
            return f;
        }
        return {};
    case Qt::ForegroundRole:
        // "The Bin icon appears dimmed in the Bins list, indicating the bin
        // is open" (p.295).
        if (n->kind == Kind::Bin && container_.isOpen(n->id)) {
            return QPalette().color(QPalette::Disabled, QPalette::Text);
        }
        return {};
    default:
        return {};
    }
}

bool BinTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid()) return false;
    return container_.renameNode(rowFor(index)->id, value.toString());
}

QVariant BinTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    return section == kNameColumn ? tr("Name") : tr("Size");
}

Qt::ItemFlags BinTreeModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractItemModel::flags(index);
    const model::ProjectBins* bins = container_.bins();
    if (!index.isValid() || !bins) return f | Qt::ItemIsDropEnabled;
    const model::BinNode* n = bins->node(rowFor(index)->id);
    if (!n) return f;
    using Kind = model::BinNode::Kind;
    if (n->kind != Kind::Trash && index.column() == kNameColumn) f |= Qt::ItemIsEditable;
    if (n->kind == Kind::Bin || n->kind == Kind::Folder) f |= Qt::ItemIsDragEnabled;
    if (n->isContainer() && !flat_) f |= Qt::ItemIsDropEnabled;
    return f;
}

Qt::DropActions BinTreeModel::supportedDropActions() const {
    return Qt::MoveAction;
}

QStringList BinTreeModel::mimeTypes() const {
    return {kNodeMime};
}

QMimeData* BinTreeModel::mimeData(const QModelIndexList& indexes) const {
    QStringList ids;
    for (const QModelIndex& i : indexes) {
        if (i.column() == kNameColumn) ids << rowFor(i)->id;
    }
    auto* data = new QMimeData;
    data->setData(kNodeMime, ids.join(QLatin1Char('\n')).toUtf8());
    return data;
}

bool BinTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int, const QModelIndex& parent) {
    if (action != Qt::MoveAction || !data->hasFormat(kNodeMime) || flat_) return false;
    const QString parentId = idOf(parent);
    const QStringList ids = QString::fromUtf8(data->data(kNodeMime)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bool moved = false;
    for (const QString& id : ids) {
        moved = container_.moveNode(id, parentId, row) || moved;
        if (row >= 0) ++row;
    }
    return moved;
}

// ---------------------------------------------------------------- BinContainer

BinContainer::BinContainer(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("binContainer"));

    // Sidebar: quick filter, fast menu, and the list (p.240, p.299).
    sidebar_ = new QWidget(this);
    sidebar_->setObjectName(QStringLiteral("binSidebar"));
    binFilter_ = new QLineEdit(sidebar_);
    binFilter_->setObjectName(QStringLiteral("binFilter"));
    binFilter_->setPlaceholderText(tr("Filter bins"));
    binFilter_->setClearButtonEnabled(true);
    binFilter_->addAction(glyphIcon(GlyphIcon::Search), QLineEdit::LeadingPosition);
    connect(binFilter_, &QLineEdit::textChanged, this, &BinContainer::setBinFilter);
    auto* fast = new QToolButton(sidebar_);
    fast->setObjectName(QStringLiteral("sidebarFastMenu"));
    fast->setIcon(glyphIcon(GlyphIcon::FastMenu));
    fast->setToolTip(tr("Sidebar Fast Menu"));
    fast->setAutoRaise(true);
    fast->setFixedSize(26, kToolButtonHeight);
    fast->setPopupMode(QToolButton::InstantPopup);
    auto* fastMenu = new QMenu(fast);
    connect(fastMenu, &QMenu::aboutToShow, this, [this, fastMenu] { sidebarMenu(fastMenu, {}); });
    fast->setMenu(fastMenu);

    model_ = new BinTreeModel(*this, this);
    auto* tree = new SidebarTree(sidebar_);
    tree_ = tree;
    tree_->setObjectName(QStringLiteral("binTree"));
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setIndentation(14);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(16, 16));
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(BinTreeModel::kNameColumn, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(BinTreeModel::kSizeColumn, QHeaderView::Fixed);
    tree_->header()->resizeSection(BinTreeModel::kSizeColumn, 52);
    tree->onDelete = [this] {
        for (const QString& id : selectedNodes()) deleteNode(id);
    };
    connect(tree_, &QTreeView::customContextMenuRequested, this, &BinContainer::showTreeMenu);
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        const QString id = model_->idOf(index);
        if (bins_ && bins_->node(id) && bins_->node(id)->kind == model::BinNode::Kind::Bin) openBin(id);
    });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(4, 3, 2, 3);
    header->setSpacing(2);
    header->addWidget(binFilter_, 1);
    header->addWidget(fast);
    auto* sideLayout = new QVBoxLayout(sidebar_);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);
    sideLayout->addLayout(header);
    sideLayout->addWidget(tree_, 1);

    // Open bins, as tabs (p.258).
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("binTabs"));
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    tabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    auto* tabMenuButton = new QToolButton(tabs_);
    tabMenuButton->setObjectName(QStringLiteral("binTabMenu"));
    tabMenuButton->setIcon(glyphIcon(GlyphIcon::FastMenu));
    tabMenuButton->setToolTip(tr("Tab Menu"));
    tabMenuButton->setAutoRaise(true);
    tabMenuButton->setFixedSize(26, kToolButtonHeight);
    tabMenuButton->setPopupMode(QToolButton::InstantPopup);
    auto* tabMenu = new QMenu(tabMenuButton);
    connect(tabMenu, &QMenu::aboutToShow, this, [this, tabMenu] {
        // Every open bin, including those whose tabs do not fit (p.259).
        tabMenu->clear();
        for (int i = 0; i < tabs_->count(); ++i) {
            QAction* a = tabMenu->addAction(tabs_->tabText(i));
            a->setCheckable(true);
            a->setChecked(i == tabs_->currentIndex());
            connect(a, &QAction::triggered, this, [this, i] { tabs_->setCurrentIndex(i); });
        }
    });
    tabMenuButton->setMenu(tabMenu);
    tabs_->setCornerWidget(tabMenuButton, Qt::TopRightCorner);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int i) {
        if (auto* view = qobject_cast<BinView*>(tabs_->widget(i))) closeBin(&view->document());
    });
    connect(tabs_, &QTabWidget::currentChanged, this, &BinContainer::activeBinChanged);
    connect(tabs_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        QAction* show = menu.addAction(tr("Show Sidebar"));
        show->setEnabled(!isSidebarVisible());
        connect(show, &QAction::triggered, this, [this] { setSidebarVisible(true); });
        menu.exec(tabs_->mapToGlobal(pos));
    });

    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setObjectName(QStringLiteral("binSplit"));
    split_->setChildrenCollapsible(false);
    split_->setHandleWidth(kSplitterHandle);
    split_->addWidget(sidebar_);
    split_->addWidget(tabs_);
    // Explicit minimums: otherwise they come from the widgets' contents,
    // which each Qt version measures differently, and a minimum larger than
    // the panel's share overrides the layout's proportions.
    sidebar_->setMinimumWidth(kMinimumSidebar);
    tabs_->setMinimumWidth(kMinimumBins);
    split_->setStretchFactor(1, 1);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split_);

    autoSave_ = new QTimer(this);
    connect(autoSave_, &QTimer::timeout, this, [this] { saveAll(); });
    // The guide does not give the default interval; ours is 15 minutes.
    setAutoSaveMinutes(15);
}

BinContainer::~BinContainer() {
    // Unsaved changes are not lost when the window goes.
    if (bins_) saveAll();
}

void BinContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!sized_ && width() > 1) {
        sized_ = true;
        const int side = width() * kSidebarPercent / 100;
        split_->setSizes({side, width() - side});
    }
}

void BinContainer::setAutoSaveMinutes(int minutes) {
    if (minutes <= 0) {
        autoSave_->stop();
    } else {
        autoSave_->start(minutes * 60 * 1000);
    }
}

void BinContainer::setProject(const QString& folder, const QString& name, const Rational& editRate) {
    if (bins_) {
        saveAll();
        while (!documents_.isEmpty()) {
            BinDocument* d = documents_.takeLast();
            if (QWidget* v = viewFor(d)) {
                tabs_->removeTab(tabs_->indexOf(v));
                delete v;
            }
            delete d;
        }
        bins_.reset();
    }
    editRate_ = editRate;
    if (!folder.isEmpty()) {
        bins_ = std::make_unique<model::ProjectBins>(folder, name);
        bins_->load();
        const QStringList open = bins_->openBins;
        model_->rebuild();
        tree_->expandAll();
        for (const QString& id : open) openBin(id);
        // A project's first session opens its bin.
        if (open.isEmpty() && bins_->bins().size() == 1) openBin(bins_->bins().first()->id);
    } else {
        model_->rebuild();
    }
    emit activeBinChanged();
}

void BinContainer::saveTree() {
    if (!bins_) return;
    bins_->openBins.clear();
    for (int i = 0; i < tabs_->count(); ++i) {
        if (auto* v = qobject_cast<BinView*>(tabs_->widget(i))) bins_->openBins << v->document().nodeId();
    }
    bins_->save();
}

void BinContainer::refreshTree() {
    QStringList expanded;
    std::function<void(const QModelIndex&)> collect = [&](const QModelIndex& parent) {
        for (int r = 0; r < model_->rowCount(parent); ++r) {
            const QModelIndex i = model_->index(r, 0, parent);
            if (tree_->isExpanded(i)) expanded << model_->idOf(i);
            collect(i);
        }
    };
    collect({});
    const bool first = model_->rowCount() == 0;
    model_->rebuild();
    if (first) {
        tree_->expandAll();
        return;
    }
    for (const QString& id : expanded) tree_->setExpanded(model_->indexOf(id), true);
}

QList<BinDocument*> BinContainer::openDocuments() const {
    return documents_;
}

BinDocument* BinContainer::document(const QString& id) const {
    for (BinDocument* d : documents_) {
        if (d->nodeId() == id) return d;
    }
    return nullptr;
}

BinView* BinContainer::viewFor(const BinDocument* document) const {
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* v = qobject_cast<BinView*>(tabs_->widget(i));
        if (v && &v->document() == document) return v;
    }
    return nullptr;
}

BinView* BinContainer::activeView() const {
    return qobject_cast<BinView*>(tabs_->currentWidget());
}

BinDocument* BinContainer::activeDocument() const {
    BinView* v = activeView();
    return v ? &v->document() : nullptr;
}

void BinContainer::updateTab(BinDocument* document) {
    if (BinView* v = viewFor(document)) {
        // "an asterisk appears before the bin name" (p.258).
        tabs_->setTabText(tabs_->indexOf(v), (document->isDirty() ? QStringLiteral("* ") : QString()) + document->name());
    }
}

BinDocument* BinContainer::openBin(const QString& id) {
    if (!bins_) return nullptr;
    if (BinDocument* d = document(id)) {
        tabs_->setCurrentWidget(viewFor(d));
        return d;
    }
    const model::BinNode* node = bins_->node(id);
    if (!node || node->kind != model::BinNode::Kind::Bin || bins_->isInTrash(id)) return nullptr;
    const QString path = bins_->binPath(id);
    auto bin = model::BinFile::read(path);
    if (!bin) return nullptr;
    bin->name = node->name;
    auto* doc = new BinDocument(id, path, *bin, this);
    documents_ << doc;
    auto* view = new BinView(*doc, binMenu_, tabs_);
    connect(doc, &BinDocument::dirtyChanged, this, [this, doc] {
        updateTab(doc);
        emit dirtyChanged();
    });
    connect(doc, &BinDocument::displayChanged, this, [this, doc] { updateTab(doc); });
    connect(view, &BinView::deleteRequested, this, [this] { deleteSelection(); });
    tabs_->addTab(view, doc->name());
    tabs_->setCurrentWidget(view);
    updateTab(doc);
    saveTree();
    refreshTree();
    return doc;
}

void BinContainer::closeBin(BinDocument* document) {
    if (!document || !documents_.contains(document)) return;
    if (document->isDirty()) document->save();
    if (BinView* v = viewFor(document)) {
        tabs_->removeTab(tabs_->indexOf(v));
        v->deleteLater();
    }
    documents_.removeAll(document);
    document->deleteLater();
    saveTree();
    refreshTree();
    emit activeBinChanged();
}

void BinContainer::closeAllBinsButActive() {
    BinDocument* active = activeDocument();
    for (BinDocument* d : QList<BinDocument*>(documents_)) {
        if (d != active) closeBin(d);
    }
}

bool BinContainer::saveBin(BinDocument* document, QString* error) {
    if (!document) return false;
    const bool ok = document->save(error);
    model_->dataChanged(model_->index(0, 0), model_->index(model_->rowCount() - 1, 1));
    refreshTree(); // sizes
    return ok;
}

bool BinContainer::saveAll(QString* error) {
    bool ok = true;
    for (BinDocument* d : documents_) {
        if (d->isDirty()) ok = d->save(error) && ok;
    }
    saveTree();
    if (model_) refreshTree();
    return ok;
}

QString BinContainer::folderForNew(const QString& id) const {
    if (!bins_ || id.isEmpty()) return {};
    const model::BinNode* n = bins_->node(id);
    if (!n || bins_->isInTrash(id) || n->kind == model::BinNode::Kind::Trash) return {};
    if (n->kind == model::BinNode::Kind::Folder) return id;
    const model::BinNode* parent = bins_->parentOf(id);
    return parent && parent->kind == model::BinNode::Kind::Folder ? parent->id : QString();
}

QString BinContainer::newBin(const QString& parentFolder) {
    if (!bins_) return {};
    const QString id = bins_->createBin(parentFolder);
    if (id.isEmpty()) return {};
    saveTree();
    refreshTree();
    if (!parentFolder.isEmpty()) tree_->setExpanded(model_->indexOf(parentFolder), true);
    // "A new (empty) bin opens ... with a default name highlighted" (p.295).
    openBin(id);
    const QModelIndex index = model_->indexOf(id);
    tree_->setCurrentIndex(index);
    if (isVisible()) tree_->edit(index);
    return id;
}

QString BinContainer::newFolder(const QString& parentFolder) {
    if (!bins_) return {};
    const QString id = bins_->createFolder(parentFolder);
    if (id.isEmpty()) return {};
    saveTree();
    refreshTree();
    if (!parentFolder.isEmpty()) tree_->setExpanded(model_->indexOf(parentFolder), true);
    const QModelIndex index = model_->indexOf(id);
    tree_->setCurrentIndex(index);
    if (isVisible()) tree_->edit(index);
    return id;
}

bool BinContainer::renameNode(const QString& id, const QString& name, QString* error) {
    if (!bins_) return false;
    BinDocument* doc = document(id);
    if (doc && doc->isDirty() && !doc->save(error)) return false;
    if (!bins_->rename(id, name, error)) return false;
    if (doc) {
        doc->setPath(bins_->binPath(id));
        doc->setName(bins_->node(id)->name);
        updateTab(doc);
    }
    saveTree();
    QTimer::singleShot(0, this, [this] { refreshTree(); });
    return true;
}

bool BinContainer::deleteNode(const QString& id, QString* error) {
    if (!bins_) return false;
    const model::BinNode* n = bins_->node(id);
    if (!n || n->kind == model::BinNode::Kind::Trash || bins_->isInTrash(id)) return false;
    // Close what is open inside it first.
    std::function<void(const model::BinNode&)> closeOpen = [&](const model::BinNode& node) {
        if (BinDocument* d = document(node.id)) closeBin(d);
        for (const model::BinNode& c : node.children) closeOpen(c);
    };
    closeOpen(*n);
    if (!bins_->remove(id, error)) return false;
    saveTree();
    refreshTree();
    return true;
}

bool BinContainer::emptyTrash(QString* error) {
    if (!bins_) return false;
    const bool ok = bins_->emptyTrash(error);
    saveTree();
    refreshTree();
    return ok;
}

QString BinContainer::openOtherBin(const QString& path, QString* error) {
    if (!bins_) return {};
    const QString id = bins_->addOtherBin(path, error);
    if (id.isEmpty()) return {};
    saveTree();
    refreshTree();
    openBin(id);
    return id;
}

bool BinContainer::moveNode(const QString& id, const QString& parentFolder, int index, QString* error) {
    if (!bins_) return false;
    const model::BinNode* target = bins_->node(parentFolder);
    bool ok = false;
    if (target && target->kind == model::BinNode::Kind::Trash) {
        ok = deleteNode(id, error); // dropping into the Trash deletes
    } else {
        ok = bins_->move(id, parentFolder, index, error);
        if (ok) {
            saveTree();
            QTimer::singleShot(0, this, [this, parentFolder] {
                refreshTree();
                if (!parentFolder.isEmpty()) tree_->setExpanded(model_->indexOf(parentFolder), true);
            });
        }
    }
    return ok;
}

QStringList BinContainer::selectedNodes() const {
    QStringList ids;
    for (const QModelIndex& i : tree_->selectionModel()->selectedRows(BinTreeModel::kNameColumn)) ids << model_->idOf(i);
    return ids;
}

void BinContainer::setSidebarVisible(bool visible) {
    sidebar_->setVisible(visible);
    if (visible) tabs_->setVisible(true);
}

bool BinContainer::isSidebarVisible() const {
    return !sidebar_->isHidden();
}

void BinContainer::setSidebarOnly(bool only) {
    tabs_->setVisible(!only);
    if (only) sidebar_->setVisible(true);
}

bool BinContainer::isSidebarOnly() const {
    return tabs_->isHidden();
}

void BinContainer::setFlatView(bool flat) {
    model_->setFlat(flat);
    if (!flat) tree_->expandAll();
}

void BinContainer::setBinFilter(const QString& text) {
    if (binFilter_->text() != text) binFilter_->setText(text);
    model_->setFilter(text);
    if (text.isEmpty() && !model_->isFlat()) tree_->expandAll();
}

QMenu* BinContainer::sidebarMenu(QMenu* menu, const QString& id) {
    menu->clear();
    const QString folder = folderForNew(id);
    const bool project = bins_ != nullptr;
    QAction* newBinAction = menu->addAction(tr("New Bin"), this, [this, folder] { newBin(folder); });
    QAction* newFolderAction = menu->addAction(tr("New Folder"), this, [this, folder] { newFolder(folder); });
    newBinAction->setEnabled(project);
    newFolderAction->setEnabled(project);
    const model::BinNode* n = project ? bins_->node(id) : nullptr;
    if (n) {
        menu->addSeparator();
        if (n->kind == model::BinNode::Kind::Bin && !bins_->isInTrash(id)) {
            menu->addAction(tr("Open"), this, [this, id] { openBin(id); });
        }
        if (n->kind != model::BinNode::Kind::Trash) {
            menu->addAction(tr("Rename"), this, [this, id] { tree_->edit(model_->indexOf(id)); });
            if (!bins_->isInTrash(id)) menu->addAction(tr("Delete"), this, [this, id] { deleteNode(id); });
        }
    }
    if (id.isEmpty()) {
        menu->addSeparator();
        QAction* flat = menu->addAction(tr("Flat View"));
        flat->setCheckable(true);
        flat->setChecked(model_->isFlat());
        flat->setEnabled(project);
        connect(flat, &QAction::toggled, this, &BinContainer::setFlatView);
    }
    if (project && bins_->trash() && (id.isEmpty() || (n && n->kind == model::BinNode::Kind::Trash))) {
        menu->addAction(tr("Empty Trash"), this, [this] { emptyTrash(); });
    }
    if (id.isEmpty()) {
        menu->addSeparator();
        menu->addAction(tr("Hide Sidebar"), this, [this] { setSidebarVisible(false); });
    }
    return menu;
}

void BinContainer::showTreeMenu(const QPoint& pos) {
    const QString id = model_->idOf(tree_->indexAt(pos));
    QMenu menu(this);
    sidebarMenu(&menu, id);
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------- bin contents

QString BinContainer::newSequence(const QString& targetBin) {
    BinDocument* doc = targetBin.isEmpty() ? activeDocument() : openBin(targetBin);
    if (!doc) return {};
    const model::BinItem item = model::newSequence(doc->bin(), editRate_, QDateTime::currentDateTimeUtc());
    doc->addItem(item);
    if (BinView* v = viewFor(doc)) v->selectIds({item.id});
    return item.id;
}

QStringList BinContainer::duplicateSelection() {
    BinView* view = activeView();
    if (!view) return {};
    BinDocument& doc = view->document();
    QStringList created;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* original = doc.bin().item(id)) {
            const model::BinItem copy = model::duplicateItem(doc.bin(), *original, now);
            doc.addItem(copy);
            created << copy.id;
        }
    }
    view->selectIds(created);
    return created;
}

int BinContainer::deleteSelection() {
    BinView* view = activeView();
    if (!view) return 0;
    QStringList ids;
    for (const QString& id : view->selectedIds()) {
        // Locked items cannot be deleted (p.265).
        if (const model::BinItem* i = view->document().bin().item(id); i && !i->locked) ids << id;
    }
    view->document().removeItems(ids);
    return static_cast<int>(ids.size());
}

void BinContainer::selectAll() {
    if (BinView* v = activeView()) v->selectAll();
}

} // namespace lumenis::ui

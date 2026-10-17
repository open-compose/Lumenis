#include "lumenis/ui/bin_container.h"

#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDrag>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace lumenis::ui {

namespace {

// The Sidebar's share of the Bin Container, from the picture on p.74.
constexpr int kSidebarPercent = 36;
constexpr int kMinimumSidebar = 80;
constexpr int kMinimumBins = 160;
// Folders open while items are dragged over them (p.243).
constexpr int kAutoExpandDelay = 500;

// Deletes with the Delete key, and moves entries by dragging (p.242, p.297).
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

QString favoriteName(const QString& path) {
    return QFileInfo(path).completeBaseName();
}

} // namespace

// ---------------------------------------------------------------- FavoriteBins

QStringList& FavoriteBins::paths() {
    static QStringList list;
    return list;
}

bool& FavoriteBins::atTop() {
    // The guide offers top or bottom without saying which is the default.
    static bool top = true;
    return top;
}

void FavoriteBins::load(QSettings& settings) {
    paths() = settings.value(QStringLiteral("favoriteBins/paths")).toStringList();
    atTop() = settings.value(QStringLiteral("favoriteBins/atTop"), true).toBool();
}

void FavoriteBins::save(QSettings& settings) {
    settings.setValue(QStringLiteral("favoriteBins/paths"), paths());
    settings.setValue(QStringLiteral("favoriteBins/atTop"), atTop());
}

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

void BinTreeModel::addFavorites() {
    // The Favorite Bins folder (p.293), shown once it has bins.
    if (FavoriteBins::paths().isEmpty()) return;
    auto folder = std::make_unique<Row>();
    folder->id = FavoriteBins::folderId();
    folder->parent = &root_;
    for (const QString& path : FavoriteBins::paths()) {
        auto row = std::make_unique<Row>();
        row->id = FavoriteBins::idFor(path);
        row->parent = folder.get();
        folder->children.push_back(std::move(row));
    }
    root_.children.push_back(std::move(folder));
}

void BinTreeModel::rebuild() {
    beginResetModel();
    root_.children.clear();
    if (const model::ProjectBins* bins = container_.bins()) {
        if (flat_ || !filter_.isEmpty()) {
            // A flat list of matching bins (p.297, p.299).
            QStringList ids;
            for (const model::BinNode* b : bins->bins()) ids << b->id;
            for (const QString& path : FavoriteBins::paths()) ids << FavoriteBins::idFor(path);
            for (const QString& id : ids) {
                if (!filter_.isEmpty() && !container_.nodeName(id).contains(filter_, Qt::CaseInsensitive)) continue;
                auto row = std::make_unique<Row>();
                row->id = id;
                row->parent = &root_;
                root_.children.push_back(std::move(row));
            }
        } else {
            if (FavoriteBins::atTop()) addFavorites();
            build(bins->nodes(), &root_);
            if (!FavoriteBins::atTop()) addFavorites();
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
    const QString id = rowFor(index)->id;
    if (role == kIdRole) return id;

    using Kind = model::BinNode::Kind;
    Kind kind = Kind::Bin;
    bool italic = false;
    if (id == FavoriteBins::folderId()) {
        kind = Kind::Folder;
    } else if (!FavoriteBins::isFavoriteId(id)) {
        const model::BinNode* n = bins->node(id);
        if (!n) return {};
        kind = n->kind;
        // Bins from other projects, and Other Bins, are in italic (p.294, p.296).
        italic = n->external || n->kind == Kind::OtherBins;
    }
    if (index.column() == kSizeColumn) {
        if (role == Qt::TextAlignmentRole) return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        if (role != Qt::DisplayRole || kind != Kind::Bin) return {};
        // Bin sizes in kilobytes, as in the pictures on p.240.
        const qint64 bytes = QFileInfo(container_.nodePath(id)).size();
        return QStringLiteral("%1K").arg((bytes + 1023) / 1024);
    }
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return container_.nodeName(id);
    case Qt::DecorationRole:
        switch (kind) {
        case Kind::Bin: return glyphIcon(GlyphIcon::Bin);
        case Kind::Folder:
        case Kind::OtherBins: return glyphIcon(GlyphIcon::Folder);
        case Kind::Trash: return glyphIcon(GlyphIcon::Trash);
        }
        return {};
    case Qt::FontRole:
        if (italic) {
            QFont f;
            f.setItalic(true);
            return f;
        }
        return {};
    case Qt::ForegroundRole:
        // "The Bin icon appears dimmed in the Bins list, indicating the bin
        // is open" (p.295).
        if (kind == Kind::Bin && container_.isOpen(id)) return QPalette().color(QPalette::Disabled, QPalette::Text);
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
    const QString id = rowFor(index)->id;
    if (id == FavoriteBins::folderId()) return f;
    if (FavoriteBins::isFavoriteId(id)) return f | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    const model::BinNode* n = bins->node(id);
    if (!n) return f;
    using Kind = model::BinNode::Kind;
    if (n->kind != Kind::Trash && index.column() == kNameColumn) f |= Qt::ItemIsEditable;
    if (n->kind == Kind::Bin || n->kind == Kind::Folder) f |= Qt::ItemIsDragEnabled;
    // Folders take bins and folders; bins take bin items (p.243).
    if ((n->isContainer() && !flat_) || (n->kind == Kind::Bin && !bins->isInTrash(id))) f |= Qt::ItemIsDropEnabled;
    return f;
}

Qt::DropActions BinTreeModel::supportedDropActions() const {
    return Qt::MoveAction | Qt::CopyAction;
}

QStringList BinTreeModel::mimeTypes() const {
    return {binNodesMime(), binItemsMime()};
}

QMimeData* BinTreeModel::mimeData(const QModelIndexList& indexes) const {
    QStringList ids;
    for (const QModelIndex& i : indexes) {
        if (i.column() == kNameColumn) ids << rowFor(i)->id;
    }
    auto* data = new QMimeData;
    data->setData(binNodesMime(), ids.join(QLatin1Char('\n')).toUtf8());
    return data;
}

bool BinTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent) const {
    const model::ProjectBins* bins = container_.bins();
    if (!bins) return false;
    const QString target = idOf(parent);
    const bool targetIsBin = FavoriteBins::isFavoriteId(target) ||
                             (bins->node(target) && bins->node(target)->kind == model::BinNode::Kind::Bin);
    if (data->hasFormat(binItemsMime())) return targetIsBin && row < 0;
    if (data->hasFormat(binNodesMime())) return !flat_ && !targetIsBin && target != FavoriteBins::folderId();
    return false;
}

bool BinTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                const QModelIndex& parent) {
    if (!canDropMimeData(data, action, row, column, parent)) return false;
    const QString parentId = idOf(parent);
    if (data->hasFormat(binItemsMime())) {
        // Items dragged from a bin onto a bin in the list (p.243).
        QStringList lines = QString::fromUtf8(data->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (lines.size() < 2) return false;
        const QString from = lines.takeFirst();
        const bool copy = action == Qt::CopyAction || (QApplication::keyboardModifiers() & Qt::AltModifier);
        // After the drag has finished, so the source bin is not changed under it.
        QTimer::singleShot(0, &container_, [c = &container_, from, lines, parentId, copy] {
            c->transferItems(from, lines, parentId, copy);
        });
        return true;
    }
    if (action != Qt::MoveAction) return false;
    const QStringList ids = QString::fromUtf8(data->data(binNodesMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bool moved = false;
    for (const QString& id : ids) {
        moved = container_.moveNode(id, parentId, row) || moved;
        if (row >= 0) ++row;
    }
    return moved;
}

// ---------------------------------------------------------------- BinSidebar

BinSidebar::BinSidebar(BinContainer& container, QWidget* parent) : QWidget(parent), container_(container) {
    setObjectName(QStringLiteral("binSidebar"));
    // Quick filter, fast menu, and the list (p.240, p.299).
    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("binFilter"));
    filter_->setPlaceholderText(tr("Filter bins"));
    filter_->setClearButtonEnabled(true);
    filter_->addAction(glyphIcon(GlyphIcon::Search), QLineEdit::LeadingPosition);
    connect(filter_, &QLineEdit::textChanged, this, &BinSidebar::setFilter);
    auto* fast = new QToolButton(this);
    fast->setObjectName(QStringLiteral("sidebarFastMenu"));
    fast->setIcon(glyphIcon(GlyphIcon::FastMenu));
    fast->setToolTip(tr("Sidebar Fast Menu"));
    fast->setAutoRaise(true);
    fast->setFixedSize(26, kToolButtonHeight);
    fast->setPopupMode(QToolButton::InstantPopup);
    auto* fastMenu = new QMenu(fast);
    connect(fastMenu, &QMenu::aboutToShow, this, [this, fastMenu] { container_.sidebarMenu(fastMenu, {}, this); });
    fast->setMenu(fastMenu);

    model_ = new BinTreeModel(container_, this);
    auto* tree = new SidebarTree(this);
    tree_ = tree;
    tree_->setObjectName(QStringLiteral("binTree"));
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setIndentation(14);
    tree_->setUniformRowHeights(true);
    tree_->setIconSize(QSize(16, 16));
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    // Entries move within the list; bins also drag to the bins (p.243), and
    // bin items drop on bins.
    tree_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->setAutoExpandDelay(kAutoExpandDelay);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(BinTreeModel::kNameColumn, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(BinTreeModel::kSizeColumn, QHeaderView::Fixed);
    tree_->header()->resizeSection(BinTreeModel::kSizeColumn, 52);
    tree->onDelete = [this] {
        for (const QString& id : selectedNodes()) container_.deleteNode(id);
    };
    connect(tree_, &QTreeView::customContextMenuRequested, this, &BinSidebar::showMenu);
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        // Double-click a bin to open it (p.240).
        const QString id = model_->idOf(index);
        if (FavoriteBins::isFavoriteId(id)) {
            container_.openBinAsSet(id);
        } else if (const model::ProjectBins* bins = container_.bins();
                   bins && bins->node(id) && bins->node(id)->kind == model::BinNode::Kind::Bin) {
            container_.openBinAsSet(id);
        }
    });

    auto* header = new QHBoxLayout;
    header->setContentsMargins(4, 3, 2, 3);
    header->setSpacing(2);
    header->addWidget(filter_, 1);
    header->addWidget(fast);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(tree_, 1);

    container_.registerSidebar(this);
    refresh();
}

void BinSidebar::refresh() {
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

QStringList BinSidebar::selectedNodes() const {
    QStringList ids;
    for (const QModelIndex& i : tree_->selectionModel()->selectedRows(BinTreeModel::kNameColumn)) ids << model_->idOf(i);
    return ids;
}

void BinSidebar::setFlatView(bool flat) {
    model_->setFlat(flat);
    if (!flat) tree_->expandAll();
}

void BinSidebar::setFilter(const QString& text) {
    if (filter_->text() != text) filter_->setText(text);
    model_->setFilter(text);
    if (text.isEmpty() && !model_->isFlat()) tree_->expandAll();
}

void BinSidebar::expand(const QString& folderId) {
    if (!folderId.isEmpty()) tree_->setExpanded(model_->indexOf(folderId), true);
}

void BinSidebar::showMenu(const QPoint& pos) {
    const QString id = model_->idOf(tree_->indexAt(pos));
    QMenu menu(this);
    container_.sidebarMenu(&menu, id, this);
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------- FloatingBinContainer

FloatingBinContainer::FloatingBinContainer(BinContainer& container, QWidget* owner)
    : QWidget(owner, Qt::Tool), container_(container) {
    setObjectName(QStringLiteral("floatingBinContainer"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(tr("Bins"));
    sidebar_ = new BinSidebar(container_, this);
    sidebar_->onHide = [this] { setSidebarVisible(false); };
    area_ = new BinDockArea(this);
    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setChildrenCollapsible(false);
    split_->setHandleWidth(kSplitterHandle);
    split_->addWidget(sidebar_);
    split_->addWidget(area_);
    split_->setStretchFactor(1, 1);
    sidebar_->setMinimumWidth(kMinimumSidebar);
    area_->setMinimumWidth(kMinimumBins);
    // "The bin will appear in a new Bin Container with the Sidebar closed"
    // (p.241).
    sidebar_->hide();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split_);
    container_.connectArea(area_);
}

void FloatingBinContainer::setSidebarVisible(bool visible) {
    sidebar_->setVisible(visible);
}

bool FloatingBinContainer::isSidebarVisible() const {
    return !sidebar_->isHidden();
}

void FloatingBinContainer::closeEvent(QCloseEvent* event) {
    // Closing the window closes its bins, which are saved.
    for (BinView* v : area_->views()) container_.closeBin(&v->document());
    event->accept();
}

// ---------------------------------------------------------------- BinContainer

BinContainer::BinContainer(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("binContainer"));

    sidebar_ = new BinSidebar(*this, this);
    sidebar_->onHide = [this] { setSidebarVisible(false); };

    // Open bins: tabbed, or side by side (p.240-242).
    area_ = new BinDockArea(this);
    connectArea(area_);

    split_ = new QSplitter(Qt::Horizontal, this);
    split_->setObjectName(QStringLiteral("binSplit"));
    split_->setChildrenCollapsible(false);
    split_->setHandleWidth(kSplitterHandle);
    split_->addWidget(sidebar_);
    split_->addWidget(area_);
    // Explicit minimums: otherwise they come from the widgets' contents,
    // which each Qt version measures differently, and a minimum larger than
    // the panel's share overrides the layout's proportions.
    sidebar_->setMinimumWidth(kMinimumSidebar);
    area_->setMinimumWidth(kMinimumBins);
    split_->setStretchFactor(1, 1);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split_);

    // The bin last clicked or typed in is the active one.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        for (QWidget* w = now; w; w = w->parentWidget()) {
            if (auto* v = qobject_cast<BinView*>(w)) {
                if (documents_.contains(&v->document())) setActiveView(v);
                return;
            }
        }
    });

    autoSave_ = new QTimer(this);
    connect(autoSave_, &QTimer::timeout, this, [this] { saveAll(); });
    // The guide does not give the default interval; ours is 15 minutes.
    setAutoSaveMinutes(15);
}

BinContainer::~BinContainer() {
    // Unsaved changes are not lost when the window goes.
    if (bins_) saveAll();
    qDeleteAll(floating_);
    floating_.clear();
    // Children die after this object's members: nothing may call back.
    disconnect(qApp, nullptr, this, nullptr);
    disconnect(area_, nullptr, this, nullptr);
    for (BinDocument* d : documents_) disconnect(d, nullptr, this, nullptr);
    for (BinSidebar* s : sidebars_) {
        disconnect(s, nullptr, this, nullptr);
        // A Sidebar elsewhere (the Project Bin Container) loses its list.
        if (s != sidebar_) s->tree()->setModel(nullptr);
    }
    sidebars_.clear();
    for (const BinDockArea* a : {area_}) {
        for (BinView* v : a->views()) disconnect(v, nullptr, this, nullptr);
    }
}

void BinContainer::registerSidebar(BinSidebar* sidebar) {
    sidebars_ << sidebar;
    connect(sidebar, &QObject::destroyed, this, [this, sidebar] { sidebars_.removeAll(sidebar); });
}

void BinContainer::connectArea(BinDockArea* area) {
    connect(area, &BinDockArea::closeRequested, this, [this](BinView* v) { closeBin(&v->document()); });
    connect(area, &BinDockArea::currentViewChanged, this, [this](BinView* v) {
        if (v) setActiveView(v);
    });
    connect(area, &BinDockArea::tabDragged, this, &BinContainer::dragTab);
    connect(area, &BinDockArea::binDropped, this, [this, area](const QString& id, const BinDockArea::Zone& zone) {
        if (BinView* v = viewFor(document(id))) placeView(v, area, zone.group, zone.side);
    });
    connect(area, &BinDockArea::nodesDropped, this,
            [this, area](const QStringList& ids, const BinDockArea::Zone& zone) { dropNodes(ids, area, zone); });
    connect(area, &BinDockArea::tabMenuRequested, this, [this, area](const QPoint& globalPos) {
        // "right click and select Show Sidebar" (p.241).
        QMenu menu(this);
        QAction* show = menu.addAction(tr("Show Sidebar"));
        if (area == area_) {
            show->setEnabled(!isSidebarVisible());
            connect(show, &QAction::triggered, this, [this] { setSidebarVisible(true); });
        } else if (auto* f = qobject_cast<FloatingBinContainer*>(area->window())) {
            show->setEnabled(!f->isSidebarVisible());
            connect(show, &QAction::triggered, f, [f] { f->setSidebarVisible(true); });
        }
        menu.exec(globalPos);
    });
}

void BinContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!sized_ && width() > 1) {
        sized_ = true;
        const int side = width() * kSidebarPercent / 100;
        split_->setSizes({side, width() - side});
    }
}

void BinContainer::setFavoritesAtTop(bool top) {
    if (FavoriteBins::atTop() == top) return;
    FavoriteBins::atTop() = top;
    refreshTree();
}

void BinContainer::setQuickFilterAllColumns(bool all) {
    if (BinFilterModel::searchAllColumns() == all) return;
    BinFilterModel::searchAllColumns() = all;
    for (BinDocument* d : documents_) {
        if (BinView* v = viewFor(d); v && !v->quickFilter().isEmpty()) v->setQuickFilter(v->quickFilter());
    }
}

BinDocument* BinContainer::openBinAsSet(const QString& id) {
    const bool wasOpen = document(canonicalBin(id)) != nullptr;
    BinDocument* doc = openBin(id);
    const bool invert = QApplication::keyboardModifiers() & Qt::ControlModifier;
    if (doc && !wasOpen && openFloating_ != invert) floatBin(doc);
    return doc;
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
            if (BinView* v = viewFor(d)) {
                if (BinDockArea* a = areaOf(v)) a->takeView(v);
                delete v;
            }
            delete d;
        }
        active_ = nullptr;
        qDeleteAll(floating_);
        floating_.clear();
        bins_.reset();
    }
    editRate_ = editRate;
    if (!folder.isEmpty()) {
        bins_ = std::make_unique<model::ProjectBins>(folder, name);
        bins_->load();
        const QStringList open = bins_->openBins;
        refreshTree();
        for (BinSidebar* s : sidebars_) s->tree()->expandAll();
        for (const QString& id : open) openBin(id);
        // A project's first session opens its bin.
        if (open.isEmpty() && bins_->bins().size() == 1) openBin(bins_->bins().first()->id);
    } else {
        refreshTree();
    }
    emit activeBinChanged();
}

void BinContainer::saveTree() {
    if (!bins_) return;
    bins_->openBins.clear();
    for (const BinDockArea* a : areas()) {
        for (const BinView* v : a->views()) bins_->openBins << v->document().nodeId();
    }
    bins_->save();
}

void BinContainer::refreshTree() {
    for (BinSidebar* s : sidebars_) s->refresh();
}

QTreeView* BinContainer::tree() const {
    return sidebar_->tree();
}

BinTreeModel* BinContainer::treeModel() const {
    return sidebar_->model();
}

QList<BinDocument*> BinContainer::openDocuments() const {
    return documents_;
}

BinDocument* BinContainer::document(const QString& id) const {
    const QString canonical = canonicalBin(id);
    for (BinDocument* d : documents_) {
        if (d->nodeId() == canonical) return d;
    }
    return nullptr;
}

QList<BinDockArea*> BinContainer::areas() const {
    QList<BinDockArea*> out{area_};
    for (FloatingBinContainer* f : floating_) out << f->area();
    return out;
}

BinDockArea* BinContainer::areaOf(const BinView* view) const {
    for (BinDockArea* a : areas()) {
        if (a->groupOf(view)) return a;
    }
    return nullptr;
}

BinTabGroup* BinContainer::tabs() const {
    return area_->currentGroup();
}

BinView* BinContainer::viewFor(const BinDocument* document) const {
    if (!document) return nullptr;
    for (const BinDockArea* a : areas()) {
        for (BinView* v : a->views()) {
            if (&v->document() == document) return v;
        }
    }
    return nullptr;
}

void BinContainer::setActiveView(BinView* view) {
    if (active_ == view) return;
    active_ = view;
    emit activeBinChanged();
}

BinView* BinContainer::activeView() const {
    if (active_ && areaOf(active_)) return active_;
    for (const BinDockArea* a : areas()) {
        if (BinTabGroup* g = a->currentGroup()) return g->currentView();
    }
    return nullptr;
}

BinDocument* BinContainer::activeDocument() const {
    BinView* v = activeView();
    return v ? &v->document() : nullptr;
}

void BinContainer::updateTab(BinDocument* document) {
    if (BinView* v = viewFor(document)) {
        // "an asterisk appears before the bin name" (p.258), and "(sifted)"
        // after it while it is shown sifted (p.289).
        QString text = (document->isDirty() ? QStringLiteral("* ") : QString()) + document->name();
        if (document->bin().isSiftShown()) text += tr(" (sifted)");
        if (BinDockArea* a = areaOf(v)) a->setTitle(v, text);
    }
}

QString BinContainer::canonicalBin(const QString& id) const {
    // A Favorite Bin that is one of this project's bins is that bin.
    if (bins_ && FavoriteBins::isFavoriteId(id)) {
        if (const QString own = bins_->binForPath(FavoriteBins::pathOf(id)); !own.isEmpty()) return own;
    }
    return id;
}

QString BinContainer::nodeName(const QString& id) const {
    if (id == FavoriteBins::folderId()) return tr("Favorite Bins");
    if (FavoriteBins::isFavoriteId(id)) return favoriteName(FavoriteBins::pathOf(id));
    const model::BinNode* n = bins_ ? bins_->node(id) : nullptr;
    return n ? n->name : QString();
}

QString BinContainer::nodePath(const QString& id) const {
    if (FavoriteBins::isFavoriteId(id)) return FavoriteBins::pathOf(id);
    return bins_ ? bins_->binPath(id) : QString();
}

BinDocument* BinContainer::openBin(const QString& requested) {
    if (!bins_) return nullptr;
    const QString id = canonicalBin(requested);
    if (BinDocument* d = document(id)) {
        BinView* v = viewFor(d);
        if (BinDockArea* a = areaOf(v)) {
            a->showView(v);
            a->window()->raise();
        }
        setActiveView(v);
        return d;
    }
    QString path;
    QString name;
    if (FavoriteBins::isFavoriteId(id)) {
        path = FavoriteBins::pathOf(id);
        name = favoriteName(path);
    } else {
        const model::BinNode* node = bins_->node(id);
        if (!node || node->kind != model::BinNode::Kind::Bin || bins_->isInTrash(id)) return nullptr;
        path = bins_->binPath(id);
        name = node->name;
    }
    auto bin = model::BinFile::read(path);
    if (!bin) return nullptr;
    bin->name = name;
    if (binLoader_) binLoader_(*bin);
    auto* doc = new BinDocument(id, path, *bin, this);
    documents_ << doc;
    auto* view = new BinView(*doc, binMenu_);
    view->setInputMenu(inputMenu_);
    connect(doc, &BinDocument::dirtyChanged, this, [this, doc] {
        updateTab(doc);
        emit dirtyChanged();
    });
    connect(doc, &BinDocument::displayChanged, this, [this, doc] { updateTab(doc); });
    connect(view, &BinView::deleteRequested, this, [this] { deleteSelection(); });
    connect(view, &BinView::itemActivated, this,
            [this, doc](const QString& itemId) { emit itemActivated(doc->nodeId(), itemId); });
    // Items dragged in from another bin, after the drag has finished.
    connect(view, &BinView::itemsDropped, this,
            [this, doc](const QString& from, const QStringList& ids, bool copy) {
                transferItems(from, ids, doc->nodeId(), copy);
            },
            Qt::QueuedConnection);
    connect(
        view, &BinView::filesDropped, this, [this, doc](const QStringList& paths) { emit filesDropped(doc->nodeId(), paths); },
        Qt::QueuedConnection);
    connect(view, &QObject::destroyed, this, [this, view] {
        if (active_ == view) active_ = nullptr;
    });
    // "If you continue to double click bins, they will open, tabbed to bins
    // in the right panel" (p.240).
    area_->addView(view, doc->name());
    setActiveView(view);
    updateTab(doc);
    saveTree();
    refreshTree();
    return doc;
}

void BinContainer::closeBin(BinDocument* document) {
    if (!document || !documents_.contains(document)) return;
    if (document->isDirty()) document->save();
    if (BinView* v = viewFor(document)) {
        if (active_ == v) active_ = nullptr;
        if (BinDockArea* a = areaOf(v)) a->takeView(v);
        v->deleteLater();
    }
    documents_.removeAll(document);
    document->deleteLater();
    removeEmptyFloating();
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
    refreshTree(); // sizes
    return ok;
}

bool BinContainer::saveAll(QString* error) {
    bool ok = true;
    for (BinDocument* d : documents_) {
        if (d->isDirty()) ok = d->save(error) && ok;
    }
    saveTree();
    refreshTree();
    return ok;
}

// ---------------------------------------------------------------- arranging bins

void BinContainer::placeView(BinView* view, BinDockArea* area, BinTabGroup* target, BinDropSide side) {
    if (!view || !area) return;
    BinDockArea* from = areaOf(view);
    BinTabGroup* own = from ? from->groupOf(view) : nullptr;
    // Onto its own group: nothing moves (a bin cannot sit beside itself).
    if (target && target == own && (side == BinDropSide::Tab || own->count() == 1)) return;
    if (!target && area == from) return;
    if (from) from->takeView(view);
    QString title = view->document().name();
    area->addView(view, title, target, side);
    updateTab(&view->document());
    removeEmptyFloating();
    setActiveView(view);
    saveTree();
}

void BinContainer::dockBin(BinDocument* document, BinTabGroup* target, BinDropSide side) {
    BinView* v = viewFor(document);
    if (!v) return;
    placeView(v, target ? &target->area() : area_, target, side);
}

FloatingBinContainer* BinContainer::floatBin(BinDocument* document, const QRect& geometry) {
    BinView* v = viewFor(document);
    if (!v) return nullptr;
    BinDockArea* from = areaOf(v);
    // Already alone in a floating container.
    if (auto* f = from ? qobject_cast<FloatingBinContainer*>(from->window()) : nullptr;
        f && from->views().size() == 1) {
        if (geometry.isValid()) f->setGeometry(geometry);
        return f;
    }
    auto* f = new FloatingBinContainer(*this, window());
    floating_ << f;
    emit floatingContainerCreated(f);
    QRect g = geometry;
    if (!g.isValid()) {
        const QSize size = v->isVisible() ? v->size() : QSize(520, 360);
        g = QRect(QCursor::pos() - QPoint(40, 10), size);
    }
    f->setGeometry(g);
    if (from) from->takeView(v);
    f->area()->addView(v, document->name());
    updateTab(document);
    f->show();
    removeEmptyFloating();
    setActiveView(v);
    saveTree();
    return f;
}

void BinContainer::removeEmptyFloating() {
    // "If a Bin Container is floating, it will be closed when the last bin
    // is closed or dragged to another Bin Container" (p.243).
    for (FloatingBinContainer* f : QList<FloatingBinContainer*>(floating_)) {
        if (f->area()->views().isEmpty()) {
            floating_.removeAll(f);
            f->hide();
            f->deleteLater();
        }
    }
}

void BinContainer::dragTab(BinView* view) {
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData;
    mime->setData(binTabMime(), view->document().nodeId().toUtf8());
    drag->setMimeData(mime);
    QPixmap pm(view->size().scaled(160, 100, Qt::KeepAspectRatio));
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setOpacity(0.6);
        p.drawPixmap(pm.rect(), view->grab());
    }
    drag->setPixmap(pm);
    // Released where no Bin Container takes it: the bin floats (p.241).
    if (drag->exec(Qt::MoveAction) == Qt::IgnoreAction && documents_.contains(&view->document())) {
        floatBin(&view->document(), QRect(QCursor::pos() - QPoint(40, 10), view->size()));
    }
}

void BinContainer::dropNodes(const QStringList& ids, BinDockArea* area, const BinDockArea::Zone& zone) {
    // Bins dragged from the Sidebar: tabbed with Alt, or into a new sub
    // panel (p.243). Folders are ignored.
    BinTabGroup* target = zone.group;
    BinDropSide side = zone.side;
    for (const QString& id : ids) {
        const bool isBin = FavoriteBins::isFavoriteId(id) ||
                           (bins_ && bins_->node(id) && bins_->node(id)->kind == model::BinNode::Kind::Bin);
        if (!isBin) continue;
        BinDocument* doc = openBin(id);
        BinView* v = viewFor(doc);
        if (!v) continue;
        placeView(v, area, target, side);
        // The rest join the first one's group.
        if (BinDockArea* a = areaOf(v)) target = a->groupOf(v);
        side = BinDropSide::Tab;
    }
}

// ---------------------------------------------------------------- Sidebar entries

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
    sidebar_->expand(parentFolder);
    // "A new (empty) bin opens ... with a default name highlighted" (p.295).
    openBinAsSet(id);
    const QModelIndex index = treeModel()->indexOf(id);
    tree()->setCurrentIndex(index);
    if (isVisible()) tree()->edit(index);
    return id;
}

QString BinContainer::newFolder(const QString& parentFolder) {
    if (!bins_) return {};
    const QString id = bins_->createFolder(parentFolder);
    if (id.isEmpty()) return {};
    saveTree();
    refreshTree();
    sidebar_->expand(parentFolder);
    const QModelIndex index = treeModel()->indexOf(id);
    tree()->setCurrentIndex(index);
    if (isVisible()) tree()->edit(index);
    return id;
}

bool BinContainer::renameNode(const QString& id, const QString& name, QString* error) {
    if (!bins_ || FavoriteBins::isFavoriteId(id)) return false;
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
    if (FavoriteBins::isFavoriteId(id)) {
        // Delete removes a bin from the Favorite Bins folder only (p.293).
        removeFromFavorites(FavoriteBins::pathOf(id));
        return true;
    }
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
    if (!bins_ || FavoriteBins::isFavoriteId(id)) return false;
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
                for (BinSidebar* s : sidebars_) s->expand(parentFolder);
            });
        }
    }
    return ok;
}

QStringList BinContainer::selectedNodes() const {
    // The Sidebar last used, or the main one.
    for (BinSidebar* s : sidebars_) {
        if (s->tree()->hasFocus()) return s->selectedNodes();
    }
    return sidebar_->selectedNodes();
}

bool BinContainer::addToFavorites(BinDocument* document) {
    if (!document) return false;
    const QString path = QFileInfo(document->path()).absoluteFilePath();
    if (FavoriteBins::paths().contains(path)) return false;
    if (document->isDirty()) document->save();
    FavoriteBins::paths() << path;
    refreshTree();
    for (BinSidebar* s : sidebars_) s->expand(FavoriteBins::folderId());
    return true;
}

void BinContainer::removeFromFavorites(const QString& path) {
    if (FavoriteBins::paths().removeAll(path) == 0) return;
    // An open Favorite Bin from elsewhere stays open until it is closed.
    refreshTree();
}

void BinContainer::setSidebarVisible(bool visible) {
    sidebar_->setVisible(visible);
    if (visible) area_->setVisible(true);
}

bool BinContainer::isSidebarVisible() const {
    return !sidebar_->isHidden();
}

void BinContainer::setSidebarOnly(bool only) {
    area_->setVisible(!only);
    if (only) sidebar_->setVisible(true);
}

bool BinContainer::isSidebarOnly() const {
    return area_->isHidden();
}

void BinContainer::setFlatView(bool flat) {
    sidebar_->setFlatView(flat);
}

void BinContainer::setBinFilter(const QString& text) {
    sidebar_->setFilter(text);
}

QMenu* BinContainer::sidebarMenu(QMenu* menu, const QString& id, BinSidebar* sidebar) {
    menu->clear();
    const QString folder = folderForNew(id);
    const bool project = bins_ != nullptr;
    QAction* newBinAction = menu->addAction(tr("New Bin"), this, [this, folder] { newBin(folder); });
    QAction* newFolderAction = menu->addAction(tr("New Folder"), this, [this, folder] { newFolder(folder); });
    newBinAction->setEnabled(project);
    newFolderAction->setEnabled(project);
    if (project && FavoriteBins::isFavoriteId(id)) {
        menu->addSeparator();
        menu->addAction(tr("Open"), this, [this, id] { openBin(id); });
        menu->addAction(tr("Delete"), this, [this, id] { deleteNode(id); });
    }
    const model::BinNode* n = project ? bins_->node(id) : nullptr;
    if (n) {
        menu->addSeparator();
        if (n->kind == model::BinNode::Kind::Bin && !bins_->isInTrash(id)) {
            menu->addAction(tr("Open"), this, [this, id] { openBin(id); });
        }
        if (n->kind != model::BinNode::Kind::Trash) {
            menu->addAction(tr("Rename"), this,
                            [sidebar, id] { sidebar->tree()->edit(sidebar->model()->indexOf(id)); });
            if (!bins_->isInTrash(id)) menu->addAction(tr("Delete"), this, [this, id] { deleteNode(id); });
        }
    }
    if (id.isEmpty()) {
        menu->addSeparator();
        QAction* flat = menu->addAction(tr("Flat View"));
        flat->setCheckable(true);
        flat->setChecked(sidebar->model()->isFlat());
        flat->setEnabled(project);
        connect(flat, &QAction::toggled, sidebar, &BinSidebar::setFlatView);
    }
    if (project && bins_->trash() && (id.isEmpty() || (n && n->kind == model::BinNode::Kind::Trash))) {
        menu->addAction(tr("Empty Trash"), this, [this] { emptyTrash(); });
    }
    if (id.isEmpty() && sidebar->onHide) {
        menu->addSeparator();
        menu->addAction(tr("Hide Sidebar"), this, [sidebar] { sidebar->onHide(); });
    }
    return menu;
}

// ---------------------------------------------------------------- bin contents

QString BinContainer::newSequence(const QString& targetBin) {
    BinDocument* doc = targetBin.isEmpty() ? activeDocument() : openBin(targetBin);
    if (!doc) return {};
    const model::BinItem item =
        model::newSequence(doc->bin(), editRate_, QDateTime::currentDateTimeUtc(), startTimecode_);
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
    // With headings selected, Edit > Delete removes columns (p.268).
    if (view->mode() == model::BinViewMode::Text && !view->selectedHeadings().isEmpty()) {
        view->deleteColumns(view->selectedHeadings());
        return 0;
    }
    QStringList ids;
    for (const QString& id : view->selectedIds()) {
        // Locked items cannot be deleted (p.265).
        if (const model::BinItem* i = view->document().bin().item(id); i && !i->locked) ids << id;
    }
    if (deleteFilter_ && !ids.isEmpty()) {
        const std::optional<QStringList> keep = deleteFilter_(view->document(), ids);
        if (!keep) return 0;
        ids = *keep;
    }
    view->document().removeItems(ids);
    return static_cast<int>(ids.size());
}

void BinContainer::selectAll() {
    if (BinView* v = activeView()) v->selectAll();
}

QStringList BinContainer::transferItems(const QString& fromBin, const QStringList& ids, const QString& toBin,
                                        bool copy) {
    const QString from = canonicalBin(fromBin);
    const QString to = canonicalBin(toBin);
    // "You cannot copy clips to the same bin" (p.260).
    if (from == to) return {};
    BinDocument* source = document(from);
    if (!source) return {};
    BinDocument* target = document(to);
    if (!target) {
        // A closed bin opens with the dragged items selected (p.243).
        target = openBin(to);
        if (!target) return {};
    }
    // Custom columns come along, in the order they were made (p.260).
    model::BinDisplay display = target->bin().display;
    bool columnsAdded = false;
    for (const QString& c : source->bin().display.customColumns) {
        if (display.customColumns.contains(c)) continue;
        display.customColumns << c;
        display.columns << c;
        columnsAdded = true;
    }
    if (columnsAdded) target->setDisplay(display);

    QStringList added;
    for (const QString& id : ids) {
        const model::BinItem* item = source->bin().item(id);
        // A copy is the same clip in another bin, so it keeps its identity
        // and, unlike Duplicate, its name (p.260).
        if (!item || target->bin().item(id)) continue;
        target->addItem(*item);
        added << id;
    }
    if (!copy) source->removeItems(added);
    if (BinView* v = viewFor(target)) v->selectIds(added);
    return added;
}

} // namespace lumenis::ui

#include "lumenis/ui/bin_container.h"

#include "lumenis/core/branding.h"
#include "lumenis/model/bin.h"
#include "lumenis/model/bin_lock.h"
#include "lumenis/model/export_volume.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDialog>
#include <QDir>
#include <QDateTime>
#include <QDrag>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// The Bin Container (guide p.239-299) and its Sidebar. BinContainer owns the
// open bins (BinDocument, bin_view.h) and their views, places the views in
// BinDockAreas (bin_dock.cpp): its own, and one per FloatingBinContainer. It
// also keeps the project's bin list, model::ProjectBins
// (src/model/project_bins.h), saved and every BinSidebar showing it.
// BinTreeModel is the Sidebar's Qt model; FavoriteBins is the user's list of
// bins kept across projects.
//
// Ids: Sidebar entries are model::BinNode ids, "favorites" for the Favorite
// Bins folder, or "favorite:<path>" for a Favorite Bin (see FavoriteBins). A
// Favorite Bin that belongs to the open project is treated as that project's
// bin (canonicalBin()), so the same file never opens twice.
//
// MainWindow (main_window.cpp) creates the container, connects the menus to
// it, and gives the Project panel a second BinSidebar. Drags between bins end
// in transferItems(); drags of bins end in placeView() or dropNodes().

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

// A Favorite Bin is named after its file.
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

QJsonArray FavoriteBins::toJson() {
    return QJsonArray::fromStringList(paths());
}

bool FavoriteBins::fromJson(const QJsonValue& value) {
    QStringList list;
    for (const QJsonValue& v : value.toArray()) {
        if (!v.toString().isEmpty() && !list.contains(v.toString())) list << v.toString();
    }
    if (list == paths()) return false;
    paths() = list;
    return true;
}

QJsonValue FavoriteBins::legacy(const QSettings& settings) {
    const QStringList list = settings.value(QStringLiteral("favoriteBins/paths")).toStringList();
    return list.isEmpty() ? QJsonValue() : QJsonValue(QJsonArray::fromStringList(list));
}

void FavoriteBins::forgetLegacy(QSettings& settings) {
    settings.remove(QStringLiteral("favoriteBins"));
}

// ---------------------------------------------------------------- BinTreeModel

BinTreeModel::BinTreeModel(BinContainer& container, QObject* parent) : QAbstractItemModel(parent), container_(container) {}

// Mirrors the project's node tree into Rows under `into`.
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

// The whole list is rebuilt on every change: the tree view's expanded folders
// are restored by BinSidebar::refresh(), by id.
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

// Indexes point at their Row; the invalid index is the root.
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
    return 3;
}

// A depth-first search of the whole tree; the lists are short.
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

// Names, icons and sizes are looked up each time from the container, so the
// rows never hold stale data. Favorite Bins and their folder have no BinNode
// and are treated as a bin and a folder.
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
    if (index.column() == kTranscriptionColumn) {
        if (role != Qt::DisplayRole || kind != Kind::Bin || FavoriteBins::isFavoriteId(id)) return {};
        const auto on = container_.transcriptionStatus(id);
        if (!on) return {};
        return *on ? tr("Transcription On") : tr("Transcription Off");
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
        // "Bold text in the Bin Container also identifies bins that are
        // locked by another user" (p.302).
        if (italic || container_.isLockedByOther(id)) {
            QFont f;
            f.setItalic(italic);
            f.setBold(container_.isLockedByOther(id));
            return f;
        }
        return {};
    case Qt::ToolTipRole:
        // Who holds it, as the guide's bin title shows (p.302); a tooltip,
        // since the Sidebar has no column for it.
        if (const auto lock = container_.lockOf(id)) return BinContainer::tr("Locked by %1").arg(lock->describe());
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

// Renaming in place goes through BinContainer::renameNode, which renames the
// bin file too.
bool BinTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid()) return false;
    return container_.renameNode(rowFor(index)->id, value.toString());
}

QVariant BinTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    if (section == kTranscriptionColumn) return tr("Transcription");
    return section == kNameColumn ? tr("Name") : tr("Size");
}

// What can be renamed, dragged and dropped on. The empty area (invalid index)
// takes drops, which move entries to the top level.
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

// Dragged entries, one id a line; the same data docks bins when dropped on a
// BinDockArea.
QMimeData* BinTreeModel::mimeData(const QModelIndexList& indexes) const {
    QStringList ids;
    for (const QModelIndex& i : indexes) {
        if (i.column() == kNameColumn) ids << rowFor(i)->id;
    }
    auto* data = new QMimeData;
    data->setData(binNodesMime(), ids.join(QLatin1Char('\n')).toUtf8());
    return data;
}

// Bin items drop onto a bin (not between rows: `row` < 0 means onto
// `parent`). Entries drop into folders or the top level, not onto bins, not
// into Favorite Bins and not in Flat View.
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
        const bool copy = isCopyDrop(action, QApplication::keyboardModifiers());
        // After the drag has finished, so the source bin is not changed under it.
        QTimer::singleShot(0, &container_, [c = &container_, from, lines, parentId, copy] {
            c->transferItems(from, lines, parentId, copy);
        });
        return true;
    }
    // Entries move only, never copy. Each goes after the one before when a
    // row was given; the tree is rebuilt later, by moveNode().
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
    tree_->header()->setSectionResizeMode(BinTreeModel::kTranscriptionColumn, QHeaderView::ResizeToContents);
    // Delete and Backspace delete the selected entries, unless a name is being
    // typed.
    tree->onDelete = [this] {
        for (const QString& id : selectedNodes()) container_.deleteNode(id);
    };
    connect(tree_, &QTreeView::customContextMenuRequested, this, &BinSidebar::showMenu);
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        // Double-click a bin to open it (p.240); Alt (Option) opens it
        // "without controlling the lock" (p.302).
        const QString id = model_->idOf(index);
        const bool takeLock = !(QApplication::keyboardModifiers() & Qt::AltModifier);
        if (FavoriteBins::isFavoriteId(id)) {
            container_.openBinAsSet(id, takeLock);
        } else if (const model::ProjectBins* bins = container_.bins();
                   bins && bins->node(id) && bins->node(id)->kind == model::BinNode::Kind::Bin) {
            container_.openBinAsSet(id, takeLock);
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
    // Note the open folders by id, rebuild, then reopen them.
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

// Right-click: the menu for the entry under the pointer, or for the list.
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
    // BinContainer deletes it (removeEmptyFloating), not closing.
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(tr("Bins"));
    // Its own Sidebar and area, laid out as the main Bin Container's.
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
    // Closing the window closes its bins, which are saved. A bin that cannot
    // be saved stays open, and so does the window.
    bool closed = true;
    for (BinView* v : area_->views()) closed = container_.closeBin(&v->document()) && closed;
    if (closed) {
        event->accept();
    } else {
        event->ignore();
    }
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

    // The auto-save is checked on a short tick; key and mouse presses
    // anywhere in the application restart the inactivity period.
    autoSave_ = new QTimer(this);
    connect(autoSave_, &QTimer::timeout, this, &BinContainer::autoSaveTick);
    qApp->installEventFilter(this);
    sinceInput_.start();
    // The defaults are the guide's: 15 minutes, 15 seconds, 17 minutes (p.1152-1153).
    setAutoSaveMinutes(15);

    // Until MainWindow names the User profile's user: the system's.
    lockUser_ = model::localBinLockHolder(qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME")));
    // Other systems' locks and saves are noticed by reading the lock files
    // again; file watching does not reach across network file systems.
    lockTimer_ = new QTimer(this);
    connect(lockTimer_, &QTimer::timeout, this, &BinContainer::pollLocks);
}

BinContainer::~BinContainer() {
    // Unsaved changes are not lost when the window goes, and the bins' locks
    // are given up.
    if (bins_) saveAll();
    for (BinDocument* d : documents_) releaseOnClose(d);
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

// Every BinSidebar on this container registers itself, so refreshTree()
// reaches them all; it leaves the list when it is destroyed.
void BinContainer::registerSidebar(BinSidebar* sidebar) {
    sidebars_ << sidebar;
    applySidebarAppearance(sidebar);
    connect(sidebar, &QObject::destroyed, this, [this, sidebar] { sidebars_.removeAll(sidebar); });
}

// What the container does with an area's signals: the main area's and every
// floating one's.
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

// The Sidebar gets its share once, at the first real size; after that the
// user's splitter position is kept.
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

// Re-runs the quick filter of every open bin that has one.
void BinContainer::setQuickFilterAllColumns(bool all) {
    if (BinFilterModel::searchAllColumns() == all) return;
    BinFilterModel::searchAllColumns() = all;
    for (BinDocument* d : documents_) {
        if (BinView* v = viewFor(d); v && !v->quickFilter().isEmpty()) v->setQuickFilter(v->quickFilter());
    }
}

// Only a bin that was not open already floats; an open one just comes to the
// front.
BinDocument* BinContainer::openBinAsSet(const QString& id, bool takeLock) {
    const bool wasOpen = document(canonicalBin(id)) != nullptr;
    BinDocument* doc = openBin(id, takeLock);
    const bool invert = QApplication::keyboardModifiers() & Qt::ControlModifier;
    if (doc && !wasOpen && openFloating_ != invert) floatBin(doc);
    return doc;
}

void BinContainer::setAutoSaveMinutes(int minutes) {
    setAutoSaveTiming(minutes * 60 * 1000, autoSaveInactivity_, autoSaveForce_);
}

void BinContainer::setAutoSaveTiming(int intervalMs, int inactivityMs, int forceMs) {
    const bool changed = intervalMs != autoSaveInterval_;
    autoSaveInterval_ = intervalMs;
    autoSaveInactivity_ = std::max(0, inactivityMs);
    autoSaveForce_ = forceMs;
    if (intervalMs <= 0) {
        autoSave_->stop();
        return;
    }
    // A new interval counts from now; the inactivity and force values only
    // change when the save that is waiting runs.
    if (changed || !sinceAutoSave_.isValid()) sinceAutoSave_.start();
    autoSave_->start(std::clamp(std::min(intervalMs, std::max(autoSaveInactivity_, 1)) / 4, 10, 1000));
}

// Due once the interval has passed; then saved after the inactivity period,
// or at once when "Force Auto-Save at" has passed too (p.1152-1153).
void BinContainer::autoSaveTick() {
    if (!sinceAutoSave_.isValid() || sinceAutoSave_.elapsed() < autoSaveInterval_) return;
    const bool idle = sinceInput_.elapsed() >= autoSaveInactivity_;
    const bool forced = autoSaveForce_ > 0 && sinceAutoSave_.elapsed() >= std::max(autoSaveForce_, autoSaveInterval_);
    if (!idle && !forced) return;
    sinceAutoSave_.start();
    lastAutoSaveAt_ = QDateTime::currentDateTime();
    QString error;
    bool reported = false;
    for (BinDocument* d : documents_) {
        if (!d->isDirty()) continue;
        if (QString why; !d->save(&why)) {
            // A lock refusing the save is not shown with the setting on (p.1154).
            if (skipLockedPrompts_ && d->isReadOnly()) continue;
            error = why;
            reported = true;
        }
    }
    saveTree();
    refreshTree();
    if (reported) emit saveFailed({}, error);
}

bool BinContainer::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        sinceInput_.start();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

QString BinContainer::atticRoot() const {
    if (projectFolder_.isEmpty()) return {};
    if (projectShared_) {
        return QDir::cleanPath(QDir(projectFolder_).filePath(QStringLiteral("../%1 Attic").arg(QString::fromUtf8(branding::kProductName))));
    }
    return atticRoot_;
}

// Only the project's own bins: a Favorite Bin belongs to another project.
void BinContainer::copyToAttic(const BinDocument* document) {
    if (!bins_ || FavoriteBins::isFavoriteId(document->nodeId())) return;
    const QString root = atticRoot();
    if (root.isEmpty()) return;
    // A failed copy does not fail the save: the bin itself is written.
    model::saveToAttic(root, projectName_, document->name(), document->path(), atticLimits_);
}

void BinContainer::setProject(const QString& folder, const QString& name, const Rational& editRate) {
    // Closing the old project: save everything, then delete the views,
    // documents and floating containers directly (not deleteLater), before
    // the bin list goes. saveAll() has already recorded which bins were open.
    if (bins_) {
        // The project is closing either way; a failure is at least reported.
        if (QString error; !saveAll(&error)) emit saveFailed({}, error);
        while (!documents_.isEmpty()) {
            BinDocument* d = documents_.takeLast();
            releaseOnClose(d);
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
    projectFolder_ = folder;
    projectName_ = name;
    locks_.clear();
    lockTimer_->stop();
    projectShared_ = false;
    if (!folder.isEmpty()) {
        bins_ = std::make_unique<model::ProjectBins>(folder, name);
        // On shared storage each machine keeps its own list of open bins, in
        // a folder named for it, as Media Composer keeps a project folder for
        // each computer (p.301); the bin list itself is shared.
        projectShared_ = model::isSharedLocation(folder);
        if (projectShared_) {
            bins_->setOpenBinsFile(
                QDir(folder).filePath(QStringLiteral("%1/open-bins.json").arg(lockUser_.machine.isEmpty() ? QStringLiteral("this")
                                                                                                         : lockUser_.machine)));
        }
        bins_->load();
        treeReadAt_ = QFileInfo(bins_->treeFilePath()).lastModified();
        if (projectShared_) {
            for (const model::BinNode* n : bins_->bins()) readLock(n->id);
        }
        lockTimer_->start(2000);
        // Copied: openBin() rewrites openBins as each bin opens.
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

// Saves the bin list, with the bins open in every area, so the project
// reopens them.
void BinContainer::saveTree() {
    if (!bins_) return;
    bins_->openBins.clear();
    for (const BinDockArea* a : areas()) {
        for (const BinView* v : a->views()) bins_->openBins << v->document().nodeId();
    }
    bins_->save();
    // Our own write is not someone else's change (pollLocks()).
    treeReadAt_ = QFileInfo(bins_->treeFilePath()).lastModified();
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

// Emits activeBinChanged() only when the active bin really changes.
void BinContainer::setActiveView(BinView* view) {
    if (active_ == view) return;
    active_ = view;
    emit activeBinChanged();
}

// `active_` may point at a view that has left every area; fall back to the
// current tab of the first area that has one.
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
        // "the bin title provides a status in parentheses indicating that the
        // locked bin has been modified" (p.302).
        if (document->lockState() == BinDocument::LockState::Modified) text += tr(" (modified)");
        if (BinDockArea* a = areaOf(v)) {
            a->setTitle(v, text);
            // "apply the bin background color to tabs only" (p.1201).
            const BinAppearance& look = BinAppearance::current();
            const QString& background = document->bin().display.background;
            a->setTabColor(v, look.backgrounds && look.tabsOnly && !background.isEmpty() ? QColor(background) : QColor());
        }
    }
}

void BinContainer::setBinAppearance(const BinAppearance& appearance) {
    if (BinAppearance::current() == appearance) return;
    BinAppearance::current() = appearance;
    for (BinDocument* d : documents_) {
        if (BinView* v = viewFor(d)) v->refreshAppearance();
        updateTab(d);
    }
}

void BinContainer::setSidebarAppearance(const QColor& background, const QString& fontFamily, int fontSize) {
    sidebarBackground_ = background;
    sidebarFont_ = fontFamily;
    sidebarFontSize_ = fontSize;
    for (BinSidebar* s : sidebars_) applySidebarAppearance(s);
}

// The list's Base colour and its font; the quick filter keeps the theme's. Roles and font attributes
// not set here are not resolved, so they keep following the theme.
void BinContainer::applySidebarAppearance(BinSidebar* sidebar) const {
    QTreeView* list = sidebar->tree();
    if (!list) return;
    QPalette p;
    if (sidebarBackground_.isValid()) {
        p.setColor(QPalette::Base, sidebarBackground_);
        p.setColor(QPalette::AlternateBase, sidebarBackground_.darker(110));
    }
    list->setPalette(p);
    QFont f;
    if (!sidebarFont_.isEmpty()) f.setFamily(sidebarFont_);
    if (sidebarFontSize_ > 0) f.setPointSize(sidebarFontSize_);
    list->setFont(f);
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

// Opening a bin: read its file, let the loader point managed clips at their
// media, wrap it in a BinDocument and a BinView, connect the view's signals to
// the container, and tab it into the main area. The view's own drops are
// queued so that the source bin is not changed while its drag is still
// running.
BinDocument* BinContainer::openBin(const QString& requested, bool takeLock) {
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
    // The name shown is the Sidebar's (for a Favorite Bin, its file's), not
    // the one stored in the file.
    auto bin = model::BinFile::read(path);
    if (!bin) return nullptr;
    bin->name = name;
    if (binLoader_) binLoader_(*bin);
    auto* doc = new BinDocument(id, path, *bin, this);
    documents_ << doc;
    if (model::isSharedLocation(path)) lockOnOpen(doc, takeLock);
    auto* view = new BinView(*doc, binMenu_);
    view->setInputMenu(inputMenu_);
    connect(doc, &BinDocument::dirtyChanged, this, [this, doc] {
        updateTab(doc);
        emit dirtyChanged();
    });
    connect(doc, &BinDocument::displayChanged, this, [this, doc] { updateTab(doc); });
    connect(doc, &BinDocument::itemsChanged, this, &BinContainer::binItemsChanged);
    connect(doc, &BinDocument::saved, this, [this, doc] { copyToAttic(doc); });
    connect(view, &BinView::deleteRequested, this, [this] { deleteSelection(); });
    connect(view, &BinView::lockButtonClicked, this, [this, doc](bool alt) { lockButtonClicked(doc, alt); });
    connect(view, &BinView::volumePropertiesClicked, this, [this, doc] { emit exportVolumePropertiesRequested(doc->nodeId()); });
    connect(view, &BinView::volumeCommitClicked, this, [this, doc] { emit exportVolumeCommitRequested(doc->nodeId()); });
    connect(doc, &BinDocument::lockChanged, this, [this, doc] {
        updateTab(doc);
        refreshTree();
    });
    connect(doc, &BinDocument::writeRefused, this, [this, doc] { emit lockRefused(doc->nodeId()); });
    // A view saved in one bin is offered in every bin's Bin View menu.
    connect(view, &BinView::viewSaved, this, [this] {
        refreshViewMenus();
        emit binViewsChanged();
    });
    connect(view, &BinView::itemActivated, this,
            [this, doc](const QString& itemId) { emit itemActivated(doc->nodeId(), itemId); });
    connect(view, &BinView::editTemplateRequested, this,
            [this, doc](const QString& itemId) { emit editTemplateRequested(doc->nodeId(), itemId); });
    connect(view, &BinView::commandRequested, this, &BinContainer::commandRequested);
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

// A bin that cannot be saved stays open with its changes, so nothing is lost;
// the failure is reported with saveFailed().
bool BinContainer::closeBin(BinDocument* document, QString* error) {
    if (!document || !documents_.contains(document)) return false;
    if (document->isDirty()) {
        QString why;
        if (!document->save(&why)) {
            if (error) *error = why;
            emit saveFailed(document->name(), why);
            return false;
        }
    }
    releaseOnClose(document);
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
    return true;
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

// Keeps going after a failure; `error` holds the last one.
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

// Moves an open bin's view into `area`: tabbed into or beside `target`, or
// into the area's current group if `target` is null. Floating containers left
// empty close.
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

QJsonObject BinContainer::binLayout() const {
    auto ids = [](const BinDockArea* area) {
        QJsonArray out;
        for (const BinView* v : area->views()) out.append(v->document().nodeId());
        return out;
    };
    QJsonArray floating;
    for (const FloatingBinContainer* f : floating_) {
        const QRect g = f->geometry();
        floating.append(QJsonObject{{QStringLiteral("geometry"), QJsonArray{g.x(), g.y(), g.width(), g.height()}},
                                    {QStringLiteral("bins"), ids(f->area())}});
    }
    return QJsonObject{{QStringLiteral("main"), ids(area_)}, {QStringLiteral("floating"), floating}};
}

void BinContainer::applyBinLayout(const QJsonObject& layout) {
    QStringList wanted;
    for (const QJsonValue& v : layout.value(QStringLiteral("main")).toArray()) wanted << v.toString();
    const QJsonArray floating = layout.value(QStringLiteral("floating")).toArray();
    for (const QJsonValue& f : floating) {
        for (const QJsonValue& v : f.toObject().value(QStringLiteral("bins")).toArray()) wanted << v.toString();
    }
    for (BinDocument* d : QList<BinDocument*>(documents_)) {
        if (!wanted.contains(d->nodeId())) closeBin(d);
    }
    for (const QJsonValue& v : layout.value(QStringLiteral("main")).toArray()) {
        BinDocument* d = openBin(v.toString());
        if (!d) continue;
        if (BinView* view = viewFor(d); view && areaOf(view) != area_) dockBin(d, nullptr, BinDropSide::Tab);
    }
    for (const QJsonValue& fv : floating) {
        const QJsonObject f = fv.toObject();
        const QJsonArray g = f.value(QStringLiteral("geometry")).toArray();
        const QRect rect(g.at(0).toInt(), g.at(1).toInt(), g.at(2).toInt(520), g.at(3).toInt(360));
        FloatingBinContainer* window = nullptr;
        for (const QJsonValue& v : f.value(QStringLiteral("bins")).toArray()) {
            BinDocument* d = openBin(v.toString());
            if (!d) continue;
            if (!window) {
                window = floatBin(d, rect);
            } else if (!window->area()->groups().isEmpty()) {
                dockBin(d, window->area()->groups().first(), BinDropSide::Tab);
            }
        }
    }
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
    // A new window over the main one; MainWindow is told so that the window
    // gets the application's commands too.
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

// A bin tab dragged out of its bar: a drag carrying the bin's id with a faded
// picture of the bin. exec() blocks until the drop; an area that takes it
// moves the bin through binDropped.
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

// Where New Bin and New Folder put the entry, for a right-click on `id`: the
// folder itself, a bin's folder, or the top level (empty) for anything else,
// including the Trash and what is in it.
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
    // "Default background color for new bins", "Default font for new bins" (p.1201).
    if (!newBinBackground_.isEmpty() || !newBinFont_.isEmpty()) {
        const QString path = bins_->binPath(id);
        if (auto bin = model::BinFile::read(path)) {
            if (!newBinBackground_.isEmpty()) bin->display.background = newBinBackground_;
            if (!newBinFont_.isEmpty()) bin->display.fontFamily = newBinFont_;
            model::BinFile::write(path, *bin);
        }
    }
    // "a backup copy is placed in the Avid Attic folder" (p.295).
    if (const QString root = atticRoot(); !root.isEmpty()) {
        model::saveToAttic(root, projectName_, bins_->node(id)->name, bins_->binPath(id), atticLimits_);
    }
    saveTree();
    emit binCreated(id);
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
    if (!bins_ || FavoriteBins::isFavoriteId(id) || refuseLocked(id, error)) return false;
    BinDocument* doc = document(id);
    if (doc && doc->isDirty() && !doc->save(error)) return false;
    if (!bins_->rename(id, name, error)) return false;
    if (doc) {
        doc->setPath(bins_->binPath(id));
        doc->setName(bins_->node(id)->name);
        updateTab(doc);
    }
    saveTree();
    // Later: this may be called from the tree's own editor (setData), which
    // must not have its model reset under it.
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
    // Nothing goes when a bin inside is someone else's.
    bool held = false;
    std::function<void(const model::BinNode&)> checkHeld = [&](const model::BinNode& node) {
        held = held || refuseLocked(node.id, error);
        for (const model::BinNode& c : node.children) checkHeld(c);
    };
    checkHeld(*n);
    if (held) return false;
    // Close what is open inside it first.
    // A bin that cannot be saved stays open, and nothing moves to the Trash.
    bool closed = true;
    std::function<void(const model::BinNode&)> closeOpen = [&](const model::BinNode& node) {
        if (BinDocument* d = document(node.id)) closed = closeBin(d, error) && closed;
        for (const model::BinNode& c : node.children) closeOpen(c);
    };
    closeOpen(*n);
    if (!closed) return false;
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
    if (!bins_ || FavoriteBins::isFavoriteId(id) || refuseLocked(id, error)) return false;
    const model::BinNode* target = bins_->node(parentFolder);
    bool ok = false;
    if (target && target->kind == model::BinNode::Kind::Trash) {
        ok = deleteNode(id, error); // dropping into the Trash deletes
    } else {
        ok = bins_->move(id, parentFolder, index, error);
        if (ok) {
            // Refreshed later, as this runs inside the tree's dropMimeData.
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
    emit favoritesChanged();
    return true;
}

void BinContainer::removeFromFavorites(const QString& path) {
    if (FavoriteBins::paths().removeAll(path) == 0) return;
    // An open Favorite Bin from elsewhere stays open until it is closed.
    refreshTree();
    emit favoritesChanged();
}

void BinContainer::refreshViewMenus() {
    for (BinDocument* d : documents_) {
        if (BinView* v = viewFor(d)) v->updateViewMenu();
    }
}

// Showing the Sidebar also ends Show Sidebar Only; Show Sidebar Only shows it.
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

// The menu's sections: New Bin and New Folder always; Open and Delete for a
// Favorite Bin; Open, Rename and Delete for a project entry; Flat View, Empty
// Trash and Hide Sidebar for the list itself (Empty Trash also on the Trash).
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
        // For the selected bins, or the one right-clicked (p.302).
        if (n->kind == model::BinNode::Kind::Bin && !bins_->isInTrash(id) && model::isSharedLocation(bins_->binPath(id))) {
            QStringList ids = sidebar->selectedNodes();
            if (!ids.contains(id)) ids = {id};
            menu->addSeparator();
            menu->addAction(tr("Lock Project Bin"), this, [this, ids] { lockProjectBins(ids, model::BinLockKind::Locked); });
            menu->addAction(tr("Protect Project Bin"), this,
                            [this, ids] { lockProjectBins(ids, model::BinLockKind::Protected); });
            menu->addAction(tr("Unlock Project Bin"), this, [this, ids] { unlockProjectBins(ids); });
        }
        // Copy Media on the selected bins and folders, or the one right-clicked (p.512).
        if ((n->kind == model::BinNode::Kind::Bin || n->kind == model::BinNode::Kind::Folder) && !bins_->isInTrash(id)) {
            QStringList ids = sidebar->selectedNodes();
            if (!ids.contains(id)) ids = {id};
            menu->addSeparator();
            QAction* copy = menu->addAction(tr("Copy Media"), this, [this, ids] { emit copyMediaRequested(ids); });
            copy->setObjectName(QStringLiteral("sidebarCopyMedia"));
        }
        // "right-click on a bin and choose Include Bin in Transcription or Exclude Bin from Transcription" (p.447): the
        // Bin menu's commands, on the Sidebar's selection.
        if (n->kind == model::BinNode::Kind::Bin && !bins_->isInTrash(id) && transcriptionStatus_) {
            if (!sidebar->selectedNodes().contains(id)) sidebar->tree()->setCurrentIndex(sidebar->model()->indexOf(id));
            menu->addSeparator();
            menu->addAction(tr("Include Bin in Transcription"), this,
                            [this] { emit commandRequested(QStringLiteral("bin.include_in_transcription")); });
            menu->addAction(tr("Exclude Bin from Transcription"), this,
                            [this] { emit commandRequested(QStringLiteral("bin.exclude_from_transcription")); });
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

// ---------------------------------------------------------------- shared bins

namespace {

// The state of a bin held by `holder`, for `me`: red when someone else holds
// it, and for everyone when it is protected (p.302).
BinDocument::LockState stateFor(const model::BinLockHolder& holder, const model::BinLockHolder& me) {
    if (holder.kind == model::BinLockKind::Protected) return BinDocument::LockState::Protected;
    return holder.isSameAs(me) ? BinDocument::LockState::Mine : BinDocument::LockState::Others;
}

QDateTime modifiedAt(const QString& path) {
    return QFileInfo(path).lastModified();
}

} // namespace

void BinContainer::setLockUser(const QString& user) {
    // Open bins keep the user that took their locks, so that they release them.
    lockUser_ = model::localBinLockHolder(user);
}

// A bin opened without the lock (Alt+double-click) is red while someone holds
// it and blue while no one does; a lock this user already holds is theirs.
void BinContainer::lockOnOpen(BinDocument* document, bool take) {
    document->setLockUser(lockUser_);
    readAt_[document] = modifiedAt(document->path());
    std::optional<model::BinLockHolder> holder;
    if (take) {
        model::BinLockHolder got;
        switch (model::acquireBinLock(document->path(), lockUser_, &got)) {
        case model::BinLockResult::Acquired:
        case model::BinLockResult::Held: holder = got; break;
        case model::BinLockResult::Failed:
            // The lock file cannot be made (a read-only share): the bin can be
            // read, not changed.
            document->setLockState(BinDocument::LockState::Others);
            readLock(document->nodeId());
            return;
        }
    } else {
        holder = model::readBinLock(document->path());
    }
    document->setLockState(holder ? stateFor(*holder, lockUser_) : BinDocument::LockState::Released, holder);
    readLock(document->nodeId());
}

// Only a lock this bin took is given up; a Locked or Protected one stays
// (releaseBinLock()).
void BinContainer::releaseOnClose(BinDocument* document) {
    readAt_.remove(document);
    if (document->lockState() != BinDocument::LockState::Mine || !document->lockUser()) return;
    model::releaseBinLock(document->path(), *document->lockUser(), false);
    readLock(document->nodeId());
}

void BinContainer::readLock(const QString& id) {
    if (!bins_ || FavoriteBins::isFavoriteId(id) || !bins_->node(id)) return;
    const QString path = bins_->binPath(id);
    if (!projectShared_ && !model::isSharedLocation(path)) {
        locks_.remove(id);
        return;
    }
    locks_[id] = model::readBinLock(path);
}

std::optional<model::BinLockHolder> BinContainer::lockOf(const QString& id) const {
    return locks_.value(canonicalBin(id));
}

void BinContainer::setTranscriptionStatus(TranscriptionStatus status) {
    transcriptionStatus_ = std::move(status);
    refreshTree();
}

std::optional<bool> BinContainer::transcriptionStatus(const QString& id) const {
    if (!transcriptionStatus_ || !bins_) return std::nullopt;
    const model::BinNode* n = bins_->node(id);
    if (!n || n->kind != model::BinNode::Kind::Bin) return std::nullopt;
    return transcriptionStatus_(id);
}

bool BinContainer::isLockedByOther(const QString& id) const {
    const auto lock = lockOf(id);
    return lock && !lock->isSameAs(lockUser_);
}

bool BinContainer::refuseLocked(const QString& id, QString* error) const {
    if (!isLockedByOther(id)) return false;
    if (error) *error = tr("%1 is locked by %2").arg(nodeName(id), lockOf(id)->describe());
    return true;
}

// For each open shared bin: a lock lost to someone else turns it red, a free
// lock blue, and a save by the holder (the file newer than when it was read)
// yellow. The Sidebar's bold follows the lock files, and the bin list is read
// again when another system changed it.
void BinContainer::pollLocks() {
    if (!bins_) return;
    using State = BinDocument::LockState;
    bool treeChanged = false;
    if (projectShared_) {
        const QDateTime treeAt = modifiedAt(bins_->treeFilePath());
        if (treeAt.isValid() && treeAt != treeReadAt_) {
            // Someone else's new, renamed or moved bins. This system's open
            // bins are its own (setOpenBinsFile()), so they stay as they are.
            const QStringList open = bins_->openBins;
            if (bins_->load()) {
                bins_->openBins = open;
                for (BinDocument* d : documents_) {
                    if (bins_->node(d->nodeId())) d->setPath(bins_->binPath(d->nodeId()));
                }
            }
            treeReadAt_ = treeAt;
            treeChanged = true;
        }
        const auto before = locks_;
        locks_.clear();
        for (const model::BinNode* n : bins_->bins()) readLock(n->id);
        auto same = [](const std::optional<model::BinLockHolder>& a, const std::optional<model::BinLockHolder>& b) {
            return a.has_value() == b.has_value() && (!a || (a->isSameAs(*b) && a->kind == b->kind));
        };
        for (auto it = locks_.cbegin(); it != locks_.cend() && !treeChanged; ++it) {
            if (!before.contains(it.key()) || !same(before.value(it.key()), it.value())) treeChanged = true;
        }
        treeChanged = treeChanged || before.size() != locks_.size();
    }
    for (BinDocument* d : documents_) {
        const State state = d->lockState();
        if (state == State::NotShared) continue;
        const auto holder = model::readBinLock(d->path());
        const model::BinLockHolder me = d->lockUser().value_or(lockUser_);
        if (!holder) {
            // Mine lost (someone removed the file), or let go by its holder.
            d->setLockState(State::Released);
        } else if (holder->isSameAs(me)) {
            if (holder->kind == model::BinLockKind::Protected) {
                d->setLockState(State::Protected, holder);
            } else if (state == State::Mine || state == State::Protected) {
                d->setLockState(State::Mine, holder);
            }
        } else if (holder->kind == model::BinLockKind::Protected) {
            d->setLockState(State::Protected, holder);
        } else if (state == State::Modified || modifiedAt(d->path()) > readAt_.value(d)) {
            d->setLockState(State::Modified, holder);
        } else {
            d->setLockState(State::Others, holder);
        }
    }
    if (treeChanged) refreshTree();
}

void BinContainer::lockButtonClicked(BinDocument* document, bool alt) {
    if (!document || !documents_.contains(document)) return;
    using State = BinDocument::LockState;
    const State state = document->lockState();
    // Read again what the holder saved (p.302): the bin turns red.
    auto reread = [this, document] {
        auto bin = model::BinFile::read(document->path());
        if (!bin) return false;
        if (binLoader_) binLoader_(*bin);
        document->reload(*bin);
        readAt_[document] = modifiedAt(document->path());
        return true;
    };
    if (state == State::Modified) {
        if (reread()) document->setLockState(State::Others, document->lockHolder());
        return;
    }
    if (state == State::Released) {
        // "If you click the blue icon, you now control the bin and the icon
        // turns green" (p.303); what was saved since it was read comes first.
        model::BinLockHolder got;
        const model::BinLockResult r = model::acquireBinLock(document->path(), lockUser_, &got);
        if (r == model::BinLockResult::Failed) return;
        if (modifiedAt(document->path()) > readAt_.value(document)) reread();
        document->setLockUser(lockUser_);
        document->setLockState(stateFor(got, lockUser_), got);
        readLock(document->nodeId());
        return;
    }
    if (state == State::Mine && alt) {
        // "You can release the lock by Alt + clicking the green icon" (p.303),
        // after saving what was changed.
        if (document->isDirty()) {
            if (QString why; !document->save(&why)) {
                emit saveFailed(document->name(), why);
                return;
            }
        }
        if (document->lockUser() && model::releaseBinLock(document->path(), *document->lockUser(), true)) {
            document->setLockState(State::Released);
            readLock(document->nodeId());
        }
        return;
    }
    showLockHistory(document);
}

QWidget* BinContainer::showLockHistory(BinDocument* document) {
    if (!document) return nullptr;
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("binLockHistory"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("%1 History").arg(document->name()));
    auto* text = new QPlainTextEdit(dialog);
    text->setObjectName(QStringLiteral("binLockHistoryText"));
    text->setReadOnly(true);
    QStringList lines;
    if (const auto lock = model::readBinLock(document->path())) {
        lines << tr("Locked by %1 since %2").arg(lock->describe(), QLocale().toString(lock->since.toLocalTime(), QLocale::ShortFormat));
    } else {
        lines << tr("Not locked");
    }
    lines << QString();
    // "which computers and users have modified the bin and the date and time
    // of the modifications" (p.301): the newest first.
    const QList<model::BinHistoryEntry> history = model::readBinHistory(document->path());
    for (auto it = history.crbegin(); it != history.crend(); ++it) {
        lines << QStringLiteral("%1\t%2\t%3\t%4")
                     .arg(QLocale().toString(it->when.toLocalTime(), QLocale::ShortFormat), it->user, it->machine, it->action);
    }
    if (history.isEmpty()) lines << tr("No changes recorded.");
    text->setPlainText(lines.join(QLatin1Char('\n')));
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(text);
    dialog->resize(520, 320);
    dialog->show();
    return dialog;
}

// Each bin on its own: a refusal leaves the others locked, and `error` says
// which bins were refused.
bool BinContainer::lockProjectBins(const QStringList& ids, model::BinLockKind kind, QString* error) {
    if (!bins_) return false;
    QStringList problems;
    model::BinLockHolder me = lockUser_;
    me.kind = kind;
    for (const QString& requested : ids) {
        const QString id = canonicalBin(requested);
        const model::BinNode* n = bins_->node(id);
        if (!n || n->kind != model::BinNode::Kind::Bin) continue;
        const QString path = bins_->binPath(id);
        if (!model::isSharedLocation(path)) {
            problems << tr("%1 is not on shared storage").arg(n->name);
            continue;
        }
        BinDocument* doc = document(id);
        // Changes go in before a protected bin turns read-only.
        if (doc && doc->isDirty() && !doc->isReadOnly()) {
            if (QString why; !doc->save(&why)) {
                problems << tr("%1: %2").arg(n->name, why);
                continue;
            }
        }
        model::BinLockHolder got;
        QString why;
        switch (model::acquireBinLock(path, me, &got, &why)) {
        case model::BinLockResult::Acquired:
            model::appendBinHistory(path, me, kind == model::BinLockKind::Protected ? QStringLiteral("protected") : QStringLiteral("locked"));
            if (doc) {
                // A bin opened without the lock may be older than the file.
                if (doc->isReadOnly() && got.kind != model::BinLockKind::Protected &&
                    modifiedAt(path) > readAt_.value(doc)) {
                    if (auto bin = model::BinFile::read(path)) {
                        if (binLoader_) binLoader_(*bin);
                        doc->reload(*bin);
                        readAt_[doc] = modifiedAt(path);
                    }
                }
                doc->setLockUser(lockUser_);
                doc->setLockState(stateFor(got, lockUser_), got);
            }
            break;
        case model::BinLockResult::Held: problems << tr("%1 is locked by %2").arg(n->name, got.describe()); break;
        case model::BinLockResult::Failed: problems << tr("%1: %2").arg(n->name, why); break;
        }
        readLock(id);
    }
    refreshTree();
    if (error) *error = problems.join(QLatin1Char('\n'));
    return problems.isEmpty();
}

// A bin this user still has open keeps an ordinary lock: unlocking ends only
// the Locked or Protected one.
bool BinContainer::unlockProjectBins(const QStringList& ids, QString* error) {
    if (!bins_) return false;
    QStringList problems;
    for (const QString& requested : ids) {
        const QString id = canonicalBin(requested);
        const model::BinNode* n = bins_->node(id);
        if (!n || n->kind != model::BinNode::Kind::Bin) continue;
        const QString path = bins_->binPath(id);
        if (!model::isSharedLocation(path)) continue;
        const auto lock = model::readBinLock(path);
        if (!lock) continue;
        QString why;
        if (!model::releaseBinLock(path, lockUser_, true, &why)) {
            problems << tr("%1: %2").arg(n->name, why);
            continue;
        }
        model::appendBinHistory(path, lockUser_, QStringLiteral("unlocked"));
        if (BinDocument* doc = document(id)) {
            using State = BinDocument::LockState;
            if (doc->lockState() == State::Mine || doc->lockState() == State::Protected) {
                model::BinLockHolder got;
                if (model::acquireBinLock(path, lockUser_, &got) == model::BinLockResult::Acquired) {
                    doc->setLockState(State::Mine, got);
                } else {
                    doc->setLockState(State::Released);
                }
            }
        }
        readLock(id);
    }
    refreshTree();
    if (error) *error = problems.join(QLatin1Char('\n'));
    return problems.isEmpty();
}

// ---------------------------------------------------------------- bin contents

QString BinContainer::newSequence(const QString& targetBin) {
    BinDocument* doc = targetBin.isEmpty() ? activeDocument() : openBin(targetBin);
    if (!doc) return {};
    // A bin someone else holds cannot be chosen for new items (p.304).
    if (doc->isReadOnly()) {
        emit lockRefused(doc->nodeId());
        return {};
    }
    model::BinItem item = model::newSequence(doc->bin(), editRate_, QDateTime::currentDateTimeUtc(), startTimecode_);
    if (defaultSyncLocks_) model::syncLockAllTracks(item);
    doc->addItem(item);
    if (BinView* v = viewFor(doc)) v->selectIds({item.id});
    return item.id;
}

// Duplicates get new ids, and names as model::duplicateItem() makes them.
QStringList BinContainer::duplicateSelection() {
    BinView* view = activeView();
    if (!view) return {};
    BinDocument& doc = view->document();
    if (doc.isReadOnly()) {
        emit lockRefused(doc.nodeId());
        return {};
    }
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
    if (view->document().isReadOnly()) {
        emit lockRefused(view->document().nodeId());
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
    // "You cannot drag an item to a locked bin"; one dragged from a locked bin
    // to a writable one is duplicated there, and stays where it was (p.304).
    if (target->isReadOnly()) {
        emit lockRefused(target->nodeId());
        return {};
    }
    const bool fromLocked = source->isReadOnly();
    // An Export Volume (7.2d): "Select and drag the sequence(s) to the Export Volume. A copy of the sequence(s)
    // appears"; "You can only drag sequences ... you will receive a message ... open the Console Tool to see the list of
    // items that were not written" (p.903-904). The copies are Not Committed.
    if (model::exportVolumeOf(target->bin())) {
        QStringList refused;
        QStringList added;
        for (const QString& id : ids) {
            const model::BinItem* item = source->bin().item(id);
            if (!item) continue;
            if (item->kind != model::BinItemKind::Sequence) {
                refused << item->name;
                continue;
            }
            model::BinItem copied = model::duplicateItem(target->bin(), *item, QDateTime::currentDateTimeUtc());
            copied.name = item->name;
            copied.fields.insert(model::kMediaStatusHeading, model::kNotCommitted);
            target->addItem(copied);
            added << copied.id;
        }
        if (!refused.isEmpty()) emit volumeRefused(target->nodeId(), refused);
        if (!added.isEmpty()) {
            model::BinDisplay shown = target->bin().display;
            if (!shown.columns.contains(model::kMediaStatusHeading)) {
                if (!shown.customColumns.contains(model::kMediaStatusHeading)) shown.customColumns << model::kMediaStatusHeading;
                shown.columns << model::kMediaStatusHeading;
                target->setDisplay(shown);
            }
        }
        if (BinView* v = viewFor(target)) v->selectIds(added);
        return added;
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
    QStringList moved;
    for (const QString& id : ids) {
        const model::BinItem* item = source->bin().item(id);
        // A copy is the same clip in another bin, so it keeps its identity
        // and, unlike Duplicate, its name (p.260).
        if (!item || target->bin().item(id)) continue;
        if (fromLocked) {
            const model::BinItem duplicate = model::duplicateItem(target->bin(), *item, QDateTime::currentDateTimeUtc());
            target->addItem(duplicate);
            added << duplicate.id;
            continue;
        }
        // Locking prevents deletion (p.265), so a moved locked item is only
        // copied: it stays in the source bin, as Delete leaves it.
        if (!item->locked) moved << id;
        target->addItem(*item);
        added << id;
    }
    if (!copy && !fromLocked) source->removeItems(moved);
    if (BinView* v = viewFor(target)) v->selectIds(added);
    return added;
}

} // namespace lumenis::ui

#include "lumenis/ui/bin_view.h"

#include "lumenis/ui/theme.h"

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace lumenis::ui {

namespace {

constexpr int kIconWidth = 26;
constexpr int kNameWidth = 220;
constexpr int kColumnWidth = 120;
constexpr int kDateWidth = 160;

GlyphIcon iconFor(model::BinItemKind kind) {
    using K = model::BinItemKind;
    switch (kind) {
    case K::MasterClip: return GlyphIcon::ItemClip;
    case K::Subclip: return GlyphIcon::ItemSubclip;
    case K::AudioClip: return GlyphIcon::ItemAudio;
    case K::Sequence: return GlyphIcon::ItemSequence;
    case K::Source: return GlyphIcon::ItemSource;
    case K::Effect:
    case K::MotionEffect:
    case K::RenderedEffect: return GlyphIcon::ItemEffect;
    case K::Group: return GlyphIcon::ItemGroup;
    case K::Title: return GlyphIcon::ItemTitle;
    }
    return GlyphIcon::ItemClip;
}

QString dateText(const QDateTime& when) {
    // As in the guide's pictures: 7/23/2020 12:52:49 PM.
    return when.isValid() ? QLocale::c().toString(when.toLocalTime(), QStringLiteral("M/d/yyyy h:mm:ss AP")) : QString();
}

QToolButton* toolButton(QWidget* parent, const QString& name, const QIcon& icon, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(icon);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFixedSize(26, kToolButtonHeight);
    return b;
}

} // namespace

// ---------------------------------------------------------------- BinDocument

BinDocument::BinDocument(QString nodeId, QString path, model::Bin bin, QObject* parent)
    : QObject(parent), nodeId_(std::move(nodeId)), path_(std::move(path)), bin_(std::move(bin)) {}

void BinDocument::touch() {
    if (!dirty_) {
        dirty_ = true;
        emit dirtyChanged(true);
    }
}

void BinDocument::setName(const QString& name) {
    bin_.name = name;
    emit displayChanged();
}

void BinDocument::addItem(const model::BinItem& item) {
    bin_.items.append(item);
    touch();
    emit itemsChanged();
}

void BinDocument::removeItems(const QStringList& ids) {
    const auto removed = bin_.items.removeIf([&](const model::BinItem& i) { return ids.contains(i.id); });
    if (removed == 0) return;
    touch();
    emit itemsChanged();
}

bool BinDocument::setField(const QString& id, const QString& heading, const QString& value) {
    model::BinItem* item = bin_.item(id);
    if (!item) return false;
    if (heading == QLatin1String("Name")) {
        if (value.trimmed().isEmpty() || value == item->name) return false;
        item->name = value;
    } else if (bin_.display.customColumns.contains(heading)) {
        item->fields.insert(heading, value);
    } else {
        return false;
    }
    touch();
    emit itemsChanged();
    return true;
}

void BinDocument::setDisplay(const model::BinDisplay& display) {
    bin_.display = display;
    touch();
    emit displayChanged();
}

bool BinDocument::save(QString* error) {
    if (!model::BinFile::write(path_, bin_, error)) return false;
    if (dirty_) {
        dirty_ = false;
        emit dirtyChanged(false);
    }
    return true;
}

// ---------------------------------------------------------------- BinItemModel

BinItemModel::BinItemModel(BinDocument& document, QObject* parent) : QAbstractTableModel(parent), document_(document) {
    connect(&document_, &BinDocument::itemsChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
    connect(&document_, &BinDocument::displayChanged, this, [this] {
        beginResetModel();
        endResetModel();
    });
}

QString BinItemModel::heading(int column) const {
    if (column == kIconColumn) return {};
    if (column == kNameColumn) return QStringLiteral("Name");
    return document_.bin().display.columns.value(column - 2);
}

int BinItemModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(document_.bin().items.size());
}

int BinItemModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 2 + static_cast<int>(document_.bin().display.columns.size());
}

QVariant BinItemModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) return {};
    const model::BinItem& item = document_.bin().items.at(index.row());
    if (role == kIdRole) return item.id;
    const QString h = heading(index.column());
    if (index.column() == kIconColumn) {
        if (role == Qt::DecorationRole) return glyphIcon(iconFor(item.kind));
        return {};
    }
    if (role == Qt::DecorationRole && h == QLatin1String("Color") && !item.color.isEmpty()) {
        return QColor(item.color);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    if (h == QLatin1String("Name")) return item.name;
    if (h == QLatin1String("Creation Date")) return dateText(item.created);
    if (h == QLatin1String("Modified Date")) return dateText(item.modified);
    if (h == QLatin1String("Duration")) return model::durationText(item.durationFrames, item.editRate);
    if (h == QLatin1String("Lock")) return item.locked ? tr("Locked") : QString();
    return item.fields.value(h);
}

bool BinItemModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid()) return false;
    const QString id = document_.bin().items.at(index.row()).id;
    return document_.setField(id, heading(index.column()), value.toString());
}

QVariant BinItemModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) return heading(section);
    return {};
}

Qt::ItemFlags BinItemModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    const QString h = heading(index.column());
    if (h == QLatin1String("Name") || document_.bin().display.customColumns.contains(h)) f |= Qt::ItemIsEditable;
    return f;
}

// ---------------------------------------------------------------- BinView

BinView::BinView(BinDocument& document, QMenu* fastMenu, QWidget* parent) : QWidget(parent), document_(document) {
    setObjectName(QStringLiteral("binView"));

    // Bin View menu, view buttons, quick filter and fast menu (p.249).
    auto* viewMenu = new QComboBox(this);
    viewMenu->setObjectName(QStringLiteral("binViewMenu"));
    viewMenu->addItem(document.bin().display.viewName.isEmpty() ? tr("Untitled") : document.bin().display.viewName);
    viewMenu->setFixedWidth(110);
    auto* textView = toolButton(this, QStringLiteral("textViewButton"), glyphIcon(GlyphIcon::ListView), tr("Text View"));
    auto* frameView = toolButton(this, QStringLiteral("frameViewButton"), glyphIcon(GlyphIcon::FrameView), tr("Frame View"));
    auto* scriptView = toolButton(this, QStringLiteral("scriptViewButton"), glyphIcon(GlyphIcon::ScriptView), tr("Script View"));
    textView->setCheckable(true);
    textView->setChecked(true);
    // Frame and Script views come with step 1.3b.
    frameView->setEnabled(false);
    scriptView->setEnabled(false);
    auto* search = toolButton(this, QStringLiteral("quickFindButton"), glyphIcon(GlyphIcon::Search), tr("Filter"));
    search->setCheckable(true);
    auto* fast = toolButton(this, QStringLiteral("binFastMenu"), glyphIcon(GlyphIcon::FastMenu), tr("Bin Fast Menu"));
    fast->setPopupMode(QToolButton::InstantPopup);
    if (fastMenu) fast->setMenu(fastMenu);

    filter_ = new QLineEdit(this);
    filter_->setObjectName(QStringLiteral("quickFilter"));
    filter_->setPlaceholderText(tr("Filter"));
    filter_->setClearButtonEnabled(true);
    filter_->setVisible(false);
    filter_->setFixedWidth(140);
    connect(search, &QToolButton::toggled, this, [this](bool on) {
        filter_->setVisible(on);
        if (on) {
            filter_->setFocus();
        } else {
            filter_->clear();
        }
    });
    connect(filter_, &QLineEdit::textChanged, this, &BinView::setQuickFilter);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(4, 3, 4, 3);
    top->setSpacing(2);
    top->addStretch(1);
    top->addWidget(viewMenu);
    top->addSpacing(4);
    top->addWidget(textView);
    top->addWidget(frameView);
    top->addWidget(scriptView);
    top->addStretch(1);
    top->addWidget(filter_);
    top->addWidget(search);
    top->addWidget(fast);

    model_ = new BinItemModel(document_, this);
    proxy_ = new QSortFilterProxyModel(this);
    proxy_->setSourceModel(model_);
    proxy_->setFilterKeyColumn(BinItemModel::kNameColumn);
    proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("binTable"));
    table_->setModel(proxy_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(kRowHeight);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table_->setIconSize(QSize(16, 16));
    table_->installEventFilter(this);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("binStatus"));
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_->setFixedHeight(kRowHeight);
    status_->setContentsMargins(0, 0, 8, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(table_, 1);
    layout->addWidget(status_);

    connect(&document_, &BinDocument::displayChanged, this, &BinView::applyDisplay);
    connect(&document_, &BinDocument::itemsChanged, this, &BinView::updateStatus);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BinView::updateStatus);
    connect(proxy_, &QAbstractItemModel::modelReset, this, [this] {
        connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BinView::updateStatus,
                Qt::UniqueConnection);
    });
    applyDisplay();
}

void BinView::applyDisplay() {
    const model::BinDisplay& d = document_.bin().display;
    // Fixed widths, not measured from text, so that bins lay out the same
    // on every platform.
    QHeaderView* header = table_->horizontalHeader();
    header->resizeSection(BinItemModel::kIconColumn, kIconWidth);
    header->setSectionResizeMode(BinItemModel::kIconColumn, QHeaderView::Fixed);
    for (int c = BinItemModel::kNameColumn; c < model_->columnCount(); ++c) {
        const QString h = model_->heading(c);
        const int fallback = c == BinItemModel::kNameColumn ? kNameWidth
                             : h.endsWith(QLatin1String("Date")) ? kDateWidth
                                                                 : kColumnWidth;
        header->resizeSection(c, d.widths.value(h, fallback));
    }
    // Bin background colour (p.263).
    QPalette p = table_->palette();
    if (d.background.isEmpty()) {
        p = palette();
    } else {
        p.setColor(QPalette::Base, QColor(d.background));
        p.setColor(QPalette::AlternateBase, QColor(d.background).darker(110));
    }
    table_->setPalette(p);
    status_->setVisible(d.statusBar);
    updateStatus();
}

void BinView::updateStatus() {
    // "Viewing: 22/22" (p.255), and how many are selected.
    const int selected = static_cast<int>(selectedIds().size());
    QString text = tr("Viewing: %1/%2").arg(visibleCount()).arg(document_.bin().items.size());
    if (selected > 0) text += tr("    Selected: %1").arg(selected);
    status_->setText(text);
}

QStringList BinView::selectedIds() const {
    QStringList ids;
    if (!table_->selectionModel()) return ids;
    for (const QModelIndex& row : table_->selectionModel()->selectedRows()) {
        ids << row.data(BinItemModel::kIdRole).toString();
    }
    return ids;
}

void BinView::selectIds(const QStringList& ids) {
    QItemSelection selection;
    for (int r = 0; r < proxy_->rowCount(); ++r) {
        const QModelIndex i = proxy_->index(r, 0);
        if (ids.contains(i.data(BinItemModel::kIdRole).toString())) {
            selection.select(i, proxy_->index(r, proxy_->columnCount() - 1));
        }
    }
    table_->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
}

void BinView::selectAll() {
    table_->selectAll();
}

void BinView::setQuickFilter(const QString& text) {
    proxy_->setFilterFixedString(text);
    updateStatus();
}

int BinView::visibleCount() const {
    return proxy_->rowCount();
}

bool BinView::eventFilter(QObject* watched, QEvent* event) {
    // While a cell is being typed in, its editor has the keys, not the table.
    if (watched == table_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            emit deleteRequested();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lumenis::ui

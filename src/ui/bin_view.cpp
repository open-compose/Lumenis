#include "lumenis/ui/bin_view.h"

#include "lumenis/ui/bin_dialogs.h"
#include "lumenis/ui/bin_dock.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDrag>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

namespace {

constexpr int kIconWidth = 26;
constexpr int kNameWidth = 220;
constexpr int kColumnWidth = 120;
constexpr int kDateWidth = 160;
constexpr int kColorWidth = 44;
constexpr int kFrameMargin = 8;
constexpr int kScriptTextHeight = 64;
constexpr int kSaveAsRole = Qt::UserRole + 1; // the Bin View menu's Save As

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

QToolButton* toolButton(QWidget* parent, const QString& name, const QIcon& icon, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(icon);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFixedSize(26, kToolButtonHeight);
    return b;
}

// A clip's frame: the head frame of a linked clip (p.250), once decoded;
// otherwise black with the object's icon.
void paintFrame(QPainter& p, const QRect& frame, model::BinItemKind kind, const QImage& picture = {}) {
    p.fillRect(frame, QColor(12, 12, 12));
    if (!picture.isNull()) {
        const QSize fit = picture.size().scaled(frame.size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(frame.left() + (frame.width() - fit.width()) / 2,
                          frame.top() + (frame.height() - fit.height()) / 2, fit.width(), fit.height()),
                    picture);
        return;
    }
    p.setPen(QColor(70, 70, 70));
    p.drawRect(frame.adjusted(0, 0, -1, -1));
    const int s = std::max(12, std::min(frame.width(), frame.height()) / 3);
    const QRect icon(frame.center() - QPoint(s / 2, s / 2), QSize(s, s));
    glyphIcon(iconFor(kind)).paint(&p, icon, Qt::AlignCenter, QIcon::Disabled);
}

QPixmap swatch(const QColor& color) {
    QPixmap pm(14, 14);
    pm.fill(color.isValid() ? color : QColor(Qt::transparent));
    QPainter p(&pm);
    p.setPen(QColor(20, 20, 20));
    p.drawRect(0, 0, 13, 13);
    if (!color.isValid()) p.drawLine(0, 13, 13, 0); // None
    return pm;
}

// Draws Frame view cells: the frame, then the name below it (p.250).
class FrameDelegate : public QStyledItemDelegate {
public:
    explicit FrameDelegate(BinFrameView* view) : QStyledItemDelegate(view), view_(view) {}

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return view_->cellSize(); }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        p->save();
        const QSize f = view_->frameSize(view_->property("frameStep").toInt());
        const QRect cell = option.rect;
        const QRect frame(cell.left() + (cell.width() - f.width()) / 2, cell.top() + kFrameMargin / 2, f.width(),
                          f.height());
        const bool selected = option.state & QStyle::State_Selected;
        if (selected) p->fillRect(frame.adjusted(-3, -3, 3, 3), option.palette.color(QPalette::Highlight));
        paintFrame(*p, frame, static_cast<model::BinItemKind>(index.data(BinItemModel::kKindRole).toInt()),
                   view_->picture(index.data(BinItemModel::kIdRole).toString(), f));
        const QRect text(cell.left() + 2, frame.bottom() + 3, cell.width() - 4, kRowHeight);
        p->setPen(option.palette.color(selected ? QPalette::BrightText : QPalette::WindowText));
        p->drawText(text, Qt::AlignHCenter | Qt::AlignTop,
                    option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                  text.width()));
        p->restore();
    }

private:
    BinFrameView* view_;
};

// Moving between cells while typing (p.270): Tab and Shift+Tab along the
// row, Enter and Shift+Enter down and up the column, wrapping at the ends.
class CellDelegate : public QStyledItemDelegate {
public:
    explicit CellDelegate(QTableView* table) : QStyledItemDelegate(table), table_(table) {}

protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        auto* editor = qobject_cast<QWidget*>(object);
        if (!editor || event->type() != QEvent::KeyPress) return QStyledItemDelegate::eventFilter(object, event);
        const auto* key = static_cast<QKeyEvent*>(event);
        const bool shift = key->modifiers() & Qt::ShiftModifier;
        int dRow = 0;
        int dColumn = 0;
        switch (key->key()) {
        case Qt::Key_Tab: dColumn = shift ? -1 : 1; break;
        case Qt::Key_Backtab: dColumn = -1; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: dRow = shift ? -1 : 1; break;
        default: return QStyledItemDelegate::eventFilter(object, event);
        }
        const QModelIndex at = table_->currentIndex();
        QAbstractItemModel* m = table_->model();
        const int rows = m->rowCount();
        QHeaderView* header = table_->horizontalHeader();
        // Columns in the order shown, without the icon column.
        const int first = 1;
        const int last = header->count() - 1;
        int row = at.row();
        int visual = header->visualIndex(at.column());
        if (dColumn != 0) {
            visual += dColumn;
            if (visual > last) visual = first;
            if (visual < first) visual = last;
        } else if (rows > 0) {
            row = (row + dRow + rows) % rows;
        }
        const int column = header->logicalIndex(visual);
        emit commitData(editor);
        emit closeEditor(editor, QAbstractItemDelegate::NoHint);
        // The model may have been rebuilt by the change: find the cell again.
        QTableView* table = table_;
        QMetaObject::invokeMethod(
            table,
            [table, row, column] {
                const QModelIndex next = table->model()->index(row, column);
                if (!next.isValid()) return;
                table->setCurrentIndex(next);
                if (next.flags() & Qt::ItemIsEditable) table->edit(next);
            },
            Qt::QueuedConnection);
        return true;
    }

private:
    QTableView* table_;
};

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

bool BinDocument::replaceItem(const model::BinItem& item) {
    model::BinItem* existing = bin_.item(item.id);
    if (!existing) return false;
    *existing = item;
    touch();
    emit itemsChanged();
    return true;
}

void BinDocument::removeItems(const QStringList& ids) {
    const auto removed = bin_.items.removeIf([&](const model::BinItem& i) { return ids.contains(i.id); });
    if (removed == 0) return;
    for (const QString& id : ids) bin_.display.framePositions.remove(id);
    touch();
    emit itemsChanged();
}

bool BinDocument::setField(const QString& id, const QString& heading, const QString& value) {
    model::BinItem* item = bin_.item(id);
    if (!item) return false;
    if (heading == QLatin1String("Name")) {
        if (value.trimmed().isEmpty() || value == item->name) return false;
        item->name = value;
    } else if (bin_.display.customColumns.contains(heading) || model::isEditableHeading(heading)) {
        if (item->fields.value(heading) == value) return false;
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

void BinDocument::sort(const QList<model::BinSortKey>& keys) {
    if (keys.isEmpty()) return;
    model::sortItems(bin_, keys);
    bin_.display.sort = keys;
    touch();
    emit itemsChanged();
}

void BinDocument::moveItems(const QStringList& ids, int toIndex) {
    QList<model::BinItem> moving;
    int before = 0;
    for (qsizetype i = 0; i < bin_.items.size(); ++i) {
        if (ids.contains(bin_.items.at(i).id)) {
            moving << bin_.items.at(i);
            if (i < toIndex) ++before;
        }
    }
    if (moving.isEmpty()) return;
    bin_.items.removeIf([&](const model::BinItem& i) { return ids.contains(i.id); });
    const qsizetype at = std::clamp<qsizetype>(toIndex - before, 0, bin_.items.size());
    for (qsizetype k = 0; k < moving.size(); ++k) bin_.items.insert(at + k, moving.at(k));
    touch();
    emit itemsChanged();
}

void BinDocument::setColor(const QStringList& ids, const QString& color) {
    bool changed = false;
    for (const QString& id : ids) {
        if (model::BinItem* i = bin_.item(id); i && i->color != color) {
            i->color = color;
            changed = true;
        }
    }
    if (!changed) return;
    touch();
    emit itemsChanged();
}

void BinDocument::setLocked(const QStringList& ids, bool locked) {
    bool changed = false;
    for (const QString& id : ids) {
        if (model::BinItem* i = bin_.item(id); i && i->locked != locked) {
            i->locked = locked;
            changed = true;
        }
    }
    if (!changed) return;
    touch();
    emit itemsChanged();
}

void BinDocument::setScript(const QString& id, const QString& text) {
    if (model::BinItem* i = bin_.item(id); i && i->script != text) {
        i->script = text;
        touch();
    }
}

void BinDocument::setColumnWidth(const QString& heading, int width) {
    if (bin_.display.widths.value(heading, -1) == width) return;
    bin_.display.widths.insert(heading, width);
    touch();
}

void BinDocument::setFramePositions(const QHash<QString, QPoint>& positions) {
    if (bin_.display.framePositions == positions) return;
    bin_.display.framePositions = positions;
    touch();
}

void BinDocument::deleteCustomColumn(const QString& heading) {
    if (!bin_.display.customColumns.contains(heading)) return;
    bin_.display.customColumns.removeAll(heading);
    bin_.display.columns.removeAll(heading);
    bin_.display.widths.remove(heading);
    for (model::BinItem& i : bin_.items) i.fields.remove(heading);
    touch();
    emit displayChanged();
}

bool BinDocument::renameCustomColumn(const QString& from, const QString& to) {
    const QString name = to.trimmed();
    if (!bin_.display.customColumns.contains(from) || name.isEmpty() || name == from) return false;
    // Standard headings cannot be renamed or reused (p.269).
    if (model::binHeadings().contains(name) || bin_.display.customColumns.contains(name)) return false;
    bin_.display.customColumns.replace(bin_.display.customColumns.indexOf(from), name);
    if (const auto c = bin_.display.columns.indexOf(from); c >= 0) bin_.display.columns.replace(c, name);
    if (bin_.display.widths.contains(from)) bin_.display.widths.insert(name, bin_.display.widths.take(from));
    for (model::BinItem& i : bin_.items) {
        if (i.fields.contains(from)) i.fields.insert(name, i.fields.take(from));
    }
    for (model::BinSortKey& k : bin_.display.sort) {
        if (k.heading == from) k.heading = name;
    }
    touch();
    emit displayChanged();
    return true;
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

int BinItemModel::columnOf(const QString& heading) const {
    if (heading == QLatin1String("Name")) return kNameColumn;
    const auto i = document_.bin().display.columns.indexOf(heading);
    return i < 0 ? -1 : static_cast<int>(i) + 2;
}

void BinItemModel::setSelectedHeadings(const QStringList& headings) {
    selectedHeadings_ = headings;
    if (columnCount() > 0) emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
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
    switch (role) {
    case kIdRole: return item.id;
    case kKindRole: return static_cast<int>(item.kind);
    case kColorRole: return item.color.isEmpty() ? QColor() : QColor(item.color);
    case kSummaryRole:
        // The Frame view's tooltip: Duration, Format and Creation Date (p.250).
        return tr("%1\nDuration: %2\nFormat: %3\nCreation Date: %4")
            .arg(item.name, model::cellText(item, QStringLiteral("Duration")),
                 model::cellText(item, QStringLiteral("Format")),
                 model::cellText(item, QStringLiteral("Creation Date")));
    default: break;
    }
    const QString h = heading(index.column());
    if (index.column() == kIconColumn) {
        if (role == Qt::DecorationRole) return glyphIcon(iconFor(item.kind));
        return {};
    }
    if (h == QLatin1String("Color")) {
        // A swatch, not the colour's name (p.264).
        if (role == Qt::DecorationRole && !item.color.isEmpty()) return QColor(item.color);
        if (role == Qt::ToolTipRole) return model::clipColorName(item.color);
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    return model::cellText(item, h);
}

bool BinItemModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (role != Qt::EditRole || !index.isValid()) return false;
    const QString id = document_.bin().items.at(index.row()).id;
    return document_.setField(id, heading(index.column()), value.toString());
}

QVariant BinItemModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role == Qt::DisplayRole) return heading(section);
    // A clicked heading is highlighted (p.268).
    const bool selected = selectedHeadings_.contains(heading(section));
    if (role == Qt::BackgroundRole && selected) return QApplication::palette().color(QPalette::Highlight);
    if (role == Qt::ForegroundRole && selected) return QApplication::palette().color(QPalette::BrightText);
    return {};
}

Qt::ItemFlags BinItemModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
    const QString h = heading(index.column());
    if (model::isEditableHeading(h) || document_.bin().display.customColumns.contains(h)) f |= Qt::ItemIsEditable;
    return f;
}

Qt::DropActions BinItemModel::supportedDragActions() const {
    return Qt::MoveAction | Qt::CopyAction;
}

Qt::DropActions BinItemModel::supportedDropActions() const {
    return Qt::MoveAction | Qt::CopyAction;
}

QStringList BinItemModel::mimeTypes() const {
    return {binItemsMime(), mediaFilesMime()};
}

QMimeData* BinItemModel::mimeData(const QModelIndexList& indexes) const {
    // The bin first, then the items.
    QStringList lines{document_.nodeId()};
    for (const QModelIndex& i : indexes) {
        const QString id = data(i, kIdRole).toString();
        if (!id.isEmpty() && !lines.contains(id)) lines << id;
    }
    auto* mime = new QMimeData;
    mime->setData(binItemsMime(), lines.join(QLatin1Char('\n')).toUtf8());
    return mime;
}

bool BinItemModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex&) const {
    // Files from the Source Browser are linked into the bin (p.317).
    if (data->hasFormat(mediaFilesMime())) return true;
    if (!data->hasFormat(binItemsMime())) return false;
    const QString from = QString::fromUtf8(data->data(binItemsMime())).section(QLatin1Char('\n'), 0, 0);
    return from != document_.nodeId();
}

bool BinItemModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                const QModelIndex& parent) {
    if (!canDropMimeData(data, action, row, column, parent)) return false;
    if (data->hasFormat(mediaFilesMime())) {
        emit filesDropped(QString::fromUtf8(data->data(mediaFilesMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts));
        return true;
    }
    QStringList lines = QString::fromUtf8(data->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString from = lines.takeFirst();
    // Alt/Option (p.260) or Ctrl (p.261, the platform's copy key) copies.
    const bool copy = action == Qt::CopyAction || (QApplication::keyboardModifiers() & Qt::AltModifier);
    emit itemsDropped(from, lines, copy);
    return true;
}

// ---------------------------------------------------------------- BinFilterModel

BinFilterModel::BinFilterModel(BinDocument& document, QObject* parent)
    : QSortFilterProxyModel(parent), document_(document) {}

void BinFilterModel::setQuickFilter(const QString& text, const QStringList& headings) {
    // Qt 6.10 replaced invalidateFilter() with begin/endFilterChange().
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    quick_ = text;
    quickHeadings_ = headings;
    endFilterChange(Direction::Rows);
#else
    quick_ = text;
    quickHeadings_ = headings;
    invalidateFilter();
#endif
}

bool& BinFilterModel::searchAllColumns() {
    static bool all = false;
    return all;
}

bool BinFilterModel::filterAcceptsRow(int row, const QModelIndex&) const {
    const model::Bin& bin = document_.bin();
    if (row < 0 || row >= bin.items.size()) return false;
    const model::BinItem& item = bin.items.at(row);
    // Set Bin Display (p.288).
    if (bin.display.hiddenKinds.contains(item.kind)) return false;
    if (!quick_.isEmpty()) {
        QStringList searched = quickHeadings_;
        if (searched.isEmpty()) {
            searched << QStringLiteral("Name");
            if (searchAllColumns()) searched << bin.display.columns;
        }
        const bool found = std::any_of(searched.begin(), searched.end(), [&](const QString& h) {
            return model::cellText(item, h).contains(quick_, Qt::CaseInsensitive);
        });
        if (!found) return false;
    }
    if (bin.isSiftShown()) {
        return model::siftMatches(item, bin.display.sift, QStringList{QStringLiteral("Name")} + bin.display.columns);
    }
    return true;
}

// ---------------------------------------------------------------- BinFrameView

BinFrameView::BinFrameView(BinDocument& document, QWidget* parent) : QListView(parent), document_(document) {
    setObjectName(QStringLiteral("binFrames"));
    setViewMode(QListView::IconMode);
    // Frames are moved anywhere; the window does not reflow them (p.250-251).
    setMovement(QListView::Free);
    setResizeMode(QListView::Fixed);
    setWrapping(true);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setItemDelegate(new FrameDelegate(this));
    connect(this, &QListView::indexesMoved, this, &BinFrameView::storePositions);
    if (MediaFrames* frames = MediaFrames::instance()) {
        connect(frames, &MediaFrames::thumbnailReady, viewport(), qOverload<>(&QWidget::update));
    }
}

QImage BinFrameView::picture(const QString& id, QSize size) const {
    const model::BinItem* item = document_.bin().item(id);
    MediaFrames* frames = MediaFrames::instance();
    return item && frames ? frames->thumbnail(*item, 0, size) : QImage();
}

QSize BinFrameView::frameSize(int step) {
    // Eight sizes, 16:9 frames.
    static constexpr int kWidths[model::BinDisplay::kMaxFrameSize + 1] = {64, 80, 96, 120, 144, 176, 216, 264};
    const int w = kWidths[std::clamp(step, 0, model::BinDisplay::kMaxFrameSize)];
    return {w, w * 9 / 16};
}

QSize BinFrameView::cellSize() const {
    const QSize f = frameSize(document_.bin().display.frameSize);
    return {f.width() + 2 * kFrameMargin, f.height() + kFrameMargin + kRowHeight + 6};
}

void BinFrameView::applyDisplay() {
    setProperty("frameStep", document_.bin().display.frameSize);
    setGridSize(cellSize());
    doItemsLayout();
    viewport()->update();
}

void BinFrameView::doItemsLayout() {
    QListView::doItemsLayout();
    if (!model()) return;
    const auto& positions = document_.bin().display.framePositions;
    if (positions.isEmpty()) return;
    QList<std::pair<QModelIndex, QPoint>> places;
    for (int r = 0; r < model()->rowCount(); ++r) {
        const QModelIndex i = model()->index(r, modelColumn());
        const auto it = positions.constFind(i.data(BinItemModel::kIdRole).toString());
        if (it != positions.cend()) places.append({i, it.value()});
    }
    placeFrames(places);
}

void BinFrameView::placeFrames(const QList<std::pair<QModelIndex, QPoint>>& places) {
    for (const auto& [index, pos] : places) setPositionForIndex(pos, index);
}

QPoint BinFrameView::framePosition(const QString& id) const {
    for (int r = 0; model() && r < model()->rowCount(); ++r) {
        const QModelIndex i = model()->index(r, modelColumn());
        if (i.data(BinItemModel::kIdRole).toString() == id) return rectForIndex(i).topLeft();
    }
    return {-1, -1};
}

QPoint BinFrameView::snapped(const QPoint& p) const {
    const QSize c = cellSize();
    auto snap = [](int v, int step) { return std::max(0, (v + step / 2) / step * step); };
    return {snap(p.x(), c.width()), snap(p.y(), c.height())};
}

void BinFrameView::storePositions(const QModelIndexList& moved) {
    QHash<QString, QPoint> positions = document_.bin().display.framePositions;
    const bool snap = document_.bin().display.snap != model::SnapToGrid::Disable;
    for (const QModelIndex& i : moved) {
        QPoint p = rectForIndex(i).topLeft();
        if (snap) {
            // Dropped frames go to the nearest grid point (p.252).
            p = snapped(p);
            setPositionForIndex(p, i);
        }
        positions.insert(i.data(BinItemModel::kIdRole).toString(), p);
    }
    document_.setFramePositions(positions);
}

void BinFrameView::alignToGrid(bool selectedOnly) {
    if (!model()) return;
    // Each frame goes to its nearest free grid cell, the first frames first.
    const QSize c = cellSize();
    QSet<quint64> taken;
    auto key = [&](const QPoint& p) {
        return (static_cast<quint64>(static_cast<quint32>(p.y() / c.height())) << 32) |
               static_cast<quint32>(p.x() / c.width());
    };
    QList<QModelIndex> moving;
    for (int r = 0; r < model()->rowCount(); ++r) {
        const QModelIndex i = model()->index(r, modelColumn());
        if (!selectedOnly || selectionModel()->isSelected(i)) {
            moving << i;
        } else {
            taken.insert(key(snapped(rectForIndex(i).topLeft())));
        }
    }
    const int columns = std::max(1, viewport()->width() / c.width());
    QHash<QString, QPoint> positions = document_.bin().display.framePositions;
    for (const QModelIndex& i : moving) {
        QPoint p = snapped(rectForIndex(i).topLeft());
        while (taken.contains(key(p))) {
            p.rx() += c.width();
            if (p.x() / c.width() >= columns) p = QPoint(0, p.y() + c.height());
        }
        taken.insert(key(p));
        setPositionForIndex(p, i);
        positions.insert(i.data(BinItemModel::kIdRole).toString(), p);
    }
    document_.setFramePositions(positions);
    viewport()->update();
}

void BinFrameView::fillWindow() {
    if (!model()) return;
    // Evenly across the window, keeping the frames' order on screen (p.251).
    QList<QModelIndex> order;
    for (int r = 0; r < model()->rowCount(); ++r) order << model()->index(r, modelColumn());
    std::stable_sort(order.begin(), order.end(), [this](const QModelIndex& a, const QModelIndex& b) {
        const QPoint pa = rectForIndex(a).topLeft();
        const QPoint pb = rectForIndex(b).topLeft();
        return pa.y() != pb.y() ? pa.y() < pb.y() : pa.x() < pb.x();
    });
    const QSize c = cellSize();
    const int columns = std::max(1, viewport()->width() / c.width());
    const int gap = columns > 1 ? (viewport()->width() - columns * c.width()) / (columns - 1) : 0;
    QHash<QString, QPoint> positions;
    for (int n = 0; n < order.size(); ++n) {
        const QPoint p((n % columns) * (c.width() + gap), (n / columns) * c.height());
        setPositionForIndex(p, order.at(n));
        positions.insert(order.at(n).data(BinItemModel::kIdRole).toString(), p);
    }
    document_.setFramePositions(positions);
    viewport()->update();
}

void BinFrameView::fillSorted() {
    // In the bin's order, which Text view sorts (p.251).
    document_.setFramePositions({});
    doItemsLayout();
    viewport()->update();
}

void BinFrameView::paintEvent(QPaintEvent* event) {
    const model::SnapToGrid snap = document_.bin().display.snap;
    if (snap == model::SnapToGrid::Enable || (snap == model::SnapToGrid::Temporary && state() == DraggingState)) {
        QPainter p(viewport());
        p.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
        const QSize c = cellSize();
        const int dx = horizontalOffset();
        const int dy = verticalOffset();
        for (int x = c.width() - dx % c.width(); x < viewport()->width(); x += c.width()) {
            p.drawLine(x, 0, x, viewport()->height());
        }
        for (int y = c.height() - dy % c.height(); y < viewport()->height(); y += c.height()) {
            p.drawLine(0, y, viewport()->width(), y);
        }
    }
    QListView::paintEvent(event);
}

bool BinFrameView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const QModelIndex i = indexAt(help->pos());
        if (i.isValid()) {
            QToolTip::showText(help->globalPos(), i.data(BinItemModel::kSummaryRole).toString(), viewport());
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QListView::viewportEvent(event);
}

// ---------------------------------------------------------------- BinScriptView

BinScriptView::BinScriptView(BinDocument& document, QAbstractItemModel* rows, QItemSelectionModel* selection,
                             QWidget* parent)
    : QScrollArea(parent), document_(document), rows_(rows), selection_(selection) {
    setObjectName(QStringLiteral("binScript"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    connect(rows_, &QAbstractItemModel::modelReset, this, &BinScriptView::rebuild);
    connect(rows_, &QAbstractItemModel::layoutChanged, this, &BinScriptView::rebuild);
    connect(rows_, &QAbstractItemModel::rowsInserted, this, &BinScriptView::rebuild);
    connect(rows_, &QAbstractItemModel::rowsRemoved, this, &BinScriptView::rebuild);
    connect(selection_, &QItemSelectionModel::selectionChanged, this, &BinScriptView::updateSelection);
    if (MediaFrames* frames = MediaFrames::instance()) {
        connect(frames, &MediaFrames::thumbnailReady, this, &BinScriptView::updatePictures);
    }
    rebuild();
}

void BinScriptView::rebuild() {
    content_ = new QWidget;
    content_->setObjectName(QStringLiteral("scriptRows"));
    content_->setAcceptDrops(true);
    content_->installEventFilter(this);
    auto* layout = new QVBoxLayout(content_);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    const QSize f = BinFrameView::frameSize(document_.bin().display.frameSize);
    for (int r = 0; r < rows_->rowCount(); ++r) {
        const QModelIndex index = rows_->index(r, BinItemModel::kNameColumn);
        const QString id = index.data(BinItemModel::kIdRole).toString();
        const model::BinItem* item = document_.bin().item(id);
        if (!item) continue;

        auto* row = new QFrame(content_);
        row->setObjectName(QStringLiteral("scriptRow"));
        row->setProperty("binItemId", id);
        row->setAutoFillBackground(true);
        row->installEventFilter(this);

        auto* frame = new QLabel(row);
        frame->setObjectName(QStringLiteral("scriptFrame"));
        frame->setProperty("binItemId", id);
        frame->setPixmap(scriptPicture(*item, f));
        frame->setFixedSize(f);
        frame->setAttribute(Qt::WA_TransparentForMouseEvents);

        // The clip's details sit above its text box (p.252).
        auto* info = new QLabel(QStringLiteral("%1    %2").arg(item->name, model::cellText(*item, QStringLiteral("Duration"))),
                                row);
        info->setObjectName(QStringLiteral("scriptInfo"));
        info->setFixedHeight(kRowHeight); // not from the font: rows match on every platform
        info->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* text = new QPlainTextEdit(row);
        text->setObjectName(QStringLiteral("scriptText"));
        text->setPlainText(item->script);
        text->setFixedHeight(kScriptTextHeight);
        connect(text, &QPlainTextEdit::textChanged, this,
                [this, id, text] { document_.setScript(id, text->toPlainText()); });

        auto* right = new QVBoxLayout;
        right->setSpacing(2);
        right->addWidget(info);
        right->addWidget(text);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(4, 4, 4, 4);
        h->addWidget(frame, 0, Qt::AlignTop);
        h->addLayout(right, 1);
        layout->addWidget(row);
    }
    layout->addStretch(1);
    setWidget(content_); // deletes the previous rows
    updateSelection();
}

QPixmap BinScriptView::scriptPicture(const model::BinItem& item, QSize size) const {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    MediaFrames* frames = MediaFrames::instance();
    paintFrame(p, QRect(QPoint(0, 0), size), item.kind, frames ? frames->thumbnail(item, 0, size) : QImage());
    return pm;
}

void BinScriptView::updatePictures() {
    if (!content_) return;
    for (QLabel* frame : content_->findChildren<QLabel*>(QStringLiteral("scriptFrame"))) {
        if (const model::BinItem* item = document_.bin().item(frame->property("binItemId").toString())) {
            frame->setPixmap(scriptPicture(*item, frame->size()));
        }
    }
}

QWidget* BinScriptView::rowFor(const QString& id) const {
    if (!content_) return nullptr;
    for (QFrame* row : content_->findChildren<QFrame*>(QStringLiteral("scriptRow"))) {
        if (row->property("binItemId").toString() == id) return row;
    }
    return nullptr;
}

QString BinScriptView::idAt(QObject* rowWidget) const {
    return rowWidget ? rowWidget->property("binItemId").toString() : QString();
}

void BinScriptView::updateSelection() {
    if (!content_) return;
    QSet<QString> selected;
    for (const QModelIndex& i : selection_->selectedIndexes()) selected.insert(i.data(BinItemModel::kIdRole).toString());
    for (QFrame* row : content_->findChildren<QFrame*>(QStringLiteral("scriptRow"))) {
        row->setBackgroundRole(selected.contains(idAt(row)) ? QPalette::Highlight : QPalette::AlternateBase);
    }
}

bool BinScriptView::eventFilter(QObject* watched, QEvent* event) {
    const QString id = idAt(watched);
    if (!id.isEmpty() && event->type() == QEvent::MouseButtonPress) {
        // Clicking a clip selects it; Ctrl/Cmd+click adds to the selection.
        const auto* me = static_cast<QMouseEvent*>(event);
        for (int r = 0; r < rows_->rowCount(); ++r) {
            const QModelIndex i = rows_->index(r, BinItemModel::kNameColumn);
            if (i.data(BinItemModel::kIdRole).toString() != id) continue;
            const auto flags = QItemSelectionModel::Rows |
                               ((me->modifiers() & Qt::ControlModifier) ? QItemSelectionModel::Toggle
                                                                        : QItemSelectionModel::ClearAndSelect);
            if (!(selection_->isSelected(i) && !(me->modifiers() & Qt::ControlModifier))) selection_->select(i, flags);
        }
        pressPos_ = me->position().toPoint();
        pressId_ = id;
        return false;
    }
    if (!id.isEmpty() && event->type() == QEvent::MouseMove && !pressId_.isEmpty()) {
        const auto* me = static_cast<QMouseEvent*>(event);
        if ((me->position().toPoint() - pressPos_).manhattanLength() < QApplication::startDragDistance()) return false;
        // Drag clips up or down to a new place (p.254).
        QStringList ids;
        for (const QModelIndex& i : selection_->selectedRows(BinItemModel::kNameColumn)) {
            ids << i.data(BinItemModel::kIdRole).toString();
        }
        if (ids.isEmpty()) ids << pressId_;
        pressId_.clear();
        auto* mime = new QMimeData;
        mime->setData(binItemsMime(), (QStringList{document_.nodeId()} + ids).join(QLatin1Char('\n')).toUtf8());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) pressId_.clear();
    if (watched == content_ && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto* de = static_cast<QDropEvent*>(event);
        if (de->mimeData()->hasFormat(binItemsMime())) de->acceptProposedAction();
        return true;
    }
    if (watched == content_ && event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        QStringList ids =
            QString::fromUtf8(de->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (ids.isEmpty()) return true;
        const QString from = ids.takeFirst();
        if (from != document_.nodeId()) {
            // From another bin.
            de->acceptProposedAction();
            emit itemsDropped(from, ids,
                              de->dropAction() == Qt::CopyAction || (de->modifiers() & Qt::AltModifier));
            return true;
        }
        // Before the row under the pointer, or at the end.
        int to = static_cast<int>(document_.bin().items.size());
        const int y = de->position().toPoint().y();
        for (QFrame* row : content_->findChildren<QFrame*>(QStringLiteral("scriptRow"))) {
            if (y < row->geometry().center().y()) {
                const QString target = idAt(row);
                for (int k = 0; k < document_.bin().items.size(); ++k) {
                    if (document_.bin().items.at(k).id == target) to = std::min(to, k);
                }
            }
        }
        de->acceptProposedAction();
        document_.moveItems(ids, to);
        return true;
    }
    return QScrollArea::eventFilter(watched, event);
}

// ---------------------------------------------------------------- BinView

BinView::BinView(BinDocument& document, QMenu* fastMenu, QWidget* parent)
    : QWidget(parent), document_(document), fastMenu_(fastMenu) {
    setObjectName(QStringLiteral("binView"));

    // Bin View menu, view buttons, quick filter and fast menu (p.249).
    viewMenu_ = new QComboBox(this);
    viewMenu_->setObjectName(QStringLiteral("binViewMenu"));
    viewMenu_->setFixedWidth(110);
    connect(viewMenu_, &QComboBox::activated, this, [this](int index) {
        const QString name = viewMenu_->itemData(index).toString();
        if (viewMenu_->itemData(index, kSaveAsRole).toBool()) {
            // Save As: the View Name dialog (p.249).
            bool ok = false;
            const QString viewName =
                QInputDialog::getText(this, tr("View Name"), tr("View name:"), QLineEdit::Normal, {}, &ok);
            if (ok && !viewName.trimmed().isEmpty()) saveViewAs(viewName.trimmed());
            updateViewMenu();
        } else if (!name.isEmpty()) {
            applyView(name);
        }
    });
    auto* textView = toolButton(this, QStringLiteral("textViewButton"), glyphIcon(GlyphIcon::ListView), tr("Text View"));
    auto* frameView = toolButton(this, QStringLiteral("frameViewButton"), glyphIcon(GlyphIcon::FrameView), tr("Frame View"));
    auto* scriptView = toolButton(this, QStringLiteral("scriptViewButton"), glyphIcon(GlyphIcon::ScriptView), tr("Script View"));
    modes_ = new QButtonGroup(this);
    modes_->setExclusive(true);
    modes_->addButton(textView, static_cast<int>(model::BinViewMode::Text));
    modes_->addButton(frameView, static_cast<int>(model::BinViewMode::Frame));
    modes_->addButton(scriptView, static_cast<int>(model::BinViewMode::Script));
    for (QAbstractButton* b : modes_->buttons()) b->setCheckable(true);
    connect(modes_, &QButtonGroup::idClicked, this, [this](int id) { setMode(static_cast<model::BinViewMode>(id)); });
    auto* search = toolButton(this, QStringLiteral("quickFindButton"), glyphIcon(GlyphIcon::Search), tr("Filter"));
    search->setCheckable(true);
    auto* fast = toolButton(this, QStringLiteral("binFastMenu"), glyphIcon(GlyphIcon::FastMenu), tr("Bin Fast Menu"));
    fast->setPopupMode(QToolButton::InstantPopup);
    if (fastMenu) {
        // The Bin menu, and Input > Source Browser (p.315).
        auto* menu = new QMenu(fast);
        menu->setObjectName(QStringLiteral("binFastMenuItems"));
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->clear();
            for (QAction* a : fastMenu_->actions()) menu->addAction(a);
            if (inputMenu_) {
                menu->addSeparator();
                menu->addMenu(inputMenu_);
            }
        });
        fast->setMenu(menu);
    }

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
    top->addWidget(viewMenu_);
    top->addSpacing(4);
    top->addWidget(textView);
    top->addWidget(frameView);
    top->addWidget(scriptView);
    top->addStretch(1);
    top->addWidget(filter_);
    top->addWidget(search);
    top->addWidget(fast);

    model_ = new BinItemModel(document_, this);
    proxy_ = new BinFilterModel(document_, this);
    proxy_->setSourceModel(model_);

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
    QHeaderView* header = table_->horizontalHeader();
    header->setSectionsMovable(true);
    header->setSectionsClickable(true);
    header->setHighlightSections(false);
    header->setStretchLastSection(true);
    header->setDefaultAlignment(Qt::AlignCenter);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setIconSize(QSize(16, 16));
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    // Items drag to other bins and to bins in the Sidebar (p.243, p.260).
    table_->setDragEnabled(true);
    table_->setDragDropMode(QAbstractItemView::DragDrop);
    table_->setDefaultDropAction(Qt::MoveAction);
    table_->setDropIndicatorShown(false);
    table_->setItemDelegate(new CellDelegate(table_));
    table_->installEventFilter(this);
    table_->viewport()->installEventFilter(this);
    connect(header, &QHeaderView::sectionClicked, this, &BinView::headerClicked);
    connect(header, &QHeaderView::sectionDoubleClicked, this, [this](int logical) {
        // Double-click a heading to sort on it; again to reverse (p.247).
        const QString h = model_->heading(logical);
        if (h.isEmpty()) return;
        const auto& last = document_.bin().display.sort;
        const bool ascending = !(last.size() == 1 && last.first().heading == h && last.first().ascending);
        sortOn(h, ascending);
    });
    connect(header, &QHeaderView::customContextMenuRequested, this, &BinView::headerMenu);
    connect(header, &QHeaderView::sectionMoved, this, [this] {
        if (!applying_) QMetaObject::invokeMethod(this, &BinView::columnsMoved, Qt::QueuedConnection);
    });
    connect(header, &QHeaderView::sectionResized, this, [this](int logical, int, int size) {
        if (applying_ || logical == BinItemModel::kIconColumn) return;
        document_.setColumnWidth(model_->heading(logical), size);
    });

    frames_ = new BinFrameView(document_, this);
    frames_->setModel(proxy_);
    frames_->setModelColumn(BinItemModel::kNameColumn);
    frames_->setSelectionModel(table_->selectionModel());
    frames_->setContextMenuPolicy(Qt::CustomContextMenu);
    frames_->setAcceptDrops(true);
    frames_->setDefaultDropAction(Qt::MoveAction);
    frames_->installEventFilter(this);

    script_ = new BinScriptView(document_, proxy_, table_->selectionModel(), this);
    script_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(model_, &BinItemModel::itemsDropped, this, &BinView::itemsDropped);
    connect(model_, &BinItemModel::filesDropped, this, &BinView::filesDropped);
    for (QAbstractItemView* v : {static_cast<QAbstractItemView*>(table_), static_cast<QAbstractItemView*>(frames_)}) {
        connect(v, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex& index) {
            emit itemActivated(index.data(BinItemModel::kIdRole).toString());
        });
    }
    connect(script_, &BinScriptView::itemsDropped, this, &BinView::itemsDropped);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(table_);
    stack_->addWidget(frames_);
    stack_->addWidget(script_);

    for (QWidget* w : {static_cast<QWidget*>(table_), static_cast<QWidget*>(frames_), static_cast<QWidget*>(script_)}) {
        connect(w, &QWidget::customContextMenuRequested, this, [this, w](const QPoint& pos) {
            QWidget* area = w == script_ ? w : static_cast<QAbstractScrollArea*>(w)->viewport();
            QMenu* menu = contextMenu(pos, w);
            menu->exec(area->mapToGlobal(pos));
            menu->deleteLater();
        });
    }

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("binStatus"));
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_->setFixedHeight(kRowHeight);
    status_->setContentsMargins(0, 0, 8, 0);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(stack_, 1);
    layout->addWidget(status_);

    // The selection survives the bin being redrawn after a change.
    connect(proxy_, &QAbstractItemModel::modelAboutToBeReset, this, [this] { keepSelection_ = selectedIds(); });
    connect(proxy_, &QAbstractItemModel::modelReset, this, [this] {
        if (!keepSelection_.isEmpty()) selectIds(keepSelection_);
        keepSelection_.clear();
    });
    connect(&document_, &BinDocument::displayChanged, this, &BinView::applyDisplay);
    connect(&document_, &BinDocument::itemsChanged, this, &BinView::updateStatus);
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &BinView::updateStatus);
    applyDisplay();
}

void BinView::applyDisplay() {
    applying_ = true;
    const model::BinDisplay& d = document_.bin().display;
    // Fixed widths, not measured from text, so that bins lay out the same
    // on every platform.
    QHeaderView* header = table_->horizontalHeader();
    // The bin's column order is the model's; a heading dragged elsewhere
    // has already been moved there.
    for (int l = 0; l < header->count(); ++l) {
        if (const int v = header->visualIndex(l); v != l) header->moveSection(v, l);
    }
    header->resizeSection(BinItemModel::kIconColumn, kIconWidth);
    header->setSectionResizeMode(BinItemModel::kIconColumn, QHeaderView::Fixed);
    for (int c = BinItemModel::kNameColumn; c < model_->columnCount(); ++c) {
        const QString h = model_->heading(c);
        // The Color column has a fixed width (p.268).
        if (h == QLatin1String("Color")) {
            header->resizeSection(c, kColorWidth);
            header->setSectionResizeMode(c, QHeaderView::Fixed);
            continue;
        }
        header->setSectionResizeMode(c, QHeaderView::Interactive);
        const int fallback = c == BinItemModel::kNameColumn ? kNameWidth
                             : h.endsWith(QLatin1String("Date")) ? kDateWidth
                                                                 : kColumnWidth;
        header->resizeSection(c, d.widths.value(h, fallback));
    }
    selectedHeadings_.removeIf([&](const QString& h) { return model_->columnOf(h) < 0; });
    model_->setSelectedHeadings(selectedHeadings_);

    // Bin background colour (p.263).
    QPalette p = palette();
    if (!d.background.isEmpty()) {
        p.setColor(QPalette::Base, QColor(d.background));
        p.setColor(QPalette::AlternateBase, QColor(d.background).darker(110));
    }
    table_->setPalette(p);
    frames_->setPalette(p);
    script_->setPalette(p);

    stack_->setCurrentIndex(static_cast<int>(d.mode));
    if (QAbstractButton* b = modes_->button(static_cast<int>(d.mode))) b->setChecked(true);
    frames_->applyDisplay();
    status_->setVisible(d.statusBar);
    updateViewMenu();
    updateStatus();
    applying_ = false;
}

void BinView::updateViewMenu() {
    const model::BinDisplay& d = document_.bin().display;
    QSignalBlocker block(viewMenu_);
    viewMenu_->clear();
    for (const model::BinViewPreset& v : model::builtInBinViews()) viewMenu_->addItem(v.name, v.name);
    for (const model::BinViewPreset& v : BinViewLibrary::views()) viewMenu_->addItem(v.name, v.name);
    viewMenu_->insertSeparator(viewMenu_->count());
    viewMenu_->addItem(tr("Save As..."));
    viewMenu_->setItemData(viewMenu_->count() - 1, true, kSaveAsRole);
    QString shown = d.viewName.isEmpty() ? tr("Untitled") : d.viewName;
    int index = d.viewName.isEmpty() ? -1 : viewMenu_->findData(d.viewName);
    if (index < 0) {
        viewMenu_->insertItem(0, shown, d.viewName);
        index = 0;
    }
    // A view whose columns changed reads "<view>.1", in italic (p.249).
    QFont f = viewMenu_->font();
    f.setItalic(d.viewModified);
    if (d.viewModified) viewMenu_->setItemText(index, shown + QStringLiteral(".1"));
    viewMenu_->setFont(f);
    viewMenu_->setCurrentIndex(index);
    // The Bin View menu works in Text view only (p.248).
    viewMenu_->setEnabled(d.mode == model::BinViewMode::Text);
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
    QList<int> rows;
    for (const QModelIndex& i : table_->selectionModel()->selectedIndexes()) {
        if (!rows.contains(i.row())) rows << i.row();
    }
    std::sort(rows.begin(), rows.end());
    for (int r : rows) ids << proxy_->index(r, 0).data(BinItemModel::kIdRole).toString();
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
    selectIds(visibleIds());
}

void BinView::reverseSelection() {
    const QStringList selected = selectedIds();
    QStringList others;
    for (const QString& id : visibleIds()) {
        if (!selected.contains(id)) others << id;
    }
    selectIds(others);
}

void BinView::setQuickFilter(const QString& text) {
    quick_ = text;
    proxy_->setQuickFilter(text, mode() == model::BinViewMode::Text ? selectedHeadings_ : QStringList());
    updateStatus();
}

int BinView::visibleCount() const {
    return proxy_->rowCount();
}

QStringList BinView::visibleIds() const {
    QStringList ids;
    for (int r = 0; r < proxy_->rowCount(); ++r) ids << proxy_->index(r, 0).data(BinItemModel::kIdRole).toString();
    return ids;
}

// ---------------------------------------------------------------- views

void BinView::setMode(model::BinViewMode mode) {
    if (mode == document_.bin().display.mode) return;
    const QStringList selected = selectedIds();
    model::BinDisplay d = document_.bin().display;
    d.mode = mode;
    document_.setDisplay(d);
    selectIds(selected);
}

void BinView::applyView(const QString& name) {
    const model::BinViewPreset* view = BinViewLibrary::find(name);
    for (const model::BinViewPreset& v : model::builtInBinViews()) {
        if (v.name == name) view = &v;
    }
    if (!view) return;
    model::BinDisplay d = document_.bin().display;
    // Custom columns stay with the bin, hidden unless the view shows them.
    d.columns = view->columns;
    d.viewName = view->name;
    d.viewModified = false;
    document_.setDisplay(d);
}

void BinView::saveViewAs(const QString& name) {
    QList<model::BinViewPreset>& views = BinViewLibrary::views();
    const model::BinViewPreset preset{name, document_.bin().display.columns};
    auto it = std::find_if(views.begin(), views.end(), [&](const model::BinViewPreset& v) { return v.name == name; });
    if (it != views.end()) {
        *it = preset;
    } else {
        views.append(preset);
    }
    model::BinDisplay d = document_.bin().display;
    d.viewName = name;
    d.viewModified = false;
    document_.setDisplay(d);
}

// ---------------------------------------------------------------- columns

void BinView::selectHeadings(const QStringList& headings) {
    selectedHeadings_ = headings;
    selectedHeadings_.removeIf([&](const QString& h) { return model_->columnOf(h) < 0; });
    model_->setSelectedHeadings(selectedHeadings_);
    if (!quick_.isEmpty()) setQuickFilter(quick_);
}

void BinView::headerClicked(int logical) {
    const QString h = model_->heading(logical);
    if (h.isEmpty()) {
        selectHeadings({});
        return;
    }
    const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    QStringList headings = selectedHeadings_;
    if (mods & Qt::ControlModifier) {
        // Ctrl/Cmd+click adds headings; Shift+click adds a range (p.247).
        if (headings.contains(h)) {
            headings.removeAll(h);
        } else {
            headings << h;
        }
    } else if ((mods & Qt::ShiftModifier) && !headings.isEmpty()) {
        QHeaderView* header = table_->horizontalHeader();
        const int a = header->visualIndex(model_->columnOf(headings.last()));
        const int b = header->visualIndex(logical);
        for (int v = std::min(a, b); v <= std::max(a, b); ++v) {
            const QString between = model_->heading(header->logicalIndex(v));
            if (!between.isEmpty() && !headings.contains(between)) headings << between;
        }
    } else {
        headings = {h};
    }
    selectHeadings(headings);
}

void BinView::headerMenu(const QPoint& pos) {
    QHeaderView* header = table_->horizontalHeader();
    const QString h = model_->heading(header->logicalIndexAt(pos));
    if (!h.isEmpty() && !selectedHeadings_.contains(h)) selectHeadings({h});
    QMenu menu(this);
    // p.247 and p.268-269.
    QAction* ascending = menu.addAction(tr("Sort on Column, Ascending"));
    QAction* descending = menu.addAction(tr("Sort on Column, Descending"));
    ascending->setEnabled(!h.isEmpty());
    descending->setEnabled(!h.isEmpty());
    connect(ascending, &QAction::triggered, this, [this, h] { sortOn(h, true); });
    connect(descending, &QAction::triggered, this, [this, h] { sortOn(h, false); });
    menu.addSeparator();
    connect(menu.addAction(tr("Choose Columns")), &QAction::triggered, this, &BinView::chooseColumns);
    QAction* hide = menu.addAction(tr("Hide Column"));
    hide->setEnabled(!h.isEmpty() && h != QLatin1String("Name"));
    connect(hide, &QAction::triggered, this, [this, h] { hideColumns({h}); });
    connect(menu.addAction(tr("Add Custom Column")), &QAction::triggered, this, &BinView::addCustomColumn);
    QAction* rename = menu.addAction(tr("Rename Column"));
    rename->setEnabled(document_.bin().display.customColumns.contains(h));
    connect(rename, &QAction::triggered, this, [this, h] { editHeading(h); });
    menu.exec(header->mapToGlobal(pos));
}

void BinView::editHeading(const QString& heading) {
    // Typed straight into the heading (p.269).
    QHeaderView* header = table_->horizontalHeader();
    const int logical = model_->columnOf(heading);
    if (logical < 0) return;
    auto* editor = new QLineEdit(heading, header->viewport());
    editor->setObjectName(QStringLiteral("headingEditor"));
    editor->setGeometry(header->sectionViewportPosition(logical), 0, header->sectionSize(logical), header->height());
    editor->selectAll();
    editor->show();
    editor->setFocus();
    connect(editor, &QLineEdit::editingFinished, this, [this, editor, heading] {
        const QString text = editor->text();
        editor->deleteLater();
        renameColumn(heading, text);
    });
}

void BinView::chooseColumns() {
    const model::BinDisplay& d = document_.bin().display;
    QStringList available = model::binHeadings();
    available.removeAll(QStringLiteral("Name")); // always shown (p.249)
    available << d.customColumns;
    ChooseColumnsDialog dialog(available, d.columns, this);
    if (dialog.exec() == QDialog::Accepted) setColumns(dialog.chosen());
}

void BinView::setColumns(const QStringList& headings) {
    model::BinDisplay d = document_.bin().display;
    QStringList columns;
    for (const QString& c : d.columns) {
        if (headings.contains(c)) columns << c;
    }
    // New headings go to the right of the selected column, otherwise at the
    // far right (p.247).
    qsizetype at = -1;
    for (const QString& sel : selectedHeadings_) {
        if (sel == QLatin1String("Name")) {
            at = std::max<qsizetype>(at, 0);
        } else if (const auto i = columns.indexOf(sel); i >= 0) {
            at = std::max(at, i + 1);
        }
    }
    if (at < 0) at = columns.size();
    for (const QString& h : headings) {
        if (h == QLatin1String("Name") || columns.contains(h)) continue;
        columns.insert(at++, h);
    }
    if (columns == d.columns) return;
    d.columns = columns;
    d.viewModified = true;
    document_.setDisplay(d);
}

void BinView::hideColumns(const QStringList& headings) {
    model::BinDisplay d = document_.bin().display;
    const auto removed = d.columns.removeIf([&](const QString& c) { return headings.contains(c); });
    if (removed == 0) return;
    d.viewModified = true;
    document_.setDisplay(d);
}

void BinView::deleteColumns(const QStringList& headings, bool ask) {
    QStringList hide;
    for (const QString& h : headings) {
        if (h == QLatin1String("Name")) continue;
        if (!document_.bin().display.customColumns.contains(h)) {
            hide << h; // a standard column is only hidden (p.268)
            continue;
        }
        bool remove = true;
        if (ask) {
            QMessageBox box(QMessageBox::Warning, tr("Delete"),
                            tr("Delete the column \"%1\" and everything typed in it?").arg(h), QMessageBox::NoButton,
                            this);
            QPushButton* ok = box.addButton(QMessageBox::Ok);
            QPushButton* hideButton = box.addButton(tr("Hide"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == hideButton) {
                hide << h;
                remove = false;
            } else if (box.clickedButton() != ok) {
                remove = false;
            }
        }
        if (remove) document_.deleteCustomColumn(h);
    }
    hideColumns(hide);
}

QString BinView::addCustomColumn() {
    model::BinDisplay d = document_.bin().display;
    QString name = QStringLiteral("Untitled");
    for (int n = 1; d.customColumns.contains(name) || model::binHeadings().contains(name); ++n) {
        name = QStringLiteral("Untitled.%1").arg(n);
    }
    d.customColumns << name;
    document_.setDisplay(d);
    setColumns(QStringList(d.columns) << name);
    if (isVisible() && mode() == model::BinViewMode::Text) editHeading(name);
    return name;
}

bool BinView::renameColumn(const QString& from, const QString& to) {
    const bool selected = selectedHeadings_.contains(from);
    if (!document_.renameCustomColumn(from, to)) return false;
    if (selected) selectHeadings({to.trimmed()});
    return true;
}

void BinView::columnsMoved() {
    // Dragging a heading moves the column (p.268); Name and the icon stay
    // at the left.
    QHeaderView* header = table_->horizontalHeader();
    QStringList order;
    for (int v = 0; v < header->count(); ++v) {
        const QString h = model_->heading(header->logicalIndex(v));
        if (!h.isEmpty() && h != QLatin1String("Name")) order << h;
    }
    model::BinDisplay d = document_.bin().display;
    if (order == d.columns) {
        document_.setDisplay(d); // puts Name back in place
        return;
    }
    d.columns = order;
    d.viewModified = true;
    document_.setDisplay(d);
}

void BinView::alignColumns() {
    // Spaced by the length of their contents (p.268).
    for (int c = BinItemModel::kNameColumn; c < model_->columnCount(); ++c) {
        if (model_->heading(c) == QLatin1String("Color")) continue;
        table_->resizeColumnToContents(c);
    }
}

// ---------------------------------------------------------------- sorting

void BinView::sortOn(const QString& heading, bool ascending) {
    const QStringList selected = selectedIds();
    document_.sort({{heading, ascending}});
    selectIds(selected); // selected items stay selected (p.247)
}

bool BinView::sortSelected() {
    if (selectedHeadings_.isEmpty()) return false;
    // The leftmost column has the highest priority (p.247).
    QHeaderView* header = table_->horizontalHeader();
    QStringList headings = selectedHeadings_;
    std::sort(headings.begin(), headings.end(), [&](const QString& a, const QString& b) {
        return header->visualIndex(model_->columnOf(a)) < header->visualIndex(model_->columnOf(b));
    });
    QList<model::BinSortKey> keys;
    for (const QString& h : headings) keys.append({h, true});
    const QStringList selected = selectedIds();
    document_.sort(keys);
    selectIds(selected);
    return true;
}

void BinView::sortAgain() {
    const QStringList selected = selectedIds();
    document_.sort(document_.bin().display.sort);
    selectIds(selected);
}

// ---------------------------------------------------------------- sifting

void BinView::siftBinContents() {
    const model::BinDisplay& d = document_.bin().display;
    // Shown columns, and the timecode ranges they make (p.290).
    CustomSiftDialog dialog(QStringList{QStringLiteral("Name")} + d.columns + model::siftRangesFor(d.columns), d.sift,
                            this);
    connect(&dialog, &CustomSiftDialog::applied, this, [this](const model::BinSift& s) { sift(s); });
    if (dialog.exec() == QDialog::Accepted) sift(dialog.sift());
}

void BinView::sift(const model::BinSift& s) {
    model::BinDisplay d = document_.bin().display;
    d.sift = s;
    d.sifted = !s.isEmpty();
    document_.setDisplay(d);
}

void BinView::siftSelected() {
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) return;
    model::BinSift s;
    s.ids = ids;
    sift(s);
}

void BinView::setSiftShown(bool shown) {
    model::BinDisplay d = document_.bin().display;
    if (d.sift.isEmpty() || d.sifted == shown) return;
    d.sifted = shown;
    document_.setDisplay(d);
}

// ---------------------------------------------------------------- Set Bin Display

void BinView::setBinDisplay() {
    const model::BinDisplay& d = document_.bin().display;
    SetBinDisplayDialog dialog(d, this);
    if (dialog.exec() != QDialog::Accepted) return;
    model::BinDisplay changed = d;
    changed.hiddenKinds = dialog.hiddenKinds();
    changed.showUserClips = dialog.showUserClips();
    changed.showReferenceClips = dialog.showReferenceClips();
    document_.setDisplay(changed);
}

void BinView::setHiddenKinds(const QList<model::BinItemKind>& kinds) {
    model::BinDisplay d = document_.bin().display;
    d.hiddenKinds = kinds;
    document_.setDisplay(d);
}

// ---------------------------------------------------------------- frames, colours

void BinView::enlargeFrame() {
    model::BinDisplay d = document_.bin().display;
    if (d.frameSize >= model::BinDisplay::kMaxFrameSize) return;
    ++d.frameSize;
    document_.setDisplay(d);
}

void BinView::reduceFrame() {
    model::BinDisplay d = document_.bin().display;
    if (d.frameSize <= 0) return;
    --d.frameSize;
    document_.setDisplay(d);
}

void BinView::setSnapToGrid(model::SnapToGrid snap) {
    model::BinDisplay d = document_.bin().display;
    d.snap = snap;
    document_.setDisplay(d);
}

void BinView::setClipColor(const QString& color) {
    document_.setColor(selectedIds(), color);
}

void BinView::setBackground(const QString& color) {
    model::BinDisplay d = document_.bin().display;
    d.background = color;
    document_.setDisplay(d);
}

void BinView::setLocked(bool locked) {
    document_.setLocked(selectedIds(), locked);
}

QMenu* BinView::colorMenu(QMenu* parent, const QString& title, bool background) {
    QMenu* menu = parent ? parent->addMenu(title) : new QMenu(title, this);
    auto apply = [this, background](const QString& hex) {
        if (background) {
            setBackground(hex);
        } else {
            setClipColor(hex);
        }
    };
    // None resets the colour (p.264).
    connect(menu->addAction(QIcon(swatch(QColor())), tr("None")), &QAction::triggered, this,
            [apply] { apply(QString()); });
    for (const model::ClipColor& c : model::clipColors()) {
        connect(menu->addAction(QIcon(swatch(QColor(c.hex))), c.name), &QAction::triggered, this,
                [apply, hex = c.hex] { apply(hex); });
    }
    menu->addSeparator();
    // The system colour picker (p.265).
    connect(menu->addAction(tr("Other...")), &QAction::triggered, this, [this, apply] {
        const QColor c = QColorDialog::getColor(Qt::white, this);
        if (c.isValid()) apply(c.name());
    });
    return menu;
}

QMenu* BinView::contextMenu(const QPoint& viewportPos, QWidget* view) {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("binContextMenu"));
    // Right-click in the Color column offers the colours (p.264).
    if (view == table_) {
        const QModelIndex at = table_->indexAt(viewportPos);
        if (at.isValid() && model_->heading(at.column()) == QLatin1String("Color")) {
            if (!table_->selectionModel()->isSelected(at)) selectIds({at.data(BinItemModel::kIdRole).toString()});
            menu->deleteLater();
            return colorMenu(nullptr, tr("Color"), false);
        }
        if (at.isValid() && !table_->selectionModel()->isSelected(at)) {
            selectIds({at.data(BinItemModel::kIdRole).toString()});
        }
    }
    QMenu* clip = colorMenu(menu, tr("Set Clip Color"), false);
    clip->setEnabled(!selectedIds().isEmpty());
    colorMenu(menu, tr("Set Background Color"), true);
    if (mode() == model::BinViewMode::Frame) {
        // Snap to Grid (p.252).
        QMenu* snap = menu->addMenu(tr("Snap to Grid"));
        auto* group = new QActionGroup(snap);
        const std::pair<model::SnapToGrid, QString> choices[] = {
            {model::SnapToGrid::Disable, tr("Disable")},
            {model::SnapToGrid::Temporary, tr("Temporary")},
            {model::SnapToGrid::Enable, tr("Enable")},
            {model::SnapToGrid::Invisible, tr("Invisible")},
        };
        for (const auto& [value, text] : choices) {
            QAction* a = snap->addAction(text);
            a->setCheckable(true);
            a->setChecked(document_.bin().display.snap == value);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, choice = value] { setSnapToGrid(choice); });
        }
    }
    // Right-click: Input > Source Browser (p.315).
    if (inputMenu_) {
        menu->addSeparator();
        menu->addMenu(inputMenu_);
    }
    if (fastMenu_) {
        menu->addSeparator();
        for (QAction* a : fastMenu_->actions()) menu->addAction(a);
    }
    return menu;
}

bool BinView::eventFilter(QObject* watched, QEvent* event) {
    // While a cell is being typed in, its editor has the keys, not the table.
    if ((watched == table_ || watched == frames_) && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            if (watched == table_ && !selectedHeadings_.isEmpty()) {
                deleteColumns(selectedHeadings_); // p.268
            } else {
                emit deleteRequested();
            }
            return true;
        }
    }
    // Clicking the rows deselects the headings.
    if (watched == table_->viewport() && event->type() == QEvent::MouseButtonPress && !selectedHeadings_.isEmpty()) {
        selectHeadings({});
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------- BinViewLibrary

QList<model::BinViewPreset>& BinViewLibrary::views() {
    static QList<model::BinViewPreset> saved;
    return saved;
}

const model::BinViewPreset* BinViewLibrary::find(const QString& name) {
    for (const model::BinViewPreset& v : views()) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

void BinViewLibrary::load(QSettings& settings) {
    views().clear();
    const int n = settings.beginReadArray(QStringLiteral("binViews"));
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) views().append({name, settings.value(QStringLiteral("columns")).toStringList()});
    }
    settings.endArray();
}

void BinViewLibrary::save(QSettings& settings) {
    settings.beginWriteArray(QStringLiteral("binViews"), static_cast<int>(views().size()));
    for (int i = 0; i < views().size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), views().at(i).name);
        settings.setValue(QStringLiteral("columns"), views().at(i).columns);
    }
    settings.endArray();
}

} // namespace lumenis::ui

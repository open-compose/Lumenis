#include "lumenis/ui/bin_view.h"

#include "lumenis/model/matte_key_clip.h"
#include "lumenis/model/proxy.h"
#include "lumenis/model/stereo_clip.h"
#include "lumenis/model/group_clip.h"

#include "lumenis/ui/brush_preview.h"

#include "lumenis/media/custom_color.h"
#include "lumenis/media/classic_title.h"
#include "lumenis/media/titler.h"
#include "lumenis/model/audio_eq.h"
#include "lumenis/model/audio_suite.h"
#include "lumenis/model/correction_template.h"
#include "lumenis/model/effect_template.h"
#include "lumenis/model/export_volume.h"
#include "lumenis/model/frame_flex.h"
#include "lumenis/model/motion_adapter.h"
#include "lumenis/model/title_clip.h"
#include "lumenis/model/titler.h"

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
#include <QDir>
#include <QDrag>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QItemSelectionModel>
#include <QCursor>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>
#include <QMouseEvent>
#include <QTimer>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

// One open bin (guide p.245-294): BinDocument, the only code that changes a
// model::Bin while it is open; the Qt models over it (BinItemModel for the
// rows, BinFilterModel for Set Bin Display, the quick filter and the sift);
// and the widgets, BinView with its Text (a QTableView), Frame (BinFrameView)
// and Script (BinScriptView) views. Saved Bin Views live in BinViewLibrary.
//
// The data flow is one way: a view calls BinDocument, which changes the bin
// and emits itemsChanged() or displayChanged(); BinItemModel then resets and
// BinView::applyDisplay() redraws. Nothing here writes to model::Bin directly.
// BinContainer (bin_container.cpp) opens documents and routes drops between
// bins. Related code: the bin model and its helpers (cellText, sortItems,
// siftMatches) in src/model/bin.h; the dialogs in bin_dialogs.cpp; frames from
// media in media_frames.h; sizes, icons and colours in theme.h.

namespace lumenis::ui {

namespace {

// Fixed column widths and Frame/Script view sizes in pixels. They are not
// measured from the font so that bins lay out the same on every platform
// (the screenshot comparison depends on it).
constexpr int kIconWidth = 26;
constexpr int kNameWidth = 220;
constexpr int kColumnWidth = 120;
constexpr int kDateWidth = 160;
constexpr int kColorWidth = 44;
constexpr int kFrameMargin = 8;
constexpr int kScriptTextHeight = 64;
constexpr int kSaveAsRole = Qt::UserRole + 1; // the Bin View menu's Save As

// The icon column's picture for each object type.
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
    case K::Script: return GlyphIcon::ItemScript;
    }
    return GlyphIcon::ItemClip;
}

// A flat tool button of the bin's top row, all of one size.
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
// otherwise black, with the object's icon unless `withIcon` is false.
void paintFrame(QPainter& p, const QRect& frame, model::BinItemKind kind, const QImage& picture = {}, bool withIcon = true) {
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
    if (!withIcon) return;
    const int s = std::max(12, std::min(frame.width(), frame.height()) / 3);
    const QRect icon(frame.center() - QPoint(s / 2, s / 2), QSize(s, s));
    glyphIcon(iconFor(kind)).paint(&p, icon, Qt::AlignCenter, QIcon::Disabled);
}

// A Titler+ template's first frame over black (FX Guide p.319), or a classic Title Effect clip's title over black ("you
// can display a frame from the title itself rather than display the Title Effect Clip icon", p.365; ours: always, from
// the title itself, its first page); null for other items.
QImage titleTemplatePicture(const model::BinItem& item, QSize size) {
    if (const auto classic = model::classicTitleOf(item)) {
        QImage picture(size, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::black);
        QPainter p(&picture);
        p.drawImage(0, 0, media::classicTitleImage(*classic, size));
        return picture;
    }
    if (!model::isTitleTemplate(item)) return {};
    const auto t = model::effectTemplateOf(item);
    return media::titleFirstFrame(t->effect, t->length, size);
}

// A 14-pixel colour square for the colour menus; an invalid colour (None) is
// crossed out.
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

    // The frame is centred at the top of the cell, with a highlight border
    // when selected; the elided name sits on one line below it.
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        p->save();
        const QSize f = view_->frameSize(view_->property("frameStep").toInt());
        const QRect cell = option.rect;
        const QRect frame(cell.left() + (cell.width() - f.width()) / 2, cell.top() + kFrameMargin / 2, f.width(),
                          f.height());
        const bool selected = option.state & QStyle::State_Selected;
        if (selected) p->fillRect(frame.adjusted(-3, -3, 3, 3), option.palette.color(QPalette::Highlight));
        const auto kind = static_cast<model::BinItemKind>(index.data(BinItemModel::kKindRole).toInt());
        const BinAppearance& look = BinAppearance::current();
        // Frame View: Show Border Colors (p.1154): by object type, or the clip's colour.
        const QColor border = look.frameBorders == BinAppearance::FrameBorders::Type
                                  ? BinAppearance::typeColor(kind)
                                  : index.data(BinItemModel::kColorRole).value<QColor>();
        if (border.isValid()) p->fillRect(frame.adjusted(-2, -2, 2, 2), border);
        const QImage picture = view_->picture(index.data(BinItemModel::kIdRole).toString(), f);
        paintFrame(*p, frame, kind, picture, look.frameIcons);
        // "Show icons displays the applicable bin item icon" (p.1154): in the corner of a picture.
        if (look.frameIcons && !picture.isNull()) {
            const QRect corner(frame.left() + 3, frame.top() + 3, 14, 14);
            p->fillRect(corner.adjusted(-1, -1, 1, 1), QColor(0, 0, 0, 150));
            glyphIcon(iconFor(kind)).paint(p, corner);
        }
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
        // Commit and close with NoHint and consume the key, so Qt's own Tab
        // and Enter handling does not move to another cell as well.
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

// Set Bin Display's "Show clips created by user" (p.288): turned off, it
// hides "all objects except those created by the system". The system makes
// source clips (capture and import) and rendered effects (rendering); the
// user makes every other kind.
bool isMadeByUser(const model::BinItem& item) {
    return item.kind != model::BinItemKind::Source && item.kind != model::BinItemKind::RenderedEffect;
}

} // namespace

// ---------------------------------------------------------------- BinDocument

BinDocument::BinDocument(QString nodeId, QString path, model::Bin bin, QObject* parent)
    : QObject(parent), nodeId_(std::move(nodeId)), path_(std::move(path)), bin_(std::move(bin)) {}

// Marks the bin changed; dirtyChanged(true) fires only on the first change
// since it was saved. A read-only bin's display changes are not unsaved work:
// they cannot be saved.
void BinDocument::touch() {
    if (isReadOnly()) return;
    if (!dirty_) {
        dirty_ = true;
        emit dirtyChanged(true);
    }
}

bool BinDocument::isReadOnly() const {
    return lockState_ != LockState::NotShared && lockState_ != LockState::Mine;
}

bool BinDocument::refused() {
    if (!isReadOnly()) return false;
    emit writeRefused();
    return true;
}

void BinDocument::setLockState(LockState state, std::optional<model::BinLockHolder> holder) {
    if (state == lockState_ && holder.has_value() == lockHolder_.has_value() &&
        (!holder || (holder->isSameAs(*lockHolder_) && holder->kind == lockHolder_->kind))) {
        return;
    }
    lockState_ = state;
    lockHolder_ = std::move(holder);
    emit lockChanged();
}

void BinDocument::reload(const model::Bin& bin) {
    const QString name = bin_.name;
    bin_ = bin;
    bin_.name = name; // the Sidebar's
    if (dirty_) {
        dirty_ = false;
        emit dirtyChanged(false);
    }
    emit itemsChanged();
    emit displayChanged();
}

void BinDocument::setName(const QString& name) {
    bin_.name = name;
    emit displayChanged();
}

// A mixed rate clip's adapted copy (the monitors', model::adaptedClip) goes into the bin as its own
// clip again, here and in replaceItem().
void BinDocument::addItem(const model::BinItem& item) {
    if (refused()) return;
    bin_.items.append(model::unadaptedClip(item));
    touch();
    emit itemsChanged();
}

bool BinDocument::replaceItem(const model::BinItem& item) {
    if (refused()) return false;
    model::BinItem* existing = bin_.item(item.id);
    if (!existing) return false;
    *existing = model::isAdapted(item) ? model::unadaptedClip(item, *existing) : item;
    touch();
    emit itemsChanged();
    return true;
}

void BinDocument::removeItems(const QStringList& ids) {
    if (refused()) return;
    const auto removed = bin_.items.removeIf([&](const model::BinItem& i) { return ids.contains(i.id); });
    if (removed == 0) return;
    for (const QString& id : ids) bin_.display.framePositions.remove(id);
    touch();
    emit itemsChanged();
}

bool BinDocument::setField(const QString& id, const QString& heading, const QString& value) {
    if (refused()) return false;
    model::BinItem* item = bin_.item(id);
    if (!item) return false;
    if (heading == QLatin1String("Name")) {
        if (value.trimmed().isEmpty() || value == item->name) return false;
        item->name = value;
    } else if (heading == QLatin1String(model::kFieldMotionHeading)) {
        // One of the Field Motion choices (p.472-473), as the column shows it.
        const auto f = model::fieldMotionFromText(value);
        if (!f || !model::hasFieldMotion(*item) || !model::fieldMotionChoices(*item).contains(*f)) return false;
        if (item->fields.value(heading) == model::fieldMotionText(*f)) return false;
        item->fields.insert(heading, model::fieldMotionText(*f));
    } else if (heading == QLatin1String(model::kReformatHeading)) {
        // "Click the Reformat field for the clip or sub-clip, and select an option" (p.476): master clips and subclips.
        const auto r = model::reformatFromText(value);
        if (!r || (item->kind != model::BinItemKind::MasterClip && item->kind != model::BinItemKind::Subclip)) return false;
        if (item->fields.value(heading) == model::reformatText(*r)) return false;
        item->fields.insert(heading, model::reformatText(*r));
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

void BinDocument::setExportVolume(const QJsonObject& volume) {
    bin_.exportVolume = volume;
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
    if (refused()) return;
    // Take the moved items out, counting those before `toIndex`, which shifts
    // the insertion point left by that many once they are gone.
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
    if (refused()) return;
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
    if (refused()) return;
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

// The setters below mark the bin changed but emit neither itemsChanged() nor
// displayChanged(): the views already show the change, and a reset would
// rebuild the Script view's text boxes under the user's typing.
void BinDocument::setScript(const QString& id, const QString& text) {
    if (refused()) return;
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

bool BinDocument::setRepresentedFrame(const QString& id, std::int64_t frame) {
    const model::BinItem* item = bin_.item(id);
    if (!item || isReadOnly()) return false;
    frame = std::clamp<std::int64_t>(frame, 0, std::max<std::int64_t>(0, item->durationFrames - 1));
    if (bin_.display.representedFrames.value(id, 0) == frame) return false;
    if (frame == 0) bin_.display.representedFrames.remove(id);
    else bin_.display.representedFrames.insert(id, frame);
    touch();
    return true;
}

void BinDocument::deleteCustomColumn(const QString& heading) {
    if (refused()) return;
    if (!bin_.display.customColumns.contains(heading)) return;
    bin_.display.customColumns.removeAll(heading);
    bin_.display.columns.removeAll(heading);
    bin_.display.widths.remove(heading);
    for (model::BinItem& i : bin_.items) i.fields.remove(heading);
    touch();
    emit displayChanged();
}

// Renames the heading wherever the bin refers to it: the custom column list,
// the shown columns, the widths, every item's field and the sort keys.
bool BinDocument::renameCustomColumn(const QString& from, const QString& to) {
    if (refused()) return false;
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
    if (isReadOnly()) {
        if (error) {
            *error = lockHolder_ ? tr("the bin is locked by %1").arg(lockHolder_->describe()) : tr("the bin is locked");
        }
        return false;
    }
    if (lockState_ == LockState::Mine && lockUser_) {
        // The lock may have gone to someone else since it was taken (p.304).
        const auto now = model::readBinLock(path_);
        if (!now || !now->isSameAs(*lockUser_)) {
            setLockState(now ? LockState::Others : LockState::Released, now);
            if (error) *error = now ? tr("the bin is locked by %1").arg(now->describe()) : tr("the bin's lock was released");
            return false;
        }
    }
    if (!model::BinFile::write(path_, bin_, error)) return false;
    if (lockState_ == LockState::Mine && lockUser_) model::appendBinHistory(path_, *lockUser_, QStringLiteral("saved"));
    if (dirty_) {
        dirty_ = false;
        emit dirtyChanged(false);
    }
    emit saved();
    return true;
}

// ---------------------------------------------------------------- BinItemModel

// Every change resets the model. Bins are small enough, and a reset keeps the
// rows and columns simply in step with the document; BinView keeps the
// selection across it.
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

// Column 0 is the icon, 1 is Name, and 2 on are display.columns in order.
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

// The custom roles answer for any column; the other roles depend on the column.
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
        if (role == Qt::DecorationRole) {
            // Show local media icons (p.1154): the icon in the colour the media calls for.
            const BinAppearance& look = BinAppearance::current();
            // A custom colour shows as "a rectangular color swatch icon" (FX Guide p.630).
            if (const auto custom = media::customColorOf(item)) {
                QPixmap pm(16, 16);
                pm.fill(*custom);
                QPainter p(&pm);
                p.setPen(QColor(20, 20, 20));
                p.drawRect(0, 0, 15, 15);
                return QIcon(pm);
            }
            // "Proxy media is displayed in a bin with an orange clip icon" (p.509).
            if (model::hasProxy(item)) return glyphIcon(iconFor(item.kind), QColor(0xe0, 0x86, 0x1a));
            if (look.localMediaIcons && BinAppearance::mediaTint()) {
                if (const QColor tint = BinAppearance::mediaTint()(item); tint.isValid()) return glyphIcon(iconFor(item.kind), tint);
            }
            // "The multigroup clip has the same icon as the group clips, but the icon is preceded by a plus sign" (p.1125).
            if (model::isMultigroup(item)) {
                QPixmap pm(28, 16);
                pm.fill(Qt::transparent);
                QPainter p(&pm);
                p.setPen(QPen(QApplication::palette().color(QPalette::WindowText), 2));
                p.drawLine(2, 8, 10, 8);
                p.drawLine(6, 4, 6, 12);
                glyphIcon(iconFor(item.kind)).paint(&p, QRect(12, 0, 16, 16));
                return QIcon(pm);
            }
            return glyphIcon(iconFor(item.kind));
        }
        return {};
    }
    if (h == QLatin1String("Color")) {
        // A swatch, not the colour's name (p.264).
        if (role == Qt::DecorationRole && !item.color.isEmpty()) return QColor(item.color);
        if (role == Qt::ToolTipRole) return model::clipColorName(item.color);
        return {};
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    if (BinAppearance::multiRezHeadings().contains(h)) {
        return BinAppearance::multiRezCell() ? BinAppearance::multiRezCell()(item, h) : QString();
    }
    if (model::proxyHeadings().contains(h)) {
        return BinAppearance::proxyCell() ? BinAppearance::proxyCell()(item, h) : model::proxyCellText(item, h, true, {});
    }
    return model::cellText(item, h);
}

// Typing into a cell goes to BinDocument::setField, which resets the model.
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

// Every cell can be dragged; only Name, the modifiable headings and custom
// columns can be typed into.
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
    // text/uri-list: files dragged in from the desktop (an .otio dropped into a bin, p.969).
    return {binItemsMime(), mediaFilesMime(), audioSuiteTemplateMime(), eqTemplateMime(), effectTemplateMime(),
            brushTemplateMime(), customColorMime(), correctionTemplateMime(), QStringLiteral("text/uri-list")};
}

namespace {

// The local files among a drop's URLs (files dragged in from the desktop's file manager).
QStringList localFiles(const QMimeData* data) {
    QStringList paths;
    for (const QUrl& url : data->urls()) {
        if (url.isLocalFile()) paths << url.toLocalFile();
    }
    return paths;
}

} // namespace

namespace {

// An effect template dropped from the AudioSuite tool (p.798), the Audio EQ tool (p.740), the Effect
// Editor or the Motion Effect Editor (FX Guide p.53, p.182): a new Effect item. False if the bin is locked
// or the data holds no template.
bool hasTemplate(const QMimeData* data) {
    return data->hasFormat(audioSuiteTemplateMime()) || data->hasFormat(eqTemplateMime()) ||
           data->hasFormat(effectTemplateMime()) || data->hasFormat(brushTemplateMime()) || data->hasFormat(customColorMime()) ||
           data->hasFormat(correctionTemplateMime());
}

bool addTemplate(BinDocument& document, const QMimeData* data) {
    if (document.isReadOnly()) return false;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (data->hasFormat(effectTemplateMime())) {
        const QJsonObject o = QJsonDocument::fromJson(data->data(effectTemplateMime())).object();
        const auto t = model::EffectTemplate::fromJson(o.value(QStringLiteral("template")).toObject());
        if (!t) return false;
        document.addItem(model::effectTemplateItem(*t, o.value(QStringLiteral("name")).toString(), now));
        return true;
    }
    if (data->hasFormat(correctionTemplateMime())) {
        // "A new effect template appears in the bin, containing all the color correction adjustment values for the
        // segment ... By default, Media Composer names the template using the clip name of the segment" (FX Guide p.613).
        const QJsonObject o = QJsonDocument::fromJson(data->data(correctionTemplateMime())).object();
        const model::CorrectionTemplate t = model::CorrectionTemplate::fromJson(o.value(QStringLiteral("template")).toObject());
        if (t.isEmpty()) return false;
        document.addItem(model::correctionTemplateItem(t, o.value(QStringLiteral("name")).toString(), now));
        return true;
    }
    if (data->hasFormat(customColorMime())) {
        // "The color appears in the bin as a rectangular color swatch icon. Media Composer assigns the color a name
        // based on the current Saved Color Labels settings" (FX Guide p.630).
        const QJsonObject o = QJsonDocument::fromJson(data->data(customColorMime())).object();
        const QColor color = QColor::fromString(o.value(QStringLiteral("color")).toString());
        if (!color.isValid()) return false;
        document.addItem(media::customColorItem(color, o.value(QStringLiteral("name")).toString(), now));
        return true;
    }
    if (data->hasFormat(brushTemplateMime())) {
        // The Effect Editor's Brush Template icon (FX Guide p.399): named Brush unless the drag says otherwise.
        const QJsonObject o = QJsonDocument::fromJson(data->data(brushTemplateMime())).object();
        if (!o.value(QStringLiteral("brush")).isObject()) return false;
        document.addItem(model::brushTemplateItem(o.value(QStringLiteral("brush")).toObject(), o.value(QStringLiteral("name")).toString(), now));
        return true;
    }
    if (data->hasFormat(eqTemplateMime())) {
        const QJsonObject o = QJsonDocument::fromJson(data->data(eqTemplateMime())).object();
        if (!o.value(QStringLiteral("eq")).isObject()) return false;
        document.addItem(model::eqTemplateItem(model::audioEqFromJson(o.value(QStringLiteral("eq")).toObject()),
                                               o.value(QStringLiteral("name")).toString(), now));
        return true;
    }
    const QJsonObject o = QJsonDocument::fromJson(data->data(audioSuiteTemplateMime())).object();
    const auto t = model::audioSuiteTemplateFromJson(o.value(QStringLiteral("template")).toObject());
    if (!t) return false;
    document.addItem(model::audioSuiteTemplateItem(*t, o.value(QStringLiteral("name")).toString(), now));
    return true;
}

} // namespace

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
    // "You cannot drag an item to a locked bin" (p.304).
    if (document_.isReadOnly()) return false;
    // Files from the Source Browser are linked into the bin (p.317).
    if (data->hasFormat(mediaFilesMime()) || hasTemplate(data)) return true;
    if (!data->hasFormat(binItemsMime())) return !localFiles(data).isEmpty();
    // Items from this same bin are refused: the Text and Frame views do not
    // reorder by dragging (only the Script view does).
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
    if (hasTemplate(data)) return addTemplate(document_, data);
    if (!data->hasFormat(binItemsMime())) {
        emit filesDropped(localFiles(data));
        return true;
    }
    QStringList lines = QString::fromUtf8(data->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QString from = lines.takeFirst();
    // Alt/Option (p.260) or Ctrl/Command (p.261) copies; see isCopyDrop().
    const bool copy = isCopyDrop(action, QApplication::keyboardModifiers());
    // Only reported: BinContainer::transferItems does the move or copy,
    // queued until the drag has finished.
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

// One setting for all bins (Bin settings), set by
// BinContainer::setQuickFilterAllColumns.
BinAppearance& BinAppearance::current() {
    static BinAppearance appearance;
    return appearance;
}

std::function<QColor(const model::BinItem&)>& BinAppearance::mediaTint() {
    static std::function<QColor(const model::BinItem&)> tint;
    return tint;
}

QStringList& BinAppearance::multiRezHeadings() {
    static QStringList headings;
    return headings;
}

std::function<QString(const model::BinItem&, const QString&)>& BinAppearance::proxyCell() {
    static std::function<QString(const model::BinItem&, const QString&)> f;
    return f;
}

std::function<QString(const model::BinItem&, const QString&)>& BinAppearance::multiRezCell() {
    static std::function<QString(const model::BinItem&, const QString&)> cell;
    return cell;
}

// "Blue - Precomputes and source side motion effects; Green - Master clips; Dark Green - Subclips and
// Group clips; Red - Sequences" (p.1154). Ours: audio clips count as master clips; other objects get
// no border.
QColor BinAppearance::typeColor(model::BinItemKind kind) {
    switch (kind) {
    case model::BinItemKind::MotionEffect:
    case model::BinItemKind::RenderedEffect: return QColor(60, 110, 220);
    case model::BinItemKind::MasterClip:
    case model::BinItemKind::AudioClip: return QColor(60, 190, 80);
    case model::BinItemKind::Subclip:
    case model::BinItemKind::Group: return QColor(30, 110, 50);
    case model::BinItemKind::Sequence: return QColor(210, 50, 50);
    default: return {};
    }
}

bool& BinFilterModel::searchAllColumns() {
    static bool all = false;
    return all;
}

// A row is shown if Set Bin Display shows it (its kind, and who made it), it
// matches the quick filter (if any) and, while the bin is shown sifted, it
// matches the sift. The sift's Any searches Name and the shown columns.
//
// "Show reference clips" (p.288) shows objects that sequences in the bin
// reference but that were not placed in the bin (model::BinItem::userPlaced
// false, as Media Composer's bins mark them).
bool BinFilterModel::filterAcceptsRow(int row, const QModelIndex&) const {
    const model::Bin& bin = document_.bin();
    if (row < 0 || row >= bin.items.size()) return false;
    const model::BinItem& item = bin.items.at(row);
    // Set Bin Display (p.288).
    if (bin.display.hiddenKinds.contains(item.kind)) return false;
    if (!bin.display.showUserClips && isMadeByUser(item)) return false;
    if (!bin.display.showReferenceClips && !item.userPlaced) return false;
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
    // Frames the user drags are remembered in the bin; decoded head frames
    // repaint the view as they arrive.
    connect(this, &QListView::indexesMoved, this, &BinFrameView::storePositions);
    if (MediaFrames* frames = MediaFrames::instance()) {
        connect(frames, &MediaFrames::thumbnailReady, viewport(), qOverload<>(&QWidget::update));
    }
}

QImage BinFrameView::picture(const QString& id, QSize size, bool now) const {
    const model::BinItem* item = document_.bin().item(id);
    if (item) {
        if (const QImage title = titleTemplatePicture(*item, size); !title.isNull()) return title;
        if (const auto custom = media::customColorOf(*item)) {
            QImage swatch(size, QImage::Format_RGB32);
            swatch.fill(*custom);
            return swatch;
        }
    }
    MediaFrames* frames = MediaFrames::instance();
    if (!item || !frames) return {};
    // A Matte Key clip (9.2b) shows its fill precompute's frame.
    if (const auto key = model::matteKeyClipSegment(*item)) {
        const model::MatteKeyMedia media = model::matteKeyMediaOf(*key->effect);
        const model::BinItem* fill = media.binId == document_.nodeId() ? document_.bin().item(media.fill) : nullptr;
        if (!fill) return {};
        return now ? frames->frame(*fill, 0, size) : frames->thumbnail(*fill, 0, size);
    }
    return now ? frames->frame(*item, representedFrame(id), size) : frames->thumbnail(*item, representedFrame(id), size);
}

// ---- the frame identifying a clip (8.7d3; p.251)

namespace {

// As the monitors' Mouse Jog (Composer::kJogPixels): a frame every four pixels.
constexpr int kFrameJogPixels = 4;

// The mouse and the keys that end mouse control, for a Frame view's Mouse Jog and Mouse Shuttle.
class FrameMouseFilter : public QObject {
public:
    explicit FrameMouseFilter(BinFrameView* view) : QObject(view), view_(view), last_(QCursor::pos()) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::MouseMove: {
            const QPoint at = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
            const int dx = at.x() - last_.x();
            last_ = at;
            if (dx != 0) view_->mouseControlMoved(dx);
            return false;
        }
        case QEvent::MouseButtonDblClick:
            view_->stopMouseControl();
            return true;
        case QEvent::KeyPress: {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Space || key == Qt::Key_Escape) {
                view_->stopMouseControl();
                return true;
            }
            return false;
        }
        default:
            return QObject::eventFilter(watched, event);
        }
    }

private:
    BinFrameView* view_;
    QPoint last_;
};

bool isRepresentKey(const QKeyEvent* key, bool kHeld) {
    if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;
    switch (key->key()) {
    case Qt::Key_K:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_N:
    case Qt::Key_Semicolon:
        return true;
    case Qt::Key_L:
    case Qt::Key_J:
        return kHeld;
    default:
        return false;
    }
}

} // namespace

std::int64_t BinFrameView::representedFrame(const QString& id) const {
    return document_.bin().display.representedFrames.value(id, 0);
}

QStringList BinFrameView::representedItems(bool beep) const {
    QStringList ids;
    if (selectionModel()) {
        for (const QModelIndex& i : selectionModel()->selectedIndexes()) {
            const QString id = i.data(BinItemModel::kIdRole).toString();
            const model::BinItem* item = document_.bin().item(id);
            // "If you have group or multigroup clips in the bin and want to change the displayed frame, use controls in
            // Source/Record mode" (p.251).
            if (!item || item->kind == model::BinItemKind::Group || item->durationFrames <= 1 || ids.contains(id)) continue;
            ids << id;
        }
    }
    if (ids.isEmpty() && beep) QApplication::beep();
    return ids;
}

bool BinFrameView::stepRepresented(std::int64_t frames) {
    bool moved = false;
    for (const QString& id : representedItems(false)) moved |= document_.setRepresentedFrame(id, representedFrame(id) + frames);
    if (moved) viewport()->update();
    return moved;
}

bool BinFrameView::representEnd(bool last) {
    bool moved = false;
    for (const QString& id : representedItems(true)) {
        const model::BinItem* item = document_.bin().item(id);
        moved |= document_.setRepresentedFrame(id, last ? item->durationFrames - 1 : 0);
    }
    if (moved) viewport()->update();
    return moved;
}

void BinFrameView::connectRollTimer(bool shuttle) {
    if (!rollTimer_) rollTimer_ = new QTimer(this);
    disconnect(rollTimer_, nullptr, this, nullptr);
    if (shuttle) {
        // Shuttling: whole frames as the speed adds up, 25 ticks a second, a frame a second for each pixel.
        rollTimer_->setInterval(40);
        connect(rollTimer_, &QTimer::timeout, this, [this] {
            shuttleCarry_ += mouseOffset_ * 0.04;
            const auto frames = static_cast<std::int64_t>(shuttleCarry_);
            shuttleCarry_ -= static_cast<double>(frames);
            if (frames != 0) stepRepresented(frames);
        });
    } else {
        rollTimer_->setInterval(1000 / kRollFps);
        connect(rollTimer_, &QTimer::timeout, this, [this] {
            if (!stepRepresented(rollDirection_)) roll(0);
        });
    }
}

void BinFrameView::roll(int direction) {
    direction = direction > 0 ? 1 : direction < 0 ? -1 : 0;
    if (direction != 0 && representedItems(true).isEmpty()) direction = 0;
    rollDirection_ = direction;
    if (!rollTimer_) connectRollTimer(false);
    if (direction == 0) {
        if (mouseControl_ != MouseControl::Shuttle) rollTimer_->stop();
        return;
    }
    stepRepresented(direction);
    rollTimer_->start();
}

void BinFrameView::startMouseControl(MouseControl mode) {
    if (mode == MouseControl::None) {
        stopMouseControl();
        return;
    }
    if (representedItems(true).isEmpty()) return;
    roll(0);
    mouseControl_ = mode;
    mouseOffset_ = 0;
    shuttleCarry_ = 0;
    if (!mouseFilter_) {
        mouseFilter_ = new FrameMouseFilter(this);
        qApp->installEventFilter(mouseFilter_);
    }
    if (mode == MouseControl::Shuttle) {
        connectRollTimer(true);
        rollTimer_->start();
    }
}

void BinFrameView::stopMouseControl() {
    if (mouseControl_ == MouseControl::None) return;
    const bool shuttling = mouseControl_ == MouseControl::Shuttle;
    mouseControl_ = MouseControl::None;
    if (mouseFilter_) {
        qApp->removeEventFilter(mouseFilter_);
        mouseFilter_->deleteLater();
        mouseFilter_ = nullptr;
    }
    if (shuttling && rollTimer_) {
        rollTimer_->stop();
        connectRollTimer(false); // back to K+L rolling
    }
}

void BinFrameView::mouseControlMoved(double dx) {
    if (mouseControl_ == MouseControl::Jog) {
        mouseOffset_ += dx;
        const auto frames = static_cast<std::int64_t>(mouseOffset_ / kFrameJogPixels);
        if (frames == 0) return;
        mouseOffset_ -= static_cast<double>(frames) * kFrameJogPixels;
        stepRepresented(frames);
    } else if (mouseControl_ == MouseControl::Shuttle) {
        mouseOffset_ = std::clamp(mouseOffset_ + dx, -300.0, 300.0);
    }
}

bool BinFrameView::event(QEvent* event) {
    // The keys that change the frame go to the Frame view, not to the commands mapped to them, while clips are selected.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (isRepresentKey(key, kHeld_) && !representedItems(false).isEmpty()) {
            event->accept();
            return true;
        }
    }
    return QListView::event(event);
}

void BinFrameView::keyPressEvent(QKeyEvent* event) {
    if (!isRepresentKey(event, kHeld_) || representedItems(false).isEmpty()) {
        QListView::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_K: kHeld_ = true; break;
    case Qt::Key_L:
    case Qt::Key_J:
        if (!event->isAutoRepeat()) roll(event->key() == Qt::Key_L ? 1 : -1);
        break;
    case Qt::Key_Home: representEnd(false); break;
    case Qt::Key_End: representEnd(true); break;
    case Qt::Key_N: startMouseControl(MouseControl::Jog); break;
    case Qt::Key_Semicolon: startMouseControl(MouseControl::Shuttle); break;
    default: break;
    }
    event->accept();
}

void BinFrameView::keyReleaseEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat()) {
        // "When you see the frame that you want to use, release the keys" (p.251).
        if (event->key() == Qt::Key_K) {
            kHeld_ = false;
            roll(0);
        } else if ((event->key() == Qt::Key_L || event->key() == Qt::Key_J) && rollDirection_ != 0) {
            roll(0);
        }
    }
    QListView::keyReleaseEvent(event);
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

// The delegate reads the frame size from the "frameStep" property; the grid
// size makes QListView lay out whole cells.
void BinFrameView::applyDisplay() {
    setProperty("frameStep", document_.bin().display.frameSize);
    setGridSize(cellSize());
    doItemsLayout();
    viewport()->update();
}

// QListView lays every frame out in rows whenever it lays out the items (after
// a reset, for instance); frames the user placed are then moved back to their stored positions. Frames with no
// stored position keep their place in the rows.
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

// The nearest grid point (a multiple of the cell size), never negative.
QPoint BinFrameView::snapped(const QPoint& p) const {
    const QSize c = cellSize();
    auto snap = [](int v, int step) { return std::max(0, (v + step / 2) / step * step); };
    return {snap(p.x(), c.width()), snap(p.y(), c.height())};
}

// After the user drags frames: keep their new positions, snapped unless Snap
// to Grid is Disable.
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
    // Cells are keyed by (row, column) packed into 64 bits. Frames that do not
    // move occupy their cells first.
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
        // Step right to the next free cell, wrapping to the next row at the
        // window's edge.
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

// The dotted grid under the frames: always with Snap to Grid Enable, and only
// while dragging with Temporary. Disable and Invisible draw none. The lines
// follow the scroll offset so they stay on the cell edges.
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

// The tooltip is the item's summary (kSummaryRole), not the cell text.
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
    // Any change to the rows rebuilds every row widget.
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

// One QFrame per shown row: the frame on the left, the name and duration above
// a text box on the right. Rows carry their item id in the "binItemId"
// property, which the event filter, selection and pictures look up. The
// content widget takes drops; rows take the clicks and drags.
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

// The frame as a pixmap for the row's QLabel, drawn as the Frame view draws it.
QPixmap BinScriptView::scriptPicture(const model::BinItem& item, QSize size) const {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    MediaFrames* frames = MediaFrames::instance();
    const QImage title = titleTemplatePicture(item, size);
    paintFrame(p, QRect(QPoint(0, 0), size), item.kind, !title.isNull() ? title : frames ? frames->thumbnail(item, 0, size) : QImage());
    return pm;
}

// When decoded head frames arrive, redraw the pictures without a rebuild.
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

// Selected rows get the highlight background, the others AlternateBase.
void BinScriptView::updateSelection() {
    if (!content_) return;
    QSet<QString> selected;
    for (const QModelIndex& i : selection_->selectedIndexes()) selected.insert(i.data(BinItemModel::kIdRole).toString());
    for (QFrame* row : content_->findChildren<QFrame*>(QStringLiteral("scriptRow"))) {
        row->setBackgroundRole(selected.contains(idAt(row)) ? QPalette::Highlight : QPalette::AlternateBase);
    }
}

// The Script view has no QAbstractItemView, so selection and drag and drop
// are done here, on the row widgets and the content widget:
// - a press on a row selects it (or toggles it with Ctrl/Cmd); a press on an
//   already-selected row keeps the selection, so several rows can be dragged;
// - moving past the drag distance starts a drag of the selected items, in
//   the same binItemsMime() format as the other views;
// - a drop from another bin is reported with itemsDropped(); a drop from this
//   bin moves the items before the row under the pointer.
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
        if (hasTemplate(de->mimeData()) && !document_.isReadOnly()) de->acceptProposedAction();
        return true;
    }
    if (watched == content_ && event->type() == QEvent::Drop) {
        auto* de = static_cast<QDropEvent*>(event);
        if (hasTemplate(de->mimeData())) {
            if (addTemplate(document_, de->mimeData())) de->acceptProposedAction();
            return true;
        }
        QStringList ids =
            QString::fromUtf8(de->mimeData()->data(binItemsMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (ids.isEmpty()) return true;
        const QString from = ids.takeFirst();
        if (from != document_.nodeId()) {
            // From another bin.
            de->acceptProposedAction();
            emit itemsDropped(from, ids, isCopyDrop(de->dropAction(), de->modifiers()));
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

    // The widget is built top to bottom: the button row, the three views in a
    // stack (one shown per mode), then the status bar. All three views show
    // proxy_ and share the table's selection model.

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
        // The Bin menu, and Input > Source Browser (p.315). Refilled each time
        // it opens, with the main menu's own actions, so their state is current.
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
    // The search button shows the filter box; hiding it clears the filter.
    connect(search, &QToolButton::toggled, this, [this](bool on) {
        filter_->setVisible(on);
        if (on) {
            filter_->setFocus();
        } else {
            filter_->clear();
        }
    });
    connect(filter_, &QLineEdit::textChanged, this, &BinView::setQuickFilter);

    // The Bin Lock Status button, only for a bin on shared storage (p.301-302).
    lockButton_ = toolButton(this, QStringLiteral("binLockButton"), QIcon(), tr("Bin Lock Status"));
    connect(lockButton_, &QToolButton::clicked, this,
            [this] { emit lockButtonClicked(QApplication::keyboardModifiers() & Qt::AltModifier); });
    connect(&document_, &BinDocument::lockChanged, this, &BinView::updateLockButton);
    updateLockButton();

    auto* top = new QHBoxLayout;
    top->setContentsMargins(4, 3, 4, 3);
    top->setSpacing(2);
    top->addWidget(lockButton_);
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
    // A heading dragged to a new place: rewrite the bin's columns once the
    // header has finished moving it. Moves made by applyDisplay() itself are
    // ignored (applying_).
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

    // The same right-click menu in all three views. The table and Frame view
    // report positions in their viewports, the Script view in itself.
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

    // An Export Volume bin's bar: "Left to right: Export Volume Type, Path to Asset folder, Properties button, Commit
    // button" (p.903).
    volumeBar_ = new QWidget(this);
    volumeBar_->setObjectName(QStringLiteral("exportVolumeBar"));
    auto* volume = new QHBoxLayout(volumeBar_);
    volume->setContentsMargins(4, 2, 4, 2);
    volumeType_ = new QLabel(volumeBar_);
    volumeType_->setObjectName(QStringLiteral("exportVolumeType"));
    volumePath_ = new QLabel(volumeBar_);
    volumePath_->setObjectName(QStringLiteral("exportVolumePath"));
    volumePath_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* properties = new QPushButton(tr("Properties"), volumeBar_);
    properties->setObjectName(QStringLiteral("exportVolumeProperties"));
    auto* commit = new QPushButton(tr("Commit"), volumeBar_);
    commit->setObjectName(QStringLiteral("exportVolumeCommit"));
    connect(properties, &QPushButton::clicked, this, &BinView::volumePropertiesClicked);
    connect(commit, &QPushButton::clicked, this, &BinView::volumeCommitClicked);
    volume->addWidget(volumeType_);
    volume->addWidget(volumePath_, 1);
    volume->addWidget(properties);
    volume->addWidget(commit);
    connect(&document_, &BinDocument::displayChanged, this, &BinView::updateVolumeBar);
    updateVolumeBar();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(volumeBar_);
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

// Brings the widgets in line with model::BinDisplay after any display change:
// column order and widths, selected headings, background colour, the view
// mode, the Frame view, the status bar and the Bin View menu. `applying_`
// stops the header's signals from writing these back into the document.
void BinView::refreshAppearance() {
    applyDisplay();
    table_->viewport()->update();
    frames_->viewport()->update();
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

    // Bin background colour (p.263), unless the Interface settings turn bin backgrounds off or show
    // them on the tabs only (p.1201).
    const BinAppearance& look = BinAppearance::current();
    QPalette p = palette();
    if (!d.background.isEmpty() && look.backgrounds && !look.tabsOnly) {
        p.setColor(QPalette::Base, QColor(d.background));
        p.setColor(QPalette::AlternateBase, QColor(d.background).darker(110));
    }
    table_->setPalette(p);
    frames_->setPalette(p);
    script_->setPalette(p);

    // Set Font (p.81-82): the bin's own font, or the application's.
    QFont f = QApplication::font();
    if (!d.fontFamily.isEmpty()) f.setFamily(d.fontFamily);
    if (d.fontSize > 0) f.setPointSize(d.fontSize);
    // Override all bin fonts and font sizes (p.1201).
    if (!look.fontFamily.isEmpty()) f.setFamily(look.fontFamily);
    if (look.fontSize > 0) f.setPointSize(look.fontSize);
    for (QWidget* w : {static_cast<QWidget*>(table_), static_cast<QWidget*>(frames_), static_cast<QWidget*>(script_)}) {
        if (w->font() != f) w->setFont(f);
    }

    stack_->setCurrentIndex(static_cast<int>(d.mode));
    if (QAbstractButton* b = modes_->button(static_cast<int>(d.mode))) b->setChecked(true);
    frames_->applyDisplay();
    status_->setVisible(d.statusBar);
    updateViewMenu();
    updateStatus();
    applying_ = false;
}

// Refills the Bin View menu: built-in views, saved views, Save As. A bin
// whose view is not in the list (or has none, "Untitled") gets an extra
// entry at the top for it.
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
    // A view whose columns changed reads "<view>.1", in italic (p.249). Only
    // that entry is italic in the list (its Qt::FontRole); the closed menu,
    // which draws with the box's own font, follows the entry it shows.
    QFont f = viewMenu_->font();
    f.setItalic(false);
    for (int i = 0; i < viewMenu_->count(); ++i) viewMenu_->setItemData(i, f, Qt::FontRole);
    f.setItalic(d.viewModified);
    if (d.viewModified) {
        viewMenu_->setItemText(index, shown + QStringLiteral(".1"));
        viewMenu_->setItemData(index, f, Qt::FontRole);
    }
    viewMenu_->setFont(f);
    viewMenu_->setCurrentIndex(index);
    // The Bin View menu works in Text view only (p.248).
    viewMenu_->setEnabled(d.mode == model::BinViewMode::Text);
}

// Green: this user may change the bin; red: someone else holds it, or it is
// protected; yellow: its holder has saved changes since it was read; blue:
// its holder has let it go (p.301-303).
void BinView::updateVolumeBar() {
    const auto volume = model::exportVolumeOf(document_.bin());
    volumeBar_->setVisible(volume.has_value());
    if (!volume) return;
    volumeType_->setText(volume->type);
    volumePath_->setText(QDir::toNativeSeparators(volume->folder()));
}

void BinView::updateLockButton() {
    using State = BinDocument::LockState;
    const State state = document_.lockState();
    lockButton_->setVisible(state != State::NotShared);
    QColor color;
    QString tip;
    const QString who = document_.lockHolder() ? document_.lockHolder()->describe() : tr("another user");
    switch (state) {
    case State::NotShared: return;
    case State::Mine:
        color = QColor(0x3c, 0xc8, 0x50);
        tip = tr("Unlocked: you can change this bin. Click for its history; Alt+click to release the lock.");
        break;
    case State::Others:
        color = QColor(0xe0, 0x40, 0x40);
        tip = tr("Locked by %1. Click for the bin's history.").arg(who);
        break;
    case State::Protected:
        color = QColor(0xe0, 0x40, 0x40);
        tip = tr("Protected by %1: read only for everyone. Click for the bin's history.").arg(who);
        break;
    case State::Modified:
        color = QColor(0xf0, 0xd0, 0x30);
        tip = tr("%1 has changed this bin. Click to read it again.").arg(who);
        break;
    case State::Released:
        color = QColor(0x40, 0x80, 0xf0);
        tip = tr("The lock is free. Click to take it.");
        break;
    }
    lockButton_->setIcon(glyphIcon(GlyphIcon::Lock, color));
    lockButton_->setToolTip(tip);
}

void BinView::updateStatus() {
    // "Viewing: 22/22" (p.255), and how many are selected.
    const int selected = static_cast<int>(selectedIds().size());
    QString text = tr("Viewing: %1/%2").arg(visibleCount()).arg(document_.bin().items.size());
    if (selected > 0) text += tr("    Selected: %1").arg(selected);
    status_->setText(text);
}

// The selection model holds a cell per column; reduce it to rows, in the
// order shown.
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

// Selected headings narrow the search only in Text view, where they can be
// seen.
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

// A built-in view of the same name wins over a saved one.
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
    emit viewSaved();
}

// ---------------------------------------------------------------- columns

void BinView::selectHeadings(const QStringList& headings) {
    selectedHeadings_ = headings;
    selectedHeadings_.removeIf([&](const QString& h) { return model_->columnOf(h) < 0; });
    model_->setSelectedHeadings(selectedHeadings_);
    if (!quick_.isEmpty()) setQuickFilter(quick_);
}

// Clicking a heading selects it (p.247); the icon heading clears the
// selection.
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

// The heading's right-click menu. Right-clicking an unselected heading
// selects it first.
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
    // A line edit laid over the heading's section; finishing (Enter or
    // focus out) renames, and an unchanged or refused name leaves it as it was.
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
    // "These headings appear at the bottom of the Bin Column Selection list" (p.1116).
    for (const QString& h : BinAppearance::multiRezHeadings()) {
        if (!available.contains(h)) available << h;
    }
    // The proxy columns (p.509-510), after them (ours).
    for (const QString& h : model::proxyHeadings()) {
        if (!available.contains(h)) available << h;
    }
    ChooseColumnsDialog dialog(available, d.columns, this);
    if (dialog.exec() == QDialog::Accepted) setColumns(dialog.chosen());
}

void BinView::setColumns(const QStringList& headings) {
    model::BinDisplay d = document_.bin().display;
    // Columns still wanted keep their order.
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
        // A custom column: ask Delete (OK), Hide or Cancel.
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

// Queued from the header's sectionMoved, so it runs after the header has
// finished. The display is set even when the order is unchanged, so that
// applyDisplay() moves the sections back to the model's order.
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

// Apply in the dialog sifts at once; OK sifts with what the dialog holds.
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

// None, the clip colours of model::clipColors(), and Other. With `parent` the
// menu is its submenu; otherwise a new top-level menu the caller owns. It
// sets the selected items' colour, or with `background` the bin's.
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

QStringList BinView::promoteEffects(const QStringList& ids) {
    QStringList made;
    if (document_.isReadOnly()) return made;
    QStringList names;
    for (const model::BinItem& item : document_.bin().items) names << item.name;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const QString& id : ids) {
        const model::BinItem* item = document_.bin().item(id);
        if (!item || !model::effectTemplateOf(*item)) continue;
        model::BinItem copy = *item;
        copy.id = model::newItemId();
        copy.mobId = {};
        copy.name = model::promotedTemplateName(item->name, names);
        copy.created = now;
        copy.modified = now;
        names << copy.name;
        document_.addItem(copy);
        made << copy.id;
    }
    return made;
}

QMenu* BinView::contextMenu(const QPoint& viewportPos, QWidget* view) {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("binContextMenu"));
    // Right-click in the Color column offers the colours (p.264).
    if (view == table_) {
        const QModelIndex at = table_->indexAt(viewportPos);
        if (at.isValid() && model_->heading(at.column()) == QLatin1String("Color")) {
            // Only the colours: the menu built so far is thrown away.
            if (!table_->selectionModel()->isSelected(at)) selectIds({at.data(BinItemModel::kIdRole).toString()});
            menu->deleteLater();
            return colorMenu(nullptr, tr("Color"), false);
        }
        // "In the Field Motion bin column, right click the menu to change the Field Motion attribute value
        // for the clip" (Editing Guide p.472): the choices for the clicked clip, set on every selected
        // clip that has the attribute.
        if (at.isValid() && model_->heading(at.column()) == QLatin1String(model::kFieldMotionHeading)) {
            const QString id = at.data(BinItemModel::kIdRole).toString();
            const model::BinItem* item = document_.bin().item(id);
            if (item && model::hasFieldMotion(*item)) {
                if (!table_->selectionModel()->isSelected(at)) selectIds({id});
                menu->deleteLater();
                auto* fields = new QMenu(this);
                fields->setObjectName(QStringLiteral("fieldMotionMenu"));
                const QString current = model::cellText(*item, QLatin1String(model::kFieldMotionHeading));
                for (const model::FieldMotionAttribute f : model::fieldMotionChoices(*item)) {
                    QAction* a = fields->addAction(model::fieldMotionText(f));
                    a->setCheckable(true);
                    a->setChecked(a->text() == current);
                    connect(a, &QAction::triggered, this, [this, text = a->text()] {
                        for (const QString& selected : selectedIds()) {
                            const model::BinItem* clip = document_.bin().item(selected);
                            if (clip && model::hasFieldMotion(*clip)) document_.setField(selected, QLatin1String(model::kFieldMotionHeading), text);
                        }
                    });
                }
                return fields;
            }
        }
        // The Reformatting Options column (p.476): the four options, set on every selected clip.
        if (at.isValid() && model_->heading(at.column()) == QLatin1String(model::kReformatHeading)) {
            const QString id = at.data(BinItemModel::kIdRole).toString();
            const model::BinItem* item = document_.bin().item(id);
            if (item && (item->kind == model::BinItemKind::MasterClip || item->kind == model::BinItemKind::Subclip)) {
                if (!table_->selectionModel()->isSelected(at)) selectIds({id});
                menu->deleteLater();
                auto* options = new QMenu(this);
                options->setObjectName(QStringLiteral("reformatMenu"));
                const QString current = item->fields.value(QLatin1String(model::kReformatHeading));
                for (model::Reformat r : {model::Reformat::Stretch, model::Reformat::Letterbox, model::Reformat::CenterCrop,
                                          model::Reformat::CenterKeepSize}) {
                    QAction* a = options->addAction(model::reformatText(r));
                    a->setCheckable(true);
                    a->setChecked(a->text() == current);
                    connect(a, &QAction::triggered, this, [this, text = a->text()] {
                        for (const QString& selected : selectedIds()) document_.setField(selected, QLatin1String(model::kReformatHeading), text);
                    });
                }
                return options;
            }
        }
        if (at.isValid() && !table_->selectionModel()->isSelected(at)) {
            selectIds({at.data(BinItemModel::kIdRole).toString()});
        }
    }
    QMenu* clip = colorMenu(menu, tr("Set Clip Color"), false);
    clip->setEnabled(!selectedIds().isEmpty());
    // "Right-click the template in the bin, select Promote Effects, and then click OK in the Promote
    // Effects dialog box" (FX Guide p.54).
    {
        QStringList templates;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            if (item && model::effectTemplateOf(*item)) templates << id;
        }
        if (!templates.isEmpty()) {
            QAction* promote = menu->addAction(tr("Promote Effects"), this, [this, templates] {
                QMessageBox box(QMessageBox::Question, tr("Promote Effects"),
                                tr("Promote the selected effect templates to advanced keyframes? A promoted copy of each is "
                                   "made in the bin."),
                                QMessageBox::Ok | QMessageBox::Cancel, this);
                box.setObjectName(QStringLiteral("promoteEffects"));
                if (box.exec() == QMessageBox::Ok) promoteEffects(templates);
            });
            promote->setObjectName(QStringLiteral("promoteEffects"));
            promote->setEnabled(!document_.isReadOnly());
        }
    }
    // "From a bin, right-click a sequence and select Sequence Report" (p.369; 9.2c).
    {
        bool sequences = false;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            sequences = sequences || (item && item->kind == model::BinItemKind::Sequence);
        }
        if (sequences) {
            QAction* report = menu->addAction(tr("Sequence Report"), this, [this] { emit commandRequested(QStringLiteral("bin.sequence_report")); });
            report->setObjectName(QStringLiteral("sequenceReport"));
        }
    }
    // "Select one or more clips in the bin, right-click and choose Source Settings" (p.529; 8.5b1).
    {
        bool clips = false;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            clips = clips || (item && (item->kind == model::BinItemKind::MasterClip || item->kind == model::BinItemKind::Subclip));
        }
        if (clips) {
            QAction* settings = menu->addAction(tr("Source Settings"), this, [this] { emit commandRequested(QStringLiteral("bin.source_settings")); });
            settings->setObjectName(QStringLiteral("sourceSettings"));
            settings->setEnabled(!document_.isReadOnly());
        }
    }
    // "Right-click on the asset(s) and select Create Proxies" (p.508).
    {
        bool proxyable = false;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            proxyable = proxyable || (item && model::canHaveProxy(*item));
        }
        if (proxyable) {
            QAction* proxies = menu->addAction(tr("Create Proxies"), this, [this] { emit commandRequested(QStringLiteral("bin.create_proxies")); });
            proxies->setObjectName(QStringLiteral("createProxies"));
            proxies->setEnabled(!document_.isReadOnly());
        }
    }
    // "Select the items in the bin you want to copy ... Right-click and select Copy Media" (p.512).
    if (!selectedIds().isEmpty()) {
        QAction* copy = menu->addAction(tr("Copy Media"), this, [this] { emit commandRequested(QStringLiteral("bin.copy_media")); });
        copy->setObjectName(QStringLiteral("copyMedia"));
    }
    // Stereoscopic clips (8.6d1; ours, the guide leaves making them to the Help, p.213): two master clips make one, and a
    // stereo clip's S3D Leading Eye (p.66).
    {
        int masters = 0;
        bool stereo = false;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            if (item && item->kind == model::BinItemKind::MasterClip) ++masters;
            stereo = stereo || (item && model::isStereoClip(*item));
        }
        if (masters == 2 && !stereo) {
            QAction* create = menu->addAction(tr("Create Stereoscopic Clip"), this,
                                              [this] { emit commandRequested(QStringLiteral("bin.create_stereoscopic_clip")); });
            create->setObjectName(QStringLiteral("createStereoscopicClip"));
            create->setEnabled(!document_.isReadOnly());
        }
        if (stereo) {
            QMenu* eye = menu->addMenu(tr("S3D Leading Eye"));
            eye->setObjectName(QStringLiteral("s3dLeadingEye"));
            eye->setEnabled(!document_.isReadOnly());
            eye->addAction(tr("Left"), this, [this] { emit commandRequested(QStringLiteral("bin.s3d_leading_eye_left")); });
            eye->addAction(tr("Right"), this, [this] { emit commandRequested(QStringLiteral("bin.s3d_leading_eye_right")); });
        }
    }
    // "Right-click on a template in the bin and choose Edit Template" (FX Guide p.320).
    if (selectedIds().size() == 1) {
        const model::BinItem* item = document_.bin().item(selectedIds().first());
        if (item && model::isTitleTemplate(*item)) {
            const QString id = item->id;
            QAction* edit = menu->addAction(tr("Edit Template"), this, [this, id] { emit editTemplateRequested(id); });
            edit->setObjectName(QStringLiteral("editTemplate"));
        }
    }
    // "Select the group clip in the bin, right click and choose Edit Group" (p.1125); "Right-click the sequence and select
    // Commit Multicam Edits" (p.1135).
    {
        bool group = false;
        bool multicam = false;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            if (!item) continue;
            group = group || model::isGroupClip(*item);
            if (item->kind == model::BinItemKind::Sequence) {
                const model::Sequence seq = model::Sequence::fromItem(*item);
                for (const model::Track& t : seq.tracks()) {
                    for (const model::Segment& seg : t.segments) multicam = multicam || seg.groupAngle.has_value();
                }
            }
        }
        if (group) {
            QAction* edit = menu->addAction(tr("Edit Group"), this, [this] { emit commandRequested(QStringLiteral("clip.edit_group")); });
            edit->setObjectName(QStringLiteral("editGroup"));
        }
        if (multicam) {
            QAction* commit = menu->addAction(tr("Commit Multicam Edits"), this,
                                              [this] { emit commandRequested(QStringLiteral("bin.commit_multicam_edits")); });
            commit->setObjectName(QStringLiteral("commitMulticamEdits"));
            commit->setEnabled(!document_.isReadOnly());
        }
    }
    // The Transcript menu (p.450-453; 2026.8 guide p.456): Export Transcript and Share Transcripts for clips and
    // sequences, Delete Transcript for clips, Export SubCap for sequences.
    {
        bool clipSelected = false;
        bool sequence = false;
        for (const QString& id : selectedIds()) {
            const model::BinItem* item = document_.bin().item(id);
            if (!item) continue;
            sequence = sequence || item->kind == model::BinItemKind::Sequence;
            clipSelected = clipSelected || item->kind == model::BinItemKind::MasterClip || item->kind == model::BinItemKind::AudioClip ||
                   item->kind == model::BinItemKind::Subclip || item->kind == model::BinItemKind::Group;
        }
        if (clipSelected || sequence) {
            QMenu* transcript = menu->addMenu(tr("Transcript"));
            transcript->setObjectName(QStringLiteral("binTranscriptMenu"));
            auto add = [this, transcript](const QString& text, const char* id, bool enabled) {
                QAction* a = transcript->addAction(text, this, [this, id] { emit commandRequested(QString::fromLatin1(id)); });
                a->setObjectName(QString::fromLatin1(id));
                a->setEnabled(enabled);
            };
            add(tr("Export Transcript"), "bin.export_transcript", true);
            add(tr("Share Transcripts"), "bin.share_transcripts", true);
            add(tr("Delete Transcript"), "bin.delete_transcript", clipSelected);
            add(tr("Export SubCap"), "bin.export_subcap", sequence);
            // "right-click on one or more selected clips in a bin and choose Transcript > Create Script from Clip" (p.570).
            add(tr("Create Script from Clip"), "bin.create_script_from_clip", clipSelected);
        }
    }
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

// Watches the table, its viewport and the Frame view.
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

// The saved views are process-wide; toJson() and fromJson() move them to and
// from the User settings.
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

QJsonArray BinViewLibrary::toJson() {
    QJsonArray out;
    for (const model::BinViewPreset& v : views()) {
        out.append(QJsonObject{{QStringLiteral("name"), v.name},
                               {QStringLiteral("columns"), QJsonArray::fromStringList(v.columns)}});
    }
    return out;
}

bool BinViewLibrary::fromJson(const QJsonValue& value) {
    QList<model::BinViewPreset> read;
    for (const QJsonValue& v : value.toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        QStringList columns;
        for (const QJsonValue& c : o.value(QStringLiteral("columns")).toArray()) columns << c.toString();
        read.append({name, columns});
    }
    const bool same = std::equal(read.begin(), read.end(), views().begin(), views().end(),
                                 [](const model::BinViewPreset& a, const model::BinViewPreset& b) {
                                     return a.name == b.name && a.columns == b.columns;
                                 });
    if (same) return false;
    views() = read;
    return true;
}

QJsonValue BinViewLibrary::legacy(QSettings& settings) {
    QJsonArray out;
    const int n = settings.beginReadArray(QStringLiteral("binViews"));
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        const QString name = settings.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        out.append(QJsonObject{{QStringLiteral("name"), name},
                               {QStringLiteral("columns"),
                                QJsonArray::fromStringList(settings.value(QStringLiteral("columns")).toStringList())}});
    }
    settings.endArray();
    return out.isEmpty() ? QJsonValue() : QJsonValue(out);
}

void BinViewLibrary::forgetLegacy(QSettings& settings) {
    settings.remove(QStringLiteral("binViews"));
}

} // namespace lumenis::ui

#include "lumenis/ui/clip_notes.h"

#include "lumenis/core/timecode.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/timeline.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidget>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

// The Timeline Clip Notes window (clip_notes.h; Editing Guide p.483-486). The notes live on the
// record sequence's segments (model::Segment::note); the window lists them and changes them through
// Timeline::setClipNote, so every change is an undo step and the Timeline redraws its Clip Text.

namespace lumenis::ui {

namespace {

constexpr int kSortRole = Qt::UserRole;
constexpr int kTrackRole = Qt::UserRole + 1;
constexpr int kStartRole = Qt::UserRole + 2;

// A cell that sorts by kSortRole when it has one (timecodes and durations by frame), else by text.
class SortItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        const QVariant a = data(kSortRole);
        const QVariant b = other.data(kSortRole);
        if (a.isValid() && b.isValid()) return a.toLongLong() < b.toLongLong();
        return QString::localeAwareCompare(text(), other.text()) < 0;
    }
};

} // namespace

QString ClipNotesWindow::columnName(int column) {
    switch (column) {
    case FrameColumn: return tr("Frame");
    case NameColumn: return tr("Clip Name");
    case StartColumn: return tr("Start");
    case TrackColumn: return tr("Track");
    case EndColumn: return tr("End");
    case DurationColumn: return tr("Duration");
    case NoteColumn: return tr("Timeline Clip Note");
    default: return {};
    }
}

// The table, then the bottom row: the Fast menu, the Gang button and Quick Find (the picture on
// p.484). Frame starts hidden; the guide adds it with Choose Columns (p.485).
ClipNotesWindow::ClipNotesWindow(Timeline& timeline, QWidget* parent) : QWidget(parent, Qt::Tool), timeline_(timeline) {
    setObjectName(QStringLiteral("clipNotesWindow"));
    setWindowTitle(tr("Timeline Clip Notes"));
    table_ = new QTableWidget(0, ColumnCount, this);
    table_->setObjectName(QStringLiteral("clipNotesTable"));
    QStringList headings;
    for (int c = 0; c < ColumnCount; ++c) headings << columnName(c);
    table_->setHorizontalHeaderLabels(headings);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    // "Double-click the note you want to edit in the Timeline Clip Note column, type new text and
    // press Enter" (p.484): only that column's cells are editable.
    table_->setEditTriggers(QAbstractItemView::DoubleClicked);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    // "Click the column heading and drag it to the desired location" (p.485); "Click the column
    // heading. To reverse the order, click the column heading again."
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->setSortingEnabled(true);
    table_->sortByColumn(StartColumn, Qt::AscendingOrder);
    table_->setColumnHidden(FrameColumn, true);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = contextMenu(table_->rowAt(pos.y()));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(table_->viewport()->mapToGlobal(pos));
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (filling_ || item->column() != NoteColumn) return;
        const auto ref = segmentAt(item->row());
        const QString text = item->text().trimmed();
        // The Timeline's change refills the table, so it waits until this signal is over.
        if (ref) QMetaObject::invokeMethod(this, [this, ref, text] { timeline_.setClipNote({*ref}, text); }, Qt::QueuedConnection);
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (!filling_ && gang_->isChecked()) gangSelection();
    });

    fast_ = new QToolButton(this);
    fast_->setObjectName(QStringLiteral("clipNotesFastMenu"));
    fast_->setText(QStringLiteral("≡"));
    fast_->setToolTip(tr("Timeline Clip Notes Fast Menu"));
    fast_->setPopupMode(QToolButton::InstantPopup);
    fast_->setMenu(new QMenu(fast_));
    connect(fast_->menu(), &QMenu::aboutToShow, this, [this] {
        QMenu* m = fast_->menu();
        m->clear();
        for (QMenu* sub : m->findChildren<QMenu*>(Qt::FindDirectChildrenOnly)) delete sub;
        std::unique_ptr<QMenu> items(fastMenu());
        for (QAction* a : items->actions()) {
            a->setParent(m);
            m->addAction(a);
        }
    });
    // "Click the Gang button at the bottom of the window. It will highlight green when in gang mode"
    // (p.485).
    gang_ = new QToolButton(this);
    gang_->setObjectName(QStringLiteral("clipNotesGang"));
    gang_->setText(tr("Gang"));
    gang_->setCheckable(true);
    gang_->setStyleSheet(QStringLiteral("QToolButton:checked { background-color: #2f8a3c; }"));
    connect(gang_, &QToolButton::toggled, this, [this](bool on) {
        if (on) gangSelection();
    });
    find_ = new QLineEdit(this);
    find_->setObjectName(QStringLiteral("clipNotesFind"));
    find_->setPlaceholderText(tr("Quick Find"));
    find_->setClearButtonEnabled(true);
    connect(find_, &QLineEdit::textChanged, this, &ClipNotesWindow::applyFind);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(fast_);
    bottom->addWidget(gang_);
    bottom->addWidget(find_, 1);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->addWidget(table_);
    v->addLayout(bottom);
    resize(640, 300);
    // Only a change of the record sequence refills the rows: the Composer also reports every move of
    // the position.
    connect(&timeline_.composer(), &Composer::changed, this, [this] {
        const model::MonitorState& state = timeline_.composer().state(MonitorId::Record);
        const model::LoadedClip* rec = state.current();
        const QString id = rec ? rec->item.id : QString();
        const QJsonObject composition = rec ? rec->item.composition : QJsonObject();
        if (id != shownId_ || composition != shownComposition_ || state.startFrame() != shownStart_) refresh();
    });
    refresh();
}

// "All the Timeline notes for the selected sequence appear in the window" (p.484): the record
// sequence's segments with a note, with their record timecodes; End is the last frame (the picture
// on p.484: Start + Duration - 1).
void ClipNotesWindow::refresh() {
    filling_ = true;
    QList<model::SegmentRef> selected;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows()) {
        if (const auto ref = segmentAt(i.row())) selected << *ref;
    }
    table_->setSortingEnabled(false);
    table_->clearContents();
    table_->setRowCount(0);
    const model::MonitorState& state = timeline_.composer().state(MonitorId::Record);
    const model::LoadedClip* rec = state.current();
    shownId_ = rec ? rec->item.id : QString();
    shownComposition_ = rec ? rec->item.composition : QJsonObject();
    shownStart_ = state.startFrame();
    if (rec && rec->item.kind == model::BinItemKind::Sequence) {
        const model::Sequence seq = model::Sequence::fromItem(rec->item);
        const TimecodeFormat format = TimecodeFormat::forRate(rec->item.editRate, false);
        auto tc = [&](std::int64_t frame) {
            return QString::fromStdString(Timecode(std::max<std::int64_t>(state.startFrame() + frame, 0), format).toString());
        };
        for (const model::Track& t : seq.tracks()) {
            std::int64_t at = 0;
            for (const model::Segment& s : t.segments) {
                const std::int64_t start = at;
                at += s.length;
                if (s.isFiller() || s.note.isEmpty()) continue;
                const int row = table_->rowCount();
                table_->insertRow(row);
                auto cell = [&](int column, const QString& text, std::optional<std::int64_t> sortKey = {}) {
                    auto* item = new SortItem(text);
                    if (sortKey) item->setData(kSortRole, static_cast<qlonglong>(*sortKey));
                    if (column != NoteColumn) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                    table_->setItem(row, column, item);
                    return item;
                };
                QTableWidgetItem* frame = cell(FrameColumn, QString(), start);
                frame->setData(kTrackRole, t.name());
                frame->setData(kStartRole, static_cast<qlonglong>(start));
                if (!table_->isColumnHidden(FrameColumn)) {
                    if (MediaFrames* frames = MediaFrames::instance()) {
                        const auto clip = timeline_.composer().resolve(s.binId, s.clipId);
                        const QSize size(frameHeight_ * 16 / 9, frameHeight_);
                        const QImage image = clip ? frames->thumbnail(*clip, s.sourceIn, size) : QImage();
                        if (!image.isNull()) frame->setData(Qt::DecorationRole, QPixmap::fromImage(image));
                    }
                }
                cell(NameColumn, s.name);
                cell(StartColumn, tc(start), start);
                cell(TrackColumn, t.name(), t.kind == model::TrackKind::Video ? -t.number : 1000 + t.number);
                cell(EndColumn, tc(start + s.length - 1), start + s.length - 1);
                cell(DurationColumn, durationFrames_ ? QString::number(s.length) : state.durationText(s.length), s.length);
                cell(NoteColumn, s.note);
            }
        }
    }
    table_->setIconSize(QSize(frameHeight_ * 16 / 9, frameHeight_));
    table_->verticalHeader()->setDefaultSectionSize(table_->isColumnHidden(FrameColumn) ? table_->fontMetrics().height() + 6
                                                                                       : frameHeight_ + 4);
    table_->setSortingEnabled(true);
    table_->clearSelection();
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (const auto ref = segmentAt(row); ref && selected.contains(*ref)) {
            table_->selectionModel()->select(table_->model()->index(row, 0),
                                             QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    filling_ = false;
    applyFind();
}

std::optional<model::SegmentRef> ClipNotesWindow::segmentAt(int row) const {
    const QTableWidgetItem* item = table_->item(row, FrameColumn);
    if (!item) return std::nullopt;
    return model::SegmentRef{item->data(kTrackRole).toString(), item->data(kStartRole).toLongLong()};
}

// "Right-click the note and select Delete. Or, click to highlight the note and press Delete" (p.484).
void ClipNotesWindow::deleteSelected() {
    QList<model::SegmentRef> refs;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows()) {
        if (const auto ref = segmentAt(i.row())) refs << *ref;
    }
    if (!refs.isEmpty()) timeline_.setClipNote(refs, QString());
}

void ClipNotesWindow::setColumnShown(int column, bool shown) {
    if (column < 0 || column >= ColumnCount) return;
    table_->setColumnHidden(column, !shown);
    if (column == FrameColumn) refresh(); // the pictures are made only while the column shows
}

bool ClipNotesWindow::isColumnShown(int column) const {
    return column >= 0 && column < ColumnCount && !table_->isColumnHidden(column);
}

void ClipNotesWindow::setDurationAsFrames(bool frames) {
    durationFrames_ = frames;
    refresh();
}

void ClipNotesWindow::resizeFrames(int steps) {
    frameHeight_ = std::clamp(steps > 0 ? frameHeight_ * 2 : frameHeight_ / 2, 18, 144);
    refresh();
}

// "The tab delimited .txt file is saved to the set location" (p.485). Ours: a heading row first.
bool ClipNotesWindow::exportText(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&file);
    QList<int> columns;
    const QHeaderView* header = table_->horizontalHeader();
    for (int visual = 0; visual < ColumnCount; ++visual) {
        const int c = header->logicalIndex(visual);
        if (c != FrameColumn && !table_->isColumnHidden(c)) columns << c;
    }
    QStringList line;
    for (int c : columns) line << columnName(c);
    out << line.join(QLatin1Char('\t')) << '\n';
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->isRowHidden(row)) continue;
        line.clear();
        for (int c : columns) {
            const QTableWidgetItem* item = table_->item(row, c);
            line << (item ? item->text().simplified() : QString());
        }
        out << line.join(QLatin1Char('\t')) << '\n';
    }
    return out.status() == QTextStream::Ok;
}

// "The window displays any clips that include the text entered in the find field in any column"
// (p.486).
void ClipNotesWindow::applyFind() {
    const QString text = find_ ? find_->text().trimmed() : QString();
    for (int row = 0; row < table_->rowCount(); ++row) {
        bool match = text.isEmpty();
        for (int c = 0; c < ColumnCount && !match; ++c) {
            const QTableWidgetItem* item = table_->item(row, c);
            match = item && item->text().contains(text, Qt::CaseInsensitive);
        }
        table_->setRowHidden(row, !match);
    }
}

// "The corresponding clips are highlighted and selected in the Timeline ... If one clip note is
// selected, the blue bar will move to the beginning of that clip" (p.485).
void ClipNotesWindow::gangSelection() {
    QList<model::SegmentRef> refs;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows()) {
        if (const auto ref = segmentAt(i.row())) refs << *ref;
    }
    if (refs.isEmpty()) return;
    timeline_.setSelection(refs);
    if (refs.size() == 1) {
        Composer& composer = timeline_.composer();
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, refs.first().start);
    }
}

void ClipNotesWindow::chooseColumns() {
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("clipNotesColumns"));
    dialog.setWindowTitle(tr("Choose Columns"));
    auto* v = new QVBoxLayout(&dialog);
    QList<QCheckBox*> boxes;
    for (int c = 0; c < ColumnCount; ++c) {
        auto* box = new QCheckBox(columnName(c), &dialog);
        box->setChecked(isColumnShown(c));
        v->addWidget(box);
        boxes << box;
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    v->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    for (int c = 0; c < ColumnCount; ++c) setColumnShown(c, boxes.at(c)->isChecked());
}

// "Select the Font and enter the Font size you want to appear in the Timeline Clip Notes window"
// (p.486).
void ClipNotesWindow::setFont() {
    bool ok = false;
    const QFont font = QFontDialog::getFont(&ok, table_->font(), this, tr("Set Font"));
    if (!ok) return;
    table_->setFont(font);
    refresh();
}

QMenu* ClipNotesWindow::fastMenu() {
    auto* menu = new QMenu(this);
    connect(menu->addAction(tr("Choose Columns...")), &QAction::triggered, this, &ClipNotesWindow::chooseColumns);
    QAction* frames = menu->addAction(tr("Show as Frames"));
    frames->setCheckable(true);
    frames->setChecked(durationFrames_);
    connect(frames, &QAction::triggered, this, [this] { setDurationAsFrames(true); });
    QAction* timecode = menu->addAction(tr("Show as Timecode"));
    timecode->setCheckable(true);
    timecode->setChecked(!durationFrames_);
    connect(timecode, &QAction::triggered, this, [this] { setDurationAsFrames(false); });
    menu->addSeparator();
    QAction* enlarge = menu->addAction(tr("Enlarge Frame"));
    enlarge->setEnabled(isColumnShown(FrameColumn));
    connect(enlarge, &QAction::triggered, this, [this] { resizeFrames(1); });
    QAction* reduce = menu->addAction(tr("Reduce Frame"));
    reduce->setEnabled(isColumnShown(FrameColumn));
    connect(reduce, &QAction::triggered, this, [this] { resizeFrames(-1); });
    menu->addSeparator();
    connect(menu->addAction(tr("Export to Text...")), &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export to Text"), QString(), tr("Text (*.txt)"));
        if (!path.isEmpty()) exportText(path.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive) ? path : path + QStringLiteral(".txt"));
    });
    connect(menu->addAction(tr("Set Font...")), &QAction::triggered, this, &ClipNotesWindow::setFont);
    return menu;
}

QMenu* ClipNotesWindow::contextMenu(int row) {
    QMenu* menu = fastMenu();
    QAction* first = menu->actions().value(0);
    QAction* del = new QAction(tr("Delete"), menu);
    del->setEnabled(row >= 0 && row < table_->rowCount());
    connect(del, &QAction::triggered, this, [this, row] {
        if (!table_->selectionModel()->isRowSelected(row, {})) table_->selectRow(row);
        deleteSelected();
    });
    menu->insertAction(first, del);
    menu->insertSeparator(first);
    return menu;
}

void ClipNotesWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui

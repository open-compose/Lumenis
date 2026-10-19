#include "lumenis/ui/marker_dialogs.h"

#include "lumenis/core/timecode.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/timeline.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QVBoxLayout>

// Markers (Editing Guide p.412-424): the Edit Marker dialog and the Markers window, declared in
// marker_dialogs.h. Markers are stored in the bin item (src/model/marker.*); the Timeline (timeline.cpp)
// adds, changes and deletes them, and opens MarkerDialog from its marker commands. MainWindow opens the
// Markers window from Tools > Markers.

namespace lumenis::ui {

namespace {

// A small oval in the marker colour `color`, for the colour menus and the table's first column.
QIcon swatch(const QString& color) {
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor::fromRgb(model::markerColor(color)));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(1, 3, 12, 8)); // markers are ovals (p.413)
    return QIcon(pm);
}

} // namespace

QStringList trackChoices(const model::BinItem& item) {
    if (item.kind == model::BinItemKind::Sequence) {
        QStringList tracks = model::Sequence::fromItem(item).trackNames();
        tracks << QStringLiteral("TC1");
        return tracks;
    }
    return model::sourceTracksOf(item);
}

MarkerDialog::MarkerDialog(const model::Marker& marker, const QStringList& tracks, QWidget* parent)
    : QDialog(parent), marker_(marker) {
    setObjectName(QStringLiteral("editMarkerDialog"));
    setWindowTitle(tr("Edit Marker"));
    name_ = new QLineEdit(marker.name, this);
    name_->setObjectName(QStringLiteral("markerName"));
    user_ = new QLineEdit(marker.user, this);
    user_->setObjectName(QStringLiteral("markerUser"));
    color_ = new QComboBox(this);
    color_->setObjectName(QStringLiteral("markerColor"));
    for (const model::MarkerColor& c : model::markerColors()) color_->addItem(swatch(c.name), c.name);
    color_->setCurrentText(marker.color);
    track_ = new QComboBox(this);
    track_->setObjectName(QStringLiteral("markerTrack"));
    track_->addItems(tracks);
    if (!tracks.contains(marker.track)) track_->addItem(marker.track);
    track_->setCurrentText(marker.track);
    comment_ = new QPlainTextEdit(marker.comment, this);
    comment_->setObjectName(QStringLiteral("markerComment"));
    auto* frame = new QLabel(marker.isSpanned() ? tr("%1 to %2").arg(marker.frame).arg(marker.end() - 1)
                                                : QString::number(marker.frame),
                             this);

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("User:"), user_);
    form->addRow(tr("Color:"), color_);
    form->addRow(tr("Frame:"), frame);
    form->addRow(tr("Track:"), track_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* v = new QVBoxLayout(this);
    v->addLayout(form);
    v->addWidget(comment_, 1);
    v->addWidget(buttons);
    resize(380, 320);
}

model::Marker MarkerDialog::marker() const {
    model::Marker m = marker_;
    m.name = name_->text();
    m.user = user_->text();
    m.color = color_->currentText();
    m.track = track_->currentText();
    m.comment = comment_->toPlainText();
    return m;
}

// A table of the markers, one row each, with double-click to jump and a right-click menu.
MarkersWindow::MarkersWindow(Timeline& timeline, QWidget* parent) : QWidget(parent, Qt::Tool), timeline_(timeline) {
    setObjectName(QStringLiteral("markersWindow"));
    setWindowTitle(tr("Markers"));
    table_ = new QTableWidget(0, 6, this);
    table_->setObjectName(QStringLiteral("markersTable"));
    table_->setHorizontalHeaderLabels({QString(), tr("Name"), tr("User"), tr("TC"), tr("Track"), tr("Comment")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(ColorColumn, 28);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setSortingEnabled(false);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { jumpTo(row); });
    connect(table_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = contextMenu(table_->rowAt(pos.y()));
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(table_->viewport()->mapToGlobal(pos));
    });
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->addWidget(table_);
    resize(520, 260);
    connect(&timeline_.composer(), &Composer::changed, this, &MarkersWindow::refresh);
    refresh();
}

QList<model::Marker> MarkersWindow::markers() const {
    const model::LoadedClip* loaded = timeline_.composer().state(monitor_).current();
    return loaded ? model::markersOf(loaded->item) : QList<model::Marker>();
}

void MarkersWindow::refresh() {
    // The window shows the active monitor's markers (p.420).
    monitor_ = timeline_.composer().activeMonitor();
    const model::MonitorState& s = timeline_.composer().state(monitor_);
    const QList<model::Marker> list = markers();
    table_->setRowCount(static_cast<int>(list.size()));
    const TimecodeFormat format =
        TimecodeFormat::forRate(s.isEmpty() ? Rational(24000, 1001) : s.current()->item.editRate, false);
    // Marker frames count from the clip's start; the TC column adds the clip's start timecode.
    for (int row = 0; row < list.size(); ++row) {
        const model::Marker& m = list.at(row);
        auto* color = new QTableWidgetItem(swatch(m.color), QString());
        color->setToolTip(m.color);
        table_->setItem(row, ColorColumn, color);
        table_->setItem(row, NameColumn, new QTableWidgetItem(m.name));
        table_->setItem(row, UserColumn, new QTableWidgetItem(m.user));
        table_->setItem(row, TimecodeColumn, new QTableWidgetItem(QString::fromStdString(
                                                 Timecode(s.startFrame() + m.frame, format).toString())));
        table_->setItem(row, TrackColumn, new QTableWidgetItem(m.track));
        table_->setItem(row, CommentColumn, new QTableWidgetItem(m.comment));
    }
}

void MarkersWindow::jumpTo(int row) {
    const QList<model::Marker> list = markers();
    if (row < 0 || row >= list.size()) return;
    timeline_.composer().setActiveMonitor(monitor_);
    timeline_.composer().moveTo(monitor_, list.at(row).frame);
}

// The ids are gathered first: each deletion changes the Composer, which refreshes the table.
void MarkersWindow::deleteSelected() {
    const QList<model::Marker> list = markers();
    QStringList ids;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows()) {
        if (i.row() < list.size()) ids << list.at(i.row()).id;
    }
    for (const QString& id : ids) timeline_.deleteMarker(monitor_, id);
}

// Jump, Edit, Change Color, Change Track and Delete for one row. Each action reads the markers again
// when it runs, since the list may have changed since the menu opened.
QMenu* MarkersWindow::contextMenu(int row) {
    auto* menu = new QMenu(this);
    const QList<model::Marker> list = markers();
    const bool valid = row >= 0 && row < list.size();
    QAction* jump = menu->addAction(tr("Jump to Marker"));
    jump->setEnabled(valid);
    connect(jump, &QAction::triggered, this, [this, row] { jumpTo(row); });
    QAction* edit = menu->addAction(tr("Edit Marker"));
    edit->setEnabled(valid);
    connect(edit, &QAction::triggered, this, [this, row] {
        const QList<model::Marker> l = markers();
        const model::LoadedClip* loaded = timeline_.composer().state(monitor_).current();
        if (row >= l.size() || !loaded) return;
        MarkerDialog dialog(l.at(row), trackChoices(loaded->item), this);
        if (dialog.exec() == QDialog::Accepted) timeline_.changeMarker(monitor_, dialog.marker());
    });
    QMenu* colors = menu->addMenu(tr("Change Color"));
    colors->setEnabled(valid);
    for (const model::MarkerColor& c : model::markerColors()) {
        connect(colors->addAction(swatch(c.name), c.name), &QAction::triggered, this, [this, row, name = c.name] {
            QList<model::Marker> l = markers();
            if (row >= l.size()) return;
            l[row].color = name;
            timeline_.changeMarker(monitor_, l.at(row));
        });
    }
    QMenu* tracks = menu->addMenu(tr("Change Track"));
    tracks->setEnabled(valid);
    if (const model::LoadedClip* loaded = timeline_.composer().state(monitor_).current()) {
        for (const QString& t : trackChoices(loaded->item)) {
            connect(tracks->addAction(t), &QAction::triggered, this, [this, row, t] {
                QList<model::Marker> l = markers();
                if (row >= l.size()) return;
                l[row].track = t;
                timeline_.changeMarker(monitor_, l.at(row));
            });
        }
    }
    menu->addSeparator();
    QAction* del = menu->addAction(tr("Delete Marker"));
    del->setEnabled(valid);
    connect(del, &QAction::triggered, this, [this, row] {
        const QList<model::Marker> l = markers();
        if (row < l.size()) timeline_.deleteMarker(monitor_, l.at(row).id);
    });
    return menu;
}

// Delete or Backspace deletes the selected markers.
void MarkersWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui

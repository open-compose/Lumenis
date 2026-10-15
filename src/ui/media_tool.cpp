#include "lumenis/ui/media_tool.h"

#include "lumenis/core/timecode.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lumenis::ui {

// ---------------------------------------------------------------- MediaToolDisplayDialog

MediaToolDisplayDialog::MediaToolDisplayDialog(const QString& currentProject, const QStringList& drives,
                                               const MediaToolDisplay& display, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("mediaToolDisplay"));
    setWindowTitle(tr("Media Tool Display"));

    drives_ = new QListWidget(this);
    drives_->setObjectName(QStringLiteral("mediaDrives"));
    drives_->setSelectionMode(QAbstractItemView::MultiSelection);
    drives_->addItems(drives);
    for (int i = 0; i < drives_->count(); ++i) {
        drives_->item(i)->setSelected(display.drives.isEmpty() || display.drives.contains(drives_->item(i)->text()));
    }
    auto* allDrives = new QPushButton(tr("All Drives"), this);
    connect(allDrives, &QPushButton::clicked, drives_, &QListWidget::selectAll);

    // Projects with media online, and the current project (p.346). Other
    // projects' media come with Phase 2.
    projects_ = new QListWidget(this);
    projects_->setObjectName(QStringLiteral("mediaProjects"));
    projects_->setSelectionMode(QAbstractItemView::MultiSelection);
    projects_->addItem(currentProject);
    projects_->item(0)->setSelected(true);
    auto* current = new QPushButton(tr("Current Project"), this);
    connect(current, &QPushButton::clicked, this, [this] {
        projects_->clearSelection();
        projects_->item(0)->setSelected(true);
    });
    auto* all = new QPushButton(tr("All Projects"), this);
    connect(all, &QPushButton::clicked, projects_, &QListWidget::selectAll);

    auto box = [this](const QString& text, const QString& name, bool on) {
        auto* c = new QCheckBox(text, this);
        c->setObjectName(name);
        c->setChecked(on);
        return c;
    };
    master_ = box(tr("Master Clips"), QStringLiteral("masterClips"), display.masterClips);
    linked_ = box(tr("Linked Master Clips"), QStringLiteral("linkedMasterClips"), display.linkedMasterClips);
    rendered_ = box(tr("Precompute Clips - Rendered Effects"), QStringLiteral("renderedEffects"), display.renderedEffects);
    titles_ = box(tr("Precompute Clips - Titles and Matte Keys"), QStringLiteral("titlesAndMattes"),
                  display.titlesAndMattes);
    media_ = box(tr("Media Files"), QStringLiteral("mediaFiles"), display.mediaFiles);

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Media Drive(s)"), this), 0, 0);
    grid->addWidget(new QLabel(tr("Project(s)"), this), 0, 1);
    grid->addWidget(drives_, 1, 0);
    grid->addWidget(projects_, 1, 1);
    grid->addWidget(allDrives, 2, 0);
    auto* projectButtons = new QHBoxLayout;
    projectButtons->addWidget(current);
    projectButtons->addWidget(all);
    grid->addLayout(projectButtons, 2, 1);
    auto* checks = new QVBoxLayout;
    for (QCheckBox* c : {master_, linked_, rendered_, titles_, media_}) checks->addWidget(c);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addLayout(checks);
    layout->addWidget(buttons);
}

MediaToolDisplay MediaToolDisplayDialog::display() const {
    MediaToolDisplay d;
    if (drives_->selectedItems().size() != drives_->count()) {
        for (const QListWidgetItem* i : drives_->selectedItems()) d.drives << i->text();
    }
    d.allProjects = projects_->selectedItems().size() > 1;
    d.masterClips = master_->isChecked();
    d.linkedMasterClips = linked_->isChecked();
    d.renderedEffects = rendered_->isChecked();
    d.titlesAndMattes = titles_->isChecked();
    d.mediaFiles = media_->isChecked();
    return d;
}

QCheckBox* MediaToolDisplayDialog::option(const QString& objectName) const {
    return findChild<QCheckBox*>(objectName);
}

// ---------------------------------------------------------------- MediaTool

MediaTool::MediaTool(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("mediaTool"));
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("mediaToolTable"));
    table_->setColumnCount(static_cast<int>(columns().size()));
    table_->setHorizontalHeaderLabels(columns());
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSortingEnabled(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(table_);
}

QStringList MediaTool::columns() {
    // The picture on p.347.
    return {tr("Color"), tr("Name"), tr("Duration"), tr("Drive"), tr("IN-OUT"), tr("Creation Date")};
}

void MediaTool::setItems(const QList<model::BinItem>& items, const MediaToolDisplay& display) {
    display_ = display;
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    for (const model::BinItem& item : items) {
        // Master clips (p.345); no sequences or subclips. Linked master
        // clips, precomputes and media files come with media (Phase 2).
        const bool master = item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip;
        if (!master || !display.masterClips) continue;
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* colour = new QTableWidgetItem;
        if (!item.color.isEmpty()) colour->setBackground(QColor(item.color));
        table_->setItem(row, 0, colour);
        table_->setItem(row, 1, new QTableWidgetItem(item.name));
        const TimecodeFormat format = TimecodeFormat::forRate(item.editRate, false);
        table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(
                                    Timecode(std::max<std::int64_t>(item.durationFrames, 0), format).toString())));
        table_->setItem(row, 3, new QTableWidgetItem(item.fields.value(QStringLiteral("Drive"))));
        table_->setItem(row, 4, new QTableWidgetItem(item.fields.value(QStringLiteral("IN-OUT"))));
        table_->setItem(row, 5, new QTableWidgetItem(item.created.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    }
    table_->setSortingEnabled(true);
}

QStringList MediaTool::names() const {
    QStringList out;
    for (int r = 0; r < table_->rowCount(); ++r) out << table_->item(r, 1)->text();
    return out;
}

bool MediaTool::deleteSelected() {
    // Deleting opens the Delete Media dialog for the selected media
    // (p.347-348); there are no media files yet.
    message_ = tr("There are no media files to delete yet.");
    return false;
}

void MediaTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui

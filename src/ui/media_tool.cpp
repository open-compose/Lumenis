#include "lumenis/ui/media_tool.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/link.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QStorageInfo>
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

#include <algorithm>

namespace lumenis::ui {

// ---------------------------------------------------------------- MediaToolDisplayDialog

MediaToolDisplayDialog::MediaToolDisplayDialog(const QString& currentProject,
                                               const QList<QPair<QString, QString>>& drives,
                                               const QStringList& projects, const MediaToolDisplay& display,
                                               QWidget* parent)
    : QDialog(parent), current_(currentProject) {
    setObjectName(QStringLiteral("mediaToolDisplay"));
    setWindowTitle(tr("Media Tool Display"));

    drives_ = new QListWidget(this);
    drives_->setObjectName(QStringLiteral("mediaDrives"));
    drives_->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto& [name, root] : drives) {
        auto* item = new QListWidgetItem(name, drives_);
        item->setData(Qt::UserRole, root);
        item->setSelected(display.drives.isEmpty() || display.drives.contains(root));
    }
    auto* allDrives = new QPushButton(tr("All Drives"), this);
    connect(allDrives, &QPushButton::clicked, drives_, &QListWidget::selectAll);

    // Projects with media online, and the current project (p.346).
    projects_ = new QListWidget(this);
    projects_->setObjectName(QStringLiteral("mediaProjects"));
    projects_->setSelectionMode(QAbstractItemView::MultiSelection);
    projects_->addItem(currentProject);
    for (const QString& p : projects) {
        if (p != currentProject) projects_->addItem(p);
    }
    projects_->item(0)->setSelected(true);
    if (display.allProjects) projects_->selectAll();
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
        for (const QListWidgetItem* i : drives_->selectedItems()) d.drives << i->data(Qt::UserRole).toString();
    }
    d.project = current_;
    d.allProjects = projects_->count() > 1 && projects_->selectedItems().size() == projects_->count();
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
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
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

namespace {

constexpr int kFilesRole = Qt::UserRole + 1; // "track\npath" for each file

QString driveName(const QString& path) {
    const QStorageInfo volume(path);
    if (!volume.isValid()) return {};
    return volume.displayName().isEmpty() ? volume.rootPath() : volume.displayName();
}

bool onDrives(const QString& path, const QStringList& drives) {
    if (drives.isEmpty()) return true;
    const QString file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    return std::any_of(drives.begin(), drives.end(), [&](const QString& d) {
        return file.startsWith(QDir::cleanPath(QFileInfo(media::mediaFilesFolder(d)).absoluteFilePath()) +
                               QLatin1Char('/'));
    });
}

} // namespace

void MediaTool::setItems(const QList<model::BinItem>& items, const MediaToolDisplay& display,
                         const QList<media::MediaFileInfo>& files) {
    display_ = display;
    table_->setSortingEnabled(false);
    table_->setRowCount(0);
    auto addRow = [&](const QString& colour, const QString& name, const QString& duration, const QString& drive,
                      const QString& inOut, const QDateTime& created, const QStringList& media) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* swatch = new QTableWidgetItem;
        if (!colour.isEmpty()) swatch->setBackground(QColor(colour));
        table_->setItem(row, 0, swatch);
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(kFilesRole, media);
        table_->setItem(row, 1, nameItem);
        table_->setItem(row, 2, new QTableWidgetItem(duration));
        table_->setItem(row, 3, new QTableWidgetItem(drive));
        table_->setItem(row, 4, new QTableWidgetItem(inOut));
        table_->setItem(row, 5, new QTableWidgetItem(created.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    };
    auto durationText = [](std::int64_t frames, const Rational& rate) {
        return QString::fromStdString(
            Timecode(std::max<std::int64_t>(frames, 0), TimecodeFormat::forRate(rate, false)).toString());
    };
    for (const model::BinItem& item : items) {
        // Master clips (p.345); no sequences or subclips.
        const bool master = item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip;
        if (!master) continue;
        const auto link = media::linkOf(item);
        const bool linked = link && !link->isManaged();
        if (linked ? !display.linkedMasterClips : !display.masterClips) continue;
        // A managed clip's files are its media; a linked clip's are not ours
        // to delete.
        QStringList media;
        if (link && link->isManaged()) {
            if (!display.drives.isEmpty() && !onDrives(link->path, display.drives)) continue;
            if (link->videoStream >= 0) media << QStringLiteral("V1\n") + link->path;
            for (int t = 0; t < link->audioPaths.size(); ++t) {
                media << QStringLiteral("A%1\n").arg(t + 1) + link->audioPaths.at(t);
            }
        }
        addRow(item.color, item.name, durationText(item.durationFrames, item.editRate),
               item.fields.value(QStringLiteral("Drive")), item.fields.value(QStringLiteral("IN-OUT")), item.created,
               media);
    }
    if (display.mediaFiles) {
        for (const media::MediaFileInfo& f : files) {
            if (!onDrives(f.path, display.drives)) continue;
            if (!display.allProjects && !f.project.isEmpty() && f.project != display.project) continue;
            addRow(QString(), QStringLiteral("%1 %2").arg(f.clipName, f.track), durationText(f.frames, f.rate),
                   driveName(f.path), QString(), f.created, {f.track + QLatin1Char('\n') + f.path});
        }
    }
    table_->setSortingEnabled(true);
}

QStringList MediaTool::names() const {
    QStringList out;
    for (int r = 0; r < table_->rowCount(); ++r) out << table_->item(r, 1)->text();
    return out;
}

void MediaTool::selectNames(const QStringList& names) {
    table_->clearSelection();
    table_->setSelectionMode(QAbstractItemView::MultiSelection);
    for (int r = 0; r < table_->rowCount(); ++r) {
        if (names.contains(table_->item(r, 1)->text())) table_->selectRow(r);
    }
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
}

bool MediaTool::deleteSelected() {
    // The selected rows' media files, by track (p.347).
    QHash<QString, QStringList> byTrack;
    QStringList tracks;
    for (const QModelIndex& index : table_->selectionModel()->selectedRows(1)) {
        for (const QString& entry : index.data(kFilesRole).toStringList()) {
            const QString track = entry.section(QLatin1Char('\n'), 0, 0);
            const QString path = entry.section(QLatin1Char('\n'), 1);
            if (!byTrack[track].contains(path)) byTrack[track] << path;
            if (!tracks.contains(track)) tracks << track;
        }
    }
    if (tracks.isEmpty()) {
        message_ = tr("The selection has no media files to delete.");
        return false;
    }
    std::sort(tracks.begin(), tracks.end(), [](const QString& a, const QString& b) {
        // V1 first, then A1, A2, ... by number.
        if (a.at(0) != b.at(0)) return a.at(0) == QLatin1Char('V');
        return a.mid(1).toInt() < b.mid(1).toInt();
    });
    DeleteMediaDialog dialog(tracks, this);
    const bool ok = deleteMediaHook ? deleteMediaHook(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return false;
    QStringList paths;
    for (const QString& track : dialog.chosenTracks()) paths += byTrack.value(track);
    if (paths.isEmpty()) return false;
    // "A dialog box opens, asking you to confirm the deletion" (p.348).
    bool confirmed = false;
    if (confirmHook) {
        confirmed = confirmHook(static_cast<int>(paths.size()));
    } else {
        QMessageBox box(QMessageBox::Warning, tr("Delete Media"),
                        tr("Delete %n media file(s)? Clips that use them will show Media Offline.", "",
                           static_cast<int>(paths.size())),
                        QMessageBox::Cancel, this);
        QPushButton* del = box.addButton(tr("Delete"), QMessageBox::DestructiveRole);
        box.exec();
        confirmed = box.clickedButton() == del;
    }
    if (!confirmed || !deleter) return false;
    const QStringList deleted = deleter(paths);
    message_ = tr("%n media file(s) deleted.", "", static_cast<int>(deleted.size()));
    if (!deleted.isEmpty()) emit mediaDeleted(deleted);
    return !deleted.isEmpty();
}

// ---------------------------------------------------------------- DeleteMediaDialog

DeleteMediaDialog::DeleteMediaDialog(const QStringList& tracks, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("deleteMedia"));
    setWindowTitle(tr("Delete Media"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Delete these media files:"), this));
    // The options of p.348, a box for each track the selection has.
    for (const QString& t : tracks) {
        const QString text = t == QLatin1String("V1") ? tr("Video media file (V)") : tr("Audio media file (%1)").arg(t);
        auto* box = new QCheckBox(text, this);
        box->setObjectName(t);
        box->setChecked(true);
        tracks_ << box;
        layout->addWidget(box);
    }
    for (const QString& later : {tr("Precompute media file (V, A)"), tr("Audio mixdown file (A)"),
                                 tr("Linked Delete metadata (files)")}) {
        auto* box = new QCheckBox(later, this);
        box->setEnabled(false);
        box->setToolTip(tr("Not available yet"));
        layout->addWidget(box);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList DeleteMediaDialog::chosenTracks() const {
    QStringList out;
    for (const QCheckBox* box : tracks_) {
        if (box->isChecked()) out << box->objectName();
    }
    return out;
}

QCheckBox* DeleteMediaDialog::track(const QString& name) const {
    for (QCheckBox* box : tracks_) {
        if (box->objectName() == name) return box;
    }
    return nullptr;
}

void MediaTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace lumenis::ui

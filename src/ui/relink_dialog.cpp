#include "lumenis/ui/relink_dialog.h"

#include "lumenis/media/link.h"
#include "lumenis/ui/media_frames.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

// The two Relink dialogs (relink_dialog.h): Linked Media, for clips linked to files in place
// (src/media/link.*), and the Relink dialog for managed media (src/media/relink.*). MainWindow
// (main_window.cpp) opens both from Clip > Relink and writes the results into the bins.

namespace lumenis::ui {

namespace {

// The Linked Media list's columns, which are also bin column names for model::cellText.
// The picture on p.365.
const QStringList& headings() {
    static const QStringList h{QStringLiteral("Color"),       QStringLiteral("Name"),     QStringLiteral("Source File"),
                               QStringLiteral("Source Path"), QStringLiteral("Format"),   QStringLiteral("Duration"),
                               QStringLiteral("Tracks"),      QStringLiteral("Creation Date")};
    return h;
}

// A row's index into clips_, in its first column.
constexpr int kClipRole = Qt::UserRole + 1;

} // namespace

// The filter and search above the list, Locate Media and the counts below.
LinkedMediaDialog::LinkedMediaDialog(const QList<Clip>& clips, QWidget* parent) : QDialog(parent), clips_(clips) {
    setObjectName(QStringLiteral("linkedMediaDialog"));
    setWindowTitle(tr("Linked Media"));
    resize(900, 420);
    auto* v = new QVBoxLayout(this);

    // Top: the filter on the left, the search on the right (p.365).
    auto* top = new QHBoxLayout;
    filter_ = new QComboBox(this);
    filter_->setObjectName(QStringLiteral("linkedMediaFilter"));
    filter_->addItem(tr("Offline only"));
    filter_->addItem(tr("All media"));
    connect(filter_, &QComboBox::currentIndexChanged, this, &LinkedMediaDialog::refresh);
    top->addWidget(filter_);
    top->addStretch();
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("linkedMediaSearch"));
    search_->setPlaceholderText(tr("Search"));
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(220);
    connect(search_, &QLineEdit::textChanged, this, &LinkedMediaDialog::refresh);
    top->addWidget(search_);
    v->addLayout(top);

    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("linkedMediaList"));
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setHeaderLabels(headings());
    list_->header()->resizeSection(1, 180);
    list_->header()->resizeSection(3, 240);
    v->addWidget(list_, 1);

    // Bottom: Locate Media and how many are shown (p.365).
    auto* bottom = new QHBoxLayout;
    result_ = new QLabel(this);
    result_->setObjectName(QStringLiteral("linkedMediaResult"));
    bottom->addWidget(result_, 1);
    locate_ = new QPushButton(tr("Locate Media"), this);
    locate_->setObjectName(QStringLiteral("locateMedia"));
    connect(locate_, &QPushButton::clicked, this, [this] {
        const QString folder =
            chooseFolder ? chooseFolder() : QFileDialog::getExistingDirectory(this, tr("Locate Media"));
        if (!folder.isEmpty()) locateMedia(folder);
    });
    bottom->addWidget(locate_);
    v->addLayout(bottom);
    viewing_ = new QLabel(this);
    viewing_->setObjectName(QStringLiteral("linkedMediaViewing"));
    viewing_->setAlignment(Qt::AlignRight);
    v->addWidget(viewing_);
    refresh();
}

bool LinkedMediaDialog::isOffline(const model::BinItem& item) const {
    if (media::isUnlinked(item)) return true;
    const auto link = media::linkOf(item);
    return !link || !QFileInfo::exists(link->file());
}

void LinkedMediaDialog::refresh() {
    const bool offlineOnly = filter_->currentIndex() == 0;
    const QString text = search_->text();
    list_->clear();
    int shown = 0;
    for (int i = 0; i < clips_.size(); ++i) {
        const model::BinItem& item = clips_.at(i).item;
        const bool offline = isOffline(item);
        if (offlineOnly && !offline) continue;
        QStringList cells;
        for (const QString& h : headings()) cells << model::cellText(item, h);
        // The search filters by keywords in any column (p.365).
        if (!text.isEmpty() && !cells.join(QLatin1Char(' ')).contains(text, Qt::CaseInsensitive)) continue;
        auto* row = new QTreeWidgetItem(list_, cells);
        row->setData(0, kClipRole, i);
        // Unlinked clips are shown in red (p.365).
        if (offline) {
            for (int c = 0; c < headings().size(); ++c) row->setForeground(c, QColor(0xe0, 0x50, 0x50));
        }
        ++shown;
    }
    viewing_->setText(tr("Viewing: %1/%2").arg(shown).arg(clips_.size()));
}

void LinkedMediaDialog::setOfflineOnly(bool offlineOnly) {
    filter_->setCurrentIndex(offlineOnly ? 0 : 1);
}

void LinkedMediaDialog::setSearch(const QString& text) {
    search_->setText(text);
}

QStringList LinkedMediaDialog::shownNames() const {
    QStringList names;
    for (int r = 0; r < list_->topLevelItemCount(); ++r) names << list_->topLevelItem(r)->text(1);
    return names;
}

// Indexes the folder's files once, then matches each clip against the index; every clip ends up
// either relinked() or notRelinked().
int LinkedMediaDialog::locateMedia(const QString& folder) {
    // The selected clips, or with none selected every clip shown (p.365:
    // "select specific clips to relink or proceed directly").
    QList<QTreeWidgetItem*> rows = list_->selectedItems();
    if (rows.isEmpty()) {
        for (int r = 0; r < list_->topLevelItemCount(); ++r) rows << list_->topLevelItem(r);
    }
    const QHash<QString, QString> index = media::indexFolder(folder);
    int done = 0;
    int missed = 0;
    for (QTreeWidgetItem* row : rows) {
        Clip& clip = clips_[row->data(0, kClipRole).toInt()];
        const QString found = media::locateMedia(clip.item, index);
        QString error;
        if (found.isEmpty()) {
            error = tr("not found in %1").arg(QDir::toNativeSeparators(folder));
        } else if (media::relinkClip(clip.item, found, &error)) {
            ++done;
            emit relinked(clip.binId, clip.item);
            continue;
        }
        ++missed;
        emit notRelinked(clip.item.name, error);
    }
    result_->setText(tr("%1 relinked, %2 not found").arg(done).arg(missed));
    // Files that came back are read again.
    if (done > 0 && MediaFrames::instance()) MediaFrames::instance()->clear();
    refresh();
    return done;
}

QString LinkedMediaDialog::summary() const {
    return result_->text();
}

// ---------------------------------------------------------------- ManagedRelinkDialog

namespace {

// Controls not implemented yet are shown disabled, with this tooltip.
QString laterTip() {
    return QObject::tr("Not available yet");
}

// A check box named `name` (for check()); `later` disables it as not available yet.
QCheckBox* checkBox(QWidget* parent, const QString& text, const QString& name, bool on, bool later = false) {
    auto* c = new QCheckBox(text, parent);
    c->setObjectName(name);
    c->setChecked(on);
    if (later) {
        c->setEnabled(false);
        c->setToolTip(laterTip());
    }
    return c;
}

// A menu whose entries after the first `working` wait for later.
QComboBox* menuOf(QWidget* parent, const QString& name, const QStringList& items, int working) {
    auto* m = new QComboBox(parent);
    m->setObjectName(name);
    m->addItems(items);
    if (auto* model = qobject_cast<QStandardItemModel*>(m->model())) {
        for (int i = working; i < m->count(); ++i) {
            model->item(i)->setEnabled(false);
            model->item(i)->setToolTip(laterTip());
        }
    }
    return m;
}

} // namespace

// The dialog's groups as on p.362-364: Relink selected items to, Relink by, Video Relink Parameters,
// and the check boxes below them.
ManagedRelinkDialog::ManagedRelinkDialog(const QString& project, QWidget* parent) : QDialog(parent), project_(project) {
    setObjectName(QStringLiteral("relinkDialog"));
    setWindowTitle(tr("Relink"));
    auto* layout = new QVBoxLayout(this);

    // Relink selected items to (p.362).
    auto* to = new QGroupBox(tr("Relink selected items to:"), this);
    auto* tv = new QVBoxLayout(to);
    allDrives_ = new QRadioButton(tr("All Available Drives"), to);
    allDrives_->setObjectName(QStringLiteral("allAvailableDrives"));
    allDrives_->setChecked(true);
    oneDrive_ = new QRadioButton(tr("Media on drive:"), to);
    oneDrive_->setObjectName(QStringLiteral("mediaOnDrive"));
    drive_ = new QComboBox(to);
    drive_->setObjectName(QStringLiteral("relinkDrive"));
    for (const media::MediaDrive& d : media::mediaDrives()) drive_->addItem(d.name, d.root);
    auto* driveRow = new QHBoxLayout;
    driveRow->addWidget(oneDrive_);
    driveRow->addWidget(drive_);
    driveRow->addStretch();
    tv->addWidget(allDrives_);
    tv->addLayout(driveRow);
    tv->addWidget(checkBox(to, tr("Master clips"), QStringLiteral("masterClips"), true));
    tv->addWidget(checkBox(to, tr("All other items"), QStringLiteral("otherItems"), true));
    tv->addWidget(checkBox(to, tr("Relink only to media from the current project"), QStringLiteral("currentProject"),
                           false));
    tv->addWidget(checkBox(to, tr("Selected items in ALL open bins"), QStringLiteral("allOpenBins"), false));
    tv->addWidget(checkBox(to, tr("Allow relinking to offline items"), QStringLiteral("offlineItems"), false, true));
    layout->addWidget(to);

    // Relink by (p.363).
    auto* by = new QGroupBox(tr("Relink by:"), this);
    auto* bf = new QFormLayout(by);
    bf->addRow(tr("Timecode:"),
               menuOf(by, QStringLiteral("relinkTimecode"),
                      {tr("Start"), tr("Aux TC1"), tr("Aux TC2"), tr("Aux TC3"), tr("Aux TC4"), tr("Aux TC5"),
                       tr("Sound Timecode")},
                      1));
    bf->addRow(tr("Source Name:"),
               menuOf(by, QStringLiteral("relinkSourceName"),
                      {tr("Tape Name or Source File Name"), tr("Name"), tr("Tape Name or a Source File ID"),
                       tr("Keynumber"), tr("Camroll"), tr("Labroll"), tr("Disk Label")},
                      2));
    bf->addRow(checkBox(by, tr("Ignore extension"), QStringLiteral("ignoreExtension"), false));
    auto* ignoreRow = new QHBoxLayout;
    ignoreRow->addWidget(checkBox(by, tr("Ignore characters after last occurrence of"), QStringLiteral("ignoreAfterOn"),
                                  false));
    ignoreAfter_ = new QLineEdit(by);
    ignoreAfter_->setObjectName(QStringLiteral("ignoreAfter"));
    ignoreRow->addWidget(ignoreAfter_);
    bf->addRow(ignoreRow);
    bf->addRow(checkBox(by, tr("Match case when comparing tape and source file names"), QStringLiteral("matchCase"),
                        false));
    layout->addWidget(by);

    // Video Relink Parameters (p.363-364).
    auto* video = new QGroupBox(tr("Video Relink Parameters"), this);
    auto* vf = new QFormLayout(video);
    auto* relinkTo = menuOf(video, QStringLiteral("relinkTo"),
                            {tr("Any video format"), tr("Video format of current project only"),
                             tr("Any HD video format"), tr("Any SD video format")},
                            1);
    vf->addRow(tr("Relink to:"), relinkTo);
    method_ = menuOf(video, QStringLiteral("relinkMethod"),
                     {tr("Highest Quality"), tr("Most Compressed"), tr("Specific Resolution")}, 3);
    vf->addRow(tr("Relink method:"), method_);
    quality_ = menuOf(video, QStringLiteral("relinkQuality"),
                      {tr("Is greater than or equal to"), tr("Is equal to"), tr("Is less than or equal to")}, 3);
    vf->addRow(tr("Relink if quality:"), quality_);
    resolution_ = new QComboBox(video);
    resolution_->setObjectName(QStringLiteral("relinkResolution"));
    for (const media::Resolution& r : media::resolutions()) resolution_->addItem(r.name, r.id);
    resolution_->setCurrentIndex(resolution_->findData(media::defaultResolution().id));
    vf->addRow(tr("Resolution:"), resolution_);
    vf->addRow(tr("If no match is found:"),
               menuOf(video, QStringLiteral("noMatch"), {tr("Use Existing Media"), tr("Unlink (Take Offline)")}, 2));
    layout->addWidget(video);

    layout->addWidget(checkBox(this, tr("Tracks to Relink"), QStringLiteral("tracksToRelink"), true, true));
    layout->addWidget(checkBox(this, tr("Create new sequences"), QStringLiteral("newSequences"), true));
    layout->addWidget(checkBox(this, tr("Allow relink across rates"), QStringLiteral("acrossRates"), false));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(oneDrive_, &QRadioButton::toggled, this, [this] { update(); });
    connect(method_, &QComboBox::currentIndexChanged, this, [this] { update(); });
    update();
}

void ManagedRelinkDialog::update() {
    drive_->setEnabled(oneDrive_->isChecked());
    const bool specific = method_->currentIndex() == 2;
    quality_->setEnabled(specific);
    resolution_->setEnabled(specific);
}

QCheckBox* ManagedRelinkDialog::check(const QString& objectName) const {
    return findChild<QCheckBox*>(objectName);
}

QComboBox* ManagedRelinkDialog::menu(const QString& objectName) const {
    return findChild<QComboBox*>(objectName);
}

bool ManagedRelinkDialog::allOpenBins() const {
    return check(QStringLiteral("allOpenBins"))->isChecked();
}

// Source Name's "Name" relinks by clip name; its other working choice by tape or source file name.
// The Relink method and quality menus are in the order of media::RelinkOptions' enums.
media::RelinkOptions ManagedRelinkDialog::options() const {
    media::RelinkOptions o;
    if (oneDrive_->isChecked() && drive_->currentIndex() >= 0) o.drives << drive_->currentData().toString();
    o.masterClips = check(QStringLiteral("masterClips"))->isChecked();
    o.otherItems = check(QStringLiteral("otherItems"))->isChecked();
    o.currentProjectOnly = check(QStringLiteral("currentProject"))->isChecked();
    o.project = project_;
    o.by = menu(QStringLiteral("relinkSourceName"))->currentIndex() == 1 ? media::RelinkOptions::By::Name
                                                                         : media::RelinkOptions::By::SourceName;
    o.ignoreExtension = check(QStringLiteral("ignoreExtension"))->isChecked();
    if (check(QStringLiteral("ignoreAfterOn"))->isChecked()) o.ignoreAfter = ignoreAfter_->text();
    o.matchCase = check(QStringLiteral("matchCase"))->isChecked();
    o.method = static_cast<media::RelinkOptions::Method>(method_->currentIndex());
    o.quality = static_cast<media::RelinkOptions::Quality>(quality_->currentIndex());
    o.resolution = resolution_->currentData().toString();
    o.unlinkWithoutMatch = menu(QStringLiteral("noMatch"))->currentIndex() == 1;
    o.newSequences = check(QStringLiteral("newSequences"))->isChecked();
    o.acrossRates = check(QStringLiteral("acrossRates"))->isChecked();
    return o;
}

} // namespace lumenis::ui

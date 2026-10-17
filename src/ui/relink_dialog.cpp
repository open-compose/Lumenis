#include "lumenis/ui/relink_dialog.h"

#include "lumenis/media/link.h"
#include "lumenis/ui/media_frames.h"

#include <QComboBox>
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

namespace lumenis::ui {

namespace {

// The picture on p.365.
const QStringList& headings() {
    static const QStringList h{QStringLiteral("Color"),       QStringLiteral("Name"),     QStringLiteral("Source File"),
                               QStringLiteral("Source Path"), QStringLiteral("Format"),   QStringLiteral("Duration"),
                               QStringLiteral("Tracks"),      QStringLiteral("Creation Date")};
    return h;
}

constexpr int kClipRole = Qt::UserRole + 1;

} // namespace

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

} // namespace lumenis::ui

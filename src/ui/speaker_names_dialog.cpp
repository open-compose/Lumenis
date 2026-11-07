// The Manage Speaker Names window (lumenis/ui/speaker_names_dialog.h; Editing Guide p.445).
#include "lumenis/ui/speaker_names_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lumenis::ui {

SpeakerNamesDialog::SpeakerNamesDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("speakerNamesDialog"));
    setWindowTitle(tr("Manage Speaker Names"));
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("Speaker names in this project:"), this));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("speakerNames"));
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // "Double-click on a name to rename it": the item edits in place, the change goes out when it is committed.
    list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    v->addWidget(list_, 1);
    auto* buttons = new QHBoxLayout;
    add_ = new QPushButton(QStringLiteral("+"), this);
    add_->setObjectName(QStringLiteral("addSpeaker"));
    add_->setToolTip(tr("Add a new speaker"));
    merge_ = new QPushButton(tr("Merge"), this);
    merge_->setObjectName(QStringLiteral("mergeSpeakers"));
    clear_ = new QPushButton(tr("Delete"), this);
    clear_->setObjectName(QStringLiteral("clearSpeaker"));
    auto* done = new QPushButton(tr("Done"), this);
    done->setObjectName(QStringLiteral("done"));
    done->setDefault(true);
    buttons->addWidget(add_);
    buttons->addWidget(merge_);
    buttons->addWidget(clear_);
    buttons->addStretch(1);
    buttons->addWidget(done);
    v->addLayout(buttons);

    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(add_, &QPushButton::clicked, this, [this] {
        // A new name to type over (ours: the guide does not say what "+" shows first).
        QString name = tr("New Speaker");
        for (int n = 2; names().contains(name); ++n) name = tr("New Speaker %1").arg(n);
        add(name);
        for (int r = 0; r < list_->count(); ++r) {
            if (list_->item(r)->text() == name) {
                list_->setCurrentRow(r);
                list_->editItem(list_->item(r));
            }
        }
    });
    connect(merge_, &QPushButton::clicked, this, [this] { mergeSelected(); });
    connect(clear_, &QPushButton::clicked, this, [this] { clearSelected(); });
    connect(list_, &QListWidget::itemSelectionChanged, this, &SpeakerNamesDialog::refreshButtons);
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (updating_) return;
        const QString from = item->data(Qt::UserRole).toString();
        const QString to = item->text().trimmed();
        if (to.isEmpty() || to == from) {
            updating_ = true;
            item->setText(from);
            updating_ = false;
            return;
        }
        rename(from, to);
    });
    resize(320, 360);
    refreshButtons();
}

void SpeakerNamesDialog::setNames(const QStringList& names, const QStringList& used) {
    const QStringList keep = selected();
    used_ = used;
    QStringList all = names;
    for (const QString& n : used) {
        if (!all.contains(n)) all << n;
    }
    all.removeDuplicates();
    all.sort(Qt::CaseInsensitive);
    updating_ = true;
    list_->clear();
    for (const QString& n : all) {
        auto* item = new QListWidgetItem(n, list_);
        item->setData(Qt::UserRole, n);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (!used_.contains(n)) {
            // Dimmed: in the project's list only.
            item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            item->setToolTip(tr("Not in any transcript of this project"));
        }
        item->setSelected(keep.contains(n));
    }
    updating_ = false;
    refreshButtons();
}

QStringList SpeakerNamesDialog::names() const {
    QStringList out;
    for (int r = 0; r < list_->count(); ++r) out << list_->item(r)->data(Qt::UserRole).toString();
    return out;
}

void SpeakerNamesDialog::select(const QStringList& names) {
    for (int r = 0; r < list_->count(); ++r) list_->item(r)->setSelected(names.contains(list_->item(r)->text()));
}

QStringList SpeakerNamesDialog::selected() const {
    QStringList out;
    for (int r = 0; r < list_->count(); ++r) {
        if (list_->item(r)->isSelected()) out << list_->item(r)->data(Qt::UserRole).toString();
    }
    return out;
}

void SpeakerNamesDialog::add(const QString& name) {
    if (name.trimmed().isEmpty() || names().contains(name.trimmed())) return;
    emit changeRequested({SpeakerNameChange::Kind::Add, {}, name.trimmed()});
}

void SpeakerNamesDialog::rename(const QString& from, const QString& to) {
    if (to.trimmed().isEmpty() || from == to.trimmed()) return;
    emit changeRequested({SpeakerNameChange::Kind::Rename, {from}, to.trimmed()});
}

bool SpeakerNamesDialog::mergeSelected() {
    const QStringList names = selected();
    if (names.size() < 2) return false;
    emit changeRequested({SpeakerNameChange::Kind::Merge, names, names.first()});
    return true;
}

bool SpeakerNamesDialog::clearSelected() {
    const QStringList names = selected();
    if (names.isEmpty()) return false;
    for (const QString& n : names) {
        emit changeRequested({used_.contains(n) ? SpeakerNameChange::Kind::Clear : SpeakerNameChange::Kind::Delete, {n}, {}});
    }
    return true;
}

QString SpeakerNamesDialog::clearButtonText() const { return clear_->text(); }

// Clear while any selected name is in a transcript, Delete for dimmed names (p.445).
void SpeakerNamesDialog::refreshButtons() {
    const QStringList names = selected();
    const bool used = std::any_of(names.cbegin(), names.cend(), [this](const QString& n) { return used_.contains(n); });
    clear_->setText(used ? tr("Clear") : tr("Delete"));
    clear_->setEnabled(!names.isEmpty());
    merge_->setEnabled(names.size() >= 2);
}

} // namespace lumenis::ui

// The Transcribe window and Manage Transcripts (lumenis/ui/transcript_dialogs.h; Editing Guide p.450-456).
#include "lumenis/ui/transcript_dialogs.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lumenis::ui {

TranscribeTracksDialog::TranscribeTracksDialog(const QStringList& tracks, bool captions, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("transcribeTracksDialog"));
    setWindowTitle(tr("Transcribe"));
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("Select the audio tracks to include:"), this));
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("tracks"));
    for (const QString& t : tracks) {
        auto* item = new QListWidgetItem(t, list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    v->addWidget(list_, 1);
    if (captions) {
        marks_ = new QCheckBox(tr("Use Marks"), this);
        marks_->setObjectName(QStringLiteral("useMarks"));
        v->addWidget(marks_);
        auto* row = new QHBoxLayout;
        limit_ = new QCheckBox(tr("Limit characters per caption"), this);
        limit_->setObjectName(QStringLiteral("limitCharacters"));
        // Ours: 32 characters to start with (the CEA-608 row width); the guide gives no value.
        characters_ = new QSpinBox(this);
        characters_->setObjectName(QStringLiteral("characters"));
        characters_->setRange(8, 200);
        characters_->setValue(32);
        characters_->setEnabled(false);
        connect(limit_, &QCheckBox::toggled, characters_, &QWidget::setEnabled);
        row->addWidget(limit_);
        row->addWidget(characters_);
        row->addStretch(1);
        v->addLayout(row);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);
}

QStringList TranscribeTracksDialog::tracks() const {
    QStringList out;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->checkState() == Qt::Checked) out << list_->item(i)->text();
    }
    return out;
}

void TranscribeTracksDialog::setTracks(const QStringList& checked) {
    for (int i = 0; i < list_->count(); ++i) list_->item(i)->setCheckState(checked.contains(list_->item(i)->text()) ? Qt::Checked : Qt::Unchecked);
}

bool TranscribeTracksDialog::useMarks() const { return marks_ && marks_->isChecked(); }

void TranscribeTracksDialog::setUseMarks(bool on) {
    if (marks_) marks_->setChecked(on);
}

int TranscribeTracksDialog::characterLimit() const { return limit_ && limit_->isChecked() ? characters_->value() : 0; }

void TranscribeTracksDialog::setCharacterLimit(int characters) {
    if (!limit_) return;
    limit_->setChecked(characters > 0);
    if (characters > 0) characters_->setValue(characters);
}

ManageTranscriptsDialog::ManageTranscriptsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("manageTranscriptsDialog"));
    setWindowTitle(tr("Manage Transcripts"));
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel(tr("Transcripts of this project's clips:"), this));
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("transcripts"));
    list_->setHeaderLabels({tr("Clip"), tr("Language"), tr("Created"), tr("Edited")});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    v->addWidget(list_, 1);
    auto* buttons = new QHBoxLayout;
    delete_ = new QPushButton(tr("Delete"), this);
    delete_->setObjectName(QStringLiteral("deleteTranscripts"));
    auto* import = new QPushButton(tr("Import"), this);
    import->setObjectName(QStringLiteral("importTranscripts"));
    auto* exportButton = new QPushButton(tr("Export"), this);
    exportButton->setObjectName(QStringLiteral("exportTranscripts"));
    auto* done = new QPushButton(tr("Done"), this);
    done->setDefault(true);
    buttons->addWidget(delete_);
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    buttons->addStretch(1);
    buttons->addWidget(done);
    v->addLayout(buttons);
    connect(delete_, &QPushButton::clicked, this, [this] {
        if (!selected().isEmpty()) emit deleteRequested(selected());
    });
    connect(import, &QPushButton::clicked, this, &ManageTranscriptsDialog::importRequested);
    connect(exportButton, &QPushButton::clicked, this, &ManageTranscriptsDialog::exportRequested);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] { delete_->setEnabled(!selected().isEmpty()); });
    delete_->setEnabled(false);
    resize(520, 360);
}

void ManageTranscriptsDialog::setEntries(const QList<Entry>& entries) {
    const QStringList keep = selected();
    entries_ = entries;
    list_->clear();
    for (const Entry& e : entries) {
        auto* item = new QTreeWidgetItem(list_, {e.clip, e.language.isEmpty() ? tr("Multilingual") : e.language, e.created,
                                                 e.edited ? tr("Yes") : QString()});
        item->setData(0, Qt::UserRole, e.mob);
        item->setSelected(keep.contains(e.mob));
    }
    delete_->setEnabled(!selected().isEmpty());
}

QStringList ManageTranscriptsDialog::selected() const {
    QStringList out;
    for (const QTreeWidgetItem* item : list_->selectedItems()) out << item->data(0, Qt::UserRole).toString();
    return out;
}

void ManageTranscriptsDialog::select(const QStringList& mobs) {
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list_->topLevelItem(i);
        item->setSelected(mobs.contains(item->data(0, Qt::UserRole).toString()));
    }
}

} // namespace lumenis::ui

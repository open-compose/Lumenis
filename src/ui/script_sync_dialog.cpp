// The ScriptSync AI dialog (lumenis/ui/script_sync_dialog.h; Editing Guide p.584-585).
#include "lumenis/ui/script_sync_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lumenis::ui {

ScriptSyncDialog::ScriptSyncDialog(const QStringList& tracks, const model::ScriptSyncOptions& options,
                                   std::function<std::optional<int>()> selectDialog, QWidget* parent)
    : QDialog(parent), selectDialog_(std::move(selectDialog)) {
    setObjectName(QStringLiteral("scriptSyncDialog"));
    setWindowTitle(tr("Script Alignment"));
    auto* v = new QVBoxLayout(this);
    // "Tracks: Select the audio tracks you want as input to ScriptSync AI."
    v->addWidget(new QLabel(tr("Tracks"), this));
    tracks_ = new QListWidget(this);
    tracks_->setObjectName(QStringLiteral("tracks"));
    for (const QString& t : tracks) {
        auto* item = new QListWidgetItem(t, tracks_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    tracks_->setMaximumHeight(110);
    v->addWidget(tracks_);
    auto box = [this, v](const QString& text, const char* name) {
        auto* c = new QCheckBox(text, this);
        c->setObjectName(QString::fromLatin1(name));
        v->addWidget(c);
        return c;
    };
    capitals_ = box(tr("Skip lines that only contain CAPITAL letters"), "skipCapitals");
    bracketed_ = box(tr("Skip text in parentheses '()' or brackets '[]'"), "skipBracketed");
    colon_ = box(tr("Skip text before colon ':'"), "skipBeforeColon");
    indented_ = box(tr("Skip lines indented less than dialog"), "skipIndented");
    auto* indentRow = new QHBoxLayout;
    indentRow->addSpacing(20);
    indentRow->addWidget(new QLabel(tr("Dialog Indent (characters)"), this));
    indent_ = new QSpinBox(this);
    indent_->setObjectName(QStringLiteral("dialogIndent"));
    indent_->setRange(0, 200);
    indentRow->addWidget(indent_);
    auto* select = new QPushButton(tr("Select Dialog"), this);
    select->setObjectName(QStringLiteral("selectDialog"));
    indentRow->addWidget(select);
    indentRow->addStretch(1);
    v->addLayout(indentRow);
    connect(select, &QPushButton::clicked, this, &ScriptSyncDialog::selectDialog);
    connect(indented_, &QCheckBox::toggled, indent_, &QWidget::setEnabled);
    connect(indented_, &QCheckBox::toggled, select, &QWidget::setEnabled);
    overwrite_ = box(tr("Overwrite existing marks"), "overwrite");
    between_ = box(tr("Sync between first and last mark"), "betweenMarks");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);
    setOptions(options);
    indent_->setEnabled(indented_->isChecked());
    select->setEnabled(indented_->isChecked());
}

QStringList ScriptSyncDialog::tracks() const {
    QStringList out;
    for (int i = 0; i < tracks_->count(); ++i)
        if (tracks_->item(i)->checkState() == Qt::Checked) out << tracks_->item(i)->text();
    return out;
}

void ScriptSyncDialog::setTracks(const QStringList& checked) {
    for (int i = 0; i < tracks_->count(); ++i)
        tracks_->item(i)->setCheckState(checked.contains(tracks_->item(i)->text()) ? Qt::Checked : Qt::Unchecked);
}

model::ScriptSyncOptions ScriptSyncDialog::options() const {
    model::ScriptSyncOptions o;
    o.skipCapitals = capitals_->isChecked();
    o.skipBracketed = bracketed_->isChecked();
    o.skipBeforeColon = colon_->isChecked();
    o.skipIndented = indented_->isChecked();
    o.dialogIndent = indent_->value();
    o.overwrite = overwrite_->isChecked();
    o.betweenMarks = between_->isChecked();
    return o;
}

void ScriptSyncDialog::setOptions(const model::ScriptSyncOptions& o) {
    capitals_->setChecked(o.skipCapitals);
    bracketed_->setChecked(o.skipBracketed);
    colon_->setChecked(o.skipBeforeColon);
    indented_->setChecked(o.skipIndented);
    indent_->setValue(o.dialogIndent);
    overwrite_->setChecked(o.overwrite);
    between_->setChecked(o.betweenMarks);
}

void ScriptSyncDialog::selectDialog() {
    if (!selectDialog_) return;
    if (const auto indent = selectDialog_()) indent_->setValue(*indent);
}

} // namespace lumenis::ui

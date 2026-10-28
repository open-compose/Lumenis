#include "lumenis/ui/set_font_dialog.h"

#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QSpinBox>

// The Set Font dialog (set_font_dialog.h).

namespace lumenis::ui {

SetFontDialog::SetFontDialog(const QFont& current, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Set Font"));
    auto* form = new QFormLayout(this);
    font_ = new QFontComboBox(this);
    font_->setObjectName(QStringLiteral("font"));
    font_->setCurrentFont(current);
    form->addRow(tr("Font:"), font_);
    size_ = new QSpinBox(this);
    size_->setObjectName(QStringLiteral("size"));
    size_->setRange(6, 72); // ours
    size_->setValue(current.pointSize() > 0 ? current.pointSize() : 10);
    form->addRow(tr("Size:"), size_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

QString SetFontDialog::family() const {
    return font_->currentFont().family();
}

int SetFontDialog::pointSize() const {
    return size_->value();
}

} // namespace lumenis::ui

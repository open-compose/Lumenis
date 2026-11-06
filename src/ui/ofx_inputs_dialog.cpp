// The OFX Optional Inputs dialog (ofx_inputs_dialog.h; FX Guide p.161).

#include "lumenis/ui/ofx_inputs_dialog.h"

#include "lumenis/media/ofx_host.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

namespace lumenis::ui {

OfxOptionalInputsDialog::OfxOptionalInputsDialog(const media::OfxPluginInfo& plugin, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("ofxOptionalInputs"));
    setWindowTitle(tr("OFX Optional Inputs"));
    auto* form = new QFormLayout(this);
    form->addRow(new QLabel(plugin.label, this));
    tracks_ = new QComboBox(this);
    tracks_->setObjectName(QStringLiteral("numberOfTracks"));
    for (int n = std::max(plugin.requiredInputs(), 1); n <= plugin.maxInputs(); ++n) tracks_->addItem(QString::number(n), n);
    tracks_->setCurrentIndex(tracks_->count() - 1);
    form->addRow(tr("Number of Tracks"), tracks_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

int OfxOptionalInputsDialog::count() const {
    return tracks_->currentData().toInt();
}

std::optional<int> OfxOptionalInputsDialog::ask(const media::OfxPluginInfo& plugin, QWidget* parent) {
    OfxOptionalInputsDialog dialog(plugin, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.count();
}

} // namespace lumenis::ui

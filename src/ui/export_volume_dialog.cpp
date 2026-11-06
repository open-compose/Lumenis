// The Export Volume dialog box (lumenis/ui/export_volume_dialog.h).
#include "lumenis/ui/export_volume_dialog.h"

#include "lumenis/media/movie_export.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumenis::ui {

ExportVolumeDialog::ExportVolumeDialog(const model::ExportVolume& volume, QWidget* parent) : QDialog(parent), original_(volume) {
    setObjectName(QStringLiteral("exportVolumeDialog"));
    setWindowTitle(tr("Export Volume"));
    auto* form = new QFormLayout;
    type_ = new QComboBox(this);
    type_->setObjectName(QStringLiteral("volumeType"));
    type_->addItem(QStringLiteral("AS-02"));
    form->addRow(tr("Volume Type:"), type_);
    auto withSet = [&](QLineEdit*& edit, const QString& objectName, const std::function<void()>& set) {
        auto* row = new QHBoxLayout;
        edit = new QLineEdit(this);
        edit->setObjectName(objectName);
        auto* button = new QPushButton(tr("Set"), this);
        connect(button, &QPushButton::clicked, this, set);
        row->addWidget(edit, 1);
        row->addWidget(button);
        return row;
    };
    form->addRow(tr("Path:"), withSet(path_, QStringLiteral("volumePath"), [this] {
                     const QString chosen = chooseFolder_ ? chooseFolder_() : QFileDialog::getExistingDirectory(this, tr("Export Volume Path"), path_->text());
                     if (!chosen.isEmpty()) setPath(chosen);
                 }));
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("volumeName"));
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Shim Template:"), withSet(shim_, QStringLiteral("volumeShim"), [this] {
                     const QString chosen = chooseShim_ ? chooseShim_()
                                                        : QFileDialog::getOpenFileName(this, tr("Shim Template"), shim_->text(),
                                                                                       tr("Shim templates (*.xml)"));
                     if (!chosen.isEmpty()) setShimFile(chosen);
                 }));
    // The essence's compression: J2K by default (p.904); DNxHR where the build has it (ours: in place of DNxHD).
    compression_ = new QComboBox(this);
    compression_->setObjectName(QStringLiteral("volumeCompression"));
    for (const QString& id : {QStringLiteral("j2k"), QStringLiteral("dnxhr_lb"), QStringLiteral("dnxhr_sq"), QStringLiteral("dnxhr_hq"),
                              QStringLiteral("dnxhr_hqx")}) {
        const media::MovieCompression* c = media::findMovieCompression(id);
        if (c && media::movieCompressionAvailable(*c)) compression_->addItem(c->label, c->id);
    }
    form->addRow(tr("Video Compression:"), compression_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    path_->setText(QDir::toNativeSeparators(volume.path));
    name_->setText(volume.name);
    shim_->setText(QDir::toNativeSeparators(volume.shimFile));
    setCompression(volume.compression);
}

void ExportVolumeDialog::setPath(const QString& path) { path_->setText(QDir::toNativeSeparators(path)); }
void ExportVolumeDialog::setName(const QString& name) { name_->setText(name); }
void ExportVolumeDialog::setShimFile(const QString& path) { shim_->setText(QDir::toNativeSeparators(path)); }

void ExportVolumeDialog::setCompression(const QString& id) {
    const int i = compression_->findData(id);
    if (i >= 0) compression_->setCurrentIndex(i);
}

model::ExportVolume ExportVolumeDialog::volume() const {
    model::ExportVolume v = original_;
    v.type = type_->currentText();
    v.path = QDir::fromNativeSeparators(path_->text().trimmed());
    v.name = name_->text().trimmed();
    v.compression = compression_->currentData().toString();
    const QString shim = QDir::fromNativeSeparators(shim_->text().trimmed());
    if (shim.isEmpty()) {
        v.shimFile.clear();
        v.shimName.clear();
        v.shimId.clear();
        v.shimAnnotation.clear();
    } else if (shim != original_.shimFile || v.shimName.isEmpty()) {
        model::readShimTemplate(shim, v);
        v.shimFile = shim;
    }
    return v;
}

} // namespace lumenis::ui

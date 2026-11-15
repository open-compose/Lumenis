// IMF in the application (11.5c; docs/spec/imf.md; Editing Guide p.333-340): Tools > IMF Window, the "IMF" panel, and
// a CPL dragged from it into a bin, which links the package's track files as clips and makes the composition a
// sequence of them.
#include "lumenis/ui/main_window.h"

#include "lumenis/interop/imf.h"
#include "lumenis/media/link.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/imf_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/tool_windows.h"

#include <QFileInfo>
#include <QHash>

namespace lumenis::ui {

void MainWindow::setUpImf() {
    imfWindow_ = new ImfWindow;
    panels_->addPanel(new Panel(QStringLiteral("IMF"), tr("IMF Window"), imfWindow_));
    connect(imfWindow_, &ImfWindow::failed, this, [this](const QString& reason) { reportFailure(this, tr("IMF: %1").arg(reason)); });
}

QStringList MainWindow::importImfComposition(const QString& binId, const QString& cplFile) {
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) doc = binContainer_->openBin(binId);
    if (!doc || doc->isReadOnly()) {
        reportFailure(this, tr("IMF: choose a bin that can be changed."));
        return {};
    }
    // The package the window lists (with the others' track files, for a Supplemental), else the CPL's own folder.
    std::optional<interop::imf::Package> package = imfWindow_ ? imfWindow_->packageOf(cplFile) : std::nullopt;
    QString error;
    if (!package) package = interop::imf::readPackage(QFileInfo(cplFile).absolutePath(), &error);
    const QString file = QFileInfo(cplFile).absoluteFilePath();
    const interop::imf::Composition* composition = nullptr;
    if (package) {
        for (const interop::imf::Composition& c : package->compositions) {
            if (c.file == file) composition = &c;
        }
    }
    std::optional<interop::imf::Composition> alone;
    if (!composition) {
        alone = interop::imf::readComposition(cplFile, &error);
        if (!alone) {
            reportFailure(this, tr("IMF: %1").arg(error));
            return {};
        }
        composition = &*alone;
    }
    // Each track file linked once, as a clip of its own.
    QStringList ids;
    QHash<QUuid, model::BinItem> clips;
    QStringList missing;
    for (const QUuid& id : composition->trackFiles()) {
        const QString path = package ? package->assets.value(id) : QString();
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            missing << interop::imf::urn(id);
            continue;
        }
        const QList<model::BinItem> linked = media::linkFiles({path}, linkOptions());
        if (linked.isEmpty()) {
            missing << QFileInfo(path).fileName();
            continue;
        }
        model::BinItem clip = linked.first();
        clip.fields.insert(QStringLiteral("IMF Track File"), interop::imf::urn(id));
        doc->addItem(clip);
        ids << clip.id;
        clips.insert(id, clip);
    }
    QStringList warnings;
    const model::BinItem sequence = interop::imf::compositionSequence(*composition, [&](const QUuid& id) -> const model::BinItem* {
        const auto it = clips.constFind(id);
        return it == clips.constEnd() ? nullptr : &*it;
    }, QDateTime::currentDateTimeUtc(), &warnings);
    doc->addItem(sequence);
    ids << sequence.id;
    if (BinView* view = binContainer_->viewFor(doc)) view->selectIds({sequence.id});
    if (!missing.isEmpty()) {
        reportFailure(this, tr("IMF: %1: track files not found or not linked: %2").arg(QFileInfo(cplFile).fileName(), missing.join(QStringLiteral(", "))));
    }
    // Console only: what the import left out (virtual tracks other than picture and sound).
    for (const QString& w : warnings) ConsoleLog::instance().append(tr("IMF: %1: %2").arg(QFileInfo(cplFile).fileName(), w));
    return ids;
}

} // namespace lumenis::ui

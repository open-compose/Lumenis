// Clip > Batch Import and Batch Re-import (9.2a; docs/spec/batch-import.md; Editing Guide p.233-237 "Reimporting Files",
// p.1097-1098 "Batch Importing File-Based Media at Different Resolutions"): imported master clips, and those the selected
// sequences use, imported again from their source files. The new media take the clip's MobID, so the clip, and every
// sequence that uses it, play them; the media made before stay on their drives as another resolution of the clip
// (MultiRez, 8.6a).
#include "lumenis/ui/main_window.h"

#include "lumenis/media/batch_import.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/batch_import_dialog.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/settings_window.h"
#include "lumenis/ui/tool_windows.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSet>

namespace lumenis::ui {

namespace {

// A clip's place: its bin and item id.
using ClipRef = std::pair<QString, QString>;

} // namespace

QList<BatchImportDialog::Entry> MainWindow::batchImportClips(bool offlineOnly) {
    QList<BatchImportDialog::Entry> out;
    BinView* view = binContainer_ ? binContainer_->activeView() : nullptr;
    if (!view || !project_) return out;
    const Rational projectRate = project_->settings.format.editRate;
    QSet<ClipRef> seen;
    auto add = [&](const QString& binId, const model::BinItem& clip) {
        if (!media::isReimportable(clip) || seen.contains({binId, clip.id})) return;
        seen.insert({binId, clip.id});
        // "... you cannot batch import material in formats other than the project format" (p.234).
        if (clip.editRate.num() != 0 && clip.editRate != projectRate) {
            ConsoleLog::instance().append(
                tr("Batch Import: %1 is not in the project's format and cannot be imported again.").arg(clip.name));
            return;
        }
        // Offline only: "Reimports only the selected imported master clips that are missing their media files."
        const bool offline = !media::isLinked(clip) || frames_->isOffline(clip);
        if (offlineOnly && !offline) return;
        BatchImportDialog::Entry e;
        e.binId = binId;
        e.itemId = clip.id;
        e.name = clip.name;
        e.lastPath = media::lastImportPath(clip);
        // "Media Composer finds the source file automatically if it is located in the same folder where you last
        // imported the file" (p.235).
        if (QFileInfo(e.lastPath).isFile()) e.path = QFileInfo(e.lastPath).absoluteFilePath();
        out << e;
    };
    const QString binId = view->document().nodeId();
    QStringList mixed;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* item = view->document().bin().item(id);
        if (!item) continue;
        if (item->kind != model::BinItemKind::Sequence) {
            add(binId, *item);
            continue;
        }
        // "A message box appears if you attempt to reimport such material" (p.234).
        if (media::isMixedRateSequence(*item, projectRate)) {
            mixed << item->name;
            continue;
        }
        // The master clips the sequence uses.
        const model::Sequence seq = model::Sequence::fromItem(*item);
        for (const QString& name : seq.trackNames()) {
            const model::Track* track = seq.track(name);
            if (!track) continue;
            for (const model::Segment& s : track->segments) {
                if (s.isFiller() || s.clipId.isEmpty()) continue;
                if (const auto clip = composer_->resolve(s.binId, s.clipId)) add(s.binId, *clip);
            }
        }
    }
    if (!mixed.isEmpty()) {
        const QString text = tr("%1 has material at more than one rate and cannot be imported again. Decompose it and "
                                "import the clips again in projects of their formats.")
                                 .arg(mixed.join(QStringLiteral(", ")));
        ConsoleLog::instance().append(tr("Batch Import: %1").arg(text));
        if (!batchImportChoiceHook_) QMessageBox::information(this, tr("Batch Import"), text);
    }
    return out;
}

int MainWindow::batchImport() {
    BinView* view = binContainer_ ? binContainer_->activeView() : nullptr;
    if (!view || view->selectedIds().isEmpty() || !project_) {
        QApplication::beep();
        return 0;
    }
    // "Import clips with offline media only?" Offline only / All clips / Cancel (the picture on p.234).
    BatchImportChoice choice = BatchImportChoice::Cancel;
    if (batchImportChoiceHook_) {
        choice = batchImportChoiceHook_();
    } else {
        QMessageBox box(QMessageBox::Information, tr("Batch Import"), tr("Import clips with offline media only?"),
                        QMessageBox::NoButton, this);
        QPushButton* offline = box.addButton(tr("Offline only"), QMessageBox::AcceptRole);
        QPushButton* all = box.addButton(tr("All clips"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(offline);
        box.exec();
        choice = box.clickedButton() == offline ? BatchImportChoice::OfflineOnly
                 : box.clickedButton() == all   ? BatchImportChoice::AllClips
                                                : BatchImportChoice::Cancel;
    }
    if (choice == BatchImportChoice::Cancel) return 0;
    const QList<BatchImportDialog::Entry> entries = batchImportClips(choice == BatchImportChoice::OfflineOnly);
    if (entries.isEmpty()) {
        ConsoleLog::instance().append(tr("Batch Import: none of the selected items has imported clips to import again."));
        QApplication::beep();
        return 0;
    }
    BatchImportDialog dialog(entries, this);
    // The Media Creation setting's Import tab gives the first resolution and drive (p.1176).
    const media::MediaCreation base = mediaCreation(QStringLiteral("import"));
    dialog.setResolution(base.resolution.id);
    if (!base.videoDrive.isEmpty()) dialog.setDrive(base.videoDrive);
    connect(&dialog, &BatchImportDialog::currentSettingsRequested, this, [this] {
        SettingsWindow* window = showSettings();
        window->showTab(SettingsWindow::Tab::User);
        const int entry = settings_->file(model::SettingsScope::User).activeIndex(QStringLiteral("Import"));
        if (entry >= 0) window->list(model::SettingsScope::User)->openEntry(entry);
    });
    const bool go = batchImportHook_ ? batchImportHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!go) return 0;
    return runBatchImport(dialog);
}

int MainWindow::runBatchImport(const BatchImportDialog& dialog) {
    media::MediaCreation creation = mediaCreation(QStringLiteral("import"));
    if (const media::Resolution* r = media::findResolution(dialog.resolution())) creation.resolution = *r;
    creation.videoDrive = dialog.videoDrive();
    creation.audioDrive = dialog.audioDrive();
    const QJsonObject current = importSettingsSnapshot();
    QList<BatchImportDialog::Entry> todo;
    for (const BatchImportDialog::Entry& e : dialog.entries()) {
        if (!e.path.isEmpty()) todo << e;
    }
    QProgressDialog progress(tr("Importing..."), tr("Cancel"), 0, 1000, this);
    progress.setObjectName(QStringLiteral("batchImportProgress"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    int done = 0;
    for (int i = 0; i < todo.size(); ++i) {
        const BatchImportDialog::Entry& e = todo.at(i);
        BinDocument* doc = binContainer_->document(e.binId);
        if (!doc) doc = binContainer_->openBin(e.binId);
        const model::BinItem* stored = doc ? doc->bin().item(e.itemId) : nullptr;
        if (!stored) continue;
        model::BinItem clip = *stored;
        progress.setLabelText(tr("Importing %1 (%2 of %3)").arg(clip.name).arg(i + 1).arg(todo.size()));
        creation.progress = [&](double part) {
            progress.setValue(static_cast<int>((i + part) / static_cast<double>(todo.size()) * 1000));
            QCoreApplication::processEvents();
            return !progress.wasCanceled();
        };
        // "When you reimport a media file, the entire media file, including all tracks, is reimported" (p.234).
        const auto info = media::probe(e.path);
        const auto linked = info ? media::linkClip(*info, linkOptions()) : std::nullopt;
        if (!linked) {
            ConsoleLog::instance().append(tr("Batch Import: %1: %2 cannot be read.").arg(clip.name, e.path));
            continue;
        }
        // "By default, Media Composer imports the file using the Import settings from the last time it imported the
        // file" unless "Override clip settings with current settings" (p.235, p.237).
        const QJsonObject last = clip.sourceSettings.value(QStringLiteral("import")).toObject();
        const QJsonObject settings = dialog.overrideSettings() || last.isEmpty() ? current : last;
        // The new media name the clip as their master (MultiRez: another resolution of the same clip, p.1098).
        creation.master = clip.mobId;
        QString error;
        const auto made = importClipMedia(*linked, settings, creation, &error);
        if (!made) {
            ConsoleLog::instance().append(tr("Batch Import: %1: %2").arg(clip.name, error));
            if (progress.wasCanceled()) break;
            continue;
        }
        // The clip keeps its name, id, MobID and the columns the user typed; its media, length and the columns that
        // describe the media come from the new import.
        clip.link = made->link;
        clip.durationFrames = made->durationFrames;
        for (auto it = made->fields.constBegin(); it != made->fields.constEnd(); ++it) clip.fields.insert(it.key(), it.value());
        clip.sourceSettings.insert(QStringLiteral("import"), settings);
        clip.modified = QDateTime::currentDateTimeUtc();
        storeClip(e.binId, clip);
        ++done;
    }
    progress.reset();
    // The new files are the clips' media from now on (the Media Tool, Dynamic Relink).
    if (done > 0) loadMediaDatabase();
    ConsoleLog::instance().append(tr("Batch Import: %n clip(s) imported again.", "", done));
    return done;
}

} // namespace lumenis::ui

// MainWindow's Dynamic Media Folders (8.4b-c; Editing Guide p.371-376, docs/spec/background-services.md): the folders
// and profiles kept in Dynamic_Media_Folders.json beside the user's settings, a timer that looks in each enabled folder
// for new entries, and a Background Queue job per entry that links it and runs its profile's Actions
// (media::processDmfEntry). What a job makes waits as an asset, shown by the Timeline's progress indicator as its green
// dot, until it is acquired into a bin from Tools > Dynamic Media Folders (the "DynamicMediaFolders" panel).
#include "lumenis/ui/main_window.h"

#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/dynamic_media_folders_window.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/tool_windows.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>

namespace lumenis::ui {

namespace {

// How often enabled folders are looked in (ours). An entry is taken once it looks the same twice in a row, so that
// files still being copied in are not linked half-written (ours: "The copy process can also be done using a
// third-party application", p.371, so we cannot know when a copy ends).
constexpr int kScanMs = 2000;

// What an entry looks like now: its files' count, total size and newest change.
QString entrySignature(const QString& path) {
    qint64 size = 0;
    qint64 newest = 0;
    const QStringList files = media::dmfEntryFiles(path);
    for (const QString& f : files) {
        const QFileInfo info(f);
        size += info.size();
        newest = std::max(newest, info.lastModified().toMSecsSinceEpoch());
    }
    return QStringLiteral("%1/%2/%3").arg(files.size()).arg(size).arg(newest);
}

} // namespace

QString MainWindow::dynamicMediaFoldersFile() const {
    // Ours: beside the user's settings, as Title_Templates and Send_To_Templates are; none without a user.
    const QString userSettings = settings_ ? settings_->path(model::SettingsScope::User) : QString();
    return userSettings.isEmpty() ? QString()
                                  : QFileInfo(userSettings).dir().filePath(QStringLiteral("Dynamic_Media_Folders.json"));
}

void MainWindow::setUpDynamicMediaFolders() {
    dmfWindow_ = new DynamicMediaFoldersWindow;
    panels_->addPanel(new Panel(QStringLiteral("DynamicMediaFolders"), tr("Dynamic Media Folders"), dmfWindow_));
    connect(dmfWindow_, &DynamicMediaFoldersWindow::foldersChanged, this,
            [this](const media::DynamicMediaFolders& folders) { setDynamicMediaFolders(folders); });
    connect(dmfWindow_, &DynamicMediaFoldersWindow::profileEditorRequested, this,
            [this](const QString& folder, const QString& profile) { editDmfProfile(folder, profile); });
    connect(dmfWindow_, &DynamicMediaFoldersWindow::acquireRequested, this,
            [this](const QString& folder) { acquireDmfAssets(folder); });
    dmfTimer_ = new QTimer(this);
    dmfTimer_->setInterval(kScanMs);
    connect(dmfTimer_, &QTimer::timeout, this, &MainWindow::scanDynamicMediaFolders);
    // The folders are the user's: read again when another user's settings load.
    auto load = [this] {
        const QString file = dynamicMediaFoldersFile();
        dmf_ = file.isEmpty() ? media::DynamicMediaFolders{} : media::DynamicMediaFolders::load(file);
        dmfSeen_.clear();
        dmfTimer_->start();
        refreshDmfWindow();
    };
    connect(settings_, &Settings::userChanged, this, load);
    load();
}

void MainWindow::setDynamicMediaFolders(const media::DynamicMediaFolders& folders) {
    dmf_ = folders;
    saveDynamicMediaFolders();
    scanDynamicMediaFolders();
    refreshDmfWindow();
}

void MainWindow::saveDynamicMediaFolders() {
    const QString file = dynamicMediaFoldersFile();
    QString error;
    if (!file.isEmpty() && !dmf_.save(file, &error)) {
        ConsoleLog::instance().append(tr("Dynamic Media Folders could not be saved to %1: %2").arg(file, error));
    }
}

// Each enabled folder with a profile: entries not done and not being processed are queued once they have looked the
// same on two scans.
void MainWindow::scanDynamicMediaFolders() {
    for (const media::DmfFolder& folder : dmf_.folders) {
        // "Select Enable ... to make sure that any files added to the Dynamic Media folder will have the actions set
        // by the associated Profile" (p.375).
        if (!folder.enabled) continue;
        const media::DmfProfile* profile = dmf_.profile(folder.profile);
        if (!profile) continue;
        for (const QString& entry : media::newDmfEntries(folder)) {
            const bool queued = std::any_of(dmfAssets_.cbegin(), dmfAssets_.cend(), [&](const DmfAsset& a) {
                return a.folder == folder.path && a.entry == entry;
            });
            if (queued) continue;
            const QString path = QDir(folder.path).filePath(entry);
            const QString signature = entrySignature(path);
            const QString key = QDir::cleanPath(path);
            if (dmfSeen_.value(key) != signature) {
                dmfSeen_.insert(key, signature);
                continue;
            }
            dmfSeen_.remove(key);
            queueDmfEntry(folder.path, entry, *profile);
        }
    }
}

void MainWindow::queueDmfEntry(const QString& folder, const QString& entry, const media::DmfProfile& profile) {
    DmfAsset asset;
    asset.folder = folder;
    asset.entry = entry;
    dmfAssets_ << asset;
    refreshDmfWindow();
    media::DmfProcessOptions options;
    options.link = linkOptions();
    options.creation = mediaCreation(QStringLiteral("transcode"));
    const QString path = QDir(folder).filePath(entry);
    auto work = [path, profile, options](const std::function<bool(double)>& progress) {
        const media::DmfResult r = media::processDmfEntry(path, profile, options, progress);
        media::ConsolidateResult out;
        out.added = r.clips;
        out.skipped = r.messages;
        out.error = r.error;
        return out;
    };
    // The result stays with the asset until it is acquired (8.4c); the entry is done either way, so that a file that
    // cannot be linked is not tried again on every scan (ours).
    backgroundQueue_->add(tr("Dynamic Media Folder: %1").arg(entry), QString(), work, BackgroundQueue::Priority::Normal,
                          [this, folder, entry](const media::ConsolidateResult& result) {
                              for (DmfAsset& a : dmfAssets_) {
                                  if (a.folder != folder || a.entry != entry) continue;
                                  a.ready = true;
                                  a.clips = result.added;
                                  a.error = result.error;
                              }
                              // An entry that made nothing has nothing to acquire.
                              dmfAssets_.erase(std::remove_if(dmfAssets_.begin(), dmfAssets_.end(),
                                                              [](const DmfAsset& a) { return a.ready && a.clips.isEmpty(); }),
                                               dmfAssets_.end());
                              for (const QString& line : result.skipped)
                                  ConsoleLog::instance().append(tr("Dynamic Media Folder %1: %2").arg(entry, line));
                              if (media::DmfFolder* f = dmf_.folder(folder); f && !f->done.contains(entry)) {
                                  f->done << entry;
                                  saveDynamicMediaFolders();
                              }
                              refreshDmfAssets();
                          });
}

void MainWindow::refreshDmfAssets() {
    // "If the progress indicator includes a solid green dot in the center, new DMF assets are available" (p.376).
    dmfAssetsWaiting_ = std::any_of(dmfAssets_.cbegin(), dmfAssets_.cend(),
                                    [](const DmfAsset& a) { return a.ready && !a.clips.isEmpty(); });
    updateBackgroundIndicator();
    refreshDmfWindow();
}

void MainWindow::refreshDmfWindow() {
    if (!dmfWindow_) return;
    QHash<QString, DynamicMediaFoldersWindow::AssetCount> counts;
    for (const DmfAsset& a : dmfAssets_) {
        DynamicMediaFoldersWindow::AssetCount& c = counts[QDir::cleanPath(a.folder)];
        // An asset already in a bin as offline placeholders waits for its processing to end (p.376).
        if (a.ready) ++c.ready;
        else if (a.placeholders.isEmpty()) ++c.processing;
    }
    dmfWindow_->setFolders(dmf_, counts);
}

void MainWindow::showDynamicMediaFolders() {
    refreshDmfWindow();
    showTool(QStringLiteral("DynamicMediaFolders"));
}

// The Profile Editor (p.372-375) on a copy of the folder's profile, or a new one; on Save the profile is kept under its
// name and given to the folder ("Assign the profile to the DMF", p.375).
bool MainWindow::editDmfProfile(const QString& folder, const QString& profileName) {
    media::DmfProfile profile;
    if (const media::DmfProfile* p = dmf_.profile(profileName)) profile = *p;
    QStringList drives;
    for (const media::MediaDrive& d : media::mediaDrives()) drives << d.root;
    DmfProfileEditor editor(profile, drives, this);
    const bool ok = dmfProfileEditorHook_ ? dmfProfileEditorHook_(editor) : editor.exec() == QDialog::Accepted;
    if (!ok || editor.result() != QDialog::Accepted) return false;
    const media::DmfProfile saved = editor.profile();
    if (saved.name.isEmpty()) return false;
    dmf_.storeProfile(saved);
    if (media::DmfFolder* f = dmf_.folder(folder)) f->profile = saved.name;
    saveDynamicMediaFolders();
    refreshDmfWindow();
    return true;
}

// "The new assets will populate the Media Composer Bin according to the Bin setting you chose in the Profile Editor. Any
// clips that have been consolidated or transcoded will also populate the bin as .new files. If some clips are still
// being processed, they will populate the bin as offline. When they are ready, the Acquire icon will redisplay next to
// the DMF" (p.376). Ours: an entry still being processed goes in as an offline master clip named after it, which its
// clips replace when it is acquired again.
QString MainWindow::acquireDmfAssets(const QString& folder) {
    if (!binContainer_->hasProject()) {
        ConsoleLog::instance().append(tr("Dynamic Media Folders: open a project to acquire assets."));
        return {};
    }
    const QString clean = QDir::cleanPath(folder);
    const media::DmfFolder* f = dmf_.folder(folder);
    const media::DmfProfile* profile = f ? dmf_.profile(f->profile) : nullptr;
    const media::DmfLinkSettings link = profile ? profile->link : media::DmfLinkSettings{};
    QString binId;
    // The bin: the active one, or a new bin named by the profile (also when there is no active bin).
    auto target = [&]() -> BinDocument* {
        if (binId.isEmpty() && link.bin == media::DmfLinkSettings::Bin::Active) {
            if (BinDocument* active = binContainer_->activeDocument()) binId = active->nodeId();
        }
        if (binId.isEmpty()) {
            QStringList taken;
            if (model::ProjectBins* bins = binContainer_->bins()) {
                for (const model::BinNode* b : bins->bins()) taken << b->name;
            }
            binId = binContainer_->newBin();
            if (binId.isEmpty()) return nullptr;
            binContainer_->renameNode(binId, media::dmfBinName(link, folder, project_ ? project_->settings.name : QString(), taken));
        }
        BinDocument* doc = binContainer_->document(binId);
        return doc ? doc : binContainer_->openBin(binId);
    };
    QStringList added;
    for (auto it = dmfAssets_.begin(); it != dmfAssets_.end();) {
        if (QDir::cleanPath(it->folder) != clean || (!it->ready && !it->placeholders.isEmpty())) {
            ++it;
            continue;
        }
        BinDocument* doc = nullptr;
        if (!it->placeholders.isEmpty()) {
            doc = binContainer_->document(it->placeholderBin);
            if (!doc) doc = binContainer_->openBin(it->placeholderBin);
            if (doc) {
                doc->removeItems(it->placeholders);
                if (binId.isEmpty()) binId = doc->nodeId();
            }
        }
        if (!doc) doc = target();
        if (!doc) break;
        if (!it->ready) {
            // Still being processed: an offline master clip until it is ready.
            model::BinItem offline;
            offline.id = model::newItemId();
            offline.kind = model::BinItemKind::MasterClip;
            offline.name = QFileInfo(it->entry).completeBaseName();
            offline.editRate = project_ ? project_->settings.format.editRate : offline.editRate;
            offline.created = QDateTime::currentDateTimeUtc();
            doc->addItem(offline);
            it->placeholderBin = doc->nodeId();
            it->placeholders = {offline.id};
            added << offline.id;
            ++it;
            continue;
        }
        QStringList taken;
        for (const model::BinItem& item : doc->bin().items) taken << item.name;
        for (model::BinItem clip : it->clips) {
            // A ".new" name taken in this bin gets the next number (p.349).
            const qsizetype dot = clip.name.indexOf(QLatin1String(".new"));
            if (dot > 0 && taken.contains(clip.name)) clip.name = media::numberedName(clip.name.left(dot), QStringLiteral(".new"), taken);
            taken << clip.name;
            doc->addItem(clip);
            added << clip.id;
        }
        it = dmfAssets_.erase(it);
    }
    if (!binId.isEmpty() || !added.isEmpty()) {
        for (BinDocument* doc : binContainer_->openDocuments()) {
            if (doc->isDirty()) doc->save();
        }
    }
    refreshDmfAssets();
    refreshSourceBins();
    return binId;
}

} // namespace lumenis::ui

// MainWindow's Dynamic Media Folders (8.4b; Editing Guide p.371-376, docs/spec/background-services.md): the folders and
// profiles kept in Dynamic_Media_Folders.json beside the user's settings, a timer that looks in each enabled folder for
// new entries, and a Background Queue job per entry that links it and runs its profile's Actions
// (media::processDmfEntry). What a job makes waits as an asset to be acquired into a bin (8.4c), which the Timeline's
// progress indicator shows as its green dot.
#include "lumenis/ui/main_window.h"

#include "lumenis/ui/background_queue.h"
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
    dmfTimer_ = new QTimer(this);
    dmfTimer_->setInterval(kScanMs);
    connect(dmfTimer_, &QTimer::timeout, this, &MainWindow::scanDynamicMediaFolders);
    // The folders are the user's: read again when another user's settings load.
    auto load = [this] {
        const QString file = dynamicMediaFoldersFile();
        dmf_ = file.isEmpty() ? media::DynamicMediaFolders{} : media::DynamicMediaFolders::load(file);
        dmfSeen_.clear();
        dmfTimer_->start();
    };
    connect(settings_, &Settings::userChanged, this, load);
    load();
}

void MainWindow::setDynamicMediaFolders(const media::DynamicMediaFolders& folders) {
    dmf_ = folders;
    saveDynamicMediaFolders();
    scanDynamicMediaFolders();
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
}

} // namespace lumenis::ui

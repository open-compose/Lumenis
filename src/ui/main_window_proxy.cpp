// Proxy media in the application (Editing Guide ch.16, p.508-513; 8.7a, docs/spec/proxy-media.md): Create Proxies from the
// bin (made here, on the Background Queue or through a shared storage module's Distributed Processing), the clip a monitor
// plays in the Play button's proxy mode, the bin's proxy columns, and Copy Media.
#include "lumenis/ui/main_window.h"

#include "lumenis/ext/provider.h"
#include "lumenis/ext/shared_storage.h"
#include "lumenis/media/copy_media.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/proxy_media.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/nest.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/model/proxy.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/proxy_dialogs.h"
#include "lumenis/ui/tool_windows.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>

namespace lumenis::ui {

namespace {

bool fileThere(const QString& path) {
    return QFileInfo::exists(path);
}

// Every render precompute clip in a sequence and its nests: segment effects' and transitions' renders, and those kept from
// other proxy modes (EffectRender::kept).
void collectRenders(const model::Sequence& sequence, QList<model::BinItem>& out) {
    auto add = [&out](const std::optional<model::EffectRender>& render) {
        if (!render) return;
        for (const QList<QJsonObject>* list : {&render->clips, &render->kept}) {
            for (const QJsonObject& json : *list) {
                if (const auto clip = model::binItemFromJson(json)) out << *clip;
            }
        }
    };
    for (const model::Track& t : sequence.tracks()) {
        for (const model::Segment& seg : t.segments) {
            add(seg.render);
            add(seg.transitionRender);
            if (!seg.nest.isEmpty()) collectRenders(model::nestOf(seg), out);
        }
    }
}

// The managed media files of a clip; none for linked media ("It will not copy Linked media", p.512).
QStringList managedFilesOf(const model::BinItem& clip) {
    const auto link = media::linkOf(clip);
    if (!link || !link->isManaged()) return {};
    QStringList out;
    for (const QString& f : link->files()) out << QFileInfo(f).absoluteFilePath();
    return out;
}

} // namespace

model::BinItem MainWindow::playedClip(const model::BinItem& clip) const {
    if (!composer_ || !model::hasProxy(clip)) return clip;
    const bool full = composer_->videoQuality() == Composer::VideoQuality::Full;
    const bool online = media::isLinked(clip) && !media::isUnlinked(clip) && !frames_->isOffline(clip);
    const QJsonObject link = model::playedProxyLink(clip, composer_->proxyPlayback(), full, online, fileThere);
    if (link.isEmpty()) return clip;
    model::BinItem played = clip;
    played.link = link;
    return played;
}

QString MainWindow::proxyCell(const model::BinItem& clip, const QString& heading) const {
    const bool full = !composer_ || composer_->videoQuality() == Composer::VideoQuality::Full;
    return model::proxyCellText(clip, heading, full, fileThere);
}

// The master clips the selected items give (p.508: "Select the master clips, subclips, groups or sequences"): master clips
// themselves, a subclip's master clip, a group's angles' clips and a sequence's clips, each once, with its bin. A sequence's
// subclips and groups give theirs in turn ("When performing Copy Media of a Multicam Group Clip, or a sequence that
// contains Multicam Group clips, all of the sources for Group clips are copied", p.513), and so do its nests. Ours: the
// clips are found in the open bins.
QList<std::pair<QString, model::BinItem>> MainWindow::proxyClips(const QList<std::pair<QString, model::BinItem>>& items) const {
    QList<std::pair<QString, model::BinItem>> out;
    QSet<QString> seen;    // master clips added
    QSet<QString> visited; // other items followed
    auto find = [this](const MobId& mob, const QString& id) -> std::optional<std::pair<QString, model::BinItem>> {
        for (const BinDocument* doc : binContainer_->openDocuments()) {
            for (const model::BinItem& item : doc->bin().items) {
                if ((!id.isEmpty() && item.id == id) || (!mob.isNull() && model::mobIdOf(item) == mob)) {
                    return std::pair{doc->nodeId(), item};
                }
            }
        }
        return std::nullopt;
    };
    std::function<void(const QString&, const model::BinItem&)> visit;
    std::function<void(const model::Sequence&)> visitSequence = [&](const model::Sequence& s) {
        for (const model::Track& t : s.tracks()) {
            for (const model::Segment& seg : t.segments) {
                if (!seg.nest.isEmpty()) visitSequence(model::nestOf(seg));
                if (seg.isFiller() || seg.clipId.isEmpty()) continue;
                if (const auto found = find(seg.sourceMobId(), seg.clipId)) visit(found->first, found->second);
            }
        }
    };
    visit = [&](const QString& binId, const model::BinItem& item) {
        if (item.kind == model::BinItemKind::MasterClip) {
            if (!seen.contains(item.id)) {
                seen.insert(item.id);
                out.append({binId, item});
            }
            return;
        }
        if (visited.contains(item.id)) return;
        visited.insert(item.id);
        switch (item.kind) {
        case model::BinItemKind::Subclip:
            for (const model::ClipSource& s : item.sources) {
                if (s.kind != model::ClipSource::Kind::Clip) continue;
                if (const auto found = find(s.mob, QString())) visit(found->first, found->second);
            }
            break;
        case model::BinItemKind::Group:
            if (const auto group = model::groupOf(item)) {
                for (const model::GroupAngle& angle : group->angles) {
                    if (const auto found = find(model::mobIdOf(angle.clip), angle.clipId)) visit(found->first, found->second);
                }
            }
            break;
        case model::BinItemKind::Sequence:
            visitSequence(model::Sequence::fromItem(item));
            break;
        default:
            break;
        }
    };
    for (const auto& [binId, item] : items) visit(binId, item);
    return out;
}

QList<MediaTarget> MainWindow::mediaTargets() const {
    QList<MediaTarget> out;
    for (const media::MediaDrive& d : media::mediaDrives()) out.append({d.name, d.root, {}, false});
    // "Select a local drive or choose an Avid NEXIS workspace" (p.510): the storage modules' workspaces.
    for (const auto& storage : providers_.usable<ext::SharedStorageProvider>()) {
        for (const ext::SharedWorkspace& w : storage->workspaces()) {
            out.append({QString::fromStdString(w.name), QString::fromStdString(w.root), QString::fromStdString(w.id),
                        w.distributedProcessing});
        }
    }
    return out;
}

// Bin > Create Proxies (p.508): the window, then the proxies of the selected items' master clips.
int MainWindow::createProxies() {
    BinView* view = binContainer_->activeView();
    if (!view || !project_) return 0;
    const QList<MediaTarget> targets = mediaTargets();
    if (targets.isEmpty()) {
        reportFailure(this, tr("Create Proxies: there is no drive to write proxies to."));
        return 0;
    }
    CreateProxiesDialog dialog(targets, this);
    if (dialog.exec() != QDialog::Accepted) return 0;
    if (dialog.distributed()) return submitDistributedProxies(dialog.target().workspaceId);
    return makeProxies(dialog.drive(), dialog.background());
}

int MainWindow::makeProxies(const QString& drive, bool background) {
    BinView* view = binContainer_->activeView();
    if (!view || !project_) return 0;
    QList<std::pair<QString, model::BinItem>> selected;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* item = view->document().bin().item(id); item && model::canHaveProxy(*item)) {
            selected.append({view->document().nodeId(), *item});
        }
    }
    const QSize raster(project_->settings.format.width, project_->settings.format.height);
    int made = 0;
    for (const auto& [binId, clip] : proxyClips(selected)) {
        if (background) {
            // "Processing Options ... 'Background Process'" (p.508): on the Background Queue; the clip takes its proxies
            // when the job is done.
            auto proxy = std::make_shared<std::optional<model::ProxyMedia>>();
            const model::BinItem copy = clip;
            const QString bin = binId;
            backgroundQueue_->add(
                tr("Proxies of %1").arg(clip.name), binId,
                [copy, drive, raster, proxy](const std::function<bool(double)>& progress) {
                    media::ConsolidateResult r;
                    QString why;
                    *proxy = media::makeProxyMedia(copy, drive, raster, progress, &why);
                    if (!*proxy) r.error = why;
                    return r;
                },
                BackgroundQueue::Priority::Normal,
                [this, bin, id = clip.id, proxy](const media::ConsolidateResult& r) {
                    if (!r.error.isEmpty()) reportFailure(this, tr("Create Proxies: %1").arg(r.error));
                    if (!*proxy) return;
                    BinDocument* doc = binContainer_->document(bin);
                    const model::BinItem* item = doc ? doc->bin().item(id) : nullptr;
                    if (!item) return;
                    model::BinItem changed = *item;
                    model::setProxy(changed, **proxy);
                    storeClip(bin, changed);
                });
            ++made;
            continue;
        }
        QString why;
        const auto proxy = media::makeProxyMedia(clip, drive, raster, {}, &why);
        if (!proxy) {
            reportFailure(this, tr("Create Proxies: %1").arg(why));
            continue;
        }
        model::BinItem changed = clip;
        model::setProxy(changed, *proxy);
        storeClip(binId, changed);
        ++made;
    }
    return made;
}

// "(Optional) Select the Distributed Processing option and click OK. You can monitor the job progress through the
// Distributed Process Job Status app" (p.510): one job per master clip, handed to the storage module.
int MainWindow::submitDistributedProxies(const QString& workspaceId) {
    BinView* view = binContainer_->activeView();
    if (!view || !project_) return 0;
    std::shared_ptr<ext::SharedStorageProvider> storage;
    for (const auto& s : providers_.usable<ext::SharedStorageProvider>()) {
        for (const ext::SharedWorkspace& w : s->workspaces()) {
            if (QString::fromStdString(w.id) == workspaceId && w.distributedProcessing) storage = s;
        }
    }
    if (!storage) {
        reportFailure(this, tr("Create Proxies: the workspace takes no Distributed Processing jobs."));
        return 0;
    }
    QList<std::pair<QString, model::BinItem>> selected;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* item = view->document().bin().item(id); item && model::canHaveProxy(*item)) {
            selected.append({view->document().nodeId(), *item});
        }
    }
    const QSize raster = media::proxyRaster(QSize(project_->settings.format.width, project_->settings.format.height));
    int sent = 0;
    for (const auto& [binId, clip] : proxyClips(selected)) {
        ext::ProxyJob job;
        job.workspaceId = workspaceId.toStdString();
        job.clipJson = QJsonDocument(model::binItemToJson(clip)).toJson(QJsonDocument::Compact).toStdString();
        job.width = raster.width();
        job.height = raster.height();
        std::string id;
        std::string error;
        if (!storage->submitProxyJob(job, &id, &error)) {
            reportFailure(this, tr("Create Proxies: %1: %2").arg(clip.name, QString::fromStdString(error)));
            continue;
        }
        distributedJobs_.append({storage, id, binId, clip.id, clip.name});
        ++sent;
    }
    if (sent > 0) {
        if (!distributedTimer_) {
            distributedTimer_ = new QTimer(this);
            distributedTimer_->setInterval(2000);
            connect(distributedTimer_, &QTimer::timeout, this, &MainWindow::pollDistributedJobs);
        }
        distributedTimer_->start();
    }
    return sent;
}

// The jobs that are done give their clips the proxies; failed ones say why in the Console.
void MainWindow::pollDistributedJobs() {
    for (int i = int(distributedJobs_.size()) - 1; i >= 0; --i) {
        const DistributedJob job = distributedJobs_.at(i);
        const ext::ProxyJobStatus status = job.storage->jobStatus(job.id);
        if (status.state == ext::ProxyJobStatus::State::Queued || status.state == ext::ProxyJobStatus::State::Running) continue;
        distributedJobs_.removeAt(i);
        if (status.state == ext::ProxyJobStatus::State::Failed) {
            reportFailure(this, tr("Create Proxies: %1: %2").arg(job.clipName, QString::fromStdString(status.error)));
            continue;
        }
        const auto proxy =
            model::ProxyMedia::fromJson(QJsonDocument::fromJson(QByteArray::fromStdString(status.proxyJson)).object());
        BinDocument* doc = binContainer_->document(job.binId);
        const model::BinItem* item = doc ? doc->bin().item(job.clipId) : nullptr;
        if (!proxy || !item) continue;
        model::BinItem changed = *item;
        model::setProxy(changed, *proxy);
        storeClip(job.binId, changed);
    }
    if (distributedJobs_.isEmpty() && distributedTimer_) distributedTimer_->stop();
}

// ------------------------------------------------------------------ Copy Media (p.512-513)

media::CopyMediaRequest MainWindow::copyMediaRequest(const QStringList& nodeIds) {
    media::CopyMediaRequest request;
    QList<std::pair<QString, model::BinItem>> items;
    QList<BinDocument*> docs;
    if (nodeIds.isEmpty()) {
        // "Select the items in the bin you want to copy" (p.512); ours: Bins copies the bin they are in.
        if (BinView* view = binContainer_->activeView()) {
            for (const QString& id : view->selectedIds()) {
                if (const model::BinItem* item = view->document().bin().item(id)) items.append({view->document().nodeId(), *item});
            }
            if (!items.isEmpty()) docs << &view->document();
        }
    } else if (const model::ProjectBins* bins = binContainer_->bins()) {
        // "Selecting a folder is the same as selecting all items in the folder"; "If you select bins that are closed, they
        // will be opened at the time the copy function begins" (p.512).
        QStringList binIds;
        std::function<void(const model::BinNode&)> walk = [&](const model::BinNode& n) {
            if (n.kind == model::BinNode::Kind::Bin) {
                if (!binIds.contains(n.id)) binIds << n.id;
                return;
            }
            if (n.kind == model::BinNode::Kind::Trash) return;
            for (const model::BinNode& c : n.children) walk(c);
        };
        for (const QString& id : nodeIds) {
            if (const model::BinNode* n = bins->node(id); n && !bins->isInTrash(id)) walk(*n);
        }
        for (const QString& id : binIds) {
            BinDocument* doc = binContainer_->document(id);
            if (!doc) doc = binContainer_->openBin(id);
            if (!doc) continue;
            docs << doc;
            for (const model::BinItem& item : doc->bin().items) items.append({doc->nodeId(), item});
        }
    }
    for (BinDocument* doc : docs) {
        // The bins as they are now.
        if (doc->isDirty()) binContainer_->saveBin(doc);
        request.binFiles << QFileInfo(doc->path()).absoluteFilePath();
    }
    // "If you select a sequence to copy, the Copy Media workflow copies the entire media file of the original master clip"
    // (p.512).
    for (const auto& [binId, clip] : proxyClips(items)) {
        request.managedFiles << managedFilesOf(clip);
        if (const auto proxy = model::proxyOf(clip)) {
            for (const QJsonObject& link : {proxy->h264Link, proxy->lbLink}) {
                const QString path = model::ProxyMedia::pathOf(link);
                if (!path.isEmpty()) request.proxyFiles << QFileInfo(path).absoluteFilePath();
            }
        }
    }
    QList<model::BinItem> renders;
    for (const auto& [binId, item] : items) {
        if (item.kind == model::BinItemKind::Sequence) collectRenders(model::Sequence::fromItem(item), renders);
    }
    for (const model::BinItem& r : renders) request.renderFiles << managedFilesOf(r);
    request.managedFiles.removeDuplicates();
    request.proxyFiles.removeDuplicates();
    request.renderFiles.removeDuplicates();
    request.binFiles.removeDuplicates();
    // Copy Bins to: the first drive at first (ours; the guide's picture shows a folder chosen with Set).
    const QList<MediaTarget> targets = mediaTargets();
    if (!targets.isEmpty()) request.binsTarget = targets.first().root;
    return request;
}

int MainWindow::copyMedia(const QStringList& nodeIds) {
    if (!project_) return 0;
    const media::CopyMediaRequest request = copyMediaRequest(nodeIds);
    if (request.binFiles.isEmpty() && request.managedFiles.isEmpty() && request.proxyFiles.isEmpty() &&
        request.renderFiles.isEmpty()) {
        reportFailure(this, tr("Copy Media: nothing is selected to copy."));
        return 0;
    }
    CopyMediaDialog dialog(mediaTargets(), request, this);
    if (dialog.exec() != QDialog::Accepted) return 0;
    return runCopyMedia(dialog.request());
}

int MainWindow::runCopyMedia(const media::CopyMediaRequest& request) {
    const media::CopyMediaPlan plan = media::planCopyMedia(request);
    if (plan.isEmpty()) {
        reportFailure(this, plan.existing > 0 ? tr("Copy Media: all selected files already exist at the target location.")
                                                        : tr("Copy Media: there is nothing to copy."));
        return 0;
    }
    // "If there is not enough space, copying will not be allowed" (p.513).
    if (!media::planFits(plan, request)) {
        reportFailure(this, tr("Copy Media: there is not enough space at the target location."));
        return 0;
    }
    QStringList warnings;
    const int copied = media::copyMedia(plan, {}, &warnings);
    for (const QString& w : warnings) reportFailure(this, tr("Copy Media: %1").arg(w));
    for (const QString& m : plan.missing) {
        reportFailure(this, tr("Copy Media: %1 is offline and was not copied.").arg(QDir::toNativeSeparators(m)));
    }
    // Console only: how many files were copied.
    ConsoleLog::instance().append(tr("Copy Media: %n file(s) copied.", nullptr, copied));
    return copied;
}

} // namespace lumenis::ui

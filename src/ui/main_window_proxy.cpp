// Proxy media in the application (Editing Guide ch.16, p.508-512; 8.7a, docs/spec/proxy-media.md): Create Proxies from the
// bin, the clip a monitor plays in the Play button's proxy mode, and the bin's proxy columns.
#include "lumenis/ui/main_window.h"

#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/proxy_media.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/proxy.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/proxy_dialogs.h"
#include "lumenis/ui/tool_windows.h"

#include <QFileInfo>
#include <QSet>

#include <memory>

namespace lumenis::ui {

namespace {

bool fileThere(const QString& path) {
    return QFileInfo::exists(path);
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
// themselves, a subclip's master clip, a group's angles' clips and a sequence's clips, each once, with its bin. Ours: the
// clips are found in the open bins.
QList<std::pair<QString, model::BinItem>> MainWindow::proxyClips(const QList<std::pair<QString, model::BinItem>>& items) const {
    QList<std::pair<QString, model::BinItem>> out;
    QSet<QString> seen;
    auto find = [this](const MobId& mob, const QString& id) -> std::optional<std::pair<QString, model::BinItem>> {
        for (const BinDocument* doc : binContainer_->openDocuments()) {
            for (const model::BinItem& item : doc->bin().items) {
                if (item.kind != model::BinItemKind::MasterClip) continue;
                if (item.id == id || (!mob.isNull() && model::mobIdOf(item) == mob)) return std::pair{doc->nodeId(), item};
            }
        }
        return std::nullopt;
    };
    auto add = [&](const std::optional<std::pair<QString, model::BinItem>>& clip) {
        if (clip && !seen.contains(clip->second.id)) {
            seen.insert(clip->second.id);
            out << *clip;
        }
    };
    for (const auto& [binId, item] : items) {
        switch (item.kind) {
        case model::BinItemKind::MasterClip:
            add(std::pair{binId, item});
            break;
        case model::BinItemKind::Subclip:
            for (const model::ClipSource& s : item.sources) {
                if (s.kind == model::ClipSource::Kind::Clip) add(find(s.mob, QString()));
            }
            break;
        case model::BinItemKind::Group:
            if (const auto group = model::groupOf(item)) {
                for (const model::GroupAngle& angle : group->angles) add(find(model::mobIdOf(angle.clip), angle.clipId));
            }
            break;
        case model::BinItemKind::Sequence: {
            const model::Sequence s = model::Sequence::fromItem(item);
            for (const model::Track& t : s.tracks()) {
                for (const model::Segment& seg : t.segments) {
                    if (!seg.isFiller() && !seg.clipId.isEmpty()) add(find(seg.sourceMobId(), seg.clipId));
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return out;
}

// Bin > Create Proxies (p.508): the window, then the proxies of the selected items' master clips.
int MainWindow::createProxies() {
    BinView* view = binContainer_->activeView();
    if (!view || !project_) return 0;
    QList<std::pair<QString, QString>> drives;
    for (const media::MediaDrive& d : media::mediaDrives()) drives.append({d.name, d.root});
    if (drives.isEmpty()) {
        ConsoleLog::instance().append(tr("Create Proxies: there is no drive to write proxies to."));
        return 0;
    }
    CreateProxiesDialog dialog(drives, this);
    if (dialog.exec() != QDialog::Accepted) return 0;
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
                    if (!r.error.isEmpty()) ConsoleLog::instance().append(tr("Create Proxies: %1").arg(r.error));
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
            ConsoleLog::instance().append(tr("Create Proxies: %1").arg(why));
            continue;
        }
        model::BinItem changed = clip;
        model::setProxy(changed, *proxy);
        storeClip(binId, changed);
        ++made;
    }
    return made;
}

} // namespace lumenis::ui

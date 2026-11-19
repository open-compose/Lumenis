// The offer to transcode long-GOP clips after linking (ours; a user's report, 2026-11-19; docs/spec/performance.md
// "Long-GOP media"). H.264, HEVC and MPEG-2 Long GOP store whole pictures only at keyframes, so they play and scrub
// less smoothly than intra-frame editing media, as Media Composer's guide also says of media it links. When files
// like that are linked and the Link setting's "Offer to transcode long-GOP clips after linking" is on, a message
// that does not block offers to transcode them in the background to the Media Creation setting's Transcode
// resolution. Each clip then takes its new media itself (media::ConsolidateOptions::clipsTakeMedia), so the clip,
// and any sequence already using it, plays the new media; the linked files stay where they are.
#include "lumenis/ui/main_window.h"

#include "lumenis/media/consolidate.h"
#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/project_store.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/settings.h"

#include <QCheckBox>
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>

namespace lumenis::ui {

void MainWindow::offerTranscode(const QString& binId, const QStringList& ids) {
    if (!settings_ || !project_ || !settings_->flag(QStringLiteral("Link"), QStringLiteral("offerTranscode"))) return;
    const BinDocument* doc = binContainer_->document(binId);
    if (!doc) return;
    QStringList clips;
    QStringList names;
    for (const QString& id : ids) {
        const model::BinItem* item = doc->bin().item(id);
        if (!item || item->kind != model::BinItemKind::MasterClip) continue;
        const auto link = media::linkOf(*item);
        if (!link || link->isManaged() || !media::isLongGopFile(link->file())) continue;
        clips << id;
        names << item->name;
    }
    if (clips.isEmpty()) return;
    const QString resolution = mediaCreation(QStringLiteral("transcode")).resolution.name;
    auto answer = [this, binId, clips](bool transcode, bool dontAskAgain) {
        if (dontAskAgain && settings_) settings_->setValue(QStringLiteral("Link"), QStringLiteral("offerTranscode"), false);
        if (transcode) transcodeLinkedClips(binId, clips);
    };
    if (transcodeOfferHook_) {
        const TranscodeOfferAnswer a = transcodeOfferHook_(names, resolution);
        answer(a.transcode, a.dontAskAgain);
        return;
    }
    // Not blocking: linking is done, and the message can wait while editing goes on.
    auto* box = new QMessageBox(QMessageBox::Question, tr("Transcode Long-GOP Clips"),
                                tr("%n of the clips just linked use long-GOP compression (as H.264 and HEVC do), which "
                                   "plays and scrubs less smoothly than editing media.\n\nTranscode them to %1 in the "
                                   "background? Each clip then plays its new media; the linked files stay where they are.",
                                   "", static_cast<int>(clips.size()))
                                    .arg(resolution),
                                QMessageBox::NoButton, this);
    box->setObjectName(QStringLiteral("transcodeOffer"));
    box->setAttribute(Qt::WA_DeleteOnClose);
    QPushButton* transcode = box->addButton(tr("Transcode"), QMessageBox::AcceptRole);
    box->addButton(tr("Not Now"), QMessageBox::RejectRole);
    box->setDefaultButton(transcode);
    box->setCheckBox(new QCheckBox(tr("Don't ask again"), box));
    connect(box, &QMessageBox::finished, this, [box, transcode, answer] {
        answer(box->clickedButton() == transcode, box->checkBox() && box->checkBox()->isChecked());
    });
    // Not modal either, so it never stands in for another dialog the user (or a test) is answering.
    box->setWindowModality(Qt::NonModal);
    box->show();
}

// One Background Queue job a clip, as Consolidate/Transcode in the background (p.356). When a job is done the clip in the
// bin takes the new media, keeping what was changed in the bin meanwhile (its name, colour, columns).
void MainWindow::transcodeLinkedClips(const QString& binId, const QStringList& ids) {
    const BinDocument* doc = binContainer_->document(binId);
    if (!doc || !project_) return;
    media::ConsolidateOptions options;
    options.mode = media::ConsolidateOptions::Mode::Transcode;
    options.creation = mediaCreation(QStringLiteral("transcode"));
    options.creation.when = QDateTime::currentDateTimeUtc();
    options.projectRate = project_->settings.format.editRate;
    options.acesWorkingSpace = model::isAcesColorSpace(project_->settings.colorSpace);
    options.clipsTakeMedia = true;
    for (const QString& id : ids) {
        const model::BinItem* found = doc->bin().item(id);
        if (!found) continue;
        const model::BinItem item = *found;
        backgroundQueue_->add(
            item.name, binId,
            [item, binId, options](const std::function<bool(double)>& progress) {
                media::ConsolidateOptions o = options;
                o.creation.progress = progress;
                const media::ClipLookup none = [](const QString&, const QString&) { return std::optional<model::BinItem>(); };
                return media::consolidate({item}, binId, none, {}, o);
            },
            BackgroundQueue::Priority::Normal,
            [this, binId, item](const media::ConsolidateResult& result) {
                media::ConsolidateResult merged = result;
                merged.changed.clear();
                const BinDocument* now = binContainer_->document(binId);
                for (const model::BinItem& made : result.changed) {
                    const model::BinItem* current = now ? now->bin().item(made.id) : nullptr;
                    if (!current) continue; // deleted while transcoding
                    model::BinItem clip = *current;
                    clip.link = made.link;
                    clip.sources = made.sources;
                    clip.mobId = made.mobId;
                    clip.modified = made.modified;
                    // The columns the transcode changed (Video, ...); the rest as the bin has them now.
                    for (auto it = made.fields.constBegin(); it != made.fields.constEnd(); ++it) {
                        if (item.fields.value(it.key()) != it.value()) clip.fields.insert(it.key(), it.value());
                    }
                    merged.changed << clip;
                }
                applyConsolidate(binId, merged);
            },
            tr("Transcode"));
    }
    showTool(QStringLiteral("BackgroundQueue"));
}

} // namespace lumenis::ui

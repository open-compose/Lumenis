// The Data track's two commands (11.5a; docs/spec/closed-captions.md "Data Mixdown" and "Extract DNxHD Data";
// Editing Guide p.974-975, p.982): Timeline > Mixdown > Data, which makes one D-track only master clip of the Record
// sequence's D1, and Clip > Extract DNxHD Data, which gives master clips a D1 track of the closed captions their media
// carries. A data track holds CEA-608 caption byte pairs, a pair a frame (model::clipCaptionData, closed_captions.h).
#include "lumenis/ui/main_window.h"

#include "lumenis/media/link.h"
#include "lumenis/media/media.h"
#include "lumenis/model/closed_captions.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAbstractButton>
#include <QFileInfo>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>

#include <algorithm>

namespace lumenis::ui {

namespace {

bool hasDataTrack(const model::BinItem& item) {
    return model::parseTrackList(item.fields.value(QStringLiteral("Tracks"))).contains(QStringLiteral("D1"));
}

} // namespace

std::optional<model::BinItem> MainWindow::dataMixdown() {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) {
        reportFailure(this, tr("Data Mixdown: load a sequence into the Record monitor first."));
        return std::nullopt;
    }
    const model::BinItem sequence = rec->item;
    const model::Sequence seq = model::Sequence::fromItem(sequence);
    const model::Track* dataTrack = seq.track(QStringLiteral("D1"));
    if (!dataTrack || std::all_of(dataTrack->segments.begin(), dataTrack->segments.end(), [](const model::Segment& s) { return s.isFiller(); })) {
        reportFailure(this, tr("Data Mixdown: the sequence has no clips on its Data track."));
        return std::nullopt;
    }
    // "Mark an In point and an Out point around the area to mix down, or clear the In and Out points to mixdown the
    // entire sequence" (p.982).
    const model::MonitorState& marks = composer_->state(MonitorId::Record);
    std::int64_t start = 0;
    std::int64_t end = seq.duration();
    if (marks.in() || marks.out()) {
        start = marks.markedStart();
        end = marks.markedEnd() + 1;
    }
    const QByteArray all = model::sequenceCaptionData(seq, [this](const model::Segment& s) { return captionClipData(s); });
    end = std::min<std::int64_t>(end, all.size() / 2);
    if (end <= start) {
        reportFailure(this, tr("Data Mixdown: there is nothing to mix down."));
        return std::nullopt;
    }
    // "Select a target bin ... for storing the new master clip" (p.982). Ours: the data stays with the clip in the bin,
    // as an imported caption clip's does, so there is no target drive to choose.
    QList<BinDocument*> open;
    for (BinDocument* doc : binContainer_->openDocuments()) {
        if (!doc->isReadOnly()) open << doc;
    }
    if (BinDocument* own = binContainer_->document(rec->binId); own && open.contains(own)) {
        open.removeAll(own);
        open.prepend(own);
    }
    if (open.isEmpty()) {
        reportFailure(this, tr("Data Mixdown: open a bin that can be changed."));
        return std::nullopt;
    }
    QStringList names;
    for (BinDocument* doc : open) names << doc->name();
    int index = -1;
    if (binChoiceHook_) {
        index = binChoiceHook_(names);
    } else {
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Data Mixdown"), tr("Target bin:"), names, 0, false, &ok);
        if (ok) index = static_cast<int>(names.indexOf(chosen));
    }
    if (index < 0 || index >= open.size()) return std::nullopt;
    BinDocument* doc = open.at(index);

    // "Your end result is a new master clip made up of all the data clips on the data track. That track becomes one
    // clip" (p.982).
    model::BinItem clip;
    clip.id = model::newItemId();
    clip.mobId = MobId::generate();
    clip.kind = model::BinItemKind::MasterClip;
    clip.name = tr("%1 Data Mixdown").arg(sequence.name);
    clip.created = clip.modified = QDateTime::currentDateTimeUtc();
    clip.editRate = sequence.editRate;
    clip.durationFrames = end - start;
    model::setClipCaptionData(clip, all.mid(static_cast<qsizetype>(2 * start), static_cast<qsizetype>(2 * (end - start))));
    clip.fields.insert(QStringLiteral("Tracks"), QStringLiteral("D1"));
    const TimecodeFormat format = TimecodeFormat::forRate(sequence.editRate, model::isDropFrame(sequence));
    clip.fields.insert(QStringLiteral("Start"),
                       QString::fromStdString(Timecode(model::startFrameOf(sequence) + start, format).toString()));
    doc->addItem(clip);
    if (BinView* view = binContainer_->viewFor(doc)) view->selectIds({clip.id});
    // Console only: what was made.
    ConsoleLog::instance().append(tr("Data Mixdown: %1 in %2").arg(clip.name, doc->name()));
    return clip;
}

int MainWindow::extractDnxhdData() {
    BinView* view = binContainer_->activeView();
    if (!view || view->selectedIds().isEmpty()) {
        reportFailure(this, tr("Extract DNxHD Data: select master clips in a bin first."));
        return 0;
    }
    BinDocument& doc = view->document();
    QList<model::BinItem> clips;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* item = doc.bin().item(id)) clips << *item;
    }
    // "The clip must be a master clip; the clip should not have a Data track associated with it already; ... the
    // media needs to be online" (p.974-975). "The media needs to be DNxHD format": ours reads the closed captions any
    // media carries as A/53 caption data (MPEG-2, H.264), since how the Legacy method embedded data in DNxHD media is
    // not published; DNxHD media without such data is refused like any other.
    QStringList problems;
    QList<model::BinItem> usable;
    for (const model::BinItem& clip : clips) {
        const auto link = media::linkOf(clip);
        if (clip.kind != model::BinItemKind::MasterClip) {
            problems << tr("%1 is not a master clip.").arg(clip.name);
        } else if (hasDataTrack(clip)) {
            problems << tr("%1 has a Data track already.").arg(clip.name);
        } else if (!link || link->videoStream < 0 || !QFileInfo::exists(link->path)) {
            problems << tr("%1 is offline or has no picture.").arg(clip.name);
        } else {
            usable << clip;
        }
    }
    if (usable.isEmpty()) {
        reportFailure(this, tr("Extract DNxHD Data: %1").arg(problems.join(QLatin1Char(' '))));
        return 0;
    }
    // "The Confirm dialog box appears ... Click Continue or Cancel" (p.975).
    QStringList names;
    for (const model::BinItem& clip : usable) names << clip.name;
    bool go = false;
    if (extractDataHook_) {
        go = extractDataHook_(names);
    } else {
        QMessageBox box(QMessageBox::Question, tr("Confirm"),
                        tr("Extract the ancillary data of %n clip(s) to a Data track?", "", static_cast<int>(names.size())),
                        QMessageBox::NoButton, this);
        QPushButton* cont = box.addButton(tr("Continue"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        go = box.clickedButton() == static_cast<QAbstractButton*>(cont);
    }
    if (!go) return 0;
    int changed = 0;
    QProgressDialog progress(tr("Extracting ancillary data..."), tr("Cancel"), 0, static_cast<int>(usable.size()), this);
    progress.setMinimumDuration(500);
    for (qsizetype i = 0; i < usable.size(); ++i) {
        progress.setValue(static_cast<int>(i));
        if (progress.wasCanceled()) break;
        model::BinItem clip = usable.at(i);
        const auto link = media::linkOf(clip);
        media::VideoReader reader;
        QString error;
        if (!reader.open(link->path, link->videoStream, &error)) {
            problems << tr("%1: %2").arg(clip.name, error);
            continue;
        }
        // A pair a frame; frames without one send nothing (0x80 0x80, as an imported caption clip has).
        QByteArray pairs;
        pairs.reserve(static_cast<qsizetype>(2 * clip.durationFrames));
        bool any = false;
        for (std::int64_t f = 0; f < clip.durationFrames; ++f) {
            const QByteArray pair = reader.captionPair(link->fileFrame(f, clip.editRate));
            if (pair.size() == 2) {
                pairs += pair;
                any = true;
            } else {
                pairs += QByteArray("\x80\x80", 2);
            }
        }
        if (!any) {
            problems << tr("%1 carries no ancillary data.").arg(clip.name);
            continue;
        }
        model::setClipCaptionData(clip, pairs);
        // "The clip in the bin displays D1 in the Track column" (p.975).
        clip.fields.insert(QStringLiteral("Tracks"), (clip.fields.value(QStringLiteral("Tracks")) + QStringLiteral(" D1")).trimmed());
        clip.modified = QDateTime::currentDateTimeUtc();
        doc.replaceItem(clip);
        composer_->updateItem(clip);
        ++changed;
    }
    progress.setValue(static_cast<int>(usable.size()));
    if (!problems.isEmpty()) reportFailure(this, tr("Extract DNxHD Data: %1").arg(problems.join(QLatin1Char(' '))));
    return changed;
}

} // namespace lumenis::ui

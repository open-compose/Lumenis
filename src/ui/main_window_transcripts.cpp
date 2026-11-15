// MainWindow's transcription (8.2a; Editing Guide p.446-450, docs/spec/transcripts.md): which bins are transcribed, the
// engine, and master clips transcribed on the Background Queue while the user is not editing.
#include "lumenis/ui/main_window.h"

#include "lumenis/ext/provider.h"
#include "lumenis/ext/transcription.h"
#include "lumenis/media/transcription.h"
#include "lumenis/model/bin_lock.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/transcript_tool.h"
#include "lumenis/ui/speaker_names_dialog.h"
#include "lumenis/ui/find_window.h"
#include "lumenis/ui/transcript_dialogs.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/model/transcript_export.h"
#include "lumenis/model/subcap.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/panels.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/transcript_view.h"
#include "lumenis/core/timecode.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QPlainTextEdit>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace lumenis::ui {

namespace {

constexpr int kIdleMs = 10'000; // "After ten seconds of inactivity, the transcription process will continue" (p.448)
constexpr int kTickMs = 2'000;

// Notes the last key press, click or wheel turn anywhere in the application.
class ActivityWatcher : public QObject {
public:
    explicit ActivityWatcher(QObject* parent) : QObject(parent) { since_.start(); }
    qint64 idleMs() const { return since_.elapsed(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel: since_.restart(); break;
        default: break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QElapsedTimer since_;
};

// A clip PhraseFind indexes: "only master clips are transcribed" (p.448), those with sound (audio-only master clips,
// BinItemKind::AudioClip, too).
bool transcribable(const model::BinItem& item) {
    const bool master = item.kind == model::BinItemKind::MasterClip || item.kind == model::BinItemKind::AudioClip;
    if (!master || item.link.isEmpty()) return false;
    for (const QString& t : model::sourceTracksOf(item)) {
        if (t.startsWith(QLatin1Char('A'))) return true;
    }
    return false;
}

} // namespace

void MainWindow::setTranscriber(std::shared_ptr<ext::TranscriptionProvider> provider) {
    transcriber_ = std::move(provider);
}

std::shared_ptr<ext::TranscriptionProvider> MainWindow::transcriber() const {
    if (transcriber_) return transcriber_;
    const auto usable = providers_.usable<ext::TranscriptionProvider>();
    return usable.empty() ? nullptr : usable.front();
}

bool MainWindow::isBinTranscribed(const QString& binId) const {
    return transcriptionBins_ && transcriptionBins_->isIncluded(binId);
}

void MainWindow::setUpTranscription(bool projectOpen) {
    if (!projectOpen || !project_) {
        transcriptionBins_.reset();
        binContainer_->setTranscriptionStatus({});
        if (transcriptionTimer_) transcriptionTimer_->stop();
        return;
    }
    transcriptionBins_.emplace(project_->folder);
    binContainer_->setTranscriptionStatus([this](const QString& id) { return isBinTranscribed(id); });
    if (!activityWatcher_) { // once per window
        activityWatcher_ = new ActivityWatcher(this);
        qApp->installEventFilter(activityWatcher_);
        // "If this setting is unchecked, all new bins will be excluded from transcription" (p.447).
        connect(binContainer_, &BinContainer::binCreated, this, [this](const QString& id) {
            if (transcriptionBins_ && !settings_->flag(QStringLiteral("Transcript"), QStringLiteral("includeNewBins"))) {
                transcriptionBins_->setIncluded(id, false);
            }
        });
    }
    if (!transcriptionTimer_) {
        transcriptionTimer_ = new QTimer(this);
        transcriptionTimer_->setInterval(kTickMs);
        connect(transcriptionTimer_, &QTimer::timeout, this, &MainWindow::transcriptionTick);
    }
    transcriptionTimer_->start();
}

void MainWindow::transcriptionTick() {
    if (!project_ || backgroundQueue_->isBusy() || !transcriptionIndexing_) return;
    // "When this option is selected (the default), the transcription process will be temporarily stopped while
    // editing" (p.447). Ours: new clips start only after ten idle seconds; a clip being transcribed finishes.
    const auto* watcher = static_cast<const ActivityWatcher*>(activityWatcher_);
    if (settings_->flag(QStringLiteral("Transcript"), QStringLiteral("pauseWhileEditing")) && watcher && watcher->idleMs() < kIdleMs) return;
    queueTranscriptions();
}

namespace {

// Every master clip with sound in the project's bins, with its bin (open bins as they are now), in `included` bins only
// when that is given.
QList<std::pair<QString, model::BinItem>> projectClips(const BinContainer& container, const model::TranscriptionBins* included) {
    QList<std::pair<QString, model::BinItem>> out;
    const model::ProjectBins* bins = container.bins();
    if (!bins) return out;
    for (const model::BinNode* node : bins->bins()) {
        if ((included && !included->isIncluded(node->id)) || bins->isInTrash(node->id)) continue;
        QList<model::BinItem> items;
        if (const BinDocument* doc = container.document(node->id)) {
            items = doc->bin().items;
        } else if (const auto bin = model::BinFile::read(bins->binPath(node->id))) {
            items = bin->items;
        }
        for (const model::BinItem& item : items) {
            if (transcribable(item)) out.append({node->id, item});
        }
    }
    return out;
}

} // namespace

bool MainWindow::queueTranscription(const QString& binId, const model::BinItem& item, bool now) {
    const auto provider = transcriber();
    const QString mob = QString::fromStdString(model::mobIdOf(item).toString());
    if (!provider || transcribing_.contains(mob)) return false;
    // "allowing multiple stations to process media transcriptions simultaneously" (p.452): an entry another station
    // holds is theirs to write; this station holds the lock while it transcribes.
    const model::TranscriptStore lockStore;
    if (!lockStore.lock(mob)) return false;
    transcribing_.insert(mob);
    ext::TranscriptionOptions options;
    // The Language Hint (p.448): empty is Multilingual, the engine detects the language.
    options.language = settings_->text(QStringLiteral("Transcript"), QStringLiteral("languageHint")).toStdString();
    const QString folder = model::TranscriptStore().folder();
    auto work = [item, provider, options, folder](const std::function<bool(double)>& progress) {
        media::ConsolidateResult result;
        QString error;
        const auto t = media::transcribeClip(item, *provider, options, progress, &error);
        if (!t) {
            result.error = error;
        } else if (model::TranscriptLock held; !model::TranscriptStore(folder).save(*t, &held)) {
            result.error = held.station.isEmpty()
                               ? QObject::tr("The transcript of \"%1\" could not be saved in %2.").arg(item.name, folder)
                               : QObject::tr("%1 took over the transcript of \"%2\"; it was not saved here.").arg(held.station, item.name);
        }
        return result;
    };
    auto done = [this, mob, folder, name = item.name](const media::ConsolidateResult& result) {
        transcribing_.remove(mob);
        model::TranscriptStore(folder).unlock(mob);
        if (!result.error.isEmpty() && console_) {
            console_->log()->appendPlainText(tr("Transcribing \"%1\": %2").arg(name, result.error));
        }
        // A new transcript: the Transcript Tool shows it.
        ++transcriptGeneration_;
        refreshTranscriptTool();
    };
    // "Foreground transcriptions will always pause the background transcription" (p.448). Ours: clips asked for go at
    // High priority, ahead of the Low background jobs; a background clip already running finishes first.
    backgroundQueue_->add(tr("Transcribe %1").arg(item.name), binId, work,
                          now ? BackgroundQueue::Priority::High : BackgroundQueue::Priority::Low, done, tr("Transcription"));
    return true;
}

int MainWindow::queueTranscriptions() {
    if (!project_ || !binContainer_->bins() || !transcriber() || !transcriptionBins_) return 0;
    const model::TranscriptStore store;
    int queued = 0;
    for (const auto& [binId, item] : projectClips(*binContainer_, &*transcriptionBins_)) {
        if (store.has(QString::fromStdString(model::mobIdOf(item).toString()))) continue;
        if (queueTranscription(binId, item, false)) ++queued;
    }
    return queued;
}

int MainWindow::transcribeNow(const QStringList& mobs) {
    if (!project_ || mobs.isEmpty() || !transcriber()) return 0;
    // Any bin, included in transcription or not: the user asked for these clips.
    QSet<QString> wanted(mobs.cbegin(), mobs.cend());
    int queued = 0;
    for (const auto& [binId, item] : projectClips(*binContainer_, nullptr)) {
        if (wanted.remove(QString::fromStdString(model::mobIdOf(item).toString())) && queueTranscription(binId, item, true)) {
            ++queued;
        }
    }
    return queued;
}

void MainWindow::setBinsTranscribed(bool included) {
    if (!transcriptionBins_ || !binContainer_->bins()) return;
    QStringList ids;
    for (const QString& id : binContainer_->selectedNodes()) {
        const model::BinNode* n = binContainer_->bins()->node(id);
        if (n && n->kind == model::BinNode::Kind::Bin) ids << id;
    }
    if (ids.isEmpty()) {
        if (const BinDocument* doc = binContainer_->activeDocument()) ids << doc->nodeId();
    }
    for (const QString& id : ids) transcriptionBins_->setIncluded(id, included);
    binContainer_->refreshTree();
}

} // namespace lumenis::ui

namespace lumenis::ui {

void MainWindow::setUpTranscriptTool() {
    transcriptTool_ = new TranscriptTool;
    panels_->addPanel(new Panel(QStringLiteral("Transcript"), tr("Transcript"), transcriptTool_));
    connect(composer_, &Composer::changed, this, &MainWindow::refreshTranscriptTool);
    // Clicks and selections move the monitor's position and set its marks (p.442).
    auto monitor = [this] { return transcriptSourceSide_ ? MonitorId::Source : MonitorId::Record; };
    connect(transcriptTool_, &TranscriptTool::positionChosen, this, [this, monitor](std::int64_t frame) {
        composer_->moveTo(monitor(), frame);
    });
    connect(transcriptTool_, &TranscriptTool::marksChosen, this, [this, monitor](std::int64_t in, std::int64_t out) {
        model::MonitorState& s = composer_->state(monitor());
        if (!s.current()) return;
        s.clearBoth();
        s.setPosition(out);
        s.markOut();
        s.setPosition(in);
        s.markIn();
        composer_->moveTo(monitor(), in);
    });
    connect(transcriptTool_, &TranscriptTool::sideChosen, this, [this](bool source) {
        transcriptSourceSide_ = source;
        transcriptKey_.clear();
        refreshTranscriptTool();
    });
    connect(transcriptTool_, &TranscriptTool::tracksChosen, this, [this](const QStringList& tracks) {
        transcriptTracks_ = tracks;
        transcriptKey_.clear();
        refreshTranscriptTool();
    });
    connect(transcriptTool_, &TranscriptTool::transcribeRequested, this, [this](const QStringList& mobs) {
        if (transcribeNow(mobs) == 0 && console_) {
            console_->log()->appendPlainText(transcriber() ? tr("Nothing to transcribe.")
                                                           : tr("No transcription engine: whisper.cpp's whisper-cli was not found."));
        }
    });
    // Edits from the tool (p.444-446): applied to the clip's transcript in the store, the tool shown again.
    connect(transcriptTool_, &TranscriptTool::speakerAngleChosen, this,
            [this](const QString& speaker, int angle) { assignSpeakerAngle(speaker, angle); });
    connect(transcriptTool_, &TranscriptTool::deleteRequested, this, [this](const QStringList& mobs) { deleteTranscripts(mobs); });
    connect(transcriptTool_, &TranscriptTool::editRequested, this,
            [this](const QString& mob, const TranscriptTool::TranscriptEdit& edit) { editTranscript(mob, edit); });
    // "Choosing the Set Font option in the fast menu opens a window to select a Font Family and Size" (p.443).
    connect(transcriptTool_, &TranscriptTool::fontRequested, this, [this] {
        bool ok = false;
        const QFont font = QFontDialog::getFont(&ok, transcriptTool_->view()->font(), this, tr("Set Font"));
        if (ok) transcriptTool_->setTextFont(font);
    });
}

void MainWindow::refreshTranscriptTool() {
    if (!transcriptTool_) return;
    // "the Transcript Tool will update each time a clip is loaded into the Source monitor, or a sequence is loaded into
    // the Record monitor" (p.442): the side follows the last load.
    const model::LoadedClip* source = composer_->state(MonitorId::Source).current();
    const model::LoadedClip* record = composer_->state(MonitorId::Record).current();
    const QString sourceId = source ? source->binId + QLatin1Char('/') + source->item.id : QString();
    const QString recordId = record ? record->binId + QLatin1Char('/') + record->item.id : QString();
    if (sourceId != lastSourceLoaded_) {
        lastSourceLoaded_ = sourceId;
        if (source) transcriptSourceSide_ = true;
    }
    if (recordId != lastRecordLoaded_) {
        lastRecordLoaded_ = recordId;
        if (record) transcriptSourceSide_ = false;
    }
    transcriptTool_->setSourceSide(transcriptSourceSide_);
    const MonitorId monitor = transcriptSourceSide_ ? MonitorId::Source : MonitorId::Record;
    const model::MonitorState& state = composer_->state(monitor);
    const model::LoadedClip* loaded = state.current();
    if (!loaded) {
        const QString key = transcriptSourceSide_ ? QStringLiteral("empty:source") : QStringLiteral("empty:record");
        if (key != transcriptKey_) {
            transcriptKey_ = key;
            transcriptTool_->clear(transcriptSourceSide_ ? tr("Load a clip into the Source monitor.")
                                                         : tr("Load a sequence into the Record monitor."));
        }
        return;
    }
    const model::BinItem& item = loaded->item;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                            .arg(transcriptSourceSide_)
                            .arg(loaded->binId, item.id)
                            .arg(item.modified.toMSecsSinceEpoch())
                            .arg(item.durationFrames)
                            .arg(transcriptTracks_.join(QLatin1Char(',')))
                            .arg(transcriptGeneration_);
    if (key != transcriptKey_) {
        transcriptKey_ = key;
        const model::ShownTranscript shown = shownTranscriptOf(item, transcriptTracks_);
        const TimecodeFormat format = TimecodeFormat::forRate(item.editRate, model::isDropFrame(item));
        const std::int64_t start = state.startFrame();
        transcriptTool_->setFrameRate(item.editRate.toDouble());
        transcriptTool_->setKnownSpeakers(projectSpeakerNames());
        // A group in the Source monitor: its angles for Assign to Camera Angle (2026.8 guide p.451).
        const auto group = transcriptSourceSide_ && !model::isMultigroup(item) ? model::groupOf(item) : std::nullopt;
        QStringList angles;
        if (group) {
            for (const model::GroupAngle& a : group->angles) angles << a.clip.name;
        }
        transcriptTool_->setCameraAngles(angles, group ? group->speakerAngles : QMap<QString, int>());
        transcriptTool_->setTranscript(shown, [format, start](std::int64_t frame) {
            return QString::fromStdString(Timecode(start + frame, format).toString());
        });
    }
    transcriptTool_->setPosition(state.position());
    transcriptTool_->setMarks(state.in(), state.out());
}

model::ShownTranscript MainWindow::shownTranscriptOf(const model::BinItem& item, const QStringList& tracks) const {
    const model::TranscriptStore store;
    const model::TranscriptFor transcriptFor = [store](const QString& mob) { return store.load(mob); };
    const model::ShownClipLookup lookup = [this](const QString& binId, const QString& clipId) { return composer_->resolve(binId, clipId); };
    if (item.kind == model::BinItemKind::Sequence) return model::shownTranscript(model::Sequence::fromItem(item), tracks, transcriptFor, lookup);
    if (const auto seq = model::multigroupSequence(item)) return model::shownTranscript(model::Sequence::fromItem(*seq), tracks, transcriptFor, lookup);
    return model::shownTranscript(item, transcriptFor);
}

bool MainWindow::editTranscript(const QString& mob, const std::function<bool(model::Transcript&)>& edit) {
    const model::TranscriptStore store;
    auto t = store.load(mob);
    if (!t || !edit) return false;
    // "Transcript entries lock when users are writing or editing them" (p.452).
    if (!store.lock(mob) && !takeOverTranscriptLock(store, mob, t->clipName)) return false;
    t = store.load(mob); // as it is now that this station holds it
    const bool changed = t && edit(*t);
    const bool saved = changed && store.save(*t);
    store.unlock(mob);
    if (!changed) return false;
    if (!saved) {
        if (console_) console_->log()->appendPlainText(tr("The transcript of \"%1\" could not be saved.").arg(t->clipName));
        return false;
    }
    ++transcriptGeneration_;
    refreshTranscriptTool();
    return true;
}

// "If a conflict occurs, users are prompted and can choose to abort on the other station and obtain the lock. Use caution
// when overriding locks—only do so if other stations are offline and not actively writing" (p.452).
bool MainWindow::takeOverTranscriptLock(const model::TranscriptStore& store, const QString& mob, const QString& clipName) {
    const auto held = store.lockOf(mob);
    if (!held) return store.lock(mob);
    bool take = false;
    if (transcriptTakeOver_) {
        take = transcriptTakeOver_(clipName, *held);
    } else {
        take = QMessageBox::question(this, tr("Transcript Locked"),
                                     tr("The transcript of \"%1\" is locked by %2, who is writing or editing it. Abort it "
                                        "there and obtain the lock?\n\nOnly do so if that station is offline and not "
                                        "actively writing.")
                                         .arg(clipName, held->station),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }
    return take && store.lock(mob, true);
}

bool MainWindow::saveTranscript(const model::Transcript& transcript) {
    const model::TranscriptStore store;
    if (store.save(transcript)) return true;
    if (!store.lockOf(transcript.mob) || !takeOverTranscriptLock(store, transcript.mob, transcript.clipName)) return false;
    const bool ok = store.save(transcript);
    store.unlock(transcript.mob);
    return ok;
}

void MainWindow::applyTranscriptSharing() {
    const QString mode = settings_->text(QStringLiteral("Transcript"), QStringLiteral("sharing"));
    const QString folder = QDir::cleanPath(settings_->text(QStringLiteral("Transcript"), QStringLiteral("sharedFolder")));
    model::TranscriptSharing sharing = mode == QLatin1String("host")  ? model::TranscriptSharing::Host
                                       : mode == QLatin1String("use") ? model::TranscriptSharing::Use
                                                                      : model::TranscriptSharing::Off;
    auto say = [this](const QString& text) {
        if (console_) console_->log()->appendPlainText(text);
    };
    if (sharing != model::TranscriptSharing::Off) {
        // "Access to this functionality requires Avid NEXIS to be enabled" (p.451); ours: any shared storage.
        if (settings_->text(QStringLiteral("Transcript"), QStringLiteral("sharedFolder")).trimmed().isEmpty()) {
            say(tr("Transcript Sharing: set the Shared Transcripts Folder in the Transcript setting."));
            sharing = model::TranscriptSharing::Off;
        } else if (!model::isSharedLocation(folder)) {
            say(tr("Transcript Sharing: %1 is not on shared storage; transcripts stay on this workstation.")
                    .arg(QDir::toNativeSeparators(folder)));
            sharing = model::TranscriptSharing::Off;
        }
    }
    const QString shownFolder = sharing == model::TranscriptSharing::Off ? QString() : folder;
    if (sharing == appliedSharing_ && shownFolder == appliedSharedFolder_) return;
    if (sharing == model::TranscriptSharing::Host) {
        const int copied = model::hostTranscripts(model::TranscriptStore::localFolder(), folder);
        if (copied < 0) {
            say(tr("Host Shared Transcripts: %1 cannot be written; transcripts stay on this workstation.")
                    .arg(QDir::toNativeSeparators(folder)));
            sharing = model::TranscriptSharing::Off;
        } else {
            say(tr("Host Shared Transcripts: %n transcript(s) of this workstation added to %1.", nullptr, copied)
                    .arg(QDir::toNativeSeparators(folder)));
        }
    }
    model::setTranscriptSharing(sharing, sharing == model::TranscriptSharing::Off ? QString() : folder);
    appliedSharing_ = sharing;
    appliedSharedFolder_ = sharing == model::TranscriptSharing::Off ? QString() : folder;
    // "if the host is offline or the search database is stopped, transcripts will not be available" (p.452).
    if (sharing == model::TranscriptSharing::Use && !model::sharedTranscriptsReachable()) {
        say(tr("Use Shared Transcripts: the shared transcripts at %1 cannot be reached; transcripts are not available "
               "until the host is back.")
                .arg(QDir::toNativeSeparators(folder)));
    }
    ++transcriptGeneration_;
    refreshTranscriptTool();
}

namespace {

// The MobIDs of the project's master clips with a transcript in the store ("all transcripts for this project", p.445).
QStringList projectTranscriptMobs(const BinContainer& container, const model::TranscriptStore& store) {
    QStringList out;
    for (const auto& [binId, item] : projectClips(container, nullptr)) {
        const QString mob = QString::fromStdString(model::mobIdOf(item).toString());
        if (!out.contains(mob) && store.has(mob)) out << mob;
    }
    return out;
}

} // namespace

QStringList MainWindow::projectSpeakerNames(QStringList* used) const {
    QStringList names;
    QStringList inTranscripts;
    if (project_) {
        names = model::ProjectSpeakers(project_->folder).names();
        const model::TranscriptStore store;
        for (const QString& mob : projectTranscriptMobs(*binContainer_, store)) {
            if (const auto t = store.load(mob)) inTranscripts += model::speakerNames(*t);
        }
    }
    inTranscripts.removeDuplicates();
    inTranscripts.sort(Qt::CaseInsensitive);
    names += inTranscripts;
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    if (used) *used = inTranscripts;
    return names;
}

SpeakerNamesDialog* MainWindow::manageSpeakers() {
    if (!project_) return nullptr;
    // Over the Transcript setting's dialog, which is modal: a window of the modal one is not blocked by it.
    QWidget* over = QApplication::activeModalWidget();
    auto* dialog = new SpeakerNamesDialog(over ? over : this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto refresh = [this, dialog] {
        QStringList used;
        const QStringList names = projectSpeakerNames(&used);
        dialog->setNames(names, used);
    };
    connect(dialog, &SpeakerNamesDialog::changeRequested, this, [this, refresh](const SpeakerNameChange& change) {
        applySpeakerNameChange(change);
        refresh();
    });
    refresh();
    dialog->open();
    return dialog;
}

void MainWindow::applySpeakerNameChange(const SpeakerNameChange& change) {
    if (!project_) return;
    model::ProjectSpeakers list(project_->folder);
    using Kind = SpeakerNameChange::Kind;
    switch (change.kind) {
    case Kind::Add: list.add(change.to); return; // only the list: no transcript has it yet
    case Kind::Delete:
        for (const QString& n : change.names) list.remove(n);
        return;
    case Kind::Rename:
        if (!change.names.isEmpty()) list.rename(change.names.first(), change.to);
        break;
    case Kind::Merge:
        // The merged names leave the list for the one they become (listed when any of them was).
        if (std::any_of(change.names.cbegin(), change.names.cend(), [&](const QString& n) { return list.names().contains(n); })) {
            for (const QString& n : change.names) {
                if (n != change.to) list.remove(n);
            }
            list.add(change.to);
        }
        break;
    case Kind::Clear: break; // "remove that name from all transcripts in this project"; the list keeps it (dimmed)
    }
    const model::TranscriptStore store;
    bool changed = false;
    for (const QString& mob : projectTranscriptMobs(*binContainer_, store)) {
        auto t = store.load(mob);
        if (!t) continue;
        bool did = false;
        switch (change.kind) {
        case Kind::Rename: did = !change.names.isEmpty() && model::renameSpeakerName(*t, change.names.first(), change.to); break;
        case Kind::Merge: did = model::mergeSpeakerNames(*t, change.names, change.to); break;
        case Kind::Clear:
            for (const QString& n : change.names) did = model::clearSpeakerName(*t, n) || did;
            break;
        default: break;
        }
        if (!did) continue;
        if (saveTranscript(*t)) {
            changed = true;
        } else if (console_) {
            console_->log()->appendPlainText(tr("The transcript of \"%1\" could not be saved.").arg(t->clipName));
        }
    }
    if (changed) {
        ++transcriptGeneration_;
        refreshTranscriptTool();
    }
}

std::pair<int, int> MainWindow::phraseIndexStatus() const {
    if (!project_ || !transcriptionBins_) return {0, 0};
    const model::TranscriptStore store;
    QSet<QString> all;
    QSet<QString> done;
    for (const auto& [binId, item] : projectClips(*binContainer_, &*transcriptionBins_)) {
        const QString mob = QString::fromStdString(model::mobIdOf(item).toString());
        all.insert(mob);
        if (store.has(mob)) done.insert(mob);
    }
    return {static_cast<int>(done.size()), static_cast<int>(all.size())};
}

void MainWindow::connectPhraseFind(FindWindow& window) {
    FindWindow::PhraseFindSource source;
    source.transcriptFor = [](const QString& mob) { return model::TranscriptStore().load(mob); };
    source.indexStatus = [this] { return phraseIndexStatus(); };
    source.indexing = [this] { return transcriptionIndexing_; };
    source.setIndexing = [this](bool on) { setTranscriptionIndexing(on); };
    source.manageTranscripts = [this] { manageTranscripts(); };
    window.setPhraseFind(std::move(source));
}

// ---------------------------------------------------------------- Transcripts out (8.2d)

namespace {

// A name usable as a file name.
QString fileNameOf(const QString& name) {
    QString out = name;
    for (QChar& c : out) {
        if (QStringLiteral("\\/:*?\"<>|").contains(c)) c = QLatin1Char('_');
    }
    return out.trimmed().isEmpty() ? QStringLiteral("Transcript") : out.trimmed();
}

QStringList audioTracksOf(const model::BinItem& item) {
    QStringList out;
    std::optional<model::BinItem> seqItem;
    if (item.kind == model::BinItemKind::Sequence) seqItem = item;
    else seqItem = model::multigroupSequence(item);
    if (!seqItem) return out;
    const model::Sequence seq = model::Sequence::fromItem(*seqItem);
    for (const model::Track& t : seq.tracks()) {
        if (t.kind == model::TrackKind::Audio) out << t.name();
    }
    return out;
}

bool hasSequence(const model::BinItem& item) {
    return item.kind == model::BinItemKind::Sequence || model::multigroupSequence(item).has_value();
}

// The MobIDs of the transcripts a clip (not a sequence) names: its master clip's, a group's selected angle's.
QStringList clipMobs(const model::BinItem& item) {
    if (const auto g = model::groupOf(item); g && !model::isMultigroup(item)) {
        if (g->selected >= 0 && g->selected < g->angles.size()) {
            if (const auto s = model::transcriptSource(g->angles[g->selected].clip)) return {s->first};
        }
        return {};
    }
    if (const auto s = model::transcriptSource(item)) return {s->first};
    return {};
}

} // namespace

QString MainWindow::transcriptPath(const QString& kind, const QString& suggested, const QString& filter) {
    if (transcriptPathHook_) return transcriptPathHook_(kind, suggested);
    if (kind == QLatin1String("folder")) return QFileDialog::getExistingDirectory(this, tr("Export Transcript"));
    if (kind == QLatin1String("open")) return QFileDialog::getOpenFileName(this, tr("Import Transcripts"), QString(), filter);
    return QFileDialog::getSaveFileName(this, tr("Export Transcript"), suggested, filter);
}

bool MainWindow::chooseTranscribeTracks(TranscribeTracksDialog& dialog) {
    return transcribeTracksHook_ ? transcribeTracksHook_(dialog) : dialog.exec() == QDialog::Accepted;
}

// "Right-click on one or more clips in a bin and choose Transcript > Export Transcript ... select a location to save the
// transcript file" (p.450); for a sequence, "the Transcribe window opens ... select the audio tracks" first (p.451).
// Ours: several items go into a folder, a file each named after it.
int MainWindow::exportTranscripts() {
    BinView* view = binContainer_->activeView();
    if (!view) return 0;
    QList<model::BinItem> items;
    for (const QString& id : view->selectedIds()) {
        if (const model::BinItem* item = view->document().bin().item(id)) items << *item;
    }
    if (items.isEmpty()) return 0;
    QString folder;
    QString single;
    if (items.size() == 1) {
        single = transcriptPath(QStringLiteral("save"), fileNameOf(items.first().name) + QStringLiteral(".txt"), tr("Text (*.txt)"));
        if (single.isEmpty()) return 0;
    } else {
        folder = transcriptPath(QStringLiteral("folder"), QString(), QString());
        if (folder.isEmpty()) return 0;
    }
    int written = 0;
    for (const model::BinItem& item : items) {
        QStringList tracks;
        if (hasSequence(item)) {
            TranscribeTracksDialog dialog(audioTracksOf(item), false, this);
            if (!chooseTranscribeTracks(dialog)) continue;
            tracks = dialog.tracks();
            if (tracks.isEmpty()) continue;
        }
        const model::ShownTranscript shown = shownTranscriptOf(item, tracks);
        if (shown.lines.isEmpty()) {
            reportFailure(this, tr("Export Transcript: \"%1\" has no transcript.").arg(item.name));
            continue;
        }
        const QString text = model::exportedTranscriptText(shown, model::timecodeFormatOf(item), model::startFrameOf(item));
        const QString path = single.isEmpty() ? QDir(folder).filePath(fileNameOf(item.name) + QStringLiteral(".txt")) : single;
        QSaveFile file(path);
        const QByteArray bytes = text.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            reportFailure(this, tr("Export Transcript: %1 cannot be written.").arg(QDir::toNativeSeparators(path)));
            continue;
        }
        ++written;
    }
    return written;
}

// "Right-click on a clip or sequence in a bin and choose Transcript > Share Transcripts ... Multiple clips or sequences
// can be shared at once" (p.451): every transcript they show, with the project's Language Hint.
bool MainWindow::shareTranscripts() {
    BinView* view = binContainer_->activeView();
    if (!view) return false;
    const model::TranscriptStore store;
    model::SharedTranscripts shared;
    shared.languageHint = settings_->value(QStringLiteral("Transcript"), QStringLiteral("languageHint")).toString();
    QStringList mobs;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* item = view->document().bin().item(id);
        if (!item) continue;
        for (const QString& mob : shownTranscriptOf(*item, {}).speakers.keys()) {
            if (!mobs.contains(mob)) mobs << mob;
        }
    }
    for (const QString& mob : mobs) {
        if (auto t = store.load(mob)) shared.transcripts << *t;
    }
    if (shared.transcripts.isEmpty()) {
        reportFailure(this, tr("Share Transcripts: the selection has no transcripts."));
        return false;
    }
    const QString suffix = QString::fromLatin1(model::kSharedTranscriptsSuffix);
    const QString path = transcriptPath(QStringLiteral("save"), QStringLiteral("Transcripts.") + suffix,
                                        tr("Shared transcripts (*.%1)").arg(suffix));
    if (path.isEmpty()) return false;
    QSaveFile file(path);
    const QByteArray bytes = model::writeSharedTranscripts(shared);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        reportFailure(this, tr("Share Transcripts: %1 cannot be written.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return true;
}

// "Right-click on a clip in a bin. Choose 'Transcript > Delete Transcript' ... The transcript for the selected clip is
// immediately deleted" (2026.8 guide p.456). With `mobs`, those (the Transcript Tool's and Manage Transcripts').
int MainWindow::deleteTranscripts(const QStringList& mobs) {
    QStringList targets = mobs;
    if (targets.isEmpty()) {
        BinView* view = binContainer_->activeView();
        if (!view) return 0;
        for (const QString& id : view->selectedIds()) {
            if (const model::BinItem* item = view->document().bin().item(id)) {
                for (const QString& mob : clipMobs(*item)) {
                    if (!targets.contains(mob)) targets << mob;
                }
            }
        }
    }
    const model::TranscriptStore store;
    int removed = 0;
    for (const QString& mob : targets) removed += store.remove(mob) ? 1 : 0;
    if (removed > 0) {
        ++transcriptGeneration_;
        refreshTranscriptTool();
    }
    return removed;
}

// "Right-click on a sequence in a bin and choose Transcript > Export SubCap. The Transcribe window opens ... The Export
// SubCap window opens ... select a location to save the SubCap file" (p.452): an Avid DS (.txt) or EBU N19 (.stl) file.
bool MainWindow::exportSubCap() {
    BinView* view = binContainer_->activeView();
    if (!view) return false;
    std::optional<model::BinItem> item;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* found = view->document().bin().item(id);
        if (found && hasSequence(*found)) {
            item = *found;
            break;
        }
    }
    if (!item) return false;
    TranscribeTracksDialog dialog(audioTracksOf(*item), true, this);
    if (!chooseTranscribeTracks(dialog) || dialog.tracks().isEmpty()) return false;
    model::TranscriptCaptionOptions options;
    options.maxCharacters = dialog.characterLimit();
    const model::MonitorState* record = nullptr;
    if (const model::LoadedClip* rec = composer_->state(MonitorId::Record).current(); rec && rec->item.id == item->id)
        record = &composer_->state(MonitorId::Record);
    if (dialog.useMarks() && record && record->in() && record->out()) options.range = std::pair{*record->in(), *record->out() + 1};
    QList<model::Caption> captions = model::transcriptCaptions(shownTranscriptOf(*item, dialog.tracks()), options);
    if (captions.isEmpty()) {
        reportFailure(this, tr("Export Subtitles: \"%1\" has no transcribed dialogue on those tracks.").arg(item->name));
        return false;
    }
    const QString path = transcriptPath(QStringLiteral("save"), fileNameOf(item->name) + QStringLiteral(".txt"),
                                        tr("Avid DS (*.txt);;EBU N19 (*.stl)"));
    if (path.isEmpty()) return false;
    const TimecodeFormat format = model::timecodeFormatOf(*item);
    const std::int64_t start = model::startFrameOf(*item);
    QByteArray bytes;
    if (path.endsWith(QLatin1String(".stl"), Qt::CaseInsensitive)) {
        // EBU N19 is 25 or 30 frames a second: the same labels at that rate (as the SubCap effect's Export Caption Data).
        const int fps = format.nominalFps % 30 == 0 ? 30 : 25;
        const TimecodeFormat stl = TimecodeFormat::forRate(Rational(fps), false);
        auto relabel = [&](std::int64_t frames) {
            const auto tc = Timecode::parse(Timecode(frames, format).toString(), stl);
            return tc ? tc->frameCount() : frames;
        };
        for (model::Caption& c : captions) {
            c.start = relabel(start + c.start);
            c.end = relabel(start + c.end);
        }
        bytes = model::writeStl(captions, fps);
    } else {
        for (model::Caption& c : captions) {
            c.start += start;
            c.end += start;
        }
        bytes = model::writeDsCaptions(captions, format).toUtf8();
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        reportFailure(this, tr("Export Subtitles: %1 cannot be written.").arg(QDir::toNativeSeparators(path)));
        return false;
    }
    return true;
}

// "Right-click inside a sequence and choose Transcript > Create SubCap. The Transcribe window opens ... Select the 'Use
// Marks' checkbox if you want to generate SubCaps only within a marked section of the timeline" (p.452). Ours: the
// captions go on a new video track over the sequence, as one undo step.
bool MainWindow::createSubCap() {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) {
        reportFailure(this, tr("Create Subtitles: load a sequence into the Record monitor."));
        return false;
    }
    const model::BinItem item = rec->item;
    TranscribeTracksDialog dialog(audioTracksOf(item), true, this);
    if (!chooseTranscribeTracks(dialog) || dialog.tracks().isEmpty()) return false;
    model::TranscriptCaptionOptions options;
    options.maxCharacters = dialog.characterLimit();
    const model::MonitorState& record = composer_->state(MonitorId::Record);
    if (dialog.useMarks()) {
        const std::int64_t in = record.in().value_or(0);
        const std::int64_t out = record.out() ? *record.out() + 1 : std::max<std::int64_t>(item.durationFrames, in + 1);
        options.range = std::pair{in, out};
    }
    const QList<model::Caption> captions = model::transcriptCaptions(shownTranscriptOf(item, dialog.tracks()), options);
    QString why;
    const bool ok = timeline_->changeRecordSequence(tr("Create Subtitles"), [&](model::Sequence& s) {
        return model::createSubCapTrack(s, captions, &why).has_value();
    });
    if (!ok) reportFailure(this, tr("Create Subtitles: %1").arg(why.isEmpty() ? tr("the captions could not be made.") : why));
    return ok;
}

int MainWindow::importSharedTranscripts(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = tr("%1 cannot be read.").arg(QDir::toNativeSeparators(path));
        return -1;
    }
    QString why;
    const auto shared = model::readSharedTranscripts(file.readAll(), &why);
    if (!shared) {
        if (error) *error = why;
        return -1;
    }
    const QString hint = settings_->value(QStringLiteral("Transcript"), QStringLiteral("languageHint")).toString();
    if (shared->languageHint != hint) {
        if (error) *error = tr("The file's Language Hint is not the project's: set the Transcript setting's Language Hint to match first.");
        return -1;
    }
    const model::TranscriptStore store;
    int saved = 0;
    for (const model::Transcript& t : shared->transcripts) saved += store.save(t) ? 1 : 0;
    if (saved > 0) {
        ++transcriptGeneration_;
        refreshTranscriptTool();
    }
    return saved;
}

// Manage Transcripts (2026.8 guide p.455-456): "Select the project whose transcripts you want to delete and click
// Delete ... Click Import, select the .avt file ... When you are finished ... click Done". Ours: the current project's
// transcribed clips, each deletable; Import reads a shared transcripts file; Export shares them all ("transcripts for an
// entire project can be exported using the Manage Transcripts window", p.451).
ManageTranscriptsDialog* MainWindow::manageTranscripts() {
    if (!project_) return nullptr;
    QWidget* over = QApplication::activeModalWidget();
    auto* dialog = new ManageTranscriptsDialog(over ? over : this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto refresh = [this, dialog] {
        const model::TranscriptStore store;
        QList<ManageTranscriptsDialog::Entry> entries;
        QSet<QString> seen;
        for (const auto& [binId, item] : projectClips(*binContainer_, nullptr)) {
            const QString mob = QString::fromStdString(model::mobIdOf(item).toString());
            if (seen.contains(mob)) continue;
            seen.insert(mob);
            const auto t = store.load(mob);
            if (!t) continue;
            entries.append({mob, item.name, t->language, QLocale().toString(t->created.toLocalTime(), QLocale::ShortFormat), t->edited()});
        }
        dialog->setEntries(entries);
    };
    connect(dialog, &ManageTranscriptsDialog::deleteRequested, this, [this, refresh](const QStringList& mobs) {
        deleteTranscripts(mobs);
        refresh();
    });
    connect(dialog, &ManageTranscriptsDialog::importRequested, this, [this, refresh] {
        const QString suffix = QString::fromLatin1(model::kSharedTranscriptsSuffix);
        const QString path = transcriptPath(QStringLiteral("open"), QString(), tr("Shared transcripts (*.%1)").arg(suffix));
        if (path.isEmpty()) return;
        QString why;
        if (importSharedTranscripts(path, &why) < 0) reportFailure(this, tr("Import Transcripts: %1").arg(why));
        refresh();
    });
    connect(dialog, &ManageTranscriptsDialog::exportRequested, this, [this, dialog] {
        const model::TranscriptStore store;
        model::SharedTranscripts shared;
        shared.languageHint = settings_->value(QStringLiteral("Transcript"), QStringLiteral("languageHint")).toString();
        for (const ManageTranscriptsDialog::Entry& e : dialog->entries()) {
            if (auto t = store.load(e.mob)) shared.transcripts << *t;
        }
        const QString suffix = QString::fromLatin1(model::kSharedTranscriptsSuffix);
        const QString path = transcriptPath(QStringLiteral("save"), QStringLiteral("Transcripts.") + suffix,
                                            tr("Shared transcripts (*.%1)").arg(suffix));
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const QByteArray bytes = model::writeSharedTranscripts(shared);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            reportFailure(this, tr("Manage Transcripts: %1 cannot be written.").arg(QDir::toNativeSeparators(path)));
    });
    refresh();
    dialog->open();
    return dialog;
}

// ---------------------------------------------------------------- AutoCut (8.2e)

bool MainWindow::assignSpeakerAngle(const QString& speaker, int angle) {
    const model::LoadedClip* loaded = composer_->state(MonitorId::Source).current();
    auto group = loaded && !model::isMultigroup(loaded->item) ? model::groupOf(loaded->item) : std::nullopt;
    if (!group || speaker.isEmpty() || angle >= group->angles.size()) return false;
    if (angle < 0) {
        if (!group->speakerAngles.remove(speaker)) return false;
    } else {
        if (group->speakerAngles.value(speaker, -1) == angle) return false;
        group->speakerAngles.insert(speaker, angle);
    }
    model::BinItem item = loaded->item;
    model::setGroup(item, *group);
    storeClip(loaded->binId, item);
    ++transcriptGeneration_;
    refreshTranscriptTool();
    return true;
}

// "Media Composer analyzes the transcript and applies camera switches based on the active speaker ... AutoCut applies
// only to the selected group clip segment" (2026.8 guide p.451-452). The camera switches go on the video tracks' group
// segments; the group's transcript is the one it shows (its selected angle's master clip, model::shownTranscript).
int MainWindow::autoCut() {
    const model::LoadedClip* rec = composer_->state(MonitorId::Record).current();
    if (!rec || rec->item.kind != model::BinItemKind::Sequence) return 0;
    const model::Sequence before = model::Sequence::fromItem(rec->item);
    struct Job {
        model::SegmentRef ref;
        model::GroupClip group;
        QList<std::pair<std::int64_t, QString>> lines;
    };
    QList<Job> jobs;
    for (const model::SegmentRef& ref : timeline_->selection()) {
        const auto kind = model::parseTrackName(ref.track);
        if (!kind || kind->kind != model::TrackKind::Video) continue;
        const auto s = model::segmentFor(before, ref);
        if (!s || !s->groupAngle) continue;
        const auto clip = composer_->resolve(s->binId, s->clipId);
        const auto group = clip ? model::groupOf(*clip) : std::nullopt;
        if (!group) continue;
        Job job{ref, *group, {}};
        for (const model::ShownLine& line : shownTranscriptOf(*clip, {}).lines) {
            if (!line.words.isEmpty()) job.lines.append({line.words.first().frame, line.speaker});
        }
        jobs << job;
    }
    if (jobs.isEmpty()) {
        reportFailure(this, tr("Speaker Cut: select a group clip segment with the Segment tool."));
        return 0;
    }
    int switches = 0;
    timeline_->changeRecordSequence(tr("Speaker Cut"), [&](model::Sequence& s) {
        for (const Job& job : jobs) switches += model::autoCut(s, job.ref.track, job.ref.start, job.group, job.lines);
        return switches > 0;
    });
    if (switches == 0) reportFailure(this, tr("Speaker Cut: no speaker with a camera angle speaks in the selected segments."));
    return switches;
}

} // namespace lumenis::ui

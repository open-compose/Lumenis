// MainWindow's transcription (8.2a; Editing Guide p.446-450, docs/spec/transcripts.md): which bins are transcribed, the
// engine, and master clips transcribed on the Background Queue while the user is not editing.
#include "lumenis/ui/main_window.h"

#include "lumenis/ext/provider.h"
#include "lumenis/ext/transcription.h"
#include "lumenis/media/transcription.h"
#include "lumenis/model/project_bins.h"
#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/tool_windows.h"
#include "lumenis/ui/transcript_tool.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/panels.h"
#include "lumenis/model/group_clip.h"
#include "lumenis/model/transcript_view.h"
#include "lumenis/core/timecode.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFontDialog>
#include <QPlainTextEdit>
#include <QTimer>

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
    if (!project_ || backgroundQueue_->isBusy()) return;
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
        } else if (!model::TranscriptStore(folder).save(*t)) {
            result.error = QObject::tr("The transcript of \"%1\" could not be saved in %2.").arg(item.name, folder);
        }
        return result;
    };
    auto done = [this, mob, name = item.name](const media::ConsolidateResult& result) {
        transcribing_.remove(mob);
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
                          now ? BackgroundQueue::Priority::High : BackgroundQueue::Priority::Low, done);
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
        const model::TranscriptStore store;
        const model::TranscriptFor transcriptFor = [store](const QString& mob) { return store.load(mob); };
        const model::ShownClipLookup lookup = [this](const QString& binId, const QString& clipId) {
            return composer_->resolve(binId, clipId);
        };
        model::ShownTranscript shown;
        if (item.kind == model::BinItemKind::Sequence) {
            shown = model::shownTranscript(model::Sequence::fromItem(item), transcriptTracks_, transcriptFor, lookup);
        } else if (const auto seq = model::multigroupSequence(item)) {
            shown = model::shownTranscript(model::Sequence::fromItem(*seq), transcriptTracks_, transcriptFor, lookup);
        } else {
            shown = model::shownTranscript(item, transcriptFor);
        }
        const TimecodeFormat format = TimecodeFormat::forRate(item.editRate, model::isDropFrame(item));
        const std::int64_t start = state.startFrame();
        transcriptTool_->setTranscript(shown, [format, start](std::int64_t frame) {
            return QString::fromStdString(Timecode(start + frame, format).toString());
        });
    }
    transcriptTool_->setPosition(state.position());
    transcriptTool_->setMarks(state.in(), state.out());
}

} // namespace lumenis::ui

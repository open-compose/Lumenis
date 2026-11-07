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

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
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

int MainWindow::queueTranscriptions() {
    const model::ProjectBins* bins = binContainer_->bins();
    const auto provider = transcriber();
    if (!project_ || !bins || !provider || !transcriptionBins_) return 0;
    const model::TranscriptStore store;
    ext::TranscriptionOptions options;
    // The Language Hint (p.448): empty is Multilingual, the engine detects the language.
    options.language = settings_->text(QStringLiteral("Transcript"), QStringLiteral("languageHint")).toStdString();
    int queued = 0;
    for (const model::BinNode* node : bins->bins()) {
        if (!transcriptionBins_->isIncluded(node->id) || bins->isInTrash(node->id)) continue;
        QList<model::BinItem> items;
        if (const BinDocument* doc = binContainer_->document(node->id)) {
            items = doc->bin().items;
        } else if (const auto bin = model::BinFile::read(bins->binPath(node->id))) {
            items = bin->items;
        }
        for (const model::BinItem& item : items) {
            if (!transcribable(item)) continue;
            const QString mob = QString::fromStdString(model::mobIdOf(item).toString());
            if (transcribing_.contains(mob) || store.has(mob)) continue;
            transcribing_.insert(mob);
            const QString folder = store.folder();
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
            };
            backgroundQueue_->add(tr("Transcribe %1").arg(item.name), node->id, work, BackgroundQueue::Priority::Low, done);
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

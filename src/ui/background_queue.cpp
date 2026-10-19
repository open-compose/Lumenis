#include "lumenis/ui/background_queue.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

// The Background Queue (background_queue.h): BackgroundQueue runs Consolidate/Transcode jobs one at a
// time on a std::thread, and BackgroundQueueWindow lists them. MainWindow (main_window.cpp) queues the
// jobs from the Consolidate/Transcode dialog (consolidate_dialog.*); the work itself is
// media::consolidate (src/media/consolidate.*).
//
// Threading: the worker only writes the Entry's shared atomics and result; the UI thread polls them on
// a timer, so no signal is emitted from the worker.

namespace lumenis::ui {

BackgroundQueue::BackgroundQueue(QObject* parent) : QObject(parent) {
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &BackgroundQueue::poll);
}

BackgroundQueue::~BackgroundQueue() {
    for (Entry& e : entries_) e.stop->store(true);
    if (worker_.joinable()) worker_.join();
}

int BackgroundQueue::add(const QString& name, const QString& binId, Work work, Priority priority) {
    Entry e;
    e.job.id = nextId_++;
    e.job.name = name;
    e.job.binId = binId;
    e.job.priority = priority;
    e.work = std::move(work);
    entries_ << e;
    startNext();
    emit changed();
    return e.job.id;
}

BackgroundQueue::Entry* BackgroundQueue::find(int id) {
    for (Entry& e : entries_) {
        if (e.job.id == id) return &e;
    }
    return nullptr;
}

// A waiting job is marked Cancelled at once. A running one is only asked to stop: its progress
// callback returns false, and poll() marks it Cancelled when its thread ends.
void BackgroundQueue::cancel(int id) {
    Entry* e = find(id);
    if (!e) return;
    if (e->job.state == State::Waiting) {
        e->job.state = State::Cancelled;
        e->job.message = tr("Cancelled");
    } else if (e->job.state == State::Running) {
        e->stop->store(true);
    }
    emit changed();
}

void BackgroundQueue::setPriority(int id, Priority priority) {
    if (Entry* e = find(id)) {
        e->job.priority = priority;
        emit changed();
    }
}

void BackgroundQueue::clearInactive() {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [](const Entry& e) {
                                      return e.job.state != State::Waiting && e.job.state != State::Running;
                                  }),
                   entries_.end());
    emit changed();
}

QList<BackgroundQueue::Job> BackgroundQueue::jobs() const {
    QList<Job> out;
    for (const Entry& e : entries_) {
        Job j = e.job;
        if (j.state == State::Running) j.progress = e.permille->load() / 1000.0;
        out << j;
    }
    return out;
}

bool BackgroundQueue::isBusy() const {
    return running_ >= 0 || std::any_of(entries_.begin(), entries_.end(),
                                        [](const Entry& e) { return e.job.state == State::Waiting; });
}

// Polls by hand, since tests have no event loop running the timer.
void BackgroundQueue::waitForIdle() {
    while (isBusy()) {
        poll();
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
}

void BackgroundQueue::startNext() {
    if (running_ >= 0) return;
    // The highest priority first, then in order (p.357).
    Entry* next = nullptr;
    for (Entry& e : entries_) {
        if (e.job.state != State::Waiting) continue;
        if (!next || static_cast<int>(e.job.priority) < static_cast<int>(next->job.priority)) next = &e;
    }
    if (!next) {
        timer_.stop();
        return;
    }
    // The previous job's thread has finished (poll() saw it done); join it before reusing worker_.
    if (worker_.joinable()) worker_.join();
    next->job.state = State::Running;
    running_ = next->job.id;
    // The thread owns copies of what it needs; the entry may move in the
    // list meanwhile.
    auto work = next->work;
    auto permille = next->permille;
    auto stop = next->stop;
    auto done = next->done;
    auto result = next->result;
    worker_ = std::thread([work, permille, stop, done, result] {
        *result = work([permille, stop](double fraction) {
            permille->store(static_cast<int>(fraction * 1000));
            return !stop->load();
        });
        done->store(true);
    });
    timer_.start();
}

// On the timer: once the running job's thread is done, its state comes from how it ended. A stop that
// was asked for wins over the result.
void BackgroundQueue::poll() {
    Entry* e = running_ >= 0 ? find(running_) : nullptr;
    if (e && e->done->load()) {
        if (worker_.joinable()) worker_.join();
        const media::ConsolidateResult result = *e->result;
        if (e->stop->load()) {
            e->job.state = State::Cancelled;
            e->job.message = tr("Cancelled");
        } else if (!result.error.isEmpty()) {
            e->job.state = State::Failed;
            e->job.message = result.error;
        } else {
            e->job.state = State::Completed;
            e->job.message = tr("Completed");
        }
        e->job.progress = 1.0;
        const QString binId = e->job.binId;
        running_ = -1;
        // What was made goes in the bin, cancelled or not.
        emit finished(binId, result);
        startNext();
    }
    emit changed();
}

// ---------------------------------------------------------------- BackgroundQueueWindow

// The Tool menu above a scrolling list of job rows.
BackgroundQueueWindow::BackgroundQueueWindow(BackgroundQueue& queue, QWidget* parent)
    : QWidget(parent), queue_(queue) {
    setObjectName(QStringLiteral("backgroundQueue"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    // The Tool menu (p.357).
    auto* tool = new QToolButton(this);
    tool->setObjectName(QStringLiteral("backgroundQueueMenu"));
    tool->setText(tr("Tool"));
    tool->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(tool);
    menu->addAction(tr("Clear Inactive Jobs"), &queue_, &BackgroundQueue::clearInactive);
    QAction* groups = menu->addAction(tr("Show Job Groups"));
    groups->setEnabled(false);
    groups->setToolTip(tr("Not available yet"));
    tool->setMenu(menu);
    layout->addWidget(tool, 0, Qt::AlignLeft);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll);
    rows_ = new QVBoxLayout(body);
    rows_->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll);
    connect(&queue_, &BackgroundQueue::changed, this, &BackgroundQueueWindow::refresh);
    refresh();
}

void BackgroundQueueWindow::refresh() {
    // Rows are rebuilt; the progress bars update in place when the jobs
    // are the same.
    const QList<BackgroundQueue::Job> jobs = queue_.jobs();
    const QList<QWidget*> existing = findChildren<QWidget*>(QStringLiteral("backgroundJob"));
    if (existing.size() == jobs.size()) {
        for (int i = 0; i < jobs.size(); ++i) {
            QWidget* row = existing.at(i);
            if (row->property("jobId").toInt() != jobs.at(i).id ||
                row->property("state").toInt() != static_cast<int>(jobs.at(i).state)) {
                break;
            }
            if (auto* bar = row->findChild<QProgressBar*>()) bar->setValue(static_cast<int>(jobs.at(i).progress * 100));
            if (i == jobs.size() - 1) return;
        }
    }
    // Otherwise every row is rebuilt. The old rows are deleted later: a row's own Cancel button may be
    // what changed the queue.
    for (QWidget* w : existing) {
        w->setObjectName(QString());
        w->hide();
        w->deleteLater();
    }
    // A row: name, Cancel (active jobs), progress, Priority (waiting jobs) and the message (inactive).
    for (const BackgroundQueue::Job& job : jobs) {
        auto* row = new QWidget;
        row->setObjectName(QStringLiteral("backgroundJob"));
        row->setProperty("jobId", job.id);
        row->setProperty("state", static_cast<int>(job.state));
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new QLabel(job.name, row), 1);
        const bool active = job.state == BackgroundQueue::State::Waiting || job.state == BackgroundQueue::State::Running;
        if (active) {
            // Cancel; Resume, for suspended jobs, comes later (p.357).
            auto* cancel = new QPushButton(tr("Cancel"), row);
            cancel->setObjectName(QStringLiteral("cancelJob"));
            const int id = job.id;
            connect(cancel, &QPushButton::clicked, &queue_, [this, id] { queue_.cancel(id); });
            h->addWidget(cancel);
        }
        auto* bar = new QProgressBar(row);
        bar->setRange(0, 100);
        bar->setValue(static_cast<int>(job.progress * 100));
        bar->setFixedWidth(160);
        h->addWidget(bar);
        auto* priority = new QComboBox(row);
        priority->setObjectName(QStringLiteral("jobPriority"));
        priority->addItems({tr("High Priority"), tr("Normal Priority"), tr("Low Priority")});
        priority->setCurrentIndex(static_cast<int>(job.priority));
        priority->setEnabled(job.state == BackgroundQueue::State::Waiting);
        const int id = job.id;
        connect(priority, &QComboBox::currentIndexChanged, &queue_,
                [this, id](int i) { queue_.setPriority(id, static_cast<BackgroundQueue::Priority>(i)); });
        h->addWidget(priority);
        if (!active) h->addWidget(new QLabel(job.message, row));
        rows_->insertWidget(rows_->count() - 1, row);
    }
}

} // namespace lumenis::ui

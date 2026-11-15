#include "lumenis/ui/background_queue.h"

#include <QComboBox>
#include <QFont>
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
    pauseTimer_.setSingleShot(true);
    connect(&pauseTimer_, &QTimer::timeout, this, [this] {
        // The pause ends, unless play goes on: then it is extended (p.377), checked again in a second (ours).
        if (autoPause_ && pausingEnabled_) {
            pauseTimer_.start(1000);
            return;
        }
        paused_ = false;
        emit servicesChanged();
        startNext();
    });
}

BackgroundQueue::ServiceState BackgroundQueue::serviceState() const {
    if (!active_) return ServiceState::Stopped;
    if (pausingEnabled_ && (paused_ || autoPause_)) return ServiceState::Paused;
    return ServiceState::Active;
}

bool BackgroundQueue::canStart() const { return serviceState() == ServiceState::Active; }

void BackgroundQueue::startServices() {
    // "Start: Immediately starts the background services if they are currently stopped" (p.377); a pause ends too.
    active_ = true;
    paused_ = false;
    pauseTimer_.stop();
    emit servicesChanged();
    startNext();
}

void BackgroundQueue::stopServices() {
    // The running job is stopped and suspended (p.357: "a Resume button (for suspended transcodes)").
    if (Entry* e = running_ >= 0 ? find(running_) : nullptr) {
        e->suspending = true;
        e->stop->store(true);
        emit changed();
    }
    active_ = false;
    paused_ = false;
    pauseTimer_.stop();
    emit servicesChanged();
}

void BackgroundQueue::pauseServices(int ms) {
    if (!active_ || !pausingEnabled_) return;
    paused_ = true;
    pauseTimer_.start(std::max(1, ms));
    emit servicesChanged();
}

void BackgroundQueue::setPausingEnabled(bool on) {
    if (pausingEnabled_ == on) return;
    pausingEnabled_ = on;
    if (!on) {
        paused_ = false;
        pauseTimer_.stop();
    }
    emit servicesChanged();
    startNext();
}

void BackgroundQueue::setAutoPause(bool on) {
    if (autoPause_ == on) return;
    autoPause_ = on;
    emit servicesChanged();
    if (!on) startNext();
}

QString BackgroundQueue::takeErrorReport() {
    const QString report = errors_.join(QLatin1Char('\n'));
    errors_.clear();
    emit servicesChanged();
    return report;
}

BackgroundQueue::~BackgroundQueue() {
    for (Entry& e : entries_) e.stop->store(true);
    if (worker_.joinable()) worker_.join();
}

int BackgroundQueue::add(const QString& name, const QString& binId, Work work, Priority priority, Done done,
                         const QString& group) {
    Entry e;
    e.job.id = nextId_++;
    e.job.name = name;
    e.job.group = group;
    e.job.binId = binId;
    e.job.priority = priority;
    e.work = std::move(work);
    e.onDone = std::move(done);
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
    if (e->job.state == State::Waiting || e->job.state == State::Suspended) {
        e->work = {};
        e->job.state = State::Cancelled;
        e->job.message = tr("Cancelled");
    } else if (e->job.state == State::Running) {
        e->stop->store(true);
    }
    emit changed();
}

void BackgroundQueue::resume(int id) {
    Entry* e = find(id);
    if (!e || e->job.state != State::Suspended || !e->work) return;
    // A fresh start: what the thread shared is new, so the stopped run cannot touch it.
    e->permille = std::make_shared<std::atomic<int>>(0);
    e->stop = std::make_shared<std::atomic<bool>>(false);
    e->stopped = std::make_shared<std::atomic<bool>>(false);
    e->done = std::make_shared<std::atomic<bool>>(false);
    e->result = std::make_shared<media::ConsolidateResult>();
    e->suspending = false;
    e->job.state = State::Waiting;
    e->job.progress = 0;
    e->job.message.clear();
    startNext();
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
                                      return e.job.state != State::Waiting && e.job.state != State::Running &&
                                             e.job.state != State::Suspended;
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
    while (running_ >= 0 || (canStart() && isBusy())) {
        poll();
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
}

void BackgroundQueue::startNext() {
    if (running_ >= 0) return;
    // Nothing starts while the services are stopped or paused (p.376-377).
    if (!canStart()) {
        timer_.stop();
        return;
    }
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
    auto stopped = next->stopped;
    auto done = next->done;
    auto result = next->result;
    worker_ = std::thread([work, permille, stop, stopped, done, result] {
        *result = work([permille, stop, stopped](double fraction) {
            permille->store(static_cast<int>(fraction * 1000));
            if (!stop->load()) return true;
            stopped->store(true);
            return false;
        });
        done->store(true);
    });
    timer_.start();
}

// On the timer: once the running job's thread is done, its state comes from how it ended. It was
// cancelled if the work was told to stop (a progress call returned false), or if a stop was asked
// for and it failed; a stop asked for after the work's last progress call, by a Cancel pressed as
// the job finished, does not undo a success.
void BackgroundQueue::poll() {
    Entry* e = running_ >= 0 ? find(running_) : nullptr;
    if (e && e->done->load()) {
        if (worker_.joinable()) worker_.join();
        const media::ConsolidateResult result = *e->result;
        // Suspended by Stop Services when the work was told to stop, or stopped with an error: it keeps its work to run
        // again, and what it made is not handed on. Finished before the stop reached it, it completes.
        if (e->suspending && (e->stopped->load() || !result.error.isEmpty())) {
            e->job.state = State::Suspended;
            e->job.progress = 0;
            e->job.message = tr("Suspended");
            e->suspending = false;
            running_ = -1;
            startNext();
            emit changed();
            return;
        }
        e->suspending = false;
        if (e->stopped->load() || (e->stop->load() && !result.error.isEmpty())) {
            e->job.state = State::Cancelled;
            e->job.message = tr("Cancelled");
        } else if (!result.error.isEmpty()) {
            e->job.state = State::Failed;
            e->job.message = result.error;
            errors_ << QStringLiteral("%1: %2").arg(e->job.name, result.error);
            emit servicesChanged();
        } else {
            e->job.state = State::Completed;
            e->job.message = tr("Completed");
        }
        e->job.progress = 1.0;
        const QString binId = e->job.binId;
        const Done done = e->onDone;
        // What the work held (a render's frame cache keeps files open, which Windows will not delete)
        // goes with it.
        e->work = {};
        e->onDone = {};
        running_ = -1;
        // What was made goes in the bin (or to the job's own `done`), cancelled or not.
        if (done) {
            done(result);
        } else {
            emit finished(binId, result);
        }
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
    toolMenu_ = menu;
    menu->addAction(tr("Clear Inactive Jobs"), &queue_, &BackgroundQueue::clearInactive);
    // Show Job Groups: "allows you to display or hide specific job groups listed in the menu" (p.357), a check box
    // for each group the queue has, made when the menu opens.
    groupsMenu_ = menu->addMenu(tr("Show Job Groups"));
    groupsMenu_->setObjectName(QStringLiteral("showJobGroups"));
    connect(groupsMenu_, &QMenu::aboutToShow, this, [this] {
        groupsMenu_->clear();
        QStringList groups;
        for (const BackgroundQueue::Job& job : queue_.jobs()) {
            if (!groups.contains(job.group)) groups << job.group;
        }
        for (const QString& group : groups) {
            QAction* a = groupsMenu_->addAction(group.isEmpty() ? tr("Other Jobs") : group);
            a->setCheckable(true);
            a->setChecked(groupShown(group));
            connect(a, &QAction::toggled, this, [this, group](bool on) { setGroupShown(group, on); });
        }
        if (groups.isEmpty()) groupsMenu_->addAction(tr("No Jobs"))->setEnabled(false);
    });
    tool->setMenu(menu);
    layout->addWidget(tool, 0, Qt::AlignLeft);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setAccessibleName(tr("Jobs")); // screen readers (10.3)
    auto* body = new QWidget(scroll);
    rows_ = new QVBoxLayout(body);
    rows_->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll);
    connect(&queue_, &BackgroundQueue::changed, this, &BackgroundQueueWindow::refresh);
    refresh();
}

void BackgroundQueueWindow::setGroupShown(const QString& group, bool shown) {
    if (shown) hidden_.removeAll(group);
    else if (!hidden_.contains(group)) hidden_ << group;
    refresh();
}

void BackgroundQueueWindow::refresh() {
    // Rows are rebuilt; the progress bars update in place when the jobs
    // are the same. The jobs of hidden groups are left out; the others are listed by group, each group under its
    // heading, in the order the groups first came.
    QList<BackgroundQueue::Job> all = queue_.jobs();
    QStringList groups;
    for (const BackgroundQueue::Job& job : all) {
        if (!groups.contains(job.group)) groups << job.group;
    }
    QList<BackgroundQueue::Job> jobs;
    for (const QString& group : groups) {
        if (!groupShown(group)) continue;
        for (const BackgroundQueue::Job& job : all) {
            if (job.group == group) jobs << job;
        }
    }
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
    for (QLabel* heading : findChildren<QLabel*>(QStringLiteral("jobGroup"))) {
        heading->setObjectName(QString());
        heading->hide();
        heading->deleteLater();
    }
    QString lastGroup;
    // A row: name, Cancel (active jobs), progress, Priority (waiting jobs) and the message (inactive).
    for (qsizetype n = 0; n < jobs.size(); ++n) {
        const BackgroundQueue::Job& job = jobs.at(n);
        // The group's heading above its first job (a job without a group has none).
        if ((n == 0 || job.group != lastGroup) && !job.group.isEmpty()) {
            auto* heading = new QLabel(job.group);
            heading->setObjectName(QStringLiteral("jobGroup"));
            QFont bold = heading->font();
            bold.setBold(true);
            heading->setFont(bold);
            rows_->insertWidget(rows_->count() - 1, heading);
        }
        lastGroup = job.group;
        auto* row = new QWidget;
        row->setObjectName(QStringLiteral("backgroundJob"));
        row->setProperty("jobId", job.id);
        row->setProperty("state", static_cast<int>(job.state));
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new QLabel(job.name, row), 1);
        const bool active = job.state == BackgroundQueue::State::Waiting || job.state == BackgroundQueue::State::Running;
        if (job.state == BackgroundQueue::State::Suspended) {
            // "Changes from a Cancel button ... to a Resume button (for suspended transcodes)" (p.357).
            auto* resume = new QPushButton(tr("Resume"), row);
            resume->setObjectName(QStringLiteral("resumeJob"));
            const int id = job.id;
            connect(resume, &QPushButton::clicked, &queue_, [this, id] { queue_.resume(id); });
            h->addWidget(resume);
        }
        if (active) {
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

#pragma once

#include "lumenis/media/consolidate.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class QVBoxLayout;

namespace lumenis::ui {

// Background Consolidate and Transcode (Editing Guide p.356-357): jobs that
// run one at a time on their own thread while editing goes on, the highest
// priority first.
//
// MainWindow owns one and adds a job per clip when Consolidate/Transcode runs in the background; its
// finished() signal puts the new clips in their bin. All functions are called on the UI thread; only
// the Work runs on the worker thread. The queue polls the worker every 100 ms while a job runs.
class BackgroundQueue : public QObject {
    Q_OBJECT

public:
    // In the order of the Priority menu; a lower value runs first.
    enum class Priority { High, Normal, Low };
    // Completed, Failed and Cancelled are inactive (clearInactive()).
    enum class State { Waiting, Running, Completed, Failed, Cancelled };
    // The work: it reports progress, which returns false when it should
    // stop. Progress is a fraction, 0 to 1. It runs on the worker thread, so it must not touch
    // widgets or the bins; it captures copies of what it needs.
    using Work = std::function<media::ConsolidateResult(const std::function<bool(double)>& progress)>;
    // A job as the window shows it.
    struct Job {
        int id = 0; // from 1, never reused
        QString name; // the clip's or sequence's
        QString binId;
        Priority priority = Priority::Normal;
        State state = State::Waiting;
        double progress = 0; // 0 to 1
        QString message;     // "Completed", "Cancelled" or the error, once inactive
    };

    explicit BackgroundQueue(QObject* parent = nullptr);
    // Asks every job to stop and waits for the running one.
    ~BackgroundQueue() override;

    // What to do with a job's result on the UI thread, in place of finished() (a background render
    // links its precomputes to the sequence rather than adding clips to the bin).
    using Done = std::function<void(const media::ConsolidateResult& result)>;

    // Queues `work` for the bin `binId` and starts it if nothing runs. Returns the job's id. With
    // `done`, the result goes there rather than to finished().
    int add(const QString& name, const QString& binId, Work work, Priority priority = Priority::Normal, Done done = {});
    // The Cancel button: a waiting job is dropped, a running one stops.
    void cancel(int id);
    // The Priority menu; a running job is not affected (p.357).
    void setPriority(int id, Priority priority);
    // Tool menu > Clear Inactive Jobs: all jobs not running or waiting.
    void clearInactive();
    // Every job, in the order added, with the running one's progress as of now.
    QList<Job> jobs() const;
    // Whether a job is running or waiting.
    bool isBusy() const;
    // Runs the queue until every job is done (tests).
    void waitForIdle();

signals:
    // Jobs were added, changed state or made progress (at least every poll while one runs).
    void changed();
    // A job's result, on the UI thread, to put in its bin. It also fires for a cancelled or failed
    // job, with whatever it made before it stopped.
    void finished(const QString& binId, const media::ConsolidateResult& result);

private:
    // A job and what its thread shares with the UI thread. The shared pointers keep those alive
    // for the thread even if the entry moves in, or is removed from, the list.
    struct Entry {
        Job job;
        Work work;
        Done onDone; // the result's handler, if not finished()
        std::shared_ptr<std::atomic<int>> permille = std::make_shared<std::atomic<int>>(0);
        std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);    // asked to stop
        std::shared_ptr<std::atomic<bool>> stopped = std::make_shared<std::atomic<bool>>(false); // the work was told
        std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<media::ConsolidateResult> result = std::make_shared<media::ConsolidateResult>();
    };
    // Checks whether the running job is done; if so records its result, emits finished() and
    // starts the next one.
    void poll();
    // Starts the highest-priority waiting job if none runs; stops the timer when none waits.
    void startNext();
    // The entry of job `id`; null if there is none.
    Entry* find(int id);

    QList<Entry> entries_;
    std::thread worker_; // the running job's, or the last one's until joined
    int running_ = -1; // the running job's id
    int nextId_ = 1;
    QTimer timer_;
};

// Tools > Background Queue Window (p.356-357): each job's name, a Cancel
// button, its progress, its Priority menu and Completed when done; the Tool
// menu's Clear Inactive Jobs.
class BackgroundQueueWindow : public QWidget {
    Q_OBJECT

public:
    // A panel's contents (MainWindow puts it in the "BackgroundQueue" panel). `queue` must outlive
    // it; the window refreshes on BackgroundQueue::changed.
    explicit BackgroundQueueWindow(BackgroundQueue& queue, QWidget* parent = nullptr);
    // Shows the queue's jobs, one row each.
    void refresh();

private:
    BackgroundQueue& queue_;
    QVBoxLayout* rows_ = nullptr;
};

} // namespace lumenis::ui

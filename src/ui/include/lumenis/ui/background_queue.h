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
class BackgroundQueue : public QObject {
    Q_OBJECT

public:
    enum class Priority { High, Normal, Low };
    enum class State { Waiting, Running, Completed, Failed, Cancelled };
    // The work: it reports progress, which returns false when it should
    // stop.
    using Work = std::function<media::ConsolidateResult(const std::function<bool(double)>& progress)>;
    struct Job {
        int id = 0;
        QString name; // the clip's or sequence's
        QString binId;
        Priority priority = Priority::Normal;
        State state = State::Waiting;
        double progress = 0;
        QString message;
    };

    explicit BackgroundQueue(QObject* parent = nullptr);
    ~BackgroundQueue() override;

    int add(const QString& name, const QString& binId, Work work, Priority priority = Priority::Normal);
    // The Cancel button: a waiting job is dropped, a running one stops.
    void cancel(int id);
    // The Priority menu; a running job is not affected (p.357).
    void setPriority(int id, Priority priority);
    // Tool menu > Clear Inactive Jobs: all jobs not running or waiting.
    void clearInactive();
    QList<Job> jobs() const;
    bool isBusy() const;
    // Runs the queue until every job is done (tests).
    void waitForIdle();

signals:
    void changed();
    // A job's result, on the UI thread, to put in its bin.
    void finished(const QString& binId, const media::ConsolidateResult& result);

private:
    struct Entry {
        Job job;
        Work work;
        std::shared_ptr<std::atomic<int>> permille = std::make_shared<std::atomic<int>>(0);
        std::shared_ptr<std::atomic<bool>> stop = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<bool>> done = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<media::ConsolidateResult> result = std::make_shared<media::ConsolidateResult>();
    };
    void poll();
    void startNext();
    Entry* find(int id);

    QList<Entry> entries_;
    std::thread worker_;
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
    explicit BackgroundQueueWindow(BackgroundQueue& queue, QWidget* parent = nullptr);
    void refresh();

private:
    BackgroundQueue& queue_;
    QVBoxLayout* rows_ = nullptr;
};

} // namespace lumenis::ui

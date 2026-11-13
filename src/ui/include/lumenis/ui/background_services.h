#pragma once

// The background services' controls (Editing Guide p.375-377; docs/spec/background-services.md, 8.4a):
// * BackgroundIndicator, the progress indicator in the Timeline: "When files are placed in a Dynamic Media Folder, you
//   will see a progress indicator in the Timeline" (p.375), drawn as the guide's table describes it (p.376): spinning
//   while background processing runs, a solid green dot when Dynamic Media Folder assets wait to be acquired, grey when
//   the services are stopped, yellow when they are paused, a yellow triangle after an error. Its right-click menu has
//   Dynamic Media Folders and Generate Error Report in Console.
// * BackgroundServicesDialog, Tools > Background Services (p.376-377): the state, Start, Stop and Pause (for a number of
//   minutes), Always Start Avid Background Services at Launch, Always Stop Avid Background Services upon Exit and Enable
//   Pausing of Background Service.
// The services themselves are BackgroundQueue's state (lumenis/ui/background_queue.h); MainWindow ties the three.

#include "lumenis/ui/background_queue.h"

#include <QDialog>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QSpinBox;

namespace lumenis::ui {

class BackgroundIndicator : public QWidget {
    Q_OBJECT

public:
    explicit BackgroundIndicator(QWidget* parent = nullptr);

    // What the indicator shows (p.376).
    struct State {
        BackgroundQueue::ServiceState services = BackgroundQueue::ServiceState::Active;
        bool busy = false;   // background processing in progress: spinning
        bool assets = false; // Dynamic Media Folder assets to acquire: the green dot
        bool error = false;  // an error since the last report: the yellow triangle
        bool operator==(const State&) const = default;
    };
    void setState(const State& state);
    State state() const { return state_; }
    // The indicator's look as a word, for its tooltip and tests: "idle", "busy", "assets", "stopped", "paused", with
    // " error" after it when the triangle shows.
    QString description() const;
    // The same words translated, as the tooltip shows them.
    QString shownDescription() const;
    QSize sizeHint() const override { return {22, 22}; }

signals:
    // The right-click menu's items (p.376).
    void dynamicMediaFoldersRequested();
    void errorReportRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    State state_;
    QTimer spin_;
    int angle_ = 0;
};

class BackgroundServicesDialog : public QDialog {
    Q_OBJECT

public:
    // The options as stored in the user's settings.
    struct Options {
        bool startAtLaunch = true; // ours: on to start with, so that background jobs run without a visit here
        bool stopOnExit = false;
        bool enablePausing = true;
        bool operator==(const Options&) const = default;
    };
    // `queue` must outlive the dialog; Start, Stop and Pause act on it at once.
    BackgroundServicesDialog(BackgroundQueue& queue, const Options& options, QWidget* parent = nullptr);
    Options options() const;
    // The buttons (tests).
    void start();
    void stop();
    void pause(int minutes);
    QString statusText() const;

private:
    void refresh();
    BackgroundQueue& queue_;
    QLabel* status_ = nullptr;
    QSpinBox* minutes_ = nullptr;
    QCheckBox* startAtLaunch_ = nullptr;
    QCheckBox* stopOnExit_ = nullptr;
    QCheckBox* enablePausing_ = nullptr;
};

} // namespace lumenis::ui

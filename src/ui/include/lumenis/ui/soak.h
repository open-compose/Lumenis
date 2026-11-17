#pragma once

// The long-run stability check (ROADMAP 10.2; docs/spec/soak.md): `cutthulhu --soak <hours>` opens a sample project,
// then loops through what an editor does all day (playback of a sequence with effects, scrubbing, rendering and
// clearing renders, edits and undo, tool windows opened and closed) and logs the process's memory, handles, threads
// and windows and the frames played and dropped, one JSON line per interval. scripts/soak-check.py reads the log and
// fails when any of them keeps growing.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <random>

namespace lumenis::ui {

class CommandRegistry;
class MainWindow;

// What the process holds now, from the operating system. A figure the system does not give is -1.
struct ProcessStats {
    double residentMb = -1;  // resident memory (working set on Windows)
    double heapMb = -1;      // the heap's bytes in use (Linux with glibc, macOS); resident memory also counts what the
                             // allocator keeps after it was freed
    int handles = -1;        // open file descriptors (Linux, macOS) or kernel handles (Windows)
    int threads = -1;
    int gdiObjects = -1;     // Windows only
    int userObjects = -1;    // Windows only
};
ProcessStats processStats();

struct SoakOptions {
    double hours = 1.0;
    int intervalSeconds = 60;   // between log lines
    QString logPath;            // JSON lines; written as they come, so a crash keeps what came before
    std::uint32_t seed = 1;     // the scrub positions and the order of the steps
    int cycles = 0;             // when above 0, the run also ends after this many cycles (ctest soak_smoke: one,
                                // however slow the build)
};

// The soak run. prepare() makes the project and its media in a folder of its own (with renders on a media drive
// there, never the computer's own drives); start() runs it on the event loop and emits finished() with 0, or 1 when
// the log cannot be written or a step can no longer run.
class Soak : public QObject {
    Q_OBJECT

public:
    Soak(MainWindow& window, CommandRegistry& commands, SoakOptions options);
    ~Soak() override;

    bool prepare(QString* error);
    void start();

    // Steps run so far, by kind, for the log and the tests.
    int cycles() const { return cycles_; }

signals:
    void finished(int code);

private:
    void nextStep();
    void watch();
    void sample();
    // Ends the run with an exit code and, for an error, why (in the log's closing line).
    void stopAll(int code, const QString& why = {});
    // Logs a failure the application reported (reportFailure), which would otherwise only open a box that does not
    // block and that nothing logs.
    void failed(const QString& title, const QString& text);
    bool writeLine(const QByteArray& json);
    int binItems() const;

    MainWindow& window_;
    CommandRegistry& commands_;
    SoakOptions options_;
    QTemporaryDir dir_;
    QString binId_;
    QString clipId_;
    std::mt19937 random_;
    QTimer stepTimer_;
    QTimer sampleTimer_;
    QTimer endTimer_;
    std::unique_ptr<class QFile> log_;
    qint64 startedMs_ = 0;
    int step_ = 0;
    int cycles_ = 0;
    int scrubsLeft_ = 0;
    std::int64_t shown_ = 0;    // frames shown and dropped over the whole run
    std::int64_t dropped_ = 0;
    std::int64_t lastShown_ = 0; // the Composer's counts when the current play started
    std::int64_t lastDropped_ = 0;
    bool done_ = false;
    int failures_ = 0; // failures reported over the run; the first kMostFailureLines are logged
    bool sampleDue_ = false;       // the interval has passed: figures at the end of this cycle
    QPointer<QWidget> lastModal_; // the modal dialog open at the last line, if any (cleared if it goes)
    int lastCycles_ = 0;
    qint64 lastCycleMs_ = 0;       // when the cycle count last went up
};

} // namespace lumenis::ui

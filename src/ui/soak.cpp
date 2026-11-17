// The long-run stability check, `--soak` (ROADMAP 10.2; docs/spec/soak.md). See soak.h for what it does; this file
// has the process figures for each operating system, the sample project and the steps. The steps run one after
// another on the event loop, as an editor's commands would, so nothing here runs while a step is half done.
#include "lumenis/ui/soak.h"

#include "lumenis/core/branding.h"
#include "lumenis/core/project_format.h"
#include "lumenis/media/link.h"
#include "lumenis/media/managed.h"
#include "lumenis/media/media.h"
#include "lumenis/model/effect_render.h"
#include "lumenis/model/project_store.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/main_window.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/render_dialogs.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProgressDialog>
#include <QSysInfo>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <vector>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX // std::max, not the macro
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#elif defined(Q_OS_MACOS)
#include <libproc.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <unistd.h>
#elif defined(Q_OS_LINUX)
#include <QTextStream>
#include <malloc.h>
#endif

namespace lumenis::ui {

// ---------------------------------------------------------------- the process's figures

ProcessStats processStats() {
    ProcessStats s;
#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters)) {
        s.residentMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) s.handles = static_cast<int>(handles);
    s.gdiObjects = static_cast<int>(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    s.userObjects = static_cast<int>(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
    // The threads: a snapshot of the system's, counted for this process.
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof entry;
        int threads = 0;
        const DWORD self = GetCurrentProcessId();
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID == self) ++threads;
        }
        CloseHandle(snapshot);
        s.threads = threads;
    }
#elif defined(Q_OS_MACOS)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        s.residentMb = static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t threadCount = 0;
    if (task_threads(mach_task_self(), &threads, &threadCount) == KERN_SUCCESS) {
        s.threads = static_cast<int>(threadCount);
        for (mach_msg_type_number_t i = 0; i < threadCount; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), threadCount * sizeof(thread_act_t));
    }
    const int bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bytes > 0) {
        std::vector<proc_fdinfo> fds(static_cast<std::size_t>(bytes) / sizeof(proc_fdinfo));
        const int used = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, fds.data(), bytes);
        if (used >= 0) s.handles = used / static_cast<int>(sizeof(proc_fdinfo));
    }
    // The heap in use over every malloc zone (resident memory also counts what the zones keep after it was freed).
    malloc_statistics_t heap{};
    malloc_zone_statistics(nullptr, &heap);
    s.heapMb = static_cast<double>(heap.size_in_use) / (1024.0 * 1024.0);
#elif defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&status);
        for (QString line = in.readLine(); !line.isNull(); line = in.readLine()) {
            const QStringList parts = line.simplified().split(QLatin1Char(' '));
            if (parts.size() < 2) continue;
            if (parts.at(0) == QLatin1String("VmRSS:")) s.residentMb = parts.at(1).toDouble() / 1024.0; // in kB
            if (parts.at(0) == QLatin1String("Threads:")) s.threads = parts.at(1).toInt();
        }
    }
    s.handles = static_cast<int>(QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).size());
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    // What the heap has handed out and not had back: resident memory also counts what glibc keeps in its per-thread
    // arenas after it was freed, which grows as decoders' threads come and go without anything leaking.
    const struct mallinfo2 heap = mallinfo2();
    s.heapMb = static_cast<double>(heap.uordblks + heap.hblkhd) / (1024.0 * 1024.0);
#endif
#endif
    return s;
}

// ---------------------------------------------------------------- the sample project

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kFrames = 250; // ten seconds at 25

// A movie whose every frame differs (a moving bar over a gradient), with a tone on two channels: decoding and the
// sound both do real work while it plays.
bool writeSampleMovie(const QString& path, QString* error) {
    media::WriterSettings s;
    s.format = QStringLiteral("mov");
    s.videoCodec = QStringLiteral("ffv1");
    s.pixelFormat = QStringLiteral("bgr0");
    s.width = kWidth;
    s.height = kHeight;
    s.frameRate = Rational(25);
    s.audioCodec = QStringLiteral("pcm_s16le");
    s.sampleRate = 48000;
    s.channels = 2;
    media::MediaWriter writer;
    if (!writer.open(path, s, error)) return false;
    constexpr int kSamplesPerFrame = 48000 / 25;
    std::vector<float> sound(static_cast<std::size_t>(kSamplesPerFrame) * 2);
    for (int f = 0; f < kFrames; ++f) {
        QImage image(kWidth, kHeight, QImage::Format_RGB32);
        QPainter p(&image);
        QLinearGradient g(0, 0, kWidth, kHeight);
        g.setColorAt(0, QColor::fromHsv((f * 3) % 360, 160, 200));
        g.setColorAt(1, QColor::fromHsv((f * 3 + 120) % 360, 160, 90));
        p.fillRect(image.rect(), g);
        p.fillRect(QRect(f * kWidth / kFrames, 0, kWidth / 20, kHeight), Qt::white);
        p.end();
        if (!writer.writeVideo(image)) {
            if (error) *error = writer.lastError();
            return false;
        }
        for (int i = 0; i < kSamplesPerFrame; ++i) {
            const double t = static_cast<double>(f * kSamplesPerFrame + i) / 48000.0;
            const auto v = static_cast<float>(0.2 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
            sound[static_cast<std::size_t>(i) * 2] = v;
            sound[static_cast<std::size_t>(i) * 2 + 1] = v;
        }
        if (!writer.writeAudio(sound.data(), kSamplesPerFrame)) {
            if (error) *error = writer.lastError();
            return false;
        }
    }
    return writer.finish(error);
}

// The steps of one cycle, in order.
enum Step { PlayForward, Scrub, PlayReverse, Render, ClearRender, EditAndUndo, ToolWindows, StepCount };

// Tool windows opened and closed again each cycle: ones that keep state about the sequence or the media.
const char* const kTools[] = {"tools.audio_mixer", "tools.effect_palette", "tools.effect_editor", "tools.markers",
                              "tools.media_tool", "tools.console", "tools.timecode_window", "tools.audio_tool"};

} // namespace

Soak::Soak(MainWindow& window, CommandRegistry& commands, SoakOptions options)
    : window_(window), commands_(commands), options_(std::move(options)), random_(options_.seed) {
    stepTimer_.setSingleShot(true);
    connect(&stepTimer_, &QTimer::timeout, this, &Soak::nextStep);
    sampleTimer_.setInterval(std::max(options_.intervalSeconds, 1) * 1000);
    connect(&sampleTimer_, &QTimer::timeout, this, &Soak::watch);
    endTimer_.setSingleShot(true);
    connect(&endTimer_, &QTimer::timeout, this, [this] { stopAll(0); });
}

Soak::~Soak() {
    window_.setRenderHook({});
    window_.setClearRendersHook({});
    setFailureObserver({});
}

namespace {
// Failures logged one by one; past these only counted (in the closing line), so a failure every cycle cannot fill the
// disk with the log.
constexpr int kMostFailureLines = 500;
} // namespace

void Soak::failed(const QString& title, const QString& text) {
    ++failures_;
    if (failures_ > kMostFailureLines) return;
    // "seconds", not "t": the check reads lines with "t" as samples.
    const QJsonObject note{{QStringLiteral("failure"), title + QStringLiteral(": ") + text},
                           {QStringLiteral("step"), step_},
                           {QStringLiteral("cycle"), cycles_},
                           {QStringLiteral("seconds"), static_cast<double>(QDateTime::currentMSecsSinceEpoch() - startedMs_) / 1000.0}};
    writeLine(QJsonDocument(note).toJson(QJsonDocument::Compact));
}

bool Soak::prepare(QString* error) {
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return false;
    };
    if (!dir_.isValid()) return fail(QStringLiteral("cannot make a temporary folder"));
    log_ = std::make_unique<QFile>(options_.logPath);
    if (!log_->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return fail(QStringLiteral("cannot write ") + options_.logPath);
    // Renders go to a drive of the soak's own; with LUMENIS_NO_SYSTEM_DRIVES the computer's drives are not even
    // listed (the ctest), and otherwise the Render dialog below picks this one.
    const QString drive = dir_.filePath(QStringLiteral("Media"));
    QDir().mkpath(drive);
    QStringList drives = media::extraMediaDrives();
    drives << drive;
    media::setExtraMediaDrives(drives);
    const QString movie = dir_.filePath(QStringLiteral("soak.mov"));
    QString why;
    if (!writeSampleMovie(movie, &why)) return fail(QStringLiteral("cannot write the sample movie: ") + why);

    model::ProjectSettings s;
    s.name = QStringLiteral("Soak");
    s.format = projectFormatPresets().front();
    s.format.width = kWidth;
    s.format.height = kHeight;
    s.format.editRate = Rational(25);
    const QString users = dir_.filePath(QStringLiteral("Users"));
    model::UserProfiles(users).create(QStringLiteral("editor"));
    window_.settings().setUser(users, QStringLiteral("editor"));
    const auto entry = model::ProjectStore(dir_.filePath(QStringLiteral("Projects"))).create(s);
    if (!entry) return fail(QStringLiteral("cannot make the project"));
    window_.setProject(*entry);
    window_.settings().setValue(QStringLiteral("Media Creation"), QStringLiteral("renderVideoDrive"), drive);
    binId_ = window_.binContainer().newBin();
    BinDocument* bin = window_.binContainer().openBin(binId_);
    if (!bin) return fail(QStringLiteral("cannot open a bin"));
    if (QWidget* editing = QApplication::focusWidget()) editing->clearFocus(); // the new bin's name editor
    const QStringList ids = window_.linkFiles(binId_, {movie});
    if (ids.isEmpty()) return fail(QStringLiteral("cannot link the sample movie"));
    clipId_ = ids.first();
    // The sequence: the clip spliced in three times, a dissolve on each cut and a Picture-in-Picture on the middle one,
    // so playback composes effects and Render has something to render.
    Composer& composer = window_.composer();
    for (int i = 0; i < 3; ++i) {
        composer.loadDefault(binId_, *bin->bin().item(clipId_));
        if (!window_.timeline().perform(QStringLiteral("edit.splice_in"))) return fail(QStringLiteral("cannot splice the clip in"));
    }
    const bool changed = window_.timeline().changeRecordSequence(QStringLiteral("Soak effects"), [](model::Sequence& seq) {
        model::Track* v1 = seq.track(QStringLiteral("V1"));
        if (!v1 || v1->segments.size() < 3) return false;
        for (qsizetype k = 1; k < v1->segments.size(); ++k) {
            model::Transition t;
            t.effect.category = QStringLiteral("Blend");
            t.effect.name = QStringLiteral("Dissolve");
            t.duration = 20;
            t.alignment = model::TransitionAlignment::CenteredOnCut;
            v1->segments[k].transition = t;
        }
        v1->segments[1].effect = model::Effect{QStringLiteral("Blend"), QStringLiteral("Picture-in-Picture"), {}};
        return true;
    });
    if (!changed) return fail(QStringLiteral("cannot add the effects"));
    // The Render dialog chooses the soak's drive and goes on, as an editor clicking OK.
    window_.setRenderHook([drive](RenderEffectDialog& d) {
        const int at = d.driveMenu()->findData(QDir::cleanPath(drive));
        if (at >= 0) d.driveMenu()->setCurrentIndex(at);
        return true;
    });
    window_.setClearRendersHook([](ClearRendersDialog&) { return true; });
    return true;
}

void Soak::start() {
    startedMs_ = QDateTime::currentMSecsSinceEpoch();
    lastCycleMs_ = startedMs_;
    const QJsonObject header{
        {QStringLiteral("soak"), QJsonObject{{QStringLiteral("product"), QString::fromUtf8(branding::kProductName)},
                                             {QStringLiteral("version"), QString::fromUtf8(branding::kVersion)},
                                             {QStringLiteral("os"), QSysInfo::prettyProductName()},
                                             {QStringLiteral("cpu"), QSysInfo::currentCpuArchitecture()},
                                             {QStringLiteral("hours"), options_.hours},
                                             {QStringLiteral("interval"), options_.intervalSeconds},
                                             {QStringLiteral("seed"), static_cast<qint64>(options_.seed)},
                                             {QStringLiteral("cycles"), options_.cycles}}}};
    if (!writeLine(QJsonDocument(header).toJson(QJsonDocument::Compact))) {
        stopAll(1, QStringLiteral("cannot write the log"));
        return;
    }
    // Every failure the application reports goes into the log, with its step and time, instead of a box nobody reads
    // until the end (the owner's first 72-hour runs ended with a write failure the logs did not have).
    setFailureObserver([this](const QString& title, const QString& text) { failed(title, text); });
    sample(); // the figures before any step, the line every later one is compared with
    sampleTimer_.start();
    endTimer_.start(static_cast<int>(std::max(options_.hours, 0.0) * 3600.0 * 1000.0));
    stepTimer_.start(0);
}

// One step, then the next after it has had its time. Each step leaves the windows as they were before it.
void Soak::nextStep() {
    if (done_) return;
    Composer& composer = window_.composer();
    const model::LoadedClip* rec = composer.state(MonitorId::Record).current();
    if (!rec) {
        stopAll(1, QStringLiteral("the Record monitor has no sequence"));
        return;
    }
    const std::int64_t length = std::max<std::int64_t>(model::Sequence::fromItem(rec->item).duration(), 1);
    std::uniform_int_distribution<std::int64_t> anywhere(0, length - 1);
    auto stopPlay = [&] {
        if (!composer.isPlaying()) return;
        composer.stop();
        shown_ += composer.playStats().shown - lastShown_;
        dropped_ += composer.playStats().dropped - lastDropped_;
    };
    stopPlay();
    switch (step_) {
    case PlayForward:
    case PlayReverse:
        composer.setActiveMonitor(MonitorId::Record);
        composer.moveTo(MonitorId::Record, step_ == PlayForward ? anywhere(random_) / 2 : length - 1 - anywhere(random_) / 2);
        composer.play(step_ == PlayForward ? 1 : -1);
        // playStats() count from this start.
        lastShown_ = composer.playStats().shown;
        lastDropped_ = composer.playStats().dropped;
        stepTimer_.start(step_ == PlayForward ? 6000 : 3000);
        break;
    case Scrub:
        if (scrubsLeft_ == 0) scrubsLeft_ = 40;
        composer.moveTo(MonitorId::Record, anywhere(random_));
        if (--scrubsLeft_ > 0) {
            stepTimer_.start(50); // the same step again
            return;
        }
        stepTimer_.start(50);
        break;
    case Render:
        // The Picture-in-Picture segment, rendered again each cycle so a render is made every time.
        composer.moveTo(MonitorId::Record, length / 2);
        window_.renderEffects(true, true);
        stepTimer_.start(500);
        break;
    case ClearRender: {
        window_.clearRenders(true);
        // "Clearing rendered effects does not delete ... their precomputes" (FX Guide p.60-61), so the cleared
        // render's precompute clip stays in the bin with its media: deleted here as an editor would (the Media Tool's
        // Delete, p.347-348, which closes the frame cache's files first). Otherwise a 72-hour run would leave thousands
        // of renders on the soak's drive.
        if (BinDocument* bin = window_.binContainer().document(binId_)) {
            QStringList ids, files;
            for (const model::BinItem& item : bin->bin().items) {
                if (item.kind != model::BinItemKind::RenderedEffect) continue;
                ids << item.id;
                if (const auto link = media::linkOf(item)) files += link->files();
            }
            if (!files.isEmpty() && window_.mediaTool().deleter) window_.mediaTool().deleter(files);
            if (!ids.isEmpty()) bin->removeItems(ids);
        }
        stepTimer_.start(200);
        break;
    }
    case EditAndUndo: {
        // A splice of the clip and its undo: the Timeline's history, the bins and the sequence's undo copies.
        if (BinDocument* bin = window_.binContainer().document(binId_)) {
            if (const model::BinItem* clip = bin->bin().item(clipId_)) {
                composer.loadDefault(binId_, *clip);
                composer.moveTo(MonitorId::Record, anywhere(random_));
                if (window_.timeline().perform(QStringLiteral("edit.splice_in"))) {
                    if (QAction* undo = commands_.action(QStringLiteral("edit.undo"))) undo->trigger();
                }
            }
        }
        stepTimer_.start(200);
        break;
    }
    case ToolWindows: {
        // Each tool opened and closed again, as panels. A tool that asks first (the Media Tool Display dialog,
        // Editing Guide p.346) is answered with OK, as an editor would.
        QTimer answer;
        answer.setInterval(50);
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->accept();
        });
        answer.start();
        PanelManager& panels = window_.panels();
        for (const char* id : kTools) {
            QList<Panel*> before;
            for (Panel* p : panels.panels()) {
                if (panels.isOpen(p)) before << p;
            }
            QAction* action = commands_.action(QLatin1String(id));
            if (!action || !action->isEnabled()) continue;
            action->trigger();
            QCoreApplication::processEvents();
            for (Panel* p : panels.panels()) {
                if (panels.isOpen(p) && !before.contains(p)) panels.closePanel(p);
            }
        }
        // A dialog a step opened (a render's failure message, say) is closed, as an editor would.
        if (QWidget* modal = QApplication::activeModalWidget()) modal->close();
        ++cycles_;
        if (options_.cycles > 0 && cycles_ >= options_.cycles) sampleDue_ = true; // the last cycle's figures
        if (sampleDue_) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); // closed windows gone before they are counted
            sample();
        }
        if (options_.cycles > 0 && cycles_ >= options_.cycles) {
            stopAll(0);
            return;
        }
        stepTimer_.start(200);
        break;
    }
    default:
        break;
    }
    step_ = (step_ + 1) % StepCount;
}

// The timer's work, also inside a step's own event loop: closes a dialog left waiting, ends a stalled run, and asks for
// the figures, which are taken at the end of the cycle (sample()).
void Soak::watch() {
    // A dialog that is still open at the next line waits for an answer nobody will give: it is logged and closed so
    // the run goes on (and the log says so), rather than hanging with the figures flat.
    QWidget* modal = QApplication::activeModalWidget();
    // A progress dialog is working, not waiting (a render that takes longer than the interval, as under the
    // sanitizers): closing it would cancel the work.
    if (qobject_cast<QProgressDialog*>(modal)) modal = nullptr;
    if (modal && modal == lastModal_) {
        const QJsonObject note{{QStringLiteral("closedDialog"), modal->objectName().isEmpty() ? QString::fromLatin1(modal->metaObject()->className())
                                                                                            : modal->objectName()},
                               {QStringLiteral("step"), step_}};
        writeLine(QJsonDocument(note).toJson(QJsonDocument::Compact));
        modal->close();
        modal = nullptr;
    }
    lastModal_ = modal;
    // No cycle for ten minutes: a step is stuck. The run ends with an error and the log says at which step.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (cycles_ != lastCycles_) {
        lastCycles_ = cycles_;
        lastCycleMs_ = now;
    } else if (now - lastCycleMs_ > 10 * 60 * 1000) {
        writeLine(QJsonDocument(QJsonObject{{QStringLiteral("stalled"), step_}}).toJson(QJsonDocument::Compact));
        stopAll(1, QStringLiteral("stalled"));
        return;
    }
    sampleDue_ = true;
}

// The figures, at the end of a cycle: every tool window closed and nothing playing, so that one line is like the next.
void Soak::sample() {
    sampleDue_ = false;
    const ProcessStats s = processStats();
    Composer& composer = window_.composer();
    // Frames of a play still going count to now.
    std::int64_t shown = shown_, dropped = dropped_;
    if (composer.isPlaying()) {
        shown += composer.playStats().shown - lastShown_;
        dropped += composer.playStats().dropped - lastDropped_;
    }
    const QJsonObject line{
        {QStringLiteral("t"), static_cast<double>(QDateTime::currentMSecsSinceEpoch() - startedMs_) / 1000.0},
        {QStringLiteral("residentMb"), std::round(s.residentMb * 10.0) / 10.0},
        {QStringLiteral("heapMb"), s.heapMb < 0 ? -1.0 : std::round(s.heapMb * 10.0) / 10.0},
        {QStringLiteral("handles"), s.handles},
        {QStringLiteral("threads"), s.threads},
        {QStringLiteral("gdiObjects"), s.gdiObjects},
        {QStringLiteral("userObjects"), s.userObjects},
        // Widgets alive (Qt's own count): a window or control made and never deleted shows here on every platform.
        {QStringLiteral("widgets"), static_cast<int>(QApplication::allWidgets().size())},
        {QStringLiteral("topLevels"), static_cast<int>(QApplication::topLevelWidgets().size())},
        // Items in the open bins: a render's precompute or an edit's copy kept for ever grows a bin.
        {QStringLiteral("binItems"), binItems()},
        {QStringLiteral("cycles"), cycles_},
        {QStringLiteral("shown"), static_cast<qint64>(shown)},
        {QStringLiteral("dropped"), static_cast<qint64>(dropped)},
    };
    if (!writeLine(QJsonDocument(line).toJson(QJsonDocument::Compact))) {
        stopAll(1, QStringLiteral("cannot write the log: ") + (log_ ? log_->errorString() : QString()));
    }
}

int Soak::binItems() const {
    qsizetype n = 0;
    for (BinDocument* d : window_.binContainer().openDocuments()) n += d->bin().items.size();
    return static_cast<int>(n);
}

bool Soak::writeLine(const QByteArray& json) {
    if (!log_ || log_->write(json + '\n') != json.size() + 1) return false;
    return log_->flush();
}

void Soak::stopAll(int code, const QString& why) {
    if (done_) return;
    done_ = true;
    stepTimer_.stop();
    if (window_.composer().isPlaying()) {
        Composer& composer = window_.composer();
        composer.stop();
        shown_ += composer.playStats().shown - lastShown_;
        dropped_ += composer.playStats().dropped - lastDropped_;
    }
    // No last sample here: the run may end in the middle of a step (a render's buffers in use), and every sample must
    // be a cycle's end to compare with the others. The closing line is not a sample (no "t"); it tells a run that
    // ended from one that crashed or was killed, which otherwise look the same (scripts/soak-check.py fails a log that
    // stops short of its hours without it).
    sampleTimer_.stop();
    endTimer_.stop();
    if (log_) {
        QJsonObject ended{{QStringLiteral("code"), code},
                          {QStringLiteral("seconds"), static_cast<double>(QDateTime::currentMSecsSinceEpoch() - startedMs_) / 1000.0},
                          {QStringLiteral("cycles"), cycles_},
                          {QStringLiteral("failures"), failures_}};
        if (!why.isEmpty()) ended.insert(QStringLiteral("why"), why);
        writeLine(QJsonDocument(QJsonObject{{QStringLiteral("ended"), ended}}).toJson(QJsonDocument::Compact));
        log_->close();
    }
    emit finished(code);
}

} // namespace lumenis::ui

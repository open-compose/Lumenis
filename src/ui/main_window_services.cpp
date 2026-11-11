// MainWindow's background services (8.4a; Editing Guide p.375-377, docs/spec/background-services.md): the Timeline's
// progress indicator following the Background Queue, pausing during playback, Tools > Background Services and its options
// kept in the user's Interface setting (keys outside the setting's schema, so not shown in the Settings window).
#include "lumenis/ui/main_window.h"

#include "lumenis/ui/background_queue.h"
#include "lumenis/ui/background_services.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>

namespace lumenis::ui {

namespace {
const QString kKind = QStringLiteral("Interface");
} // namespace

BackgroundServicesDialog::Options MainWindow::backgroundServiceOptions() const {
    BackgroundServicesDialog::Options o;
    auto flag = [this](const char* key, bool fallback) {
        const QJsonValue v = settings_->value(kKind, QString::fromLatin1(key));
        return v.isBool() ? v.toBool() : fallback;
    };
    o.startAtLaunch = flag("backgroundServicesStartAtLaunch", o.startAtLaunch);
    o.stopOnExit = flag("backgroundServicesStopOnExit", o.stopOnExit);
    o.enablePausing = flag("backgroundServicesEnablePausing", o.enablePausing);
    return o;
}

void MainWindow::setUpBackgroundServices() {
    BackgroundIndicator* indicator = timeline_->backgroundIndicator();
    connect(backgroundQueue_, &BackgroundQueue::changed, this, &MainWindow::updateBackgroundIndicator);
    connect(backgroundQueue_, &BackgroundQueue::servicesChanged, this, &MainWindow::updateBackgroundIndicator);
    // "it can be automatically performed during critical UI operations such as playback" (p.377).
    connect(composer_, &Composer::changed, this, [this] { backgroundQueue_->setAutoPause(composer_->isPlaying()); });
    connect(indicator, &BackgroundIndicator::errorReportRequested, this, [this] {
        // "Right-click the progress indicator and select Generate Error Report in Console. The yellow triangle will
        // remain until you generate an error report" (p.376).
        const QString report = backgroundQueue_->takeErrorReport();
        for (const QString& line : report.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            // Console only: the error report the user asked for (p.376).
            ConsoleLog::instance().append(tr("Background error: %1").arg(line));
    });
    connect(indicator, &BackgroundIndicator::dynamicMediaFoldersRequested, this, [this] {
        if (QAction* a = commands_.action(QStringLiteral("tools.dynamic_media_folders")); a && a->isEnabled()) a->trigger();
    });
    // "Always Start Avid Background Services at Launch" (p.377): at launch and when another user's settings load.
    auto launch = [this] {
        const BackgroundServicesDialog::Options o = backgroundServiceOptions();
        backgroundQueue_->setPausingEnabled(o.enablePausing);
        if (o.startAtLaunch) backgroundQueue_->startServices();
        else backgroundQueue_->stopServices();
    };
    connect(settings_, &Settings::userChanged, this, launch);
    launch();
    updateBackgroundIndicator();
}

void MainWindow::updateBackgroundIndicator() {
    BackgroundIndicator::State s;
    s.services = backgroundQueue_->serviceState();
    s.busy = backgroundQueue_->isBusy();
    s.assets = dmfAssetsWaiting_;
    s.error = backgroundQueue_->hasErrors();
    timeline_->backgroundIndicator()->setState(s);
}

// Tools > Background Services (p.377): Start, Stop and Pause act at once; OK keeps the options.
bool MainWindow::backgroundServices() {
    BackgroundServicesDialog dialog(*backgroundQueue_, backgroundServiceOptions(), this);
    const bool ok = backgroundServicesHook_ ? backgroundServicesHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok) return false;
    const BackgroundServicesDialog::Options o = dialog.options();
    settings_->setValue(kKind, QStringLiteral("backgroundServicesStartAtLaunch"), o.startAtLaunch);
    settings_->setValue(kKind, QStringLiteral("backgroundServicesStopOnExit"), o.stopOnExit);
    settings_->setValue(kKind, QStringLiteral("backgroundServicesEnablePausing"), o.enablePausing);
    // "If you uncheck this option and click OK, the pause state is cleared out and background services will resume."
    backgroundQueue_->setPausingEnabled(o.enablePausing);
    return true;
}

// "Always Stop Avid Background Services upon Exit" (p.377). Ours: the services stop with the application anyway; the
// option stops them as it closes, so that no waiting job starts during the shutdown.
void MainWindow::stopBackgroundServicesOnExit() {
    if (backgroundServiceOptions().stopOnExit) backgroundQueue_->stopServices();
}

} // namespace lumenis::ui

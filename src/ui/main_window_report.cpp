// The Sequence Report in the application (9.2c; docs/spec/sequence-report.md; Editing Guide p.368-371): from a bin's
// selected sequences or the sequence in a monitor, the Sequence Report dialog, Apply Changes to the sequence's name and
// Starting TC, then each report written to a text file and opened ("The application writes the report to a text file
// and opens a text editor", p.370). The report itself is made by media/sequence_report.h.
#include "lumenis/ui/main_window.h"

#include "lumenis/core/timecode.h"
#include "lumenis/media/link.h"
#include "lumenis/media/sequence_report.h"
#include "lumenis/model/marker.h"
#include "lumenis/model/sequence.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/media_frames.h"
#include "lumenis/ui/sequence_report_dialog.h"
#include "lumenis/ui/settings.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QUrl>

namespace lumenis::ui {

QList<MainWindow::ReportTarget> MainWindow::sequenceReportTargets(bool fromMonitor) const {
    QList<ReportTarget> out;
    if (fromMonitor) {
        // "With a sequence loaded in a monitor, right-click the monitor and select Sequence Report."
        const MonitorId monitor = composer_->activeMonitor();
        const model::LoadedClip* loaded = composer_->state(monitor).current();
        if (loaded && loaded->item.kind == model::BinItemKind::Sequence) out.append({loaded->binId, loaded->item, monitor});
        return out;
    }
    // "From a bin, right-click a sequence and select Sequence Report. You can select multiple sequences."
    const BinView* view = binContainer_ ? binContainer_->activeView() : nullptr;
    if (!view) return out;
    for (const QString& id : view->selectedIds()) {
        const model::BinItem* item = view->document().bin().item(id);
        if (item && item->kind == model::BinItemKind::Sequence) out.append({view->document().nodeId(), *item, std::nullopt});
    }
    return out;
}

media::SequenceReportInput MainWindow::sequenceReportInput(const ReportTarget& target) const {
    media::SequenceReportInput in;
    const BinDocument* doc = binContainer_ ? binContainer_->document(target.binId) : nullptr;
    in.binName = doc ? doc->name() : QString();
    in.name = target.sequence.name;
    in.startFrame = model::startFrameOf(target.sequence);
    in.rate = target.sequence.editRate.num() != 0 ? target.sequence.editRate : Rational(25);
    in.dropFrame = model::isDropFrame(target.sequence);
    in.project = project_ ? project_->settings.name : QString();
    in.sequence = model::Sequence::fromItem(target.sequence);
    // "Loading a sequence in the Source/Record monitor before you generate a report allows you to select which part of
    // the sequence about which you want information" (p.369): its marks, and the Timeline's selected tracks for the
    // Record monitor's.
    const MonitorId monitor = target.monitor.value_or(MonitorId::Record);
    const model::LoadedClip* loaded = composer_->state(monitor).current();
    if (loaded && loaded->item.id == target.sequence.id) {
        in.markIn = composer_->state(monitor).in();
        in.markOut = composer_->state(monitor).out();
        if (monitor == MonitorId::Record) in.enabledTracks = timeline_->patch().selectedRecordTracks(in.sequence);
    }
    in.clip = [this](const model::Segment& s) { return composer_->resolveForSegment(s); };
    in.offline = [this](const model::BinItem& clip) {
        if (clip.kind != model::BinItemKind::MasterClip && clip.kind != model::BinItemKind::Subclip &&
            clip.kind != model::BinItemKind::AudioClip) {
            return false;
        }
        return !media::isLinked(clip) || frames_->isOffline(clip);
    };
    return in;
}

bool MainWindow::applySequenceReportChanges(const ReportTarget& target, const QString& name, const QString& startTimecode) {
    BinDocument* doc = binContainer_ ? binContainer_->document(target.binId) : nullptr;
    const model::BinItem* stored = doc ? doc->bin().item(target.sequence.id) : nullptr;
    if (!stored || doc->isReadOnly()) {
        QApplication::beep();
        return false;
    }
    model::BinItem item = *stored;
    const TimecodeFormat format = model::timecodeFormatOf(item);
    const auto start = Timecode::parse(startTimecode.trimmed().toStdString(), format);
    if (name.trimmed().isEmpty() || !start) {
        ConsoleLog::instance().append(tr("Sequence Report: \"%1\" is not a timecode.").arg(startTimecode));
        QApplication::beep();
        return false;
    }
    item.name = name.trimmed();
    item.fields.insert(QStringLiteral("Start"), QString::fromStdString(start->toString()));
    item.modified = QDateTime::currentDateTimeUtc();
    storeClip(target.binId, item);
    return true;
}

QString MainWindow::writeSequenceReport(const media::SequenceReportInput& input, const media::SequenceReportOptions& options,
                                        const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        ConsoleLog::instance().append(tr("Sequence Report: %1 cannot be written.").arg(path));
        return {};
    }
    file.write(media::sequenceReport(input, options).toUtf8());
    file.close();
    // "... and opens a text editor" (p.370): the system's application for the file.
    if (openReportHook_) {
        openReportHook_(path);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    return path;
}

int MainWindow::sequenceReport(bool fromMonitor) {
    QList<ReportTarget> targets = sequenceReportTargets(fromMonitor);
    if (targets.isEmpty()) {
        QApplication::beep();
        return 0;
    }
    const ReportTarget& first = targets.first();
    const media::SequenceReportInput firstInput = sequenceReportInput(first);
    const TimecodeFormat format = TimecodeFormat::forRate(firstInput.rate, firstInput.dropFrame);
    SequenceReportDialog dialog(firstInput.binName, first.sequence.name,
                                QString::fromStdString(Timecode(firstInput.startFrame, format).toString()), targets.size() > 1, this);
    connect(&dialog, &SequenceReportDialog::applyChangesRequested, this, [this, &targets](const QString& name, const QString& start) {
        if (applySequenceReportChanges(targets.first(), name, start)) {
            BinDocument* doc = binContainer_->document(targets.first().binId);
            if (const model::BinItem* now = doc ? doc->bin().item(targets.first().sequence.id) : nullptr) targets.first().sequence = *now;
        }
    });
    const bool go = sequenceReportHook_ ? sequenceReportHook_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!go) return 0;
    // "If you select more than 8 sequences, a dialog box asks if you want to generate sequence reports for all selected
    // items" (p.370).
    if (targets.size() > 8 && !sequenceReportHook_) {
        if (QMessageBox::question(this, tr("Sequence Report"),
                                  tr("Generate sequence reports for all %1 selected sequences?").arg(targets.size())) !=
            QMessageBox::Yes) {
            return 0;
        }
    }
    // "The Save Summary Output File As dialog box opens" (p.370): for the first report; the others go beside it.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(folder).filePath(first.sequence.name + QStringLiteral(".txt"));
    const QString chosen = reportFileHook_ ? reportFileHook_(suggested)
                                           : QFileDialog::getSaveFileName(this, tr("Save Summary Output File As"), suggested,
                                                                          tr("Text files (*.txt);;All files (*)"));
    if (chosen.isEmpty()) return 0;
    const media::SequenceReportOptions options = dialog.options();
    int written = 0;
    for (int i = 0; i < targets.size(); ++i) {
        const QString path = i == 0 ? chosen
                                    : QFileInfo(chosen).absoluteDir().filePath(targets.at(i).sequence.name + QStringLiteral(".txt"));
        if (!writeSequenceReport(sequenceReportInput(targets.at(i)), options, path).isEmpty()) ++written;
    }
    return written;
}

int MainWindow::popupAafReports(const QString& binId, const QStringList& ids) {
    const QString imp = QStringLiteral("Import");
    if (!project_ || !settings_->flag(imp, QStringLiteral("popupReport"))) return 0;
    BinDocument* doc = binContainer_->document(binId);
    if (!doc) return 0;
    // The OMFI/AAF tab's options (p.238).
    media::SequenceReportOptions options;
    options.csv = settings_->text(imp, QStringLiteral("reportFormat")) == QLatin1String("csv");
    options.effectSummary = settings_->flag(imp, QStringLiteral("reportEffectSummary"));
    options.missingOnly = settings_->flag(imp, QStringLiteral("reportMissingOnly"));
    options.effectLocations = settings_->flag(imp, QStringLiteral("reportEffectLocation"));
    options.clipSummary = settings_->flag(imp, QStringLiteral("reportClipSummary"));
    options.sourceSummary = settings_->flag(imp, QStringLiteral("reportSourceSummary"));
    options.offlineOnly = settings_->flag(imp, QStringLiteral("reportOfflineSources"));
    const bool markers = options.effectSummary && settings_->flag(imp, QStringLiteral("reportEffectMarkers"));
    const QDir folder(QDir(project_->folder).filePath(QStringLiteral("Reports")));
    QDir().mkpath(folder.path());
    int written = 0;
    for (const QString& id : ids) {
        const model::BinItem* stored = doc->bin().item(id);
        if (!stored || stored->kind != model::BinItemKind::Sequence) continue;
        const ReportTarget target{binId, *stored, std::nullopt};
        const media::SequenceReportInput input = sequenceReportInput(target);
        if (markers) {
            // "Add Markers at Effect Locations ... adds markers to the sequence indicating where selected effects are
            // located, that is, effects meeting the search criteria of the sequence report" (p.238): on the effect's
            // track in the sequence (a nested effect's parent track), named after the effect.
            model::BinItem item = *stored;
            const QString color = model::markerColors().isEmpty() ? QString() : model::markerColors().first().name;
            int added = 0;
            for (const media::ReportedEffect& e : media::reportedEffects(input, options)) {
                model::Marker m;
                m.id = model::newItemId();
                m.user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME")); // as the Timeline's markers (p.413)
                m.name = e.name;
                m.comment = e.missing ? tr("Unavailable Effect: %1").arg(e.name) : e.name;
                m.color = color;
                m.track = e.depth == 0 ? e.track : e.track.section(QLatin1Char(')'), 0, 0).mid(1);
                m.frame = std::max<std::int64_t>(e.start, 0);
                if (model::addMarker(item, m)) ++added;
            }
            if (added > 0) storeClip(binId, item);
        }
        const QString name = stored->name.isEmpty() ? QStringLiteral("Sequence") : stored->name;
        const QString path = folder.filePath(name + (options.csv ? QStringLiteral(".csv") : QStringLiteral(".txt")));
        if (!writeSequenceReport(input, options, path).isEmpty()) ++written;
    }
    return written;
}

} // namespace lumenis::ui

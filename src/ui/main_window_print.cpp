// Printing in the application (9.1; docs/spec/printing.md; Editing Guide p.292 "Printing Bins", p.424 "Printing the
// Contents of the Markers Window", p.645 "Printing the Timeline"): File > Page Setup and the session's printer, Print
// Bin (Print for the Markers window and the Console; the Media Tool's table, p.345), Print Frame and Print Timeline. What goes on paper is made in
// printing.cpp; here it is gathered from the windows and sent through the Print dialog.
#include "lumenis/ui/main_window.h"

#include "lumenis/model/bin.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/marker_dialogs.h"
#include "lumenis/ui/media_tool.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/printing.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/tool_windows.h"

#include <QAction>
#include <QApplication>
#include <QPageSetupDialog>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QTableWidget>

namespace lumenis::ui {

QPrinter& MainWindow::printer() {
    if (!printer_) printer_ = std::make_unique<QPrinter>(QPrinter::HighResolution);
    return *printer_;
}

void MainWindow::connectPrinting() {
    auto handle = [this](const char* id, std::function<void()> handler) {
        commands_.setHandler(QString::fromLatin1(id), std::move(handler));
    };
    handle("file.page_setup", [this] { pageSetup(); });
    handle("file.print_bin", [this] { printActive(); });
    handle("file.print_frame", [this] { printFrame(); });
    handle("file.print_timeline", [this] { printTimeline(); });
    // The window last used decides what File > Print Bin prints (p.424). A click in the menu bar does not move the focus,
    // so the window the menu is used for is still the one before.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (now) followPrintTarget(now);
    });
}

void MainWindow::followPrintTarget(QWidget* focused) {
    PrintTarget target = PrintTarget::Bin;
    if (markersWindow_ && (focused == markersWindow_ || markersWindow_->isAncestorOf(focused))) {
        target = PrintTarget::Markers;
    } else if (console_ && (focused == console_ || console_->isAncestorOf(focused))) {
        target = PrintTarget::Console;
    } else if (mediaTool_ && (focused == mediaTool_ || mediaTool_->isAncestorOf(focused))) {
        target = PrintTarget::MediaTool;
    }
    if (target == printTarget_) return;
    printTarget_ = target;
    // "Select File > Print" (p.424) in the Markers window and the Console; Print Bin otherwise (p.292), the Media Tool
    // too ("the same procedures for printing bins", p.345).
    if (QAction* action = commands_.action(QStringLiteral("file.print_bin")))
        action->setText(target == PrintTarget::Markers || target == PrintTarget::Console ? tr("Print") : tr("Print Bin"));
}

void MainWindow::pageSetup() {
    QPrinter& p = printer();
    if (printDialogHook_) {
        printDialogHook_(p, true);
        return;
    }
    QPageSetupDialog dialog(&p, this);
    dialog.exec();
}

bool MainWindow::printDocument(const PrintDocument& document) {
    if (document.isEmpty()) {
        QApplication::beep();
        return false;
    }
    QPrinter& p = printer();
    p.setDocName(document.title());
    if (printDialogHook_) {
        if (!printDialogHook_(p, false)) return false;
    } else {
        // "The Print dialog box opens, reflecting the specific options for your printer" (p.292).
        QPrintDialog dialog(&p, this);
        dialog.setWindowTitle(tr("Print"));
        if (dialog.exec() != QDialog::Accepted) return false;
    }
    if (!ui::printDocument(document, p)) {
        reportFailure(this, tr("Print: the printer \"%1\" could not start.").arg(p.printerName()));
        QApplication::beep();
        return false;
    }
    return true;
}

bool MainWindow::printActive() {
    switch (printTarget_) {
    case PrintTarget::Markers:
        // "The system prints the current view of marker information" (p.424).
        if (markersWindow_ && markersWindow_->isVisible())
            return printDocument(PrintDocument::fromPicture(tr("Markers"), markersWindow_->grab().toImage()));
        break;
    case PrintTarget::Console:
        // "The system prints the marker information displayed in the Console window" (p.424).
        if (console_) return printDocument(consoleDocument(console_->log()->toPlainText()));
        break;
    case PrintTarget::MediaTool:
        // "You can print Media Tool data by using the same procedures for printing bins" (p.345).
        if (mediaTool_) return printDocument(tableDocument(tr("Media Tool"), *mediaTool_->table()));
        break;
    case PrintTarget::Bin:
        break;
    }
    // "The system prints the active bin" (p.292).
    BinView* view = binContainer_ ? binContainer_->activeView() : nullptr;
    if (!view) {
        QApplication::beep();
        return false;
    }
    return printDocument(binDocument(*view));
}

bool MainWindow::printFrame() {
    // "The system prints the frame currently displayed in the active monitor" (p.292).
    const MonitorId monitor = composer_->activeMonitor();
    const model::MonitorState& state = composer_->state(monitor);
    if (state.isEmpty()) {
        QApplication::beep();
        return false;
    }
    const QImage frame = composer_->picture(monitor, QSize());
    // Ours: the clip's name and its master timecode under the frame.
    const QString caption =
        QStringLiteral("%1   %2").arg(state.current()->item.name, state.tracking(model::TrackingFormat::Master));
    // A frame may be enlarged to fill the page (a small proxy frame as much as a full raster).
    return printDocument(PrintDocument::fromPicture(state.current()->item.name, frame, caption, 8.0));
}

bool MainWindow::printTimeline() {
    // "Media Composer prints the current view of the Timeline. You can also use the Print Timeline command to print the
    // Timeline in Heads view or in Heads Tails view" (p.645): what the Timeline shows, as it shows it.
    const model::MonitorState& record = composer_->state(MonitorId::Record);
    if (record.isEmpty() || record.current()->item.kind != model::BinItemKind::Sequence) {
        QApplication::beep();
        return false;
    }
    return printDocument(PrintDocument::fromPicture(record.current()->item.name, timeline_->grab().toImage(),
                                                    record.current()->item.name));
}

} // namespace lumenis::ui

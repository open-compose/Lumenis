// Help > Check for Updates and the check at start-up (ROADMAP 10.6; docs/spec/update-check.md): an UpdateChecker
// against the project's GitHub releases, its answer said in a notice that does not block, nothing downloaded.
#include "lumenis/ui/main_window.h"

#include "lumenis/core/branding.h"
#include "lumenis/ui/tool_windows.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QPushButton>

namespace lumenis::ui {

void MainWindow::checkForUpdates(bool fromMenu) {
    auto* checker = new UpdateChecker(QString::fromUtf8(branding::kVersion), latestReleaseUrl(QString::fromUtf8(branding::kHomepage)), this);
    if (updateFetcher_) checker->setFetcher(updateFetcher_);
    connect(checker, &UpdateChecker::finished, this, [this, checker, fromMenu](const UpdateResult& result) {
        checker->deleteLater();
        showUpdateResult(result, fromMenu);
    });
    checker->check();
}

void MainWindow::showUpdateResult(const UpdateResult& result, bool fromMenu) {
    if (result.state == UpdateResult::State::Newer) lastUpdatePage_ = result.release.page;
    if (updateNoticeHook_) {
        updateNoticeHook_(result, fromMenu);
        return;
    }
    switch (result.state) {
    case UpdateResult::State::Newer: {
        // "say so and link to its page, and the user decides whether and when to download and install it" (10.6).
        auto* box = new QMessageBox(QMessageBox::Information, tr("Check for Updates"),
                                    tr("%1 %2 is available. You have %3.").arg(QString::fromUtf8(branding::kProductName),
                                                                                result.release.version, result.current),
                                    QMessageBox::NoButton, this);
        box->setInformativeText(tr("Nothing is downloaded or installed: the release page has the packages and what is new."));
        QPushButton* open = box->addButton(tr("Open Release Page"), QMessageBox::AcceptRole);
        box->addButton(tr("Later"), QMessageBox::RejectRole);
        box->setAttribute(Qt::WA_DeleteOnClose);
        const QUrl page = result.release.page;
        connect(box, &QMessageBox::buttonClicked, this, [open, page](QAbstractButton* clicked) {
            if (clicked == open) QDesktopServices::openUrl(page);
        });
        box->open(); // does not block: an editor in the middle of a session carries on
        // Console only: the notice above says it; this keeps a record of the check.
        ConsoleLog::instance().append(tr("Update check: %1 is available (%2).").arg(result.release.version, page.toString()));
        return;
    }
    case UpdateResult::State::UpToDate:
    case UpdateResult::State::NoRelease:
        if (fromMenu) {
            auto* box = new QMessageBox(QMessageBox::Information, tr("Check for Updates"),
                                        result.state == UpdateResult::State::UpToDate
                                            ? tr("You have the latest version, %1.").arg(result.current)
                                            : tr("There is no release to compare with yet; you have %1.").arg(result.current),
                                        QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
        }
        // Console only: nothing newer, which needs no notice at start-up.
        ConsoleLog::instance().append(tr("Update check: no newer release than %1.").arg(result.current));
        return;
    case UpdateResult::State::Failed:
        if (fromMenu) {
            reportFailure(this, tr("Check for Updates: %1").arg(result.error));
        } else {
            // A check at start-up that could not reach GitHub (offline, a firewall) is not worth a message to someone
            // starting work. Console only: the reason is kept for whoever looks.
            ConsoleLog::instance().append(tr("Update check: %1").arg(result.error));
        }
        return;
    }
}

} // namespace lumenis::ui

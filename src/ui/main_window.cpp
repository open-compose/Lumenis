#include "lumenis/ui/main_window.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/menu_builder.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMenuBar>
#include <QMessageBox>

namespace lumenis::ui {

MainWindow::MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers, QWidget* parent)
    : QMainWindow(parent), commands_(commands), providers_(providers) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    resize(1280, 800);
    // The area panels are arranged around. Empty until Phase 1 adds panels.
    auto* workspace = new QWidget(this);
    workspace->setObjectName(QStringLiteral("WorkspaceArea"));
    setCentralWidget(workspace);
    installHandlers();
    buildBuiltinMenus(menuBar(), commands_);
}

void MainWindow::setProject(const model::ProjectEntry& project) {
    project_ = project;
    setWindowTitle(QStringLiteral("%1 - %2").arg(QString::fromUtf8(branding::kProductName), project.settings.name));
    commands_.setHandler(QStringLiteral("file.close_project"), [this] { emit closeProjectRequested(); });
}

void MainWindow::clearProject() {
    project_.reset();
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    commands_.setHandler(QStringLiteral("file.close_project"), {});
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QMainWindow::closeEvent(event);
    if (event->isAccepted()) {
        emit closed();
    }
}

void MainWindow::installHandlers() {
    commands_.setHandler(QStringLiteral("file.exit"), [] { QApplication::closeAllWindows(); });
    commands_.setHandler(QStringLiteral("help.about"), [this] { showAbout(); });
    commands_.setHandler(QStringLiteral("help.about_qt"), [] { QApplication::aboutQt(); });
}

void MainWindow::showAbout() {
    const QString name = QString::fromUtf8(branding::kProductName);
    QMessageBox::about(
        this, tr("About %1").arg(name),
        tr("<h3>%1 %2</h3><p>%3</p>"
           "<p>Licensed under the GNU General Public License v3.</p>"
           "<p>%4 extension providers registered.</p>"
           "<p>The interface font is Inter, under the SIL Open Font License 1.1.</p>"
           "<p>Avid and Media Composer are trademarks of Avid Technology, Inc. "
           "This product is not affiliated with or endorsed by Avid.</p>")
            .arg(name, QString::fromUtf8(branding::kVersion), QString::fromUtf8(branding::kDescription))
            .arg(providers_.all().size()));
}

} // namespace lumenis::ui

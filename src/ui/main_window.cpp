#include "lumenis/ui/main_window.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"

#include <QAction>
#include <QApplication>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>

namespace lumenis::ui {

MainWindow::MainWindow(const ext::ProviderRegistry& registry, QWidget* parent)
    : QMainWindow(parent), registry_(registry) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    resize(1280, 800);
    createMenus();
}

void MainWindow::createMenus() {
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->setObjectName(QStringLiteral("menuFile"));
    QAction* quit = fileMenu->addAction(tr("E&xit"), qApp, &QApplication::closeAllWindows);
    quit->setObjectName(QStringLiteral("actionExit"));
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setObjectName(QStringLiteral("menuHelp"));
    QAction* about = helpMenu->addAction(
        tr("About %1").arg(QString::fromUtf8(branding::kProductName)), this, &MainWindow::showAbout);
    about->setObjectName(QStringLiteral("actionAbout"));
    about->setMenuRole(QAction::AboutRole);
    QAction* aboutQt = helpMenu->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);
    aboutQt->setMenuRole(QAction::AboutQtRole);
}

void MainWindow::showAbout() {
    const QString name = QString::fromUtf8(branding::kProductName);
    QMessageBox::about(
        this, tr("About %1").arg(name),
        tr("<h3>%1 %2</h3><p>%3</p>"
           "<p>Licensed under the GNU General Public License v3.</p>"
           "<p>%4 extension providers registered.</p>"
           "<p>Avid and Media Composer are trademarks of Avid Technology, Inc. "
           "This product is not affiliated with or endorsed by Avid.</p>")
            .arg(name, QString::fromUtf8(branding::kVersion),
                 QString::fromUtf8(branding::kDescription))
            .arg(registry_.all().size()));
}

} // namespace lumenis::ui

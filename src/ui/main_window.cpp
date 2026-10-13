#include "lumenis/ui/main_window.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/workspace.h"
#include "lumenis/ui/workspace_bar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QToolButton>

#include <algorithm>

namespace lumenis::ui {

namespace {

// Stand-ins until the real windows exist (Bins: Phase 1.3; Composer and
// Timeline: Phase 1.4). They are painted without text, so they look the
// same on every platform.
class Placeholder : public QWidget {
public:
    enum class Kind { Bins, Composer, Timeline };

    Placeholder(Kind kind, const QString& name) : kind_(kind) { setObjectName(name); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        switch (kind_) {
        case Kind::Bins:
            p.fillRect(rect(), QColor(0x2a, 0x2a, 0x2a));
            break;
        case Kind::Timeline:
            p.fillRect(rect(), QColor(0x30, 0x30, 0x30));
            break;
        case Kind::Composer: {
            // Source and record monitors side by side (p.74).
            p.fillRect(rect(), QColor(0x26, 0x26, 0x26));
            const int margin = 6;
            const int gap = 4;
            const int w = (width() - 2 * margin - gap) / 2;
            const int h = std::min(w * 9 / 16, height() - 2 * margin);
            if (w > 0 && h > 0) {
                p.fillRect(QRect(margin, margin, w, h), Qt::black);
                p.fillRect(QRect(margin + w + gap, margin, w, h), Qt::black);
            }
            break;
        }
        }
    }

private:
    Kind kind_;
};

QJsonObject tabs(const QString& id) {
    return {{QStringLiteral("tabs"), QJsonArray{id}}, {QStringLiteral("current"), 0}};
}

QJsonObject split(const char* orientation, const QJsonArray& sizes, const QJsonArray& children) {
    return {{QStringLiteral("split"), QLatin1String(orientation)},
            {QStringLiteral("sizes"), sizes},
            {QStringLiteral("children"), children}};
}

} // namespace

MainWindow::MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers, QWidget* parent)
    : QMainWindow(parent), commands_(commands), providers_(providers) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    resize(1280, 800);

    panels_ = new PanelManager(this);
    workspaces_ = new WorkspaceManager(*panels_, this);
    for (const QString& name : workspaces_->names()) {
        workspaces_->setDefaultLayout(name, defaultLayout());
    }

    auto* area = new QWidget(this);
    area->setObjectName(QStringLiteral("WorkspaceArea"));
    auto* layout = new QHBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(panels_->mainHost(), 1);
    bar_ = new WorkspaceBar(*workspaces_, area);
    layout->addWidget(bar_);
    setCentralWidget(area);

    // With the toolbar hidden, a workspace button at the top right brings
    // up the workspace menu (p.88-89).
    workspaceButton_ = new QToolButton(area);
    workspaceButton_->setObjectName(QStringLiteral("workspaceButton"));
    workspaceButton_->setPopupMode(QToolButton::InstantPopup);
    workspaceButton_->setAutoRaise(true);
    workspaceButton_->setFixedSize(26, kToolButtonHeight);
    workspaceButton_->setMenu(new QMenu(workspaceButton_));
    area->installEventFilter(this);

    createPanels();
    installHandlers();
    buildBuiltinMenus(menuBar(), commands_);
    installPanelHandlers();
    panels_->restoreLayout(defaultLayout());
    connect(workspaces_, &WorkspaceManager::changed, this, &MainWindow::syncWorkspaceUi);
    syncWorkspaceUi();
}

MainWindow::~MainWindow() = default;

QJsonObject MainWindow::defaultLayout() {
    // Proportions measured from the picture on p.74.
    const QJsonObject top = split("h", {38, 62}, {tabs(QStringLiteral("BinContainer")), tabs(QStringLiteral("Composer"))});
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("main"), split("v", {52, 48}, {top, tabs(QStringLiteral("Timeline"))})},
            {QStringLiteral("floating"), QJsonArray()},
            {QStringLiteral("active"), QStringLiteral("Composer")},
            {QStringLiteral("blankHomes"), QJsonObject()}};
}

void MainWindow::createPanels() {
    panels_->addPanel(new Panel(QStringLiteral("BinContainer"), tr("Bin Container"),
                                new Placeholder(Placeholder::Kind::Bins, QStringLiteral("binsPlaceholder"))));
    panels_->addPanel(new Panel(QStringLiteral("Composer"), tr("Composer"),
                                new Placeholder(Placeholder::Kind::Composer, QStringLiteral("composerPlaceholder"))));
    panels_->addPanel(new Panel(QStringLiteral("Timeline"), tr("Timeline"),
                                new Placeholder(Placeholder::Kind::Timeline, QStringLiteral("timelinePlaceholder"))));
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == centralWidget() && event->type() == QEvent::Resize) {
        placeWorkspaceButton();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::installHandlers() {
    commands_.setHandler(QStringLiteral("file.exit"), [] { QApplication::closeAllWindows(); });
    commands_.setHandler(QStringLiteral("help.about"), [this] { showAbout(); });
    commands_.setHandler(QStringLiteral("help.about_qt"), [] { QApplication::aboutQt(); });
}

void MainWindow::installPanelHandlers() {
    auto handle = [this](const char* id, std::function<void()> handler) {
        commands_.setHandler(QString::fromLatin1(id), std::move(handler));
    };
    handle("windows.float_all_panels", [this] { panels_->floatAll(); });
    handle("windows.float_active_panel", [this] { panels_->floatActive(); });
    handle("windows.add_blank_panel", [this] { panels_->addBlankPanel(); });
    handle("windows.workspaces.new_workspace", [this] { newWorkspace(); });
    handle("windows.workspaces.delete_workspace", [this] { deleteWorkspace(); });
    handle("windows.workspaces.restore_to_saved", [this] { workspaces_->restoreToSaved(); });
    handle("windows.workspaces.restore_current_to_default", [this] { restoreCurrentToDefault(); });
    handle("windows.workspaces.save_current", [this] { workspaces_->saveCurrent(); });
    QAction* showBar = commands_.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"));
    showBar->setCheckable(true);
    handle("windows.workspaces.show_workspace_toolbar", [this, showBar] {
        workspaces_->setBarMode(showBar->isChecked() ? WorkspaceManager::BarMode::IconAndText
                                                     : WorkspaceManager::BarMode::Hidden);
    });
    handle("windows.next_window", [this] { panels_->cycleWindows(1); });
    handle("windows.previous_window", [this] { panels_->cycleWindows(-1); });
    handle("windows.next_tab", [this] { panels_->cycleTabs(); });
    // File > Close closes the active window.
    handle("file.close", [this] {
        if (Panel* p = panels_->activePanel()) panels_->closePanel(p);
    });
    handle("tools.timeline", [this] { panels_->showPanel(panels_->panel(QStringLiteral("Timeline"))); });

    // Keyboard-only commands work wherever the main window is active.
    for (const char* id : {"windows.next_window", "windows.previous_window", "windows.next_tab",
                           "windows.workspaces.save_current"}) {
        addAction(commands_.action(QString::fromLatin1(id)));
    }
    // Floating windows are separate windows: give them every command so
    // their shortcuts work there too.
    connect(panels_, &PanelManager::floatingWindowCreated, this, [this](QWidget* window) {
        for (const QString& id : commands_.ids()) window->addAction(commands_.action(id));
    });
}

void MainWindow::syncWorkspaceUi() {
    const bool barShown = workspaces_->barMode() != WorkspaceManager::BarMode::Hidden;
    commands_.action(QStringLiteral("windows.workspaces.show_workspace_toolbar"))->setChecked(barShown);
    commands_.action(QStringLiteral("windows.workspaces.delete_workspace"))->setEnabled(workspaces_->canRemoveActive());

    // Windows > Workspaces lists the workspaces above its commands (p.86).
    if (auto* menu = menuBar()->findChild<QMenu*>(QStringLiteral("menu:windows_workspaces"))) {
        qDeleteAll(workspaceActions_);
        workspaceActions_.clear();
        QAction* first = menu->actions().value(0);
        for (const QString& name : workspaces_->names()) {
            auto* a = new QAction(name, menu);
            a->setCheckable(true);
            a->setChecked(name == workspaces_->active());
            a->setProperty(kDynamicMenuItem, true);
            connect(a, &QAction::triggered, this, [this, name] { workspaces_->activate(name); });
            menu->insertAction(first, a);
            workspaceActions_ << a;
        }
        QAction* separator = new QAction(menu);
        separator->setSeparator(true);
        separator->setProperty(kDynamicMenuItem, true);
        menu->insertAction(first, separator);
        workspaceActions_ << separator;
    }

    // The menu behind the workspace button (p.89).
    QMenu* menu = workspaceButton_->menu();
    menu->clear();
    for (const QString& name : workspaces_->names()) {
        QAction* a = menu->addAction(WorkspaceBar::iconFor(name, false), name.toUpper());
        a->setCheckable(true);
        a->setChecked(name == workspaces_->active());
        connect(a, &QAction::triggered, this, [this, name] { workspaces_->activate(name); });
    }
    menu->addSeparator();
    for (const char* id : {"windows.workspaces.save_current", "windows.workspaces.restore_current_to_default",
                           "windows.workspaces.new_workspace", "windows.workspaces.delete_workspace"}) {
        menu->addAction(commands_.action(QString::fromLatin1(id)));
    }
    menu->addSeparator();
    QAction* show = menu->addAction(tr("Show Workspace Bar"));
    connect(show, &QAction::triggered, this,
            [this] { workspaces_->setBarMode(WorkspaceManager::BarMode::IconAndText); });
    workspaceButton_->setIcon(WorkspaceBar::iconFor(workspaces_->active(), true));
    workspaceButton_->setVisible(!barShown);
    placeWorkspaceButton();
}

void MainWindow::placeWorkspaceButton() {
    if (QWidget* area = centralWidget()) {
        workspaceButton_->move(area->width() - workspaceButton_->width() - 2, 2);
        workspaceButton_->raise();
    }
}

void MainWindow::newWorkspace() {
    NewWorkspaceDialog dialog(*workspaces_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!workspaces_->create(dialog.name(), dialog.basedOn(), &error)) {
        QMessageBox::warning(this, tr("New Workspace"), error);
    }
}

void MainWindow::deleteWorkspace() {
    if (!workspaces_->canRemoveActive()) return;
    const auto answer =
        QMessageBox::question(this, tr("Delete Workspace"),
                              tr("Delete the workspace \"%1\"?").arg(workspaces_->active()),
                              QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Ok) workspaces_->removeActive();
}

void MainWindow::restoreCurrentToDefault() {
    // "A message box warns you that the action deletes your custom
    // workspace settings" (p.86).
    const auto answer = QMessageBox::warning(
        this, tr("Restore Current to Default"),
        tr("This deletes your custom settings for the workspace \"%1\".").arg(workspaces_->active()),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Ok) workspaces_->restoreCurrentToDefault();
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

#include "lumenis/ui/main_window.h"

#include "lumenis/core/branding.h"
#include "lumenis/ext/provider.h"
#include "lumenis/ui/bin_container.h"
#include "lumenis/ui/bin_view.h"
#include "lumenis/ui/command_registry.h"
#include "lumenis/ui/composer.h"
#include "lumenis/ui/keyboard_map.h"
#include "lumenis/ui/menu_builder.h"
#include "lumenis/ui/panels.h"
#include "lumenis/ui/theme.h"
#include "lumenis/ui/timeline.h"
#include "lumenis/ui/workspace.h"
#include "lumenis/ui/workspace_bar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>

namespace lumenis::ui {

namespace {

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
    binContainer_->setBinMenu(menuBar()->findChild<QMenu*>(QStringLiteral("menu:bin")));
    installProjectHandlers(false);
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
    // Its tab reads "Bins" in the pictures on p.240-242.
    binContainer_ = new BinContainer;
    panels_->addPanel(new Panel(QStringLiteral("BinContainer"), tr("Bins"), binContainer_));
    // Tools > Project: a Sidebar-only Bin Container named Project (p.243). As
    // a panel it takes part in workspaces, floating or not.
    panels_->addPanel(new Panel(QStringLiteral("Project"), tr("Project"), new BinSidebar(*binContainer_)));
    // The Composer window (ch. 13).
    composer_ = new Composer;
    composer_->setItemResolver([this](const QString& binId, const QString& itemId) -> std::optional<model::BinItem> {
        const BinDocument* doc = binContainer_->document(binId);
        const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr;
        if (!item) return std::nullopt;
        return *item;
    });
    keyboard_ = std::make_unique<KeyboardMap>();
    keyboard_->loadBuiltin(commands_);
    composer_->setKeyResolver([this](const QKeySequence& key) { return keyboard_->commandFor(key); });
    panels_->addPanel(new Panel(QStringLiteral("Composer"), tr("Composer"), composer_));
    // Double-click in a bin loads the item (p.394).
    connect(binContainer_, &BinContainer::itemActivated, this, [this](const QString& binId, const QString& itemId) {
        const BinDocument* doc = binContainer_->document(binId);
        if (const model::BinItem* item = doc ? doc->bin().item(itemId) : nullptr) {
            composer_->loadDefault(binId, *item);
            panels_->showPanel(panels_->panel(QStringLiteral("Composer")));
        }
    });
    // The Timeline (ch. 19), showing the Record monitor's sequence.
    timeline_ = new Timeline(*composer_);
    timeline_->setKeyResolver([this](const QKeySequence& key) { return keyboard_->commandFor(key); });
    timeline_->setSequenceStore([this](const QString& binId, const model::BinItem& item, bool isNew) {
        BinDocument* doc = binContainer_->document(binId);
        if (!doc) doc = binContainer_->openBin(binId);
        if (!doc) return false;
        if (isNew) {
            doc->addItem(item);
            return true;
        }
        return doc->replaceItem(item);
    });
    timeline_->setSequenceMaker([this]() -> std::optional<std::pair<QString, model::BinItem>> {
        // The first edit makes a sequence in the active bin (p.463).
        BinDocument* doc = binContainer_->activeDocument();
        if (!doc && !binContainer_->openDocuments().isEmpty()) doc = binContainer_->openDocuments().first();
        if (!doc || !project_) return std::nullopt;
        return std::make_pair(doc->nodeId(), model::newSequence(doc->bin(), project_->settings.format.editRate,
                                                            QDateTime::currentDateTimeUtc()));
    });
    connect(timeline_, &Timeline::message, this, [](const QString& text) { QToolTip::showText(QCursor::pos(), text); });
    connect(composer_, &Composer::commandRequested, this, [this](const QString& id) {
        if (Timeline::handles(id)) timeline_->perform(id);
    });
    panels_->addPanel(new Panel(QStringLiteral("Timeline"), tr("Timeline"), timeline_));
}

void MainWindow::setProject(const model::ProjectEntry& project) {
    project_ = project;
    setWindowTitle(QStringLiteral("%1 - %2").arg(QString::fromUtf8(branding::kProductName), project.settings.name));
    commands_.setHandler(QStringLiteral("file.close_project"), [this] { emit closeProjectRequested(); });
    binContainer_->setProject(project.folder, project.settings.name, project.settings.format.editRate);
    composer_->clear();
    composer_->setAspectRatio(project.settings.aspect);
    timeline_->history().clear();
    installProjectHandlers(true);
}

void MainWindow::clearProject() {
    project_.reset();
    setWindowTitle(QString::fromUtf8(branding::kProductName));
    commands_.setHandler(QStringLiteral("file.close_project"), {});
    binContainer_->setProject({}, {}, Rational(24000, 1001));
    composer_->clear();
    timeline_->history().clear();
    installProjectHandlers(false);
}

bool MainWindow::binsActive() const {
    // A floating Bin Container (p.241) is a window of its own.
    if (qobject_cast<FloatingBinContainer*>(QApplication::activeWindow())) return true;
    const Panel* p = panels_->activePanel();
    return p && (p->id() == QLatin1String("BinContainer") || p->id() == QLatin1String("Project"));
}

void MainWindow::installProjectHandlers(bool projectOpen) {
    auto handle = [this, projectOpen](const char* id, std::function<void()> handler) {
        commands_.setHandler(QString::fromLatin1(id), projectOpen ? std::move(handler) : std::function<void()>());
    };
    BinContainer* bins = binContainer_;
    handle("file.open_bin", [this, bins] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Open a Bin"), {}, tr("Bins (*.cthbin)"));
        if (file.isEmpty()) return;
        QString error;
        if (bins->openOtherBin(file, &error).isEmpty()) QMessageBox::warning(this, tr("Open Bin"), error);
    });
    // Both open the selected bins as tabs; separate Bin Containers come later.
    handle("file.open_selected_bins", [bins] {
        for (const QString& id : bins->selectedNodes()) bins->openBin(id);
    });
    handle("file.open_selected_bins_in_one_window", [bins] {
        for (const QString& id : bins->selectedNodes()) bins->openBin(id);
    });
    handle("file.close_bin", [bins] { bins->closeBin(bins->activeDocument()); });
    handle("file.save_bin", [this, bins] {
        QString error;
        if (BinDocument* d = bins->activeDocument(); d && !bins->saveBin(d, &error)) {
            QMessageBox::warning(this, tr("Save Bin"), error);
        }
    });
    auto saveAll = [this, bins] {
        QString error;
        if (!bins->saveAll(&error)) QMessageBox::warning(this, tr("Save All"), error);
    };
    handle("file.save_all", saveAll);
    handle("file.save_all_bins", saveAll);
    QAction* sidebarOnly = commands_.action(QStringLiteral("bin.show_sidebar_only"));
    sidebarOnly->setCheckable(true);
    sidebarOnly->setChecked(bins->isSidebarOnly());
    handle("bin.show_sidebar_only", [bins, sidebarOnly] { bins->setSidebarOnly(sidebarOnly->isChecked()); });
    handle("windows.close_all_bins_but_active", [bins] { bins->closeAllBinsButActive(); });
    handle("timeline.new.sequence", [this, bins] {
        QString target;
        if (!bins->activeDocument()) {
            // With no active bin, the Select dialog asks for one (p.457).
            QStringList names;
            QStringList ids;
            for (const model::BinNode* b : bins->bins()->bins()) {
                names << b->name;
                ids << b->id;
            }
            if (names.isEmpty()) return;
            bool ok = false;
            const QString choice = QInputDialog::getItem(this, tr("Select"), tr("Bin:"), names, 0, false, &ok);
            if (!ok) return;
            target = ids.value(static_cast<int>(names.indexOf(choice)));
        }
        const QString id = bins->newSequence(target);
        // In the bin, the Record monitor and the Timeline (p.457).
        BinDocument* doc = target.isEmpty() ? bins->activeDocument() : bins->document(target);
        if (const model::BinItem* item = doc ? doc->bin().item(id) : nullptr) {
            composer_->load(MonitorId::Record, doc->nodeId(), *item);
        }
        panels_->showPanel(panels_->panel(QStringLiteral("BinContainer")));
    });
    // Edit menu commands act on the bin when the Bin Container is active.
    handle("edit.duplicate", [this, bins] {
        if (binsActive()) bins->duplicateSelection();
    });
    handle("edit.delete", [this, bins] {
        if (binsActive()) bins->deleteSelection();
    });
    handle("edit.select_all", [this, bins] {
        if (binsActive()) bins->selectAll();
    });

    // The active bin's views, columns, sorting and sifting (ch. 10).
    auto view = [bins](const char* id, std::function<void(BinView&)> act) {
        return std::pair<const char*, std::function<void()>>(id, [bins, act = std::move(act)] {
            if (BinView* v = bins->activeView()) act(*v);
        });
    };
    using Mode = model::BinViewMode;
    const std::pair<const char*, std::function<void()>> binCommands[] = {
        view("bin.choose_columns", [](BinView& v) { v.chooseColumns(); }),
        view("bin.hide_column", [](BinView& v) { v.hideColumns(v.selectedHeadings()); }),
        view("bin.sort", [](BinView& v) { v.sortSelected(); }),
        view("bin.sort_again", [](BinView& v) { v.sortAgain(); }),
        view("bin.sift_bin_contents", [](BinView& v) { v.siftBinContents(); }),
        view("bin.sift_selected_items", [](BinView& v) { v.siftSelected(); }),
        view("bin.show_sifted", [](BinView& v) { v.setSiftShown(true); }),
        view("bin.show_unsifted", [](BinView& v) { v.setSiftShown(false); }),
        view("bin.select.reverse", [](BinView& v) { v.reverseSelection(); }),
        view("bin.align_and_fill.align_columns", [](BinView& v) { v.alignColumns(); }),
        view("bin.align_and_fill.align", [](BinView& v) { v.frameView()->alignToGrid(false); }),
        view("bin.align_and_fill.align_selected", [](BinView& v) { v.frameView()->alignToGrid(true); }),
        view("bin.align_and_fill.fill_window", [](BinView& v) { v.frameView()->fillWindow(); }),
        view("bin.align_and_fill.fill_sorted", [](BinView& v) { v.frameView()->fillSorted(); }),
        view("clip.lock_bin_selection", [](BinView& v) { v.setLocked(true); }),
        view("clip.unlock_bin_selection", [](BinView& v) { v.setLocked(false); }),
        view("edit.enlarge_frame", [](BinView& v) {
            if (v.mode() != Mode::Text) v.enlargeFrame();
        }),
        view("edit.reduce_frame", [](BinView& v) {
            if (v.mode() != Mode::Text) v.reduceFrame();
        }),
    };
    for (const auto& [id, handler] : binCommands) handle(id, handler);
    handle("bin.set_bin_display", [bins] {
        if (BinView* v = bins->activeView()) v->setBinDisplay();
    });
    // Bin > Add Bin to Favorites (p.293).
    handle("bin.add_bin_to_favorites", [bins] { bins->addToFavorites(bins->activeDocument()); });
    // Tools > Project (p.243).
    handle("tools.project", [this] {
        Panel* project = panels_->panel(QStringLiteral("Project"));
        if (panels_->isOpen(project)) {
            panels_->showPanel(project);
            return;
        }
        const QPoint at = mapToGlobal(QPoint(40, 60));
        panels_->floatPanel(project, QRect(at, QSize(kProjectWidth, kProjectHeight)));
        panels_->activate(project);
    });
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
    // Playing, moving and marking in the Composer (ch. 13).
    for (const QString& id : commands_.ids()) {
        if (Composer::handles(id)) handle(id.toLatin1().constData(), [this, id] { composer_->perform(id); });
        // Editing, tracks and undo (ch. 15, 19).
        if (Timeline::handles(id)) handle(id.toLatin1().constData(), [this, id] { timeline_->perform(id); });
    }

    // Keyboard-only commands work wherever the main window is active.
    for (const char* id : {"windows.next_window", "windows.previous_window", "windows.next_tab",
                           "windows.workspaces.save_current"}) {
        addAction(commands_.action(QString::fromLatin1(id)));
    }
    // Floating windows are separate windows: give them every command so
    // their shortcuts work there too.
    auto giveCommands = [this](QWidget* window) {
        for (const QString& id : commands_.ids()) window->addAction(commands_.action(id));
    };
    connect(panels_, &PanelManager::floatingWindowCreated, this, giveCommands);
    connect(binContainer_, &BinContainer::floatingContainerCreated, this, giveCommands);
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

QMessageBox* MainWindow::createAboutBox() {
    const QString name = QString::fromUtf8(branding::kProductName);
    auto* box = new QMessageBox(this);
    box->setObjectName(QStringLiteral("aboutBox"));
    box->setWindowTitle(tr("About %1").arg(name));
    box->setTextFormat(Qt::RichText);
    box->setText(tr("<h3>%1 %2</h3><p>%3</p>"
                    "<p>Licensed under the GNU General Public License v3.</p>"
                    "<p>%4 extension providers registered.</p>"
                    "<p>The interface font is Inter, under the SIL Open Font License 1.1.</p>"
                    "<p>Avid and Media Composer are trademarks of Avid Technology, Inc. "
                    "This product is not affiliated with or endorsed by Avid.</p>")
                     .arg(name, QString::fromUtf8(branding::kVersion), QString::fromUtf8(branding::kDescription))
                     .arg(providers_.all().size()));
    // The application's picture, drawn from the 512 px image so it stays
    // sharp on high-density screens.
    QPixmap picture(QStringLiteral(":/lumenis/ui/icons/about.png"));
    const qreal dpr = devicePixelRatioF();
    picture = picture.scaled(QSize(kAboutPictureSize, kAboutPictureSize) * dpr, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    picture.setDevicePixelRatio(dpr);
    box->setIconPixmap(picture);
    box->setStandardButtons(QMessageBox::Ok);
    // Room for the text beside the picture.
    if (auto* label = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_label"))) label->setMinimumWidth(360);
    return box;
}

void MainWindow::showAbout() {
    QMessageBox* box = createAboutBox();
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->exec();
}

} // namespace lumenis::ui

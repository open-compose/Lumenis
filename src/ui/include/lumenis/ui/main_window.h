#pragma once

#include "lumenis/model/project_store.h"

#include <QJsonObject>
#include <QList>
#include <QMainWindow>

#include <optional>

class QAction;
class QToolButton;

namespace lumenis::ext {
class ProviderRegistry;
}

namespace lumenis::ui {

class CommandRegistry;
class PanelManager;
class WorkspaceBar;
class WorkspaceManager;

// Application frame: the menu bar, the paneled interface (Bin Container,
// Composer and Timeline, guide p.74) and the workspace toolbar on the right
// (p.87).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // The open project, shown in the title bar. File > Close Project is
    // available only while a project is open.
    void setProject(const model::ProjectEntry& project);
    void clearProject();
    const std::optional<model::ProjectEntry>& project() const { return project_; }

    PanelManager& panels() const { return *panels_; }
    WorkspaceManager& workspaces() const { return *workspaces_; }

    // The layout of a new user (p.74): Bin Container top left, Composer to
    // its right, Timeline along the bottom.
    static QJsonObject defaultLayout();

signals:
    // File > Close Project: the application returns to Select Project.
    void closeProjectRequested();
    // The user closed the window (or chose File > Exit).
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void installHandlers();
    void installPanelHandlers();
    void createPanels();
    void syncWorkspaceUi();
    void placeWorkspaceButton();
    void newWorkspace();
    void deleteWorkspace();
    void restoreCurrentToDefault();
    void showAbout();

    CommandRegistry& commands_;
    const ext::ProviderRegistry& providers_;
    std::optional<model::ProjectEntry> project_;
    PanelManager* panels_ = nullptr;
    WorkspaceManager* workspaces_ = nullptr;
    WorkspaceBar* bar_ = nullptr;
    QToolButton* workspaceButton_ = nullptr;
    QList<QAction*> workspaceActions_;
};

} // namespace lumenis::ui

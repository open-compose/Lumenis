#pragma once

#include "lumenis/model/project_store.h"

#include <QMainWindow>

#include <optional>

namespace lumenis::ext {
class ProviderRegistry;
}

namespace lumenis::ui {

class CommandRegistry;

// Application frame: the menu bar and the area that holds panels. Panels
// (Project, Bins, Composer, Timeline, tools) are added as Phase 1 proceeds.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(CommandRegistry& commands, const ext::ProviderRegistry& providers,
               QWidget* parent = nullptr);

    // The open project, shown in the title bar. File > Close Project is
    // available only while a project is open.
    void setProject(const model::ProjectEntry& project);
    void clearProject();
    const std::optional<model::ProjectEntry>& project() const { return project_; }

signals:
    // File > Close Project: the application returns to Select Project.
    void closeProjectRequested();
    // The user closed the window (or chose File > Exit).
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void installHandlers();
    void showAbout();

    CommandRegistry& commands_;
    const ext::ProviderRegistry& providers_;
    std::optional<model::ProjectEntry> project_;
};

} // namespace lumenis::ui

#pragma once

#include <QMainWindow>

namespace lumenis::ext {
class ProviderRegistry;
}

namespace lumenis::ui {

// Application frame. Phase 0 provides the window, the File and Help menus
// and the About box; Phase 1 fills in the full window and menu inventory.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const ext::ProviderRegistry& registry, QWidget* parent = nullptr);

private:
    void createMenus();
    void showAbout();

    const ext::ProviderRegistry& registry_;
};

} // namespace lumenis::ui

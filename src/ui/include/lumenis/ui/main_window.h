#pragma once

#include <QMainWindow>

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

private:
    void installHandlers();
    void showAbout();

    CommandRegistry& commands_;
    const ext::ProviderRegistry& providers_;
};

} // namespace lumenis::ui

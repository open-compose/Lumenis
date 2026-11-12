#pragma once

// Help > Find Command (ours: ROADMAP 10.3c, docs/spec/accessibility.md). Many commands live only on buttons, palettes
// and right-click menus, which a keyboard user cannot reach; this window lists every command that can run now, with
// where it is in the menus and its key, filtered as you type (every word must appear). Up and Down choose, Enter runs
// the chosen command in the window and panel that had the focus before, Esc closes.

#include <QDialog>

#include <functional>

class QKeyEvent;
class QLineEdit;
class QListWidget;
class QMenuBar;

namespace lumenis::ui {

class CommandRegistry;

class FindCommandWindow : public QDialog {
    Q_OBJECT

public:
    // `menus` gives each command's place in the menus ("Clip > New Title"); it may be null.
    FindCommandWindow(CommandRegistry& commands, const QMenuBar* menus, QWidget* parent = nullptr);

    // Fills the list again (commands are enabled and disabled as the project and selection change) and opens with an
    // empty filter, remembering the widget that had the focus.
    void showFor(QWidget* previousFocus);
    // The ids shown, in order (for tests).
    QStringList shownIds() const;
    // Runs the chosen command: closes, gives the focus back, triggers it. False with nothing chosen.
    bool runChosen();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refill();
    void filter(const QString& text);

    CommandRegistry& commands_;
    const QMenuBar* menus_;
    QLineEdit* search_ = nullptr;
    QListWidget* list_ = nullptr;
    QWidget* previousFocus_ = nullptr;
};

} // namespace lumenis::ui

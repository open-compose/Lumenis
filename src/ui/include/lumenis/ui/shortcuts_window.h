#pragma once

#include <QWidget>

class QLineEdit;
class QTableWidget;

namespace lumenis::ui {

class CommandRegistry;
class KeyboardMap;

// Help > Shortcuts (Editing Guide p.97; docs/spec/set-font.md): "For a list of other functions that use
// modifier keys, select Help > Shortcuts." Ours: every command with a menu shortcut or a key in the
// Keyboard setting, with both, filtered by the text typed above the list. Read when shown, so it
// follows changes to the keyboard. MainWindow keeps one.
class ShortcutsWindow : public QWidget {
    Q_OBJECT

public:
    // Both must outlive the window.
    ShortcutsWindow(const CommandRegistry& commands, const KeyboardMap& keyboard, QWidget* parent = nullptr);

    QTableWidget* table() const { return table_; }
    QLineEdit* filter() const { return filter_; }
    // Fills the list again.
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    const CommandRegistry& commands_;
    const KeyboardMap& keyboard_;
    QLineEdit* filter_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace lumenis::ui

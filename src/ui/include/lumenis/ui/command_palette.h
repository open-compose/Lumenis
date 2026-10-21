#pragma once

#include "lumenis/ui/button_mapping.h"

#include <QHash>
#include <QJsonArray>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QTabWidget;

namespace lumenis::ui {

class CommandRegistry;

// A button of the Command Palette: dragged onto a mappable button in
// Button to Button mode, clicked in Active Palette mode (p.98-99).
class PaletteButton : public QToolButton {
    Q_OBJECT

public:
    // An empty `commandId` is a blank button. The icon and tooltip come from ButtonMapper.
    PaletteButton(const QString& commandId, QWidget* parent = nullptr);
    // The command id; empty for a blank button.
    const QString& command() const { return command_; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QString command_;
    QPoint pressAt_;
};

// Tools > Command Palette (guide p.96-100): the user-selectable buttons on
// tabs by editing function, the Quick Find field, and the three modes at
// the bottom. Its buttons and tabs are in resources/command_palette.json.
class CommandPalette : public QWidget {
    Q_OBJECT

public:
    // Builds the tabs from the built-in command_palette.json, leaving out commands the registry does not
    // know and buttons for other platforms. Also hands `commands` to ButtonMapper.
    explicit CommandPalette(CommandRegistry& commands, QWidget* parent = nullptr);

    QTabWidget* tabs() const { return tabs_; }
    QLineEdit* quickFind() const { return find_; }
    PaletteButton* button(const QString& commandId) const { return buttons_.value(commandId); }
    // In the order of the file.
    QStringList tabNames() const;
    // The command ids whose first appearance is on that tab, sorted; empty for an unknown tab.
    QStringList commandsOn(const QString& tabName) const;

    // Quick Find (p.99-100): the buttons whose names contain the text; only
    // tabs with one stay open.
    // Command ids, sorted; case-insensitive, and empty for blank text.
    QStringList matches(const QString& text) const;
    // Also sets the field's text, hides the tabs without a match and dims
    // the labels (not the buttons) that do not match. Blank text shows every
    // tab and label again.
    void setQuickFind(const QString& text);
    // Shows a command's tab and selects its button.
    // False if the command has no button.
    bool showCommand(const QString& commandId);

    // Active Palette (p.99): runs a command as if chosen from its menu.
    // Does nothing if the command is unknown or its action is disabled.
    void activate(const QString& commandId);
    // Dims the buttons of commands that are not available.
    // Buttons of commands without a handler are disabled; blank buttons stay enabled. Runs on every show.
    void refresh();

    // The Workspaces tab (p.91): a workspace menu beside each W button.
    // `assigned` holds W1's workspace first; "" is None.
    void setWorkspaces(const QStringList& names, const QStringList& assigned);
    // `index` 0 is W1's menu; null out of range.
    QComboBox* workspaceMenu(int index) const { return workspaceMenus_.value(index); }

signals:
    // The user chose a workspace for W button `index` (0-based); `name` is empty for None. Not emitted while
    // setWorkspaces() fills the menus.
    void workspaceAssigned(int index, const QString& name);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Checks the radio button of ButtonMapper's current mode.
    void syncMode();

    CommandRegistry& commands_;
    QTabWidget* tabs_ = nullptr;
    QLineEdit* find_ = nullptr;
    QButtonGroup* modes_ = nullptr;
    // By command id: the first button of each command. The key "" is the first blank button.
    QHash<QString, PaletteButton*> buttons_;
    QHash<QString, QLabel*> labels_;
    // Every button, also those of commands on two tabs.
    struct Row {
        QString id;
        QLabel* label;
        PaletteButton* button;
    };
    QList<Row> rows_;
    // Command id -> index of the first tab it is on.
    QHash<QString, int> tabOf_;
    QStringList tabNames_;
    QList<QComboBox*> workspaceMenus_;
    // Set while setWorkspaces() fills the menus, so it does not emit workspaceAssigned().
    bool fillingMenus_ = false;
};

// The Tool palette (p.392): mappable buttons for editing and moving, opened
// with the Tool Palette button or from the Settings list. Its buttons are
// saved in the Tool Palette setting (p.1221).
class ToolPalette : public QWidget {
    Q_OBJECT

public:
    static constexpr int kColumns = 4;
    static constexpr int kRows = 3;

    explicit ToolPalette(QWidget* parent = nullptr);

    // kRows * kColumns ids, row by row.
    static QStringList defaultCommands();
    // The command of each button, row by row; "" for a blank one.
    QStringList commands() const;
    // Fewer entries than slots leaves the rest at their defaults.
    void setCommands(const QStringList& commands);
    // Interface settings, Show Labels in Tool Palette (p.392).
    // Labels make every button twice as tall.
    void setShowLabels(bool on);
    bool showsLabels() const { return labels_; }
    MappableButton* slot(int index) const { return slots_.value(index); }

signals:
    // A button that is not blank was clicked; the owner (MainWindow) runs the command.
    void commandClicked(const QString& commandId);
    // A command was mapped onto one of its buttons.
    void commandsChanged();

private:
    QList<MappableButton*> slots_;
    bool labels_ = false;
};

} // namespace lumenis::ui

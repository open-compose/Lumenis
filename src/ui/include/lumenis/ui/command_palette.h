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
    PaletteButton(const QString& commandId, QWidget* parent = nullptr);
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
    explicit CommandPalette(CommandRegistry& commands, QWidget* parent = nullptr);

    QTabWidget* tabs() const { return tabs_; }
    QLineEdit* quickFind() const { return find_; }
    PaletteButton* button(const QString& commandId) const { return buttons_.value(commandId); }
    QStringList tabNames() const;
    QStringList commandsOn(const QString& tabName) const;

    // Quick Find (p.99-100): the buttons whose names contain the text; only
    // tabs with one stay open.
    QStringList matches(const QString& text) const;
    void setQuickFind(const QString& text);
    // Shows a command's tab and selects its button.
    bool showCommand(const QString& commandId);

    // Active Palette (p.99): runs a command as if chosen from its menu.
    void activate(const QString& commandId);
    // Dims the buttons of commands that are not available.
    void refresh();

    // The Workspaces tab (p.91): a workspace menu beside each W button.
    // `assigned` holds W1's workspace first; "" is None.
    void setWorkspaces(const QStringList& names, const QStringList& assigned);
    QComboBox* workspaceMenu(int index) const { return workspaceMenus_.value(index); }

signals:
    void workspaceAssigned(int index, const QString& name);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void syncMode();

    CommandRegistry& commands_;
    QTabWidget* tabs_ = nullptr;
    QLineEdit* find_ = nullptr;
    QButtonGroup* modes_ = nullptr;
    QHash<QString, PaletteButton*> buttons_;
    QHash<QString, QLabel*> labels_;
    // Every button, also those of commands on two tabs.
    struct Row {
        QString id;
        QLabel* label;
        PaletteButton* button;
    };
    QList<Row> rows_;
    QHash<QString, int> tabOf_;
    QStringList tabNames_;
    QList<QComboBox*> workspaceMenus_;
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

    static QStringList defaultCommands();
    QStringList commands() const;
    // Fewer entries than slots leaves the rest at their defaults.
    void setCommands(const QStringList& commands);
    // Interface settings, Show Labels in Tool Palette (p.392).
    void setShowLabels(bool on);
    bool showsLabels() const { return labels_; }
    MappableButton* slot(int index) const { return slots_.value(index); }

signals:
    void commandClicked(const QString& commandId);
    // A command was mapped onto one of its buttons.
    void commandsChanged();

private:
    QList<MappableButton*> slots_;
    bool labels_ = false;
};

} // namespace lumenis::ui

#pragma once

#include "lumenis/ui/command_registry.h"

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QToolButton>

#include <functional>

class QMimeData;

namespace lumenis::ui {

class MappableButton;

// Button mapping with the Command Palette (guide p.96-99). The mode is
// chosen at the bottom of the Command Palette:
// * Button to Button: drag a button from the palette onto a button of a
//   Tool palette or the Composer;
// * Active Palette: clicking a palette button runs its command;
// * Menu to Button: click a target button, then choose a menu command.
class ButtonMapper : public QObject {
    Q_OBJECT

public:
    enum class Mode { ButtonToButton, Active, MenuToButton };

    static ButtonMapper& instance();
    static QString mimeType() { return QStringLiteral("application/x-cutthulhu-command"); }
    // The drag data for a command ("" is the Blank button, p.97).
    static QMimeData* mimeFor(const QString& commandId);

    Mode mode() const { return mode_; }
    void setMode(Mode mode);

    // Commands' texts, for initials and tooltips.
    void setRegistry(CommandRegistry* registry);
    CommandRegistry* registry() const { return registry_; }
    QString text(const QString& commandId) const;

    // Menu to Button: the button waiting for a menu command.
    MappableButton* target() const { return target_; }
    void setTarget(MappableButton* button);
    // Maps a chosen menu command to the target; false without one.
    bool mapMenuCommand(const QString& commandId);

    // Command pictures changed (a workspace button's name): every button
    // shows its command's picture again.
    void refreshIcons() { emit iconsChanged(); }

signals:
    void modeChanged(Mode mode);
    void targetChanged();
    void iconsChanged();

private:
    Mode mode_ = Mode::ButtonToButton;
    QPointer<CommandRegistry> registry_; // cleared when it goes
    QPointer<MappableButton> target_;
};

// A command's button picture: one registered by the window that draws it
// (the Composer's and Timeline's own buttons), otherwise the command's
// initials, as a mapped menu command shows them (p.98-99).
void registerCommandIcon(const QString& commandId, const QIcon& icon);
QIcon commandIcon(const QString& commandId);
// A picture of a few letters (initials, a workspace button's label).
QIcon labelIcon(const QString& text, bool italic = false);
// "Load Filler" -> "LF".
QString commandInitials(const QString& text);

// A user-selectable button (p.96): a command, or Blank. It takes commands
// dropped from the Command Palette, and in Menu to Button mode a click picks
// it as the target instead of running it.
class MappableButton : public QToolButton {
    Q_OBJECT

public:
    explicit MappableButton(QWidget* parent = nullptr);

    const QString& command() const { return command_; }
    // Shows the command (its picture and name); "" is Blank.
    void setCommand(const QString& commandId);
    bool isBlank() const { return command_.isEmpty(); }

signals:
    // The user mapped a command (or Blank) to this button.
    void commandMapped(const QString& commandId);
    // Clicked with a command, outside Menu to Button mode.
    void commandClicked(const QString& commandId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString command_;
};

} // namespace lumenis::ui

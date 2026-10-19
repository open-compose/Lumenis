#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QString>

#include <functional>
#include <vector>

class QAction;
class QSettings;

namespace lumenis::ui {

// Everything the user can invoke (menu items, keyboard shortcuts, and later
// the Command Palette and mapped buttons) is a command with a stable id such
// as "file.exit". Commands are declared in resources/commands.json; code
// attaches handlers. A command without a handler stays visible but disabled,
// so menus can show the full inventory before every feature exists.
struct CommandSpec {
    QString id;
    QString text;           // menu text, '&' marks the mnemonic
    QKeySequence shortcut;  // default; Ctrl means Command on macOS
    QString role;           // "quit", "about", "aboutQt", "preferences" or empty
};

// Holds every declared command with its QAction and handler. The program (app/main.cpp) loads the
// built-in declarations; MainWindow attaches handlers and builds the menus from the actions
// (menu_builder.h).
class CommandRegistry : public QObject {
    Q_OBJECT

public:
    explicit CommandRegistry(QObject* parent = nullptr);

    // Loads declarations from a commands.json document. "%PRODUCT%" in text
    // is replaced by the product name. Returns false and sets *error on a
    // malformed document or duplicate id.
    // The document is {"commands": [{"id", "text", "shortcut", "shortcutMac", "role"}]}; shortcuts are in
    // Qt's portable text ("Ctrl+Q"), and "shortcutMac" replaces "shortcut" on macOS. Commands read before an
    // error stay registered. Each new action starts disabled.
    bool load(const QByteArray& json, QString* error = nullptr);
    // Loads the built-in declarations from the application's resources.
    // Exits with a fatal error if the built-in file is invalid.
    void loadBuiltin();

    // Ids compare exactly (case-sensitive).
    bool contains(const QString& id) const;
    // In declaration order.
    QStringList ids() const;

    // The QAction for a command, owned by the registry. Null for unknown ids.
    QAction* action(const QString& id) const;

    // Runs `handler` when the command is triggered and enables its action; an empty handler disables it
    // again. Unknown ids are ignored with a warning.
    void setHandler(const QString& id, std::function<void()> handler);
    // False for unknown ids.
    bool hasHandler(const QString& id) const;
    // The command's text without its '&' mnemonic marks; empty for unknown
    // ids.
    QString text(const QString& id) const;

    // While a capture is set, a triggered command is offered to it first;
    // if it returns true the command does not run. Menu to Button
    // reassignment (guide p.98) uses it to pick a menu command.
    using Capture = std::function<bool(const QString& id)>;
    void setCapture(Capture capture) { capture_ = std::move(capture); }

    // Keyboard map: user overrides on top of the declared defaults.
    // The declared shortcut, empty for unknown ids or none.
    QKeySequence defaultShortcut(const QString& id) const;
    // The shortcut in effect (the action's).
    QKeySequence shortcut(const QString& id) const;
    // An empty sequence removes the shortcut. Unknown ids are ignored.
    void setShortcut(const QString& id, const QKeySequence& sequence);
    // Back to every declared default.
    void resetShortcuts();
    // In the "keyboard" group: one key per changed command, its id, holding the sequence in portable text.
    void saveShortcuts(QSettings& settings) const;
    // Resets to the defaults, then applies the stored changes.
    void loadShortcuts(QSettings& settings);

private:
    struct Entry {
        CommandSpec spec;
        QAction* action = nullptr;
        std::function<void()> handler;
    };

    Entry* find(const QString& id);
    const Entry* find(const QString& id) const;

    std::vector<Entry> entries_;
    QHash<QString, std::size_t> index_;
    Capture capture_;
};

} // namespace lumenis::ui

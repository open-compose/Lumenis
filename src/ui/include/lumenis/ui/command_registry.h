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

class CommandRegistry : public QObject {
    Q_OBJECT

public:
    explicit CommandRegistry(QObject* parent = nullptr);

    // Loads declarations from a commands.json document. "%PRODUCT%" in text
    // is replaced by the product name. Returns false and sets *error on a
    // malformed document or duplicate id.
    bool load(const QByteArray& json, QString* error = nullptr);
    // Loads the built-in declarations from the application's resources.
    void loadBuiltin();

    bool contains(const QString& id) const;
    QStringList ids() const;

    // The QAction for a command, owned by the registry. Null for unknown ids.
    QAction* action(const QString& id) const;

    void setHandler(const QString& id, std::function<void()> handler);
    bool hasHandler(const QString& id) const;

    // Keyboard map: user overrides on top of the declared defaults.
    QKeySequence defaultShortcut(const QString& id) const;
    QKeySequence shortcut(const QString& id) const;
    void setShortcut(const QString& id, const QKeySequence& sequence);
    void resetShortcuts();
    void saveShortcuts(QSettings& settings) const;
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
};

} // namespace lumenis::ui

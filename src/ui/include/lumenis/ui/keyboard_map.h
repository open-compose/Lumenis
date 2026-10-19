#pragma once

#include <QHash>
#include <QJsonObject>
#include <QKeySequence>
#include <QList>
#include <QString>

class QSettings;

namespace lumenis::ui {

class CommandRegistry;

// The editing keyboard: which key runs which command when an editing window
// (monitors, Timeline, bins) has focus. Separate from menu shortcuts, which
// QAction handles: these single keys (J, K, L, I, O, ...) must not fire
// while the user types in a text field, so editing windows look keys up
// here instead. Several keys may run the same command (Play has four).
// Defaults come from resources/keyboard.json; user changes are stored as
// differences, like menu shortcuts. MainWindow owns the map and gives the Composer and Timeline a
// key resolver that calls commandFor(); KeyboardPalette shows and edits it.
class KeyboardMap {
public:
    // Loads a keyboard.json document. Every command must exist in
    // `commands`, and no key may appear twice. Returns false and sets *error
    // otherwise, leaving the map unchanged. On success the loaded keys become
    // both the defaults and the current map.
    bool load(const QByteArray& json, const CommandRegistry& commands, QString* error = nullptr);
    // Loads the built-in :/lumenis/ui/keyboard.json; aborts (qFatal) if it is
    // invalid, since that is a build error.
    void loadBuiltin(const CommandRegistry& commands);

    // The document's "layout" field, or empty.
    QString layoutName() const { return layout_; }

    // Command id for a key, or empty.
    QString commandFor(const QKeySequence& key) const;
    // Every key that runs the command, sorted by portable text; empty if none.
    QList<QKeySequence> keysFor(const QString& commandId) const;
    // Every assigned key, in no particular order.
    QList<QKeySequence> keys() const;

    // Assigns a key; an empty id unassigns it.
    void assign(const QKeySequence& key, const QString& commandId);
    // Discards every user change.
    void resetToDefaults();
    // True when no key differs from keyboard.json.
    bool isDefault() const { return current_ == defaults_; }

    // Writes the differences from the defaults to the "editingKeyboard"
    // group (replacing what was there); keys are hex-encoded portable text.
    void save(QSettings& settings) const;
    // Starts from the defaults and applies the differences save() wrote.
    void restore(QSettings& settings);
    // The same differences as JSON, for the Keyboard setting (p.1202):
    // {"<portable key>": "<command id>", ...}, "" for an unassigned default.
    QJsonObject differences() const;
    // Starts from the defaults and applies differences() output; unreadable
    // keys are skipped.
    void setDifferences(const QJsonObject& differences);
    // The command keyboard.json gives the key, or empty.
    QString defaultCommandFor(const QKeySequence& key) const { return defaults_.value(key); }

private:
    QString layout_;
    QHash<QKeySequence, QString> defaults_;
    QHash<QKeySequence, QString> current_;
};

} // namespace lumenis::ui

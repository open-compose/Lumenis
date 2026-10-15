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
// differences, like menu shortcuts.
class KeyboardMap {
public:
    // Loads a keyboard.json document. Every command must exist in
    // `commands`. Returns false and sets *error otherwise.
    bool load(const QByteArray& json, const CommandRegistry& commands, QString* error = nullptr);
    void loadBuiltin(const CommandRegistry& commands);

    QString layoutName() const { return layout_; }

    // Command id for a key, or empty.
    QString commandFor(const QKeySequence& key) const;
    QList<QKeySequence> keysFor(const QString& commandId) const;
    QList<QKeySequence> keys() const;

    // Assigns a key; an empty id unassigns it.
    void assign(const QKeySequence& key, const QString& commandId);
    void resetToDefaults();
    bool isDefault() const { return current_ == defaults_; }

    void save(QSettings& settings) const;
    void restore(QSettings& settings);
    // The same differences as JSON, for the Keyboard setting (p.1202):
    // {"<portable key>": "<command id>", ...}, "" for an unassigned default.
    QJsonObject differences() const;
    void setDifferences(const QJsonObject& differences);
    QString defaultCommandFor(const QKeySequence& key) const { return defaults_.value(key); }

private:
    QString layout_;
    QHash<QKeySequence, QString> defaults_;
    QHash<QKeySequence, QString> current_;
};

} // namespace lumenis::ui

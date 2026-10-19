#include "lumenis/ui/keyboard_map.h"

#include "lumenis/ui/command_registry.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

// The editing keyboard's key-to-command map (KeyboardMap, declared in keyboard_map.h).
//
// Defaults come from the built-in resource src/ui/resources/keyboard.json; commands are checked
// against the CommandRegistry (src/ui/resources/commands.json). MainWindow owns the map, routes keys
// pressed in the Composer and Timeline through commandFor(), and persists it either in QSettings
// (save/restore) or in the Keyboard user setting as JSON differences (differences/setDifferences).
// The palette that edits it is keyboard_palette.cpp.

namespace lumenis::ui {

namespace {

constexpr auto kGroup = "editingKeyboard";

// QSettings keys cannot contain '/' or '\'; key names are portable text.
QString settingsKey(const QKeySequence& key) {
    return QString::fromLatin1(key.toString(QKeySequence::PortableText).toUtf8().toHex());
}

QKeySequence fromSettingsKey(const QString& s) {
    return QKeySequence::fromString(QString::fromUtf8(QByteArray::fromHex(s.toLatin1())), QKeySequence::PortableText);
}

} // namespace

// Parses {"layout": "...", "keys": [{"key": "<portable text>", "command": "<id>"}, ...]} and
// validates every entry before replacing the defaults and the current map.
bool KeyboardMap::load(const QByteArray& json, const CommandRegistry& commands, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        return fail(parseError.errorString());
    }
    QHash<QKeySequence, QString> map;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("keys")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString keyText = o.value(QStringLiteral("key")).toString();
        const QKeySequence key = QKeySequence::fromString(keyText, QKeySequence::PortableText);
        const QString command = o.value(QStringLiteral("command")).toString();
        if (key.isEmpty()) {
            return fail(QStringLiteral("unreadable key '%1'").arg(keyText));
        }
        if (!commands.contains(command)) {
            return fail(QStringLiteral("key '%1' maps to unknown command '%2'").arg(keyText, command));
        }
        if (map.contains(key)) {
            return fail(QStringLiteral("key '%1' is mapped twice").arg(keyText));
        }
        map.insert(key, command);
    }
    layout_ = doc.object().value(QStringLiteral("layout")).toString();
    defaults_ = map;
    current_ = map;
    return true;
}

void KeyboardMap::loadBuiltin(const CommandRegistry& commands) {
    QFile file(QStringLiteral(":/lumenis/ui/keyboard.json"));
    QString error;
    if (!file.open(QIODevice::ReadOnly) || !load(file.readAll(), commands, &error)) {
        qFatal("built-in keyboard.json is invalid: %s", qPrintable(error));
    }
}

QString KeyboardMap::commandFor(const QKeySequence& key) const {
    return current_.value(key);
}

QList<QKeySequence> KeyboardMap::keysFor(const QString& commandId) const {
    QList<QKeySequence> result;
    for (auto it = current_.cbegin(); it != current_.cend(); ++it) {
        if (it.value() == commandId) result << it.key();
    }
    std::sort(result.begin(), result.end(), [](const QKeySequence& a, const QKeySequence& b) {
        return a.toString(QKeySequence::PortableText) < b.toString(QKeySequence::PortableText);
    });
    return result;
}

QList<QKeySequence> KeyboardMap::keys() const {
    return current_.keys();
}

void KeyboardMap::assign(const QKeySequence& key, const QString& commandId) {
    if (commandId.isEmpty()) {
        current_.remove(key);
    } else {
        current_.insert(key, commandId);
    }
}

void KeyboardMap::resetToDefaults() {
    current_ = defaults_;
}

void KeyboardMap::save(QSettings& settings) const {
    // Store only what differs from the defaults: an empty value means the
    // user unassigned a default key.
    settings.remove(QLatin1String(kGroup));
    settings.beginGroup(QLatin1String(kGroup));
    for (auto it = current_.cbegin(); it != current_.cend(); ++it) {
        if (defaults_.value(it.key()) != it.value()) settings.setValue(settingsKey(it.key()), it.value());
    }
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it) {
        if (!current_.contains(it.key())) settings.setValue(settingsKey(it.key()), QString());
    }
    settings.endGroup();
}

void KeyboardMap::restore(QSettings& settings) {
    current_ = defaults_;
    settings.beginGroup(QLatin1String(kGroup));
    for (const QString& k : settings.childKeys()) {
        assign(fromSettingsKey(k), settings.value(k).toString());
    }
    settings.endGroup();
}

QJsonObject KeyboardMap::differences() const {
    QJsonObject out;
    for (auto it = current_.cbegin(); it != current_.cend(); ++it) {
        if (defaults_.value(it.key()) != it.value()) out.insert(it.key().toString(QKeySequence::PortableText), it.value());
    }
    for (auto it = defaults_.cbegin(); it != defaults_.cend(); ++it) {
        if (!current_.contains(it.key())) out.insert(it.key().toString(QKeySequence::PortableText), QString());
    }
    return out;
}

void KeyboardMap::setDifferences(const QJsonObject& differences) {
    current_ = defaults_;
    for (auto it = differences.constBegin(); it != differences.constEnd(); ++it) {
        const QKeySequence key = QKeySequence::fromString(it.key(), QKeySequence::PortableText);
        if (!key.isEmpty()) assign(key, it.value().toString());
    }
}

} // namespace lumenis::ui

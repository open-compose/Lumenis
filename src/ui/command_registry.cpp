#include "lumenis/ui/command_registry.h"

#include "lumenis/core/branding.h"

#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace lumenis::ui {

namespace {

QAction::MenuRole toMenuRole(const QString& role) {
    if (role == QLatin1String("quit")) return QAction::QuitRole;
    if (role == QLatin1String("about")) return QAction::AboutRole;
    if (role == QLatin1String("aboutQt")) return QAction::AboutQtRole;
    if (role == QLatin1String("preferences")) return QAction::PreferencesRole;
    // Keep everything else out of the macOS application menu.
    return QAction::NoRole;
}

constexpr auto kShortcutGroup = "keyboard";

} // namespace

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

bool CommandRegistry::load(const QByteArray& json, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        return fail(parseError.errorString());
    }
    const QJsonArray commands = doc.object().value(QStringLiteral("commands")).toArray();
    const QString product = QString::fromUtf8(branding::kProductName);

    for (const QJsonValue& value : commands) {
        const QJsonObject o = value.toObject();
        CommandSpec spec;
        spec.id = o.value(QStringLiteral("id")).toString();
        spec.text = o.value(QStringLiteral("text")).toString().replace(QStringLiteral("%PRODUCT%"), product);
        spec.shortcut = QKeySequence::fromString(o.value(QStringLiteral("shortcut")).toString(),
                                                 QKeySequence::PortableText);
        spec.role = o.value(QStringLiteral("role")).toString();
        if (spec.id.isEmpty() || spec.text.isEmpty()) {
            return fail(QStringLiteral("command without id or text"));
        }
        if (contains(spec.id)) {
            return fail(QStringLiteral("duplicate command id: %1").arg(spec.id));
        }

        auto* action = new QAction(spec.text, this);
        action->setObjectName(QStringLiteral("cmd:") + spec.id);
        action->setShortcut(spec.shortcut);
        action->setMenuRole(toMenuRole(spec.role));
        action->setEnabled(false);
        const QString id = spec.id;
        connect(action, &QAction::triggered, this, [this, id] {
            if (const Entry* e = find(id); e && e->handler) {
                e->handler();
            }
        });

        index_.insert(spec.id, entries_.size());
        entries_.push_back(Entry{std::move(spec), action, {}});
    }
    return true;
}

void CommandRegistry::loadBuiltin() {
    QFile file(QStringLiteral(":/lumenis/ui/commands.json"));
    QString error;
    if (!file.open(QIODevice::ReadOnly) || !load(file.readAll(), &error)) {
        qFatal("built-in commands.json is invalid: %s", qPrintable(error));
    }
}

bool CommandRegistry::contains(const QString& id) const {
    return index_.contains(id);
}

QStringList CommandRegistry::ids() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(entries_.size()));
    for (const Entry& e : entries_) {
        result << e.spec.id;
    }
    return result;
}

QAction* CommandRegistry::action(const QString& id) const {
    const Entry* e = find(id);
    return e ? e->action : nullptr;
}

void CommandRegistry::setHandler(const QString& id, std::function<void()> handler) {
    Entry* e = find(id);
    if (!e) {
        qWarning("setHandler: unknown command %s", qPrintable(id));
        return;
    }
    e->handler = std::move(handler);
    e->action->setEnabled(static_cast<bool>(e->handler));
}

bool CommandRegistry::hasHandler(const QString& id) const {
    const Entry* e = find(id);
    return e && e->handler;
}

QKeySequence CommandRegistry::defaultShortcut(const QString& id) const {
    const Entry* e = find(id);
    return e ? e->spec.shortcut : QKeySequence();
}

QKeySequence CommandRegistry::shortcut(const QString& id) const {
    const Entry* e = find(id);
    return e ? e->action->shortcut() : QKeySequence();
}

void CommandRegistry::setShortcut(const QString& id, const QKeySequence& sequence) {
    if (Entry* e = find(id)) {
        e->action->setShortcut(sequence);
    }
}

void CommandRegistry::resetShortcuts() {
    for (Entry& e : entries_) {
        e.action->setShortcut(e.spec.shortcut);
    }
}

void CommandRegistry::saveShortcuts(QSettings& settings) const {
    // Only differences from the defaults are stored, so improving a default
    // later reaches every user who has not changed that key.
    settings.remove(QLatin1String(kShortcutGroup));
    settings.beginGroup(QLatin1String(kShortcutGroup));
    for (const Entry& e : entries_) {
        if (e.action->shortcut() != e.spec.shortcut) {
            settings.setValue(e.spec.id, e.action->shortcut().toString(QKeySequence::PortableText));
        }
    }
    settings.endGroup();
}

void CommandRegistry::loadShortcuts(QSettings& settings) {
    resetShortcuts();
    settings.beginGroup(QLatin1String(kShortcutGroup));
    for (const QString& id : settings.childKeys()) {
        setShortcut(id, QKeySequence::fromString(settings.value(id).toString(), QKeySequence::PortableText));
    }
    settings.endGroup();
}

CommandRegistry::Entry* CommandRegistry::find(const QString& id) {
    const auto it = index_.constFind(id);
    return it == index_.constEnd() ? nullptr : &entries_[*it];
}

const CommandRegistry::Entry* CommandRegistry::find(const QString& id) const {
    const auto it = index_.constFind(id);
    return it == index_.constEnd() ? nullptr : &entries_[*it];
}

} // namespace lumenis::ui

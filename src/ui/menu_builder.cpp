#include "lumenis/ui/menu_builder.h"

#include "lumenis/ui/command_registry.h"

#include <QAction>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>

namespace lumenis::ui {

namespace {

bool populate(QMenu* menu, const QJsonArray& items, CommandRegistry& commands, QString* error) {
    for (const QJsonValue& item : items) {
        if (item.isString()) {
            const QString id = item.toString();
            if (id == QLatin1String("-")) {
                menu->addSeparator();
                continue;
            }
            QAction* action = commands.action(id);
            if (!action) {
                if (error) {
                    *error = QStringLiteral("menu '%1' refers to unknown command '%2'").arg(menu->title(), id);
                }
                return false;
            }
            menu->addAction(action);
        } else if (item.isObject()) {
            const QJsonObject o = item.toObject();
            QMenu* sub = menu->addMenu(o.value(QStringLiteral("title")).toString());
            sub->setObjectName(QStringLiteral("menu:") + o.value(QStringLiteral("id")).toString());
            if (!populate(sub, o.value(QStringLiteral("items")).toArray(), commands, error)) {
                return false;
            }
        } else {
            if (error) {
                *error = QStringLiteral("menu '%1' has an invalid item").arg(menu->title());
            }
            return false;
        }
    }
    return true;
}

QString plainText(QString text) {
    // "&&" is a literal ampersand; a single '&' marks the mnemonic.
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    return text;
}

QJsonArray dumpActions(const QList<QAction*>& actions, const CommandRegistry* commands) {
    QJsonArray items;
    for (const QAction* action : actions) {
        if (action->property(kDynamicMenuItem).toBool()) continue;
        QJsonObject o;
        if (action->isSeparator()) {
            o.insert(QStringLiteral("separator"), true);
        } else if (const QMenu* sub = action->menu()) {
            o.insert(QStringLiteral("text"), plainText(sub->title()));
            o.insert(QStringLiteral("items"), dumpActions(sub->actions(), commands));
        } else {
            o.insert(QStringLiteral("text"), plainText(action->text()));
            const QString keys = action->shortcut().toString(QKeySequence::PortableText);
            if (!keys.isEmpty()) {
                o.insert(QStringLiteral("shortcut"), keys);
            }
            o.insert(QStringLiteral("enabled"), action->isEnabled());
            const QString name = action->objectName();
            if (commands && name.startsWith(QLatin1String("cmd:"))) {
                const QString id = name.mid(4);
                o.insert(QStringLiteral("id"), id);
                o.insert(QStringLiteral("handled"), commands->hasHandler(id));
            }
        }
        items.append(o);
    }
    return items;
}

} // namespace

bool buildMenus(QMenuBar* bar, CommandRegistry& commands, const QByteArray& json, QString* error) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        if (error) {
            *error = parseError.errorString();
        }
        return false;
    }
    for (const QJsonValue& value : doc.object().value(QStringLiteral("menus")).toArray()) {
        const QJsonObject o = value.toObject();
        QMenu* menu = bar->addMenu(o.value(QStringLiteral("title")).toString());
        menu->setObjectName(QStringLiteral("menu:") + o.value(QStringLiteral("id")).toString());
        if (!populate(menu, o.value(QStringLiteral("items")).toArray(), commands, error)) {
            return false;
        }
    }
    return true;
}

void buildBuiltinMenus(QMenuBar* bar, CommandRegistry& commands) {
    QFile file(QStringLiteral(":/lumenis/ui/menus.json"));
    QString error;
    if (!file.open(QIODevice::ReadOnly) || !buildMenus(bar, commands, file.readAll(), &error)) {
        qFatal("built-in menus.json is invalid: %s", qPrintable(error));
    }
}

QJsonArray dumpMenus(const QMenuBar* bar, const CommandRegistry* commands) {
    return dumpActions(bar->actions(), commands);
}

} // namespace lumenis::ui

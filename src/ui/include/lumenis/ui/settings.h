#pragma once

#include "lumenis/model/settings.h"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>

namespace lumenis::ui {

// The settings and their options, from the built-in settings.json (guide
// ch.33). The Settings window lists them; a generic dialog edits them.
struct SettingChoice {
    QString value;
    QString label;
    bool later = false; // shown, not available yet
};

struct SettingOption {
    QString key;
    QString type; // bool, int, choice, timecode, text, info
    QString label;
    QString suffix;
    QString style; // choices: radio or menu
    QJsonValue defaultValue;
    int min = 0;
    int max = 0;
    int page = 0;
    bool later = false;
    QList<SettingChoice> choices;
};

struct SettingTab {
    QString name;
    QList<SettingOption> options;
};

struct SettingKind {
    QString kind;
    model::SettingsScope scope = model::SettingsScope::User;
    int page = 0;
    QList<SettingTab> tabs;

    const SettingOption* option(const QString& key) const;
    // "Bin Settings" (p.1152).
    QString title() const;
};

class SettingsSchema {
public:
    static const SettingsSchema& builtIn();
    static SettingsSchema fromJson(const QByteArray& json, QString* error = nullptr);

    const QList<SettingKind>& kinds() const { return kinds_; }
    const SettingKind* find(const QString& kind) const;
    QStringList kindNames(model::SettingsScope scope) const;

private:
    QList<SettingKind> kinds_;
};

// The settings in use: the user's, the open project's and the site's
// (p.1136-1137). Each is a file; a User or Project settings file that does
// not exist yet starts from the site settings (p.1141).
class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(QObject* parent = nullptr);

    // Where site settings are kept: empty for none.
    void setSiteFolder(const QString& folder);
    // The user profile whose settings are used (p.1138); `usersRoot` empty
    // keeps them in memory only.
    void setUser(const QString& usersRoot, const QString& name);
    const QString& user() const { return user_; }
    const QString& usersRoot() const { return usersRoot_; }
    // The open project's folder; empty when no project is open.
    void setProject(const QString& folder);
    bool hasProject() const { return !projectFolder_.isEmpty(); }

    model::SettingsFile& file(model::SettingsScope scope);
    const model::SettingsFile& file(model::SettingsScope scope) const;
    // The file a scope is saved in; empty when it is not saved.
    QString path(model::SettingsScope scope) const;
    // Saves a scope's file after a change and tells everyone.
    void commit(model::SettingsScope scope);

    // An option's value in the active version of its setting, or its
    // default.
    QJsonValue value(const QString& kind, const QString& key) const;
    bool flag(const QString& kind, const QString& key) const { return value(kind, key).toBool(); }
    int number(const QString& kind, const QString& key) const { return value(kind, key).toInt(); }
    QString text(const QString& kind, const QString& key) const { return value(kind, key).toString(); }

    // Sets an option in the active version of its setting.
    void setValue(const QString& kind, const QString& key, const QJsonValue& value);

signals:
    // Any setting changed, or other settings were loaded.
    void changed();

private:
    model::SettingsFile load(const QString& path, model::SettingsScope scope) const;

    QString siteFolder_;
    QString usersRoot_;
    QString user_;
    QString projectFolder_;
    model::SettingsFile userFile_;
    model::SettingsFile project_;
    model::SettingsFile site_;
};

} // namespace lumenis::ui

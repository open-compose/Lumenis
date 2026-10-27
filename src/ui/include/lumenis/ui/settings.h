#pragma once

#include "lumenis/model/settings.h"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>

namespace lumenis::ui {

// The settings and their options, from the built-in settings.json (guide
// ch.33). The Settings window lists them; a generic dialog edits them.
// The schema lives in src/ui/resources/settings.json; the values in settings
// files (model::SettingsFile, src/model/settings.*).

// One value of a "choice" option.
struct SettingChoice {
    QString value;
    QString label;
    bool later = false; // shown, not available yet
};

// One option of a setting: a control in its dialog and a key in the
// setting's values.
struct SettingOption {
    QString key;  // the key in the setting's values
    QString type; // bool, int, choice, drive, timecode, text, info
    QString label;
    QString suffix; // text after the control (a unit)
    QString style; // choices: radio or menu
    QJsonValue defaultValue; // also the type a stored value must have
    int min = 0; // int options' range; 0 when not given
    int max = 0;
    int page = 0; // the Editing Guide page that describes it; 0 when not given
    bool later = false; // shown, not available yet: value() returns the default
    QList<SettingChoice> choices;

    // The value a stored one stands for: the default when nothing is stored,
    // the option is not available yet, the stored value has another JSON
    // type than the default, or it is a choice that is unknown or not
    // available yet; otherwise the stored value. Settings::value() and the
    // setting's dialog both use it.
    QJsonValue effectiveValue(const QJsonValue& stored) const;
};

// A tab of a setting's dialog.
struct SettingTab {
    QString name;
    QList<SettingOption> options;
};

// A setting ("Bin", "Timeline", ...): which file it is kept in and its
// dialog's tabs.
struct SettingKind {
    QString kind;
    // Project or User; settings.json has no Site-only settings.
    model::SettingsScope scope = model::SettingsScope::User;
    int page = 0; // the Editing Guide page; 0 when not given
    QString opens; // a tool opened instead of a dialog
    QList<SettingTab> tabs;

    // The option with this key on any tab, or nullptr.
    const SettingOption* option(const QString& key) const;
    // "Bin Settings" (p.1152).
    QString title() const;
};

// Every setting kind, in settings.json order.
class SettingsSchema {
public:
    // The schema from the built-in :/lumenis/ui/settings.json, parsed once;
    // aborts (qFatal) if it is missing or not JSON.
    static const SettingsSchema& builtIn();
    // Parses a settings.json document. On a JSON error returns an empty schema
    // and sets *error; fields that are missing get their zero values.
    static SettingsSchema fromJson(const QByteArray& json, QString* error = nullptr);

    const QList<SettingKind>& kinds() const { return kinds_; }
    // The kind with this name, or nullptr.
    const SettingKind* find(const QString& kind) const;
    // The names of the kinds kept in a scope's file, in schema order.
    QStringList kindNames(model::SettingsScope scope) const;

private:
    QList<SettingKind> kinds_;
};

// The settings in use: the user's, the open project's and the site's
// (p.1136-1137). Each is a file; a User or Project settings file that does
// not exist yet starts from the site settings (p.1141).
// MainWindow owns the one instance; windows read options with value() and
// friends and apply them on changed(). The Settings window edits the files.
class Settings : public QObject {
    Q_OBJECT

public:
    explicit Settings(QObject* parent = nullptr);

    // Where site settings are kept: empty for none. Reads the site file at
    // once; emits nothing.
    void setSiteFolder(const QString& folder);
    QString siteFolder() const { return siteFolder_; }
    // The user profile whose settings are used (p.1138); `usersRoot` empty
    // keeps them in memory only. The file is <usersRoot>/<name>/ followed by
    // SettingsFile::fileName(); one that does not exist is created from the
    // site settings. Emits aboutToChangeUser, then changed and userChanged.
    void setUser(const QString& usersRoot, const QString& name);
    const QString& user() const { return user_; }
    const QString& usersRoot() const { return usersRoot_; }
    // The open project's folder; empty when no project is open. Loads (or
    // creates from the site settings) the project's file and emits changed.
    void setProject(const QString& folder);
    bool hasProject() const { return !projectFolder_.isEmpty(); }

    // A scope's settings file, for editing; call commit() after a change.
    model::SettingsFile& file(model::SettingsScope scope);
    const model::SettingsFile& file(model::SettingsScope scope) const;
    // The file a scope is saved in; empty when it is not saved.
    QString path(model::SettingsScope scope) const;
    // Saves a scope's file after a change and tells everyone (emits changed).
    // A scope with no path is only kept in memory.
    void commit(model::SettingsScope scope);

    // An option's value in the active version of its setting, or its
    // default. The default is also returned for an option or choice not
    // available yet ("later") and for a stored value of the wrong type. Keys
    // not in the schema return the stored value as is (null if none).
    QJsonValue value(const QString& kind, const QString& key) const;
    bool flag(const QString& kind, const QString& key) const { return value(kind, key).toBool(); }
    int number(const QString& kind, const QString& key) const { return value(kind, key).toInt(); }
    QString text(const QString& kind, const QString& key) const { return value(kind, key).toString(); }

    // Sets an option in the active version of its setting (adding an active
    // version if the kind has none), then commits. Unknown kinds are ignored.
    void setValue(const QString& kind, const QString& key, const QJsonValue& value);

signals:
    // Any setting changed, or other settings were loaded.
    void changed();
    // Another user's settings are about to be loaded, and were (p.1138):
    // what lives in the windows (workspaces) is saved first and read after.
    void aboutToChangeUser();
    void userChanged();

private:
    // Reads a scope's file and adds any kinds it lacks; if there is no path or
    // the file cannot be read, a new file seeded from the site settings.
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

#include "lumenis/ui/settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The settings schema and the settings in use (settings.h).
//
// SettingsSchema parses the built-in resource src/ui/resources/settings.json: each setting kind, its
// scope (User or Project), its dialog tabs and options with defaults. Settings holds the three
// settings files (model::SettingsFile in src/model/settings.*) for the user, the open project and the
// site, and resolves option values against the schema. MainWindow owns it; app/main.cpp sets the site
// folder and the user chosen in SelectProjectDialog, MainWindow::setProject() sets the project, and
// SettingsWindow (settings_window.cpp) edits the files.

namespace lumenis::ui {

using model::SettingsFile;
using model::SettingsScope;

const SettingOption* SettingKind::option(const QString& key) const {
    for (const SettingTab& t : tabs) {
        for (const SettingOption& o : t.options) {
            if (o.key == key) return &o;
        }
    }
    return nullptr;
}

QString SettingKind::title() const {
    return QObject::tr("%1 Settings").arg(kind);
}

// The document is {"settings": [{"kind", "scope", "page", "opens", "tabs": [{"name", "options":
// [{"key", "type", "label", "suffix", "specialText", "resetButton", "style", "default", "min", "max", "page", "later",
// "choices": [{"value", "label", "later"}]}]}]}]}.
SettingsSchema SettingsSchema::fromJson(const QByteArray& json, QString* error) {
    SettingsSchema schema;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) *error = parseError.errorString();
        return schema;
    }
    for (const QJsonValue& k : doc.object().value(QStringLiteral("settings")).toArray()) {
        const QJsonObject ko = k.toObject();
        SettingKind kind;
        kind.kind = ko.value(QStringLiteral("kind")).toString();
        const QString scope = ko.value(QStringLiteral("scope")).toString();
        kind.scope = scope == QLatin1String("project") ? SettingsScope::Project : SettingsScope::User;
        kind.page = ko.value(QStringLiteral("page")).toInt();
        kind.opens = ko.value(QStringLiteral("opens")).toString();
        for (const QJsonValue& t : ko.value(QStringLiteral("tabs")).toArray()) {
            SettingTab tab;
            tab.name = t.toObject().value(QStringLiteral("name")).toString();
            for (const QJsonValue& o : t.toObject().value(QStringLiteral("options")).toArray()) {
                const QJsonObject oo = o.toObject();
                SettingOption option;
                option.key = oo.value(QStringLiteral("key")).toString();
                option.type = oo.value(QStringLiteral("type")).toString();
                option.label = oo.value(QStringLiteral("label")).toString();
                option.suffix = oo.value(QStringLiteral("suffix")).toString();
                option.specialText = oo.value(QStringLiteral("specialText")).toString();
                option.resetButton = oo.value(QStringLiteral("resetButton")).toString();
                option.style = oo.value(QStringLiteral("style")).toString();
                option.defaultValue = oo.value(QStringLiteral("default"));
                option.min = oo.value(QStringLiteral("min")).toInt();
                option.max = oo.value(QStringLiteral("max")).toInt();
                option.page = oo.value(QStringLiteral("page")).toInt();
                option.later = oo.value(QStringLiteral("later")).toBool();
                for (const QJsonValue& c : oo.value(QStringLiteral("choices")).toArray()) {
                    const QJsonObject co = c.toObject();
                    option.choices << SettingChoice{co.value(QStringLiteral("value")).toString(),
                                                    co.value(QStringLiteral("label")).toString(),
                                                    co.value(QStringLiteral("later")).toBool()};
                }
                tab.options << option;
            }
            kind.tabs << tab;
        }
        schema.kinds_ << kind;
    }
    return schema;
}

const SettingsSchema& SettingsSchema::builtIn() {
    static const SettingsSchema schema = [] {
        QFile file(QStringLiteral(":/lumenis/ui/settings.json"));
        if (!file.open(QIODevice::ReadOnly)) qFatal("built-in settings.json is missing");
        QString error;
        SettingsSchema s = fromJson(file.readAll(), &error);
        if (!error.isEmpty()) qFatal("built-in settings.json is invalid: %s", qPrintable(error));
        return s;
    }();
    return schema;
}

const SettingKind* SettingsSchema::find(const QString& kind) const {
    for (const SettingKind& k : kinds_) {
        if (k.kind == kind) return &k;
    }
    return nullptr;
}

QStringList SettingsSchema::kindNames(SettingsScope scope) const {
    QStringList out;
    for (const SettingKind& k : kinds_) {
        if (k.scope == scope) out << k.kind;
    }
    return out;
}

// ---------------------------------------------------------------- Settings

// Until setUser() and setProject() are called, both files are default versions in memory.
Settings::Settings(QObject* parent) : QObject(parent) {
    userFile_ = SettingsFile::seeded(SettingsSchema::builtIn().kindNames(SettingsScope::User));
    project_ = SettingsFile::seeded(SettingsSchema::builtIn().kindNames(SettingsScope::Project));
}

SettingsFile Settings::load(const QString& path, SettingsScope scope) const {
    const QStringList kinds = SettingsSchema::builtIn().kindNames(scope);
    if (!path.isEmpty()) {
        if (auto f = SettingsFile::read(path)) {
            // Settings added since the file was written (p.1144).
            f->ensure(kinds);
            return *f;
        }
    }
    return SettingsFile::seeded(kinds, &site_);
}

void Settings::setSiteFolder(const QString& folder) {
    siteFolder_ = folder;
    site_ = {};
    if (const QString p = path(SettingsScope::Site); !p.isEmpty()) {
        if (auto f = SettingsFile::read(p)) site_ = *f;
    }
}

// A new user's file is seeded from the site settings and written at once, so the user exists on
// disk.
void Settings::setUser(const QString& usersRoot, const QString& name) {
    emit aboutToChangeUser();
    usersRoot_ = usersRoot;
    user_ = name;
    const QString p = path(SettingsScope::User);
    const bool existed = !p.isEmpty() && QFile::exists(p);
    userFile_ = load(p, SettingsScope::User);
    if (!existed && !p.isEmpty()) userFile_.write(p);
    emit changed();
    emit userChanged();
}

void Settings::setProject(const QString& folder) {
    projectFolder_ = folder;
    const QString p = path(SettingsScope::Project);
    const bool existed = !p.isEmpty() && QFile::exists(p);
    project_ = load(p, SettingsScope::Project);
    if (!existed && !p.isEmpty()) project_.write(p);
    emit changed();
}

SettingsFile& Settings::file(SettingsScope scope) {
    switch (scope) {
    case SettingsScope::Project: return project_;
    case SettingsScope::User: return userFile_;
    case SettingsScope::Site: return site_;
    }
    return userFile_;
}

const SettingsFile& Settings::file(SettingsScope scope) const {
    return const_cast<Settings*>(this)->file(scope);
}

QString Settings::path(SettingsScope scope) const {
    switch (scope) {
    case SettingsScope::Project:
        return projectFolder_.isEmpty() ? QString() : QDir(projectFolder_).filePath(SettingsFile::fileName());
    case SettingsScope::User:
        return usersRoot_.isEmpty() || user_.isEmpty()
                   ? QString()
                   : QDir(usersRoot_).filePath(user_ + QLatin1Char('/') + SettingsFile::fileName());
    case SettingsScope::Site:
        return siteFolder_.isEmpty() ? QString() : QDir(siteFolder_).filePath(SettingsFile::fileName());
    }
    return {};
}

void Settings::commit(SettingsScope scope) {
    if (const QString p = path(scope); !p.isEmpty()) file(scope).write(p);
    emit changed();
}

QJsonValue Settings::value(const QString& kind, const QString& key) const {
    const SettingKind* k = SettingsSchema::builtIn().find(kind);
    if (!k) return QJsonValue();
    const QJsonValue v = file(k->scope).activeValues(kind).value(key);
    // A key the schema does not know is returned as stored.
    const SettingOption* o = k->option(key);
    return o ? o->effectiveValue(v) : (v.isUndefined() ? QJsonValue() : v);
}

QJsonValue SettingOption::effectiveValue(const QJsonValue& stored) const {
    // Options not available yet keep their default.
    if (later || stored.isUndefined()) return defaultValue;
    if (stored.type() != defaultValue.type() && !defaultValue.isNull()) return defaultValue;
    // A choice that is not available yet counts as the default.
    if (type == QLatin1String("choice")) {
        for (const SettingChoice& c : choices) {
            if (c.value == stored.toString()) return c.later ? defaultValue : stored;
        }
        return defaultValue;
    }
    return stored;
}

void Settings::setValue(const QString& kind, const QString& key, const QJsonValue& value) {
    const SettingKind* k = SettingsSchema::builtIn().find(kind);
    if (!k) return;
    SettingsFile& f = file(k->scope);
    int i = f.activeIndex(kind);
    if (i < 0) i = f.add(model::SettingEntry{kind, {}, true, {}});
    f.entries[i].values.insert(key, value);
    commit(k->scope);
}

} // namespace lumenis::ui

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace lumenis::model {

// The three kinds of settings (guide p.1136-1137): User settings live in
// the user's folder, Project settings in the project folder, and Site
// settings, the defaults for new users and projects, in a Settings folder
// of their own.
enum class SettingsScope { Project, User, Site };

// One version of a setting (p.1138-1139). A setting can have several
// versions; one of each kind is active. Options missing from `values`
// have their default value.
struct SettingEntry {
    QString kind;  // "Bin", "Timeline", ...
    QString name;  // the custom setting name, often empty
    bool active = true;
    QJsonObject values;

    bool operator==(const SettingEntry&) const = default;
};

// A settings file: the Settings list of one scope.
class SettingsFile {
public:
    // The file in a user, project or site settings folder.
    static QString fileName() { return QStringLiteral("Settings.json"); }

    static std::optional<SettingsFile> read(const QString& path, QString* error = nullptr);
    bool write(const QString& path, QString* error = nullptr) const;

    QJsonObject toJson() const;
    static std::optional<SettingsFile> fromJson(const QJsonObject& o);

    // A new file with a default version of each kind; the versions in
    // `site` take their place where it has them (p.1141).
    static SettingsFile seeded(const QStringList& kinds, const SettingsFile* site = nullptr);

    QList<SettingEntry> entries;

    // The kinds in the file, sorted by name.
    QStringList kinds() const;
    // The versions of a kind, in file order.
    QList<int> versions(const QString& kind) const;
    // The active version of a kind: -1 if there is none.
    int activeIndex(const QString& kind) const;
    QJsonObject activeValues(const QString& kind) const;

    // Makes a version the active one of its kind (p.1139).
    void setActive(int index);
    // Edit > Duplicate (p.1138): an inactive copy after the last version
    // of its kind. A custom name gets a period and a version number.
    int duplicate(int index);
    // Deletes versions (p.1140). If a kind loses its active version, its
    // first remaining version becomes active.
    void remove(QList<int> indices);
    // A setting dragged in from another file (p.1141). Add: a new version,
    // inactive unless the kind had none. Replace: replaces the values and
    // name of the active version. Returns the entry's index.
    int add(SettingEntry entry, bool replace = false);
    // Adds a default version of each kind that has none, as Update User
    // Profile does (p.1144). Returns how many were added.
    int ensure(const QStringList& kinds);

private:
    void normalize();
};

} // namespace lumenis::model
